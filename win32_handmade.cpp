#include "handmade.h"

#include <windows.h>
#include <xinput.h>
#include <dsound.h>
#include <stdio.h>

#include "win32_handmade.h"

#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub)
{
   return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_get_state *XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_


#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pState)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub)
{
   return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_set_state *XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_

#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter);
typedef DIRECT_SOUND_CREATE(direct_sound_create);

global_variable bool32 GlobalRunning;
global_variable win32_offscreen_buffer GlobalBackBuffer;
global_variable LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;
global_variable int64 GlobalPerfCountFrequency;


DEBUG_FREE_FILE(DEBUGPlatformFreeFileMemory)
{
   if(Memory)
   {
      VirtualFree(Memory, 0, MEM_RELEASE);
   }
}

DEBUG_READ_FILE(DEBUGPlatformReadEntireFileIntoMemory)
{
   debug_read_file_result Result = {};
   HANDLE FileHandle = CreateFileA(Filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
   if(FileHandle != INVALID_HANDLE_VALUE)
   {
      LARGE_INTEGER FileSize;
      if(GetFileSizeEx(FileHandle, &FileSize))
      {
         uint32 FileSize32 = SafeTruncateUint64ToUint32(FileSize.QuadPart);
         Result.Contents = VirtualAlloc(0, FileSize32, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
         if(Result.Contents)
         {
            DWORD BytesRead;
            if(ReadFile(FileHandle, Result.Contents, FileSize32, &BytesRead, 0) &&
              (FileSize32 == BytesRead))
            {
               Result.ContentsSize = FileSize32;
            }
            else
            {
               DEBUGPlatformFreeFileMemory(Result.Contents);
               Result.Contents = 0;
            }
         }
         else
         {
         }
      }
      else
      {
      }
      CloseHandle(FileHandle);
   }
   else
   {
   }
   return Result;

}


DEBUG_WRITE_ENTIRE_FILE(DEBUGPlatformWriteEntireFileIntoMemory)
{
   bool32 Result = false;
   HANDLE FileHandle = CreateFileA(Filename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
   if(FileHandle != INVALID_HANDLE_VALUE)
   {
      DWORD BytesWritten;
      if(WriteFile(FileHandle, Memory, MemorySize, &BytesWritten, 0))
      {
         Result = (MemorySize == BytesWritten);
      }
      else
      {
      }
      CloseHandle(FileHandle);
   }
   else
   {
   }
   return Result;
}

inline FILETIME
Win32GetLastWriteTime(char* FileName)
{
   WIN32_FILE_ATTRIBUTE_DATA FileData = {};
   GetFileAttributesEx(FileName, GetFileExInfoStandard, &FileData);
   return FileData.ftLastWriteTime;
}

internal win32_game_code
Win32LoadGameCode(char *SourceDLLName, char *TempDLLName)
{
   win32_game_code Result = {};
   Result.LastWriteTime = Win32GetLastWriteTime(SourceDLLName);

   CopyFile(SourceDLLName, TempDLLName, FALSE);
   Result.GameCodeDLL = LoadLibraryA(TempDLLName);
   if(Result.GameCodeDLL)
   {
      Result.UpdateAndRender = (game_update_and_render *)GetProcAddress(Result.GameCodeDLL,
                                                                        "GameUpdateAndRender");
      Result.GetSoundSamples = (game_get_sound_samples *)GetProcAddress(Result.GameCodeDLL,
                                                                        "GameGetSoundSamples");
      Result.IsValid = true;
   }

   if(!Result.IsValid)
   {
      Result.GameCodeDLL = 0;
      Result.UpdateAndRender = GameUpdateAndRenderStub;
      Result.GetSoundSamples = GameGetSoundSamplesStub;
   }

   return Result;
}

internal void
Win32UnloadGameCode(win32_game_code *GameCode)
{
   if(GameCode->GameCodeDLL)
   {
      FreeLibrary(GameCode->GameCodeDLL);
      GameCode->GameCodeDLL = 0;
   }

   GameCode->IsValid = false;
   GameCode->UpdateAndRender = GameUpdateAndRenderStub;
   GameCode->GetSoundSamples = GameGetSoundSamplesStub;
}

internal void
Win32LoadXInput()
{
   HMODULE XInputLibrary = LoadLibraryA("xinput1_4.dll");
   if(!XInputLibrary)
   {
      XInputLibrary = LoadLibraryA("xinput9_1_0.dll");
   }
   if(!XInputLibrary)
   {
      XInputLibrary = LoadLibraryA("xinput1_3.dll");
   }

   if(XInputLibrary)
   {
      XInputGetState = (x_input_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
      if(!XInputGetState) {XInputGetState = XInputGetStateStub;}
      XInputSetState = (x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
      if(!XInputSetState) {XInputSetState = XInputSetStateStub;}
   }
   else
   {

   }
}

internal void
Win32InitDSound(HWND Window, int32 SamplesPerSecond, int32 BufferSize)
{
   HMODULE DSoundLibrary = LoadLibraryA("dsound.dll");

   if(DSoundLibrary)
   {
      direct_sound_create *DirectSoundCreate = (direct_sound_create *)GetProcAddress(DSoundLibrary,
                                                                                     "DirectSoundCreate");

      LPDIRECTSOUND DirectSound;
      if(DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0)))
      {
         WAVEFORMATEX WaveFormat = {};
         WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
         WaveFormat.nChannels = 2;
         WaveFormat.nSamplesPerSec = SamplesPerSecond;
         WaveFormat.wBitsPerSample = 16;
         WaveFormat.nBlockAlign = (WaveFormat.nChannels * WaveFormat.wBitsPerSample) / 8;
         WaveFormat.nAvgBytesPerSec = WaveFormat.nBlockAlign * WaveFormat.nSamplesPerSec;
         WaveFormat.cbSize = 0;

         if(SUCCEEDED(DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY)))
         {
             DSBUFFERDESC BufferDescription = {};
             BufferDescription.dwSize = sizeof(BufferDescription);
             BufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;

             LPDIRECTSOUNDBUFFER PrimaryBuffer;
             if(SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDescription, &PrimaryBuffer, 0)))
             {
                HRESULT Error = PrimaryBuffer->SetFormat(&WaveFormat);
                if(SUCCEEDED(Error))
                {
                   OutputDebugStringA("Primary buffer is set\n");
                    
                }
                else
                {
                }


             }
             else
             {
             }
         }
         else
         {

         }

         DSBUFFERDESC BufferDescription = {};
         BufferDescription.dwSize = sizeof(BufferDescription);
         BufferDescription.dwFlags = DSBCAPS_GETCURRENTPOSITION2;
         BufferDescription.dwBufferBytes = BufferSize;
         BufferDescription.lpwfxFormat = &WaveFormat;

         HRESULT Error = DirectSound->CreateSoundBuffer(&BufferDescription, &GlobalSecondaryBuffer, 0);
         if(SUCCEEDED(Error))
         {
            OutputDebugStringA("Secondary buffer created successfully\n");
         }
      }
      else
      {

      }
   }
}

internal win32_window_dimension 
Win32GetWindowDimension(HWND Window)
{
   win32_window_dimension Result;

	RECT ClientRect;
	GetClientRect(Window, &ClientRect);
	Result.Width = ClientRect.right - ClientRect.left;
	Result.Height = ClientRect.bottom - ClientRect.top;

   return Result;

}

internal void
Win32ResizeDIBSection(win32_offscreen_buffer *Buffer, int Width, int Height)
{
    if(Buffer->Memory)
    {
		 VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;
    int BytesPerPixel = 4;
    Buffer->BytesPerPixel = BytesPerPixel;

    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BitmapMemorySize = (Buffer->Width*Buffer->Height)*Buffer->BytesPerPixel;
    Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    Buffer->Pitch = Width*BytesPerPixel;
}

internal void
Win32DisplayBufferInWindow(win32_offscreen_buffer *Buffer,
                 		      HDC DeviceContext,
									int WindowWidth, int WindowHeight,
                           int X, int Y, int Width, int Height)
{
    StretchDIBits(DeviceContext,
						0, 0, WindowWidth, WindowHeight,
						0, 0, Buffer->Width, Buffer->Height,
						Buffer->Memory,
						&Buffer->Info,
						DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK
Win32MainWindowMessageCallback(HWND   Window,
                               UINT   Message,
                               WPARAM WParam,
                               LPARAM LParam)
{
    LRESULT Result = 0;

    switch(Message)
    {
        
        case WM_CLOSE:
        {
           GlobalRunning = false;
        } break;

        case WM_DESTROY:
        {
			  GlobalRunning = false;
        } break;

        case WM_ACTIVATEAPP:
        {
			  OutputDebugStringA("WM_ACTIVATEAPP\n");
        } break;

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
           Assert(!"Keyboard input came in through a non-dispatch message");
        } break;

        case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            HDC DeviceContext = BeginPaint(Window, &Paint);
            int X = Paint.rcPaint.left;
            int Y = Paint.rcPaint.top;
            int Height = Paint.rcPaint.bottom - Paint.rcPaint.top;
            int Width = Paint.rcPaint.right - Paint.rcPaint.left;

            win32_window_dimension Dimension = Win32GetWindowDimension(Window);

            Win32DisplayBufferInWindow(&GlobalBackBuffer, DeviceContext,
                                       Dimension.Width, Dimension.Height,
                                       X, Y, Width, Height);
        } break;

        default:
        {
//            OutputDebugStringA("default\n");
            Result = DefWindowProcA(Window, Message, WParam, LParam);
        } break;
    }

    return Result;
}


internal void
Win32ClearBuffer(win32_sound_output *SoundOutput)
{
    VOID *Region1;
    DWORD Region1Size;
    VOID *Region2;
    DWORD Region2Size;
    if(SUCCEEDED(GlobalSecondaryBuffer->Lock(0, SoundOutput->SecondaryBufferSize,
                                &Region1, &Region1Size,
                                &Region2, &Region2Size,
                                0)))
    {
        uint8 *DestSample = (uint8 *)Region1;
        for(DWORD ByteIndex = 0;
            ByteIndex < Region1Size;
            ++ByteIndex)
        {
           *DestSample++ = 0;
        }

        DestSample = (uint8 *)Region2;
        for(DWORD ByteIndex = 0;
            ByteIndex < Region2Size;
            ++ByteIndex)
        {
           *DestSample++ = 0;
        }

        GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }

}
internal void
Win32FillSoundBuffer(win32_sound_output *SoundOutput, DWORD ByteToLock, DWORD BytesToWrite,
                     game_sound_output_buffer *SourceBuffer)
{
    VOID *Region1;
    DWORD Region1Size;
    VOID *Region2;
    DWORD Region2Size;
    
    if(SUCCEEDED(GlobalSecondaryBuffer->Lock(ByteToLock, BytesToWrite,
                                &Region1, &Region1Size,
                                &Region2, &Region2Size,
                                0)))
    {
        int16 *DestSample = (int16 *)Region1;
        int16 *SourceSample = SourceBuffer->Samples;

        
    
        DWORD Region1SampleCount = Region1Size/SoundOutput->BytesPerSample;
        for(DWORD SampleIndex = 0;
            SampleIndex < Region1SampleCount;
            ++SampleIndex)
        {
           *DestSample++ = *SourceSample++;
           *DestSample++ = *SourceSample++;

           
           ++SoundOutput->RunningSampleIndex;
        }
    
        DWORD Region2SampleCount = Region2Size/SoundOutput->BytesPerSample;
        DestSample = (int16 *)Region2;
        for(DWORD SampleIndex = 0;
            SampleIndex < Region2SampleCount;
            ++SampleIndex)
        {
           *DestSample++ = *SourceSample++;
           *DestSample++ = *SourceSample++;
           
           ++SoundOutput->RunningSampleIndex;
        }
        GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }
}

internal void
Win32ProcessKeyboardMessage(game_button_state *NewState, bool32 IsDown)
{
   Assert(NewState->EndedDown != IsDown);
   NewState->EndedDown = IsDown;
   ++NewState->HalfTransitionCount;
}

internal void
Win32ProcessXInputDigitalButton(DWORD XInputButtonState,
                                game_button_state *OldState, DWORD ButtonBit,
                                game_button_state *NewState)
{
   NewState->HalfTransitionCount = (OldState->EndedDown != NewState->EndedDown) ? 1 : 0;
   NewState->EndedDown = ((XInputButtonState & ButtonBit) == ButtonBit);
}

internal real32
Win32ProcessXInputStickValue(SHORT Value, SHORT DeadZoneThreshold)
{
   real32 Result = 0;
   if(Value < -DeadZoneThreshold)
   {
      Result = (real32)(Value + DeadZoneThreshold) / (32768.0f - DeadZoneThreshold);
   }
   else if(Value > DeadZoneThreshold)
   {
      Result = (real32)(Value - DeadZoneThreshold) / (32767.0f - DeadZoneThreshold);
   }
   return Result;
}

internal void
Win32ProcessPendingMessages(game_controller_input *KeyboardController)
{
   MSG Message;
   while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
   {
      switch(Message.message)
      {
         case WM_QUIT:
         {
            GlobalRunning = false;
         } break;
         case WM_SYSKEYDOWN:
         case WM_SYSKEYUP:
         case WM_KEYDOWN:
         case WM_KEYUP:
            {
               uint32 VCode = (uint32)Message.wParam;
               bool32 WasDown = ((Message.lParam & (1 << 30)) != 0);
               bool32 IsDown = ((Message.lParam & (1 << 31)) == 0);

               if(WasDown != IsDown)
               {
                  if(VCode == 'W')
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->MoveUp, IsDown);
                  }
                  else if(VCode == 'A')
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->MoveLeft, IsDown);
                  }
                  else if(VCode == 'S')
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->MoveDown, IsDown);
                  }
                  else if(VCode == 'D')
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->MoveRight, IsDown);
                  }
                  else if(VCode == 'Q')
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->LeftShoulder, IsDown);
                  }
                  else if(VCode == 'E')
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->RightShoulder, IsDown);
                  }
                  else if(VCode == VK_UP)
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->ActionUp, IsDown);
                  }
                  else if(VCode == VK_LEFT)
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->ActionLeft, IsDown);
                  }
                  else if(VCode == VK_DOWN)
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->ActionDown, IsDown);
                  }
                  else if(VCode == VK_RIGHT)
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->ActionRight, IsDown);
                  }
                  else if(VCode == VK_ESCAPE)
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->Start, IsDown);
                  }
                  else if(VCode == VK_SPACE)
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->Back, IsDown);
                  }
               }
               bool32 AltKeyWasDown = (Message.lParam & (1 << 29));
               if((VCode == VK_F4) && AltKeyWasDown)
               {
                  GlobalRunning = false;
               }
            } break;

         default:
            {
               TranslateMessage(&Message);
               DispatchMessageA(&Message);
            } break;
      }
   }
}

