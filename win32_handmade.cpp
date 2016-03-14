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
global_variable bool32 GlobalPause;
global_variable win32_offscreen_buffer GlobalBackBuffer;
global_variable LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;
global_variable int64 GlobalPerfCountFrequency;

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

internal void
Win32GetEXEFilename(win32_state *State)
{
   DWORD EXEFilenameSize = GetModuleFileName(0, State->EXEFilename, sizeof(State->EXEFilename));
   State->OnePastLastEXEFilenameSlash = State->EXEFilename;
   for(char *Scan = State->EXEFilename;
       *Scan;
       ++Scan)
   {
      if(*Scan == '\\')
      {
         State->OnePastLastEXEFilenameSlash= Scan + 1;
      }
   }

}

internal int
StringLength(char *String)
{
   int Count = 0;
   while(*String++)
   {
      ++Count;
   }
   return Count;
}

internal void
Win32BuildEXEPathFilename(win32_state *State, char *Filename,
                          int DestCount, char *Dest)
{
   CatStrings(State->OnePastLastEXEFilenameSlash - State->EXEFilename, State->EXEFilename,
              StringLength(Filename), Filename,
              DestCount, Dest);
}


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
               DEBUGPlatformFreeFileMemory(Thread, Result.Contents);
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
   FILETIME LastWriteTime = {};
   WIN32_FILE_ATTRIBUTE_DATA FileData = {};
   if(GetFileAttributesEx(FileName, GetFileExInfoStandard, &FileData))
   {
      LastWriteTime = FileData.ftLastWriteTime;
   }
   return LastWriteTime;
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
      Result.UpdateAndRender = 0;
      Result.GetSoundSamples = 0;
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
   GameCode->UpdateAndRender = 0;
   GameCode->GetSoundSamples = 0;
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
                 		      HDC DeviceContext, int WindowWidth, int WindowHeight)
{
    StretchDIBits(DeviceContext,
						0, 0, Buffer->Width, Buffer->Height,
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
#if 0
           if(WParam == TRUE)
           {
              SetLayeredWindowAttributes(Window, RGB(0, 0, 0), 255, LWA_ALPHA);
           }
           else
           {
              SetLayeredWindowAttributes(Window, RGB(0, 0, 0), 64, LWA_ALPHA);
           }
#endif
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
                                       Dimension.Width, Dimension.Height);
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
   if(NewState->EndedDown != IsDown)
   {
      NewState->EndedDown = IsDown;
      ++NewState->HalfTransitionCount;
   }
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
Win32GetInputFileLocation(win32_state *State, bool32 Input, int SlotIndex, int DestCount, char *Dest)
{
   char Temp[64];
   wsprintf(Temp, "loop_edit_%d_%s.hmi", SlotIndex, Input ? "input" : "state");
   Win32BuildEXEPathFilename(State, Temp, DestCount, Dest);
}

win32_replay_buffer *
Win32GetReplayBuffer(win32_state *State, int ReplayIndex)
{
   Assert(ReplayIndex < ArrayCount(State->ReplayBuffers));
   win32_replay_buffer *Result = &State->ReplayBuffers[ReplayIndex];
   return Result;
}

internal void
Win32OpenRecordingHandle(win32_state *Win32State, int RecordingIndex)
{
   win32_replay_buffer *ReplayBuffer = Win32GetReplayBuffer(Win32State, RecordingIndex);
   if(ReplayBuffer->MemoryMap)
   {
      Win32State->RecordingIndex = RecordingIndex;
      char FileName[WIN32_STATE_FILE_NAME_COUNT];
      Win32GetInputFileLocation(Win32State, true, RecordingIndex, sizeof(FileName), FileName);

      Win32State->RecordingHandle = CreateFileA(FileName, GENERIC_READ | GENERIC_WRITE,
                                                0, 0, CREATE_ALWAYS, 0, 0);
#if 0
      LARGE_INTEGER FiliPosition;
      FilePosition.QuadPart = Win32State->TotalSize;
      SetFilePointerEx(Win32State->RecordingHandle, FilePosition, 0, FILE_BEGIN);
#endif

      CopyMemory(ReplayBuffer->MemoryMap, Win32State->GameMemoryBlock, Win32State->TotalSize);
   }
}

internal void
Win32CloseRecordingHandle(win32_state *Win32State)
{
   CloseHandle(Win32State->RecordingHandle);
   Win32State->RecordingIndex = 0;
}

internal void
Win32OpenPlayingHandle(win32_state *Win32State, int PlayingIndex)
{
   win32_replay_buffer *ReplayBuffer = Win32GetReplayBuffer(Win32State, PlayingIndex);
   if(ReplayBuffer->MemoryMap)
   {
      Win32State->PlayingIndex = PlayingIndex;
      char FileName[WIN32_STATE_FILE_NAME_COUNT];
      Win32GetInputFileLocation(Win32State, true, PlayingIndex, sizeof(FileName), FileName);

      Win32State->PlayingHandle = CreateFileA(FileName, GENERIC_READ,
                                              0, 0, OPEN_EXISTING, 0, 0);
#if 0
      LARGE_INTEGER FiliPosition;
      FilePosition.QuadPart = Win32State->TotalSize;
      SetFilePointerEx(Win32State->PlayingHandle, FilePosition, 0, FILE_BEGIN);
#endif

      CopyMemory(Win32State->GameMemoryBlock, ReplayBuffer->MemoryMap, Win32State->TotalSize);
   }
}

internal void
Win32ClosePlayingHandle(win32_state *Win32State)
{
   CloseHandle(Win32State->PlayingHandle);
   Win32State->PlayingIndex = 0;
}

internal void
Win32RecordInput(win32_state *Win32State, game_input *Input)
{
   DWORD BytesRecorded;
   WriteFile(Win32State->RecordingHandle, Input, sizeof(*Input), &BytesRecorded, 0);
}

internal void
Win32PlayInput(win32_state *Win32State, game_input *Input)
{
   DWORD BytesRead = 0;
   if(ReadFile(Win32State->PlayingHandle, Input, sizeof(*Input), &BytesRead, 0))
   {
      if(BytesRead == 0)
      {
         int PlayingIndex = Win32State->PlayingIndex;
         Win32ClosePlayingHandle(Win32State);
         Win32OpenPlayingHandle(Win32State, PlayingIndex);
         ReadFile(Win32State->PlayingHandle, Input, sizeof(*Input), &BytesRead, 0);
      }
   }
}

internal void
Win32ProcessPendingMessages(win32_state *Win32State, game_controller_input *KeyboardController)
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
#if HANDMADE_INTERNAL
                  else if(VCode == 'P')
                  {
                     if(IsDown)
                     {
                        GlobalPause = !GlobalPause;
                     }
                  }
                  else if(VCode == 'L')
                  {
                     if(IsDown)
                     {
                        if(Win32State->PlayingIndex == 0)
                        {
                           if(Win32State->RecordingIndex == 0)
                           {
                              Win32OpenRecordingHandle(Win32State, 1);
                              Win32State->RecordingIndex = 1;
                           }
                           else
                           {
                              Win32CloseRecordingHandle(Win32State);
                              Win32OpenPlayingHandle(Win32State, 1);
                           }
                        }
                        else
                        {
                           Win32ClosePlayingHandle(Win32State);
                        }
                     }
                  }
#endif
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
#if 0
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
#endif

int CALLBACK 
WinMain(HINSTANCE Instance,
        HINSTANCE PrevInstance,
        LPSTR     CommandLine,
        int       ShowCode)
{
   win32_state Win32State = {};

   LARGE_INTEGER PerfCountFrequencyResult;
   QueryPerformanceFrequency(&PerfCountFrequencyResult);
   GlobalPerfCountFrequency = PerfCountFrequencyResult.QuadPart;

   Win32GetEXEFilename(&Win32State);

   char SourceGameCodeFullPath[WIN32_STATE_FILE_NAME_COUNT];
   Win32BuildEXEPathFilename(&Win32State, "handmade.dll",
                             sizeof(SourceGameCodeFullPath), SourceGameCodeFullPath);

   char TempGameCodeFullPath[WIN32_STATE_FILE_NAME_COUNT];
   Win32BuildEXEPathFilename(&Win32State, "handmade_temp.dll",
                             sizeof(TempGameCodeFullPath), TempGameCodeFullPath);


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

   if(RegisterClassA(&WindowClass))
   {
      HWND Window = 
         CreateWindowExA(0, //WS_EX_TOPMOST|WS_EX_LAYERED,
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
         win32_sound_output SoundOutput = {};

         int MonitorRefreshHz = 60;
         HDC RefreshDC = GetDC(Window);
         int Win32RefreshRate = GetDeviceCaps(RefreshDC, VREFRESH);
         ReleaseDC(Window, RefreshDC);
         if(Win32RefreshRate > 1)
         {
            MonitorRefreshHz = Win32RefreshRate;
         }
         real32 GameUpdateHz = (MonitorRefreshHz / 2.0f);
         real32 TargetSecondsPerFrame = 1.0f / (real32)GameUpdateHz;

         SoundOutput.SamplesPerSecond = 48000;
         SoundOutput.BytesPerSample = sizeof(int16)*2;
         SoundOutput.SafetyBytes = (int)((real32)SoundOutput.SamplesPerSecond *
                                         (real32)SoundOutput.BytesPerSample /
                                         GameUpdateHz / 3.0f);
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
         Win32State.TotalSize = TotalSize;
         Win32State.GameMemoryBlock = VirtualAlloc(BaseAddress, (size_t)TotalSize,
                                                   MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
         GameMemory.PermanentStorage = Win32State.GameMemoryBlock;
         GameMemory.TransientStorage = ((uint8 *)GameMemory.PermanentStorage +
                                        GameMemory.PermanentStorageSize);

         for(int ReplayIndex = 0;
             ReplayIndex < ArrayCount(Win32State.ReplayBuffers);
             ++ReplayIndex)
         {
            win32_replay_buffer *ReplayBuffer = &Win32State.ReplayBuffers[ReplayIndex];
            Win32GetInputFileLocation(&Win32State, false, ReplayIndex,
                                      sizeof(ReplayBuffer->Filename),
                                      ReplayBuffer->Filename);

            ReplayBuffer->FileHandle = CreateFileA(ReplayBuffer->Filename,
                                                   GENERIC_READ | GENERIC_WRITE,
                                                   0, 0, CREATE_ALWAYS, 0, 0);

            LARGE_INTEGER MaxSize;
            MaxSize.QuadPart = Win32State.TotalSize;
            DWORD MaxSizeHight = (DWORD)(Win32State.TotalSize >> 32);
            DWORD MaxSizeLow = (DWORD)(Win32State.TotalSize & 0xFFFFFFFF);
            ReplayBuffer->MemoryMapHandle = CreateFileMapping(ReplayBuffer->FileHandle, 
                                                              0,
                                                              PAGE_READWRITE, 
                                                              MaxSize.HighPart,
                                                              MaxSize.LowPart,
                                                              0);

            ReplayBuffer->MemoryMap = MapViewOfFile(ReplayBuffer->MemoryMapHandle, FILE_MAP_ALL_ACCESS,
                                                    0,
                                                    0,
                                                    Win32State.TotalSize);
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

               Win32ProcessPendingMessages(&Win32State, NewKeyboardController);

               if(!GlobalPause)
               {

                  POINT MouseP;
                  GetCursorPos(&MouseP);
                  ScreenToClient(Window, &MouseP);
                  NewInput->MouseX = MouseP.x;
                  NewInput->MouseY = MouseP.y;
                  NewInput->MouseZ = 0;
                  //                  NewInput->MouseButtons[0] = 
                  //                  NewInput->MouseButtons[1] = 
                  //                  NewInput->MouseButtons[2] = 
                  Win32ProcessKeyboardMessage(&NewInput->MouseButtons[0],
                                              GetKeyState(VK_LBUTTON) & (1 << 15));
                  Win32ProcessKeyboardMessage(&NewInput->MouseButtons[1],
                                              GetKeyState(VK_RBUTTON) & (1 << 15));
                  Win32ProcessKeyboardMessage(&NewInput->MouseButtons[2],
                                              GetKeyState(VK_MBUTTON) & (1 << 15));
                  Win32ProcessKeyboardMessage(&NewInput->MouseButtons[3],
                                              GetKeyState(VK_XBUTTON1) & (1 << 15));
                  Win32ProcessKeyboardMessage(&NewInput->MouseButtons[4],
                                              GetKeyState(VK_XBUTTON2) & (1 << 15));


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
                        NewController->IsAnalog = OldController->IsAnalog;
                        XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;


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

                  thread_context Thread = {};

                  game_offscreen_buffer Buffer = {};
                  Buffer.Memory = GlobalBackBuffer.Memory;
                  Buffer.Width = GlobalBackBuffer.Width;
                  Buffer.Height = GlobalBackBuffer.Height;
                  Buffer.Pitch = GlobalBackBuffer.Pitch;
                  Buffer.BytesPerPixel = GlobalBackBuffer.BytesPerPixel;

                  if(Win32State.RecordingIndex == 1)
                  {
                     Win32RecordInput(&Win32State, NewInput);
                  }
                  if(Win32State.PlayingIndex == 1)
                  {
                     Win32PlayInput(&Win32State, NewInput);
                  }

                  if(Game.UpdateAndRender)
                  {
                     Game.UpdateAndRender(&Thread, &GameMemory, NewInput, &Buffer);
                  }

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
#if 0
                     char TextBuffer[256];
                     sprintf_s(TextBuffer, sizeof(TextBuffer),
                               "PC:%u WC:%u BTL:%u TC:%u BTW:%u FrameBoundaryByte:%u FrameBytes:%u\n"
                               "BytesFromStartToPlayCursor:%u AudioLatencyBytes:%u\n"
                               "TimeFromStartFrameToPlayCursor:%f\n",
                               PlayCursor, WriteCursor, ByteToLock, TargetCursor, BytesToWrite,
                               FrameBoundaryByte, FrameBytes, BytesFromStartToPlayCursor,
                               AudioLatencyBytes, TimeFromStartToPlayCursor);
                     OutputDebugStringA(TextBuffer);
#endif

                     game_sound_output_buffer SoundBuffer = {};

                     SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
                     SoundBuffer.SampleCount = BytesToWrite / SoundOutput.BytesPerSample;
                     SoundBuffer.Samples = Samples;
                     if(Game.GetSoundSamples)
                     {
                        Game.GetSoundSamples(&Thread, &GameMemory, &SoundBuffer);
                     }

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
                     Marker.FlipPlayCursor = PlayCursor1;
                     Marker.FlipWriteCursor = WriteCursor1;
                  }

                  HDC DeviceContext = GetDC(Window);
                  Win32DisplayBufferInWindow(&GlobalBackBuffer, DeviceContext,
                                             Dimension.Width, Dimension.Height);
                  ReleaseDC(Window, DeviceContext);
                  FlipCounter = Win32GetWallClock();


                  game_input *Temp = NewInput;
                  NewInput = OldInput;
                  OldInput = Temp;

                  uint64 EndCycleCount = __rdtsc();
                  uint64 CyclesElapsed = EndCycleCount - LastCycleCount;
                  LastCycleCount = EndCycleCount;
#if 0
                  real64 FPS = 0.0f;
                  real64 MCPF = (real64)(CyclesElapsed / (1000.0f * 1000.0f));
                  char FPSBuffer[256];
                  sprintf_s(FPSBuffer, sizeof(FPSBuffer),
                            "%.02fms/f, %.02fFPS - %.02fmc/f\n", MSPerFrame, FPS, MCPF);
                  OutputDebugStringA(FPSBuffer);
#endif
               }
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