inline LARGE_INTEGER
Win32GetWallClock()
{
   LARGE_INTEGER Result;
   QueryPerformanceCounter(&Result);
   return Result;
}
   
inline real32
Win32GetSecondsElapsed(LARGE_INTEGER Start, LARGE_INTEGER End)
{
   real32 Result = ((real32)(End.QuadPart - Start.QuadPart) /
                                   (real32)GlobalPerfCountFrequency);
   return Result;
}

internal void
Win32DebugDrawVertical(win32_offscreen_buffer *BackBuffer, 
                       int X, int Top, int Bottom, uint32 Color)
{
   uint8 *Pixel = (uint8 *)BackBuffer->Memory + 
                  X * BackBuffer->BytesPerPixel + 
                  Top*BackBuffer->Pitch;
   for(int Y = Top;
       Y < Bottom;
       ++Y)
   {
      *(uint32 *)Pixel = Color;
      Pixel += BackBuffer->Pitch;
   }
}

inline void
Win32DrawSoundBufferMarker(win32_offscreen_buffer *BackBuffer, 
                      win32_sound_output *SoundOutput,
                      real32 C, int PadX, int Top, int Bottom,
                      DWORD Value, uint32 Color)
{
   real32 XReal32 = C * (real32)Value;
   int X = PadX + (int)XReal32;
   Win32DebugDrawVertical(BackBuffer, X, Top, Bottom, Color);
}

internal void
Win32DebugSyncDisplay(win32_offscreen_buffer *BackBuffer, win32_debug_time_marker *Marker,
                      win32_sound_output *SoundOutput, real32 TargetSecondsPerFrame)
{
   int PadX = 16;
   int PadY = 16;

   int LineHeight = 64;
   int Top = PadY;
   int Bottom = Top + LineHeight;

   real32 C = (real32)(BackBuffer->Width - 2*PadX) / (real32)SoundOutput->SecondaryBufferSize;
   uint32 PlayColor = 0xFFFFFFFF;
   uint32 WriteColor = 0xFFFF0000;
   uint32 FrameColor = 0xFF000000;


   Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, Marker->PlayCursor,
                              PlayColor);
   Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, Marker->WriteCursor,
                              WriteColor);


   Top = PadY;
   Bottom += 3 * LineHeight;
   

   Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, Marker->FrameBeforeBoundary, 
                              FrameColor);
   Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, Marker->FrameBoundaryByte, 
                              FrameColor);

   Top = 2 * PadY + LineHeight;
   Bottom = Top + LineHeight;
 
   Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, Marker->ByteToLock,
                              PlayColor);
   Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, Marker->EndBufferByte,
                              WriteColor);

   Top = Bottom + PadY;
   Bottom = Top + LineHeight;
 
   Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, Marker->FlipPlayCursor,
                              PlayColor);
   Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, Marker->FlipWriteCursor,
                              WriteColor);
}

internal void
CatStrings(size_t StringACount, char *StringA,
           size_t StringBCount, char *StringB,
           size_t DestCount, char *Dest)
{
   for(size_t Index = 0;
       Index < StringACount;
       ++Index)
   {
      *Dest++ = *StringA++;
   }

   for(size_t Index = 0;
       Index < StringBCount;
       ++Index)
   {
      *Dest++ = *StringB++;
   }

   *Dest++ = 0;
}

int CALLBACK 
WinMain(HINSTANCE Instance,
        HINSTANCE PrevInstance,
        LPSTR     CommandLine,
        int       ShowCode)
{

   char EXEFilename[MAX_PATH];
   DWORD EXEFilenameSize = GetModuleFileName(0, EXEFilename, sizeof(EXEFilename));
   char *OnePastLastSlash = EXEFilename;
   for(char *Scan = EXEFilename;
       *Scan;
       ++Scan)
   {
      if(*Scan == '\\')
      {
         OnePastLastSlash = Scan + 1;
      }
   }

   char SourceGameCodeFilename[] = "handmade.dll";
   char SourceGameCodeFullPath[MAX_PATH];
   CatStrings(OnePastLastSlash - EXEFilename, EXEFilename,
              sizeof(SourceGameCodeFilename) - 1, SourceGameCodeFilename,
              sizeof(SourceGameCodeFullPath), SourceGameCodeFullPath);

   char TempGameCodeFilename[] = "handmade_temp.dll";
   char TempGameCodeFullPath[MAX_PATH];
   CatStrings(OnePastLastSlash - EXEFilename, EXEFilename,
              sizeof(TempGameCodeFilename) - 1, TempGameCodeFilename,
              sizeof(TempGameCodeFullPath), TempGameCodeFullPath);

   LARGE_INTEGER PerfCountFrequencyResult;
   QueryPerformanceFrequency(&PerfCountFrequencyResult);
   GlobalPerfCountFrequency = PerfCountFrequencyResult.QuadPart;

   UINT DesiredSchedularMS = 1;
   bool32 SleepIsGranular = (timeBeginPeriod(DesiredSchedularMS) == TIMERR_NOERROR);

   Win32LoadXInput();
   WNDCLASSA WindowClass = {};

   Win32ResizeDIBSection(&GlobalBackBuffer, 1280, 720);

   WindowClass.style = CS_HREDRAW | CS_VREDRAW;
   WindowClass.lpfnWndProc = Win32MainWindowMessageCallback;
   WindowClass.hInstance = Instance;
   //    WindowClass.hIcon = ;
   WindowClass.lpszClassName = "HandmadeHeroWindowClass";
#define MonitorRefreshHz 60
#define GameUpdateHz (MonitorRefreshHz / 2)
   real32 TargetSecondsPerFrame = 1.0f / (real32)GameUpdateHz;

   if(RegisterClassA(&WindowClass))
   {
      HWND Window = 
         CreateWindowExA(0,
                         WindowClass.lpszClassName,
                         "Handmade Hero",
                         WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                         CW_USEDEFAULT,
                         CW_USEDEFAULT,
                         CW_USEDEFAULT,
                         CW_USEDEFAULT,
                         0,
                         0,
                         Instance,
                         0);


      if(Window)
      {

         HDC DeviceContext = GetDC(Window);

         win32_sound_output SoundOutput = {};

         SoundOutput.SamplesPerSecond = 48000;
         SoundOutput.BytesPerSample = sizeof(int16)*2;
         SoundOutput.SafetyBytes = ((SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample) /
                                    GameUpdateHz / 3);
         SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond*SoundOutput.BytesPerSample;

         Win32InitDSound(Window, SoundOutput.SamplesPerSecond, SoundOutput.SecondaryBufferSize);
         Win32ClearBuffer(&SoundOutput);
         GlobalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
         GlobalRunning = true;
#if 0
         while(GlobalRunning)
         {
            DWORD PlayCursor;
            DWORD WriteCursor;
            GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor);
            char TextBuffer[256];
            sprintf_s(TextBuffer, sizeof(TextBuffer),
                      "PC:%u WC:%u\n",
                      PlayCursor, WriteCursor);
            OutputDebugStringA(TextBuffer);
         }
#endif

         int16 *Samples = (int16 *)VirtualAlloc(0, SoundOutput.SecondaryBufferSize,
                                                MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

#if HANDMADE_INTERNAL
         LPVOID BaseAddress = (LPVOID)Terabytes(2);
#else
         LPVOID BaseAddress = 0;
#endif
         game_memory GameMemory = {};
         GameMemory.PermanentStorageSize = Megabytes(64);
         GameMemory.TransientStorageSize = Gigabytes(1);
         GameMemory.DEBUGPlatformReadEntireFileIntoMemory = DEBUGPlatformReadEntireFileIntoMemory;
         GameMemory.DEBUGPlatformFreeFileMemory = DEBUGPlatformFreeFileMemory;
         GameMemory.DEBUGPlatformWriteEntireFileIntoMemory = DEBUGPlatformWriteEntireFileIntoMemory;

         uint64 TotalSize = GameMemory.PermanentStorageSize + GameMemory.TransientStorageSize;
         GameMemory.PermanentStorage = VirtualAlloc(BaseAddress, (size_t)TotalSize,
                                                    MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

         GameMemory.TransientStorage = ((uint8 *)GameMemory.PermanentStorage +
                                        GameMemory.PermanentStorageSize);
         if(!GameMemory.PermanentStorage)
         {
            DWORD Code = GetLastError();
            char ErrorCode[200];
            sprintf_s(ErrorCode, "Error Code: %d\n", Code);
         }

         if(Samples && GameMemory.PermanentStorage && GameMemory.TransientStorage)
         {
            game_input Input[2] = {};
            game_input *NewInput = &Input[0];
            game_input *OldInput = &Input[1];

            LARGE_INTEGER LastCounter = Win32GetWallClock();
            LARGE_INTEGER FlipCounter = LastCounter;

            win32_debug_time_marker Marker = {};

            DWORD LastPlayCursor = 0;
            DWORD LastTargetCursor = 0;
            DWORD LastWriteCursor = 0;
            uint32 LoadCounter = 0;
            win32_game_code Game = Win32LoadGameCode(SourceGameCodeFullPath,
                                                     TempGameCodeFullPath);

            bool32 SoundIsValid = false;

            uint64 LastCycleCount = __rdtsc();

            while(GlobalRunning)
            {
               FILETIME CurrentFileTime = Win32GetLastWriteTime(SourceGameCodeFullPath);
               if(CompareFileTime(&Game.LastWriteTime, &CurrentFileTime) != 0)
               {
                  Win32UnloadGameCode(&Game);
                  Game = Win32LoadGameCode(SourceGameCodeFullPath,
                                           TempGameCodeFullPath);
               }

               game_controller_input *OldKeyboardController = GetController(OldInput, 0);
               game_controller_input *NewKeyboardController = GetController(NewInput, 0);
               *NewKeyboardController = {};
               NewKeyboardController->IsConnected = true;
               for(int ButtonIndex = 0;
                   ButtonIndex < ArrayCount(NewKeyboardController->Buttons);
                   ++ButtonIndex)
               {
                  NewKeyboardController->Buttons[ButtonIndex].EndedDown =
                     OldKeyboardController->Buttons[ButtonIndex].EndedDown;
               }

               Win32ProcessPendingMessages(NewKeyboardController);

               DWORD MaxControllerCount = XUSER_MAX_COUNT;
               if(MaxControllerCount > (ArrayCount(NewInput->Controllers) -1))
               {
                  MaxControllerCount = (ArrayCount(NewInput->Controllers) -1);
               }

               for(DWORD ControllerIndex = 0;
                   ControllerIndex < MaxControllerCount;
                   ++ControllerIndex)
               {
                  DWORD OurControllerIndex = ControllerIndex + 1;
                  game_controller_input *OldController = GetController(OldInput, OurControllerIndex);
                  game_controller_input *NewController = GetController(NewInput, OurControllerIndex);

                  XINPUT_STATE ControllerState;
                  if(XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS)
                  {
                     NewController->IsConnected = true;
                     XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;


                     NewController->IsAnalog = true;
                     NewController->StickAvarageX = Win32ProcessXInputStickValue(Pad->sThumbLX,
                                                                                 XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                     NewController->StickAvarageY = Win32ProcessXInputStickValue(Pad->sThumbLY,
                                                                                 XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                     if((NewController->StickAvarageX != 0.0f) ||
                        (NewController->StickAvarageY != 0.0f))
                     {
                        NewController->IsAnalog = true;
                     }

                     if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP)
                     {
                        NewController->StickAvarageY = 1.0f;
                        NewController->IsAnalog = false;
                     }

                     if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)
                     {
                        NewController->StickAvarageY = -1.0f;
                        NewController->IsAnalog = false;
                     }

                     if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)
                     {
                        NewController->StickAvarageX = -1.0f;
                        NewController->IsAnalog = false;
                     }

                     if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)
                     {
                        NewController->StickAvarageX = 1.0f;
                        NewController->IsAnalog = false;
                     }

                     real32 Threshold = 0.5f;
                     Win32ProcessXInputDigitalButton((NewController->StickAvarageX < -Threshold) ? 1 : 0,
                                                     &OldController->MoveLeft, 1,
                                                     &NewController->MoveLeft);
                     Win32ProcessXInputDigitalButton((NewController->StickAvarageX > Threshold) ? 1 : 0,
                                                     &OldController->MoveRight, 1,
                                                     &NewController->MoveRight);
                     Win32ProcessXInputDigitalButton((NewController->StickAvarageY < -Threshold) ? 1 : 0,
                                                     &OldController->MoveDown, 1,
                                                     &NewController->MoveDown);
                     Win32ProcessXInputDigitalButton((NewController->StickAvarageY > Threshold) ? 1 : 0,
                                                     &OldController->MoveUp, 1,
                                                     &NewController->MoveUp);

                     Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                     &OldController->ActionDown, XINPUT_GAMEPAD_A,
                                                     &NewController->ActionDown);
                     Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                     &OldController->ActionRight, XINPUT_GAMEPAD_B,
                                                     &NewController->ActionRight);
                     Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                     &OldController->ActionLeft, XINPUT_GAMEPAD_X,
                                                     &NewController->ActionLeft);
                     Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                     &OldController->ActionUp, XINPUT_GAMEPAD_Y,
                                                     &NewController->ActionUp);
                     Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                     &OldController->LeftShoulder, XINPUT_GAMEPAD_LEFT_THUMB,
                                                     &NewController->LeftShoulder);
                     Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                     &OldController->RightShoulder, XINPUT_GAMEPAD_RIGHT_THUMB,
                                                     &NewController->RightShoulder);

                     Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                     &OldController->RightShoulder, XINPUT_GAMEPAD_START,
                                                     &NewController->RightShoulder);
                     Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                     &OldController->RightShoulder, XINPUT_GAMEPAD_BACK,
                                                     &NewController->RightShoulder);
                  }
                  else
                  {
                     // NOTE(maximqa): The controller is not available
                     NewController->IsConnected = false;
                  }
               }

               game_offscreen_buffer Buffer = {};
               Buffer.Memory = GlobalBackBuffer.Memory;
               Buffer.Width = GlobalBackBuffer.Width;
               Buffer.Height = GlobalBackBuffer.Height;
               Buffer.Pitch = GlobalBackBuffer.Pitch;

               Game.UpdateAndRender(&GameMemory, NewInput, &Buffer);

               DWORD PlayCursor;
               DWORD WriteCursor;
               if(GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor) == DS_OK)
               {
                  LARGE_INTEGER PlayCursorTime = Win32GetWallClock();
                  real32 TimeFromStartToPlayCursor = Win32GetSecondsElapsed(FlipCounter, PlayCursorTime);
                  DWORD BytesFromStartToPlayCursor = ((DWORD)(TimeFromStartToPlayCursor *
                                                              (real32)SoundOutput.SamplesPerSecond *
                                                              (real32)SoundOutput.BytesPerSample) %
                                                      SoundOutput.SecondaryBufferSize);

                  real32 PlayCursorFrameTimeDiff = TargetSecondsPerFrame - TimeFromStartToPlayCursor;

                  DWORD BytesFromPlayToBoundary = ((DWORD)(PlayCursorFrameTimeDiff *
                                                           (real32)SoundOutput.SamplesPerSecond *
                                                           (real32)SoundOutput.BytesPerSample) %
                                                   SoundOutput.SecondaryBufferSize);

                  DWORD FrameBoundaryByte = ((PlayCursor + BytesFromPlayToBoundary) %
                                             SoundOutput.SecondaryBufferSize);

                  DWORD FrameBytes = (DWORD)((SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample) / 
                                             GameUpdateHz);
                  DWORD UnwrappedWriteCursor = WriteCursor;
                  if(PlayCursor > WriteCursor)
                  {
                     UnwrappedWriteCursor += SoundOutput.SecondaryBufferSize;
                  }

                  if(!SoundIsValid)
                  {
                     SoundOutput.RunningSampleIndex = WriteCursor / SoundOutput.BytesPerSample;
                     SoundIsValid = true;
                  }

                  DWORD AudioLatencyBytes = 0;
                  if(WriteCursor < PlayCursor)
                  {
                     AudioLatencyBytes = SoundOutput.SecondaryBufferSize + WriteCursor;
                     AudioLatencyBytes -= PlayCursor;
                  }
                  else
                  {
                     AudioLatencyBytes = WriteCursor - PlayCursor;
                  }
                  bool32 AudioIsLowLatency = (UnwrappedWriteCursor < FrameBoundaryByte);
#if HANDMADE_INTERNAL
                  Assert(AudioLatencyBytes < SoundOutput.SecondaryBufferSize);
#endif
                  DWORD ByteToLock = ((SoundOutput.RunningSampleIndex * SoundOutput.BytesPerSample) %
                                      SoundOutput.SecondaryBufferSize);
                  DWORD TargetCursor = 0;
                  if(AudioIsLowLatency)
                  {
                     TargetCursor = FrameBoundaryByte + FrameBytes;
                  }
                  else
                  {
                     TargetCursor = WriteCursor + (2 * AudioLatencyBytes);
                  }
                  TargetCursor %= SoundOutput.SecondaryBufferSize;

                  DWORD BytesToWrite = 0;
                  if(ByteToLock > TargetCursor)
                  {
                     BytesToWrite = SoundOutput.SecondaryBufferSize - ByteToLock;
                     BytesToWrite += TargetCursor;
                  }
                  else
                  {
                     BytesToWrite = TargetCursor - ByteToLock;
                  }

                  char TextBuffer[256];
                  sprintf_s(TextBuffer, sizeof(TextBuffer),
                            "PC:%u WC:%u BTL:%u TC:%u BTW:%u FrameBoundaryByte:%u FrameBytes:%u\n"
                            "BytesFromStartToPlayCursor:%u AudioLatencyBytes:%u\n"
                            "TimeFromStartFrameToPlayCursor:%f\n",
                            PlayCursor, WriteCursor, ByteToLock, TargetCursor, BytesToWrite,
                            FrameBoundaryByte, FrameBytes, BytesFromStartToPlayCursor,
                            AudioLatencyBytes, TimeFromStartToPlayCursor);
                  OutputDebugStringA(TextBuffer);

                  game_sound_output_buffer SoundBuffer = {};

                  SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
                  SoundBuffer.SampleCount = BytesToWrite / SoundOutput.BytesPerSample;
                  SoundBuffer.Samples = Samples;
                  Game.GetSoundSamples(&GameMemory, &SoundBuffer);

                  Win32FillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite, &SoundBuffer);

                  Marker.ByteToLock = ByteToLock;
                  Marker.EndBufferByte = ((ByteToLock + BytesToWrite) %
                                          SoundOutput.SecondaryBufferSize);

                  if((FrameBoundaryByte - FrameBytes) > SoundOutput.SecondaryBufferSize)
                  {
                     Marker.FrameBeforeBoundary = ((FrameBoundaryByte + FrameBytes) %
                                                   SoundOutput.SecondaryBufferSize);
                  }
                  else
                  {
                     Marker.FrameBeforeBoundary = FrameBoundaryByte - FrameBytes;
                  }
                  Marker.FrameBoundaryByte = FrameBoundaryByte;
                  Marker.PlayCursor = PlayCursor;
                  Marker.WriteCursor = WriteCursor;
               }
               else
               {
                  SoundIsValid = false;
               }


               LARGE_INTEGER WorkCounter = Win32GetWallClock();
               real32 WorkSecondsElapsed = Win32GetSecondsElapsed(LastCounter, WorkCounter);

               real32 SecondsElapsedForFrame = WorkSecondsElapsed;
               if(SecondsElapsedForFrame < TargetSecondsPerFrame)
               {
                  if(SleepIsGranular)
                  {
                     DWORD SleepMS = (DWORD)(1000.0f * (TargetSecondsPerFrame - SecondsElapsedForFrame));
                     if(SleepMS > 0)
                     {
                        Sleep(SleepMS);
                     }
                  }
                  real32 TestSecondsElapsedForFrame = Win32GetSecondsElapsed(LastCounter, Win32GetWallClock());

                  //                      Assert(TestSecondsElapsedForFrame < TargetSecondsPerFrame);
                  while(SecondsElapsedForFrame < TargetSecondsPerFrame)
                  {
                     SecondsElapsedForFrame = Win32GetSecondsElapsed(LastCounter, Win32GetWallClock());
                  }
               }
               else
               {
               }

               LARGE_INTEGER EndCounter = Win32GetWallClock();
               real64 MSPerFrame = 1000.0f * Win32GetSecondsElapsed(LastCounter, EndCounter);
               LastCounter = EndCounter;


               win32_window_dimension Dimension = Win32GetWindowDimension(Window);
               DWORD PlayCursor1;
               DWORD WriteCursor1;
               if(GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor1, &WriteCursor1) == DS_OK)
               {
                  char TextBuffer[256];
                  sprintf_s(TextBuffer, sizeof(TextBuffer),
                            "FlipPlayCursor:%u FlipWriteCursor:%u\n",
                            PlayCursor1, WriteCursor1);
                  OutputDebugStringA(TextBuffer);
                  Marker.FlipPlayCursor = PlayCursor1;
                  Marker.FlipWriteCursor = WriteCursor1;
               }

#if HANDMADE_INTERNAL
               Win32DebugSyncDisplay(&GlobalBackBuffer, &Marker, &SoundOutput, TargetSecondsPerFrame);
#endif
               Win32DisplayBufferInWindow(&GlobalBackBuffer, DeviceContext,
                                          Dimension.Width, Dimension.Height,
                                          0, 0, Dimension.Width, Dimension.Height);
               FlipCounter = Win32GetWallClock();


               game_input *Temp = NewInput;
               NewInput = OldInput;
               OldInput = Temp;

               uint64 EndCycleCount = __rdtsc();
               uint64 CyclesElapsed = EndCycleCount - LastCycleCount;
               LastCycleCount = EndCycleCount;

               real64 FPS = 0.0f;
               real64 MCPF = (real64)(CyclesElapsed / (1000.0f * 1000.0f));
               char FPSBuffer[256];
               sprintf_s(FPSBuffer, sizeof(FPSBuffer),
                         "%.02fms/f, %.02fFPS - %.02fmc/f\n", MSPerFrame, FPS, MCPF);
               OutputDebugStringA(FPSBuffer);

            } 
         }
         else
         {
         }
      }
      else
      {
         //TODO: log
      }
   }
   else
   {
      //TODO: log
   }

   return 0;
}
