#include <stdint.h>
#include <math.h>

#define internal static
#define local_persist static
#define global_variable static

#define Pi32 3.14159265359f

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int32 bool32;

typedef float real32;
typedef double real64;

#include "handmade.cpp"

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


internal debug_read_file_result
DEBUGPlatformReadEntireFileIntoMemory(char *Filename)
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

internal void 
DEBUGPlatformFreeFileMemory(void *Memory)
{
   if(Memory)
   {
      VirtualFree(Memory, 0, MEM_RELEASE);
   }
}
internal bool32 
DEBUGPlatformWriteEntireFileIntoMemory(char *Filename, uint32 MemorySize, void *Memory)
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
      direct_sound_create *DirectSoundCreate = (direct_sound_create *)GetProcAddress(DSoundLibrary, "DirectSoundCreate");

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
         BufferDescription.dwFlags = 0;
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
    Buffer->BytesPerPixel = 4;

    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BitmapMemorySize = (Buffer->Width*Buffer->Height)*Buffer->BytesPerPixel;
    Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    Buffer->Pitch = Width*Buffer->BytesPerPixel;
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
                  }
                  else if(VCode == 'A')
                  {
                  }
                  else if(VCode == 'S')
                  {
                  }
                  else if(VCode == 'D')
                  {
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
                     Win32ProcessKeyboardMessage(&KeyboardController->Up, IsDown);
                  }
                  else if(VCode == VK_LEFT)
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->Left, IsDown);
                  }
                  else if(VCode == VK_DOWN)
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->Down, IsDown);
                  }
                  else if(VCode == VK_RIGHT)
                  {
                     Win32ProcessKeyboardMessage(&KeyboardController->Right, IsDown);
                  }
                  else if(VCode == VK_ESCAPE)
                  {
                     GlobalRunning = false;
                  }
                  else if(VCode == VK_SPACE)
                  {
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

int CALLBACK 
WinMain(HINSTANCE Instance,
        HINSTANCE PrevInstance,
        LPSTR     CommandLine,
        int       ShowCode)
{
    LARGE_INTEGER PerfCountFrequencyResult;
    QueryPerformanceFrequency(&PerfCountFrequencyResult);
    int64 PerfCountFrequency = PerfCountFrequencyResult.QuadPart;

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
            SoundOutput.LatencySampleCount = SoundOutput.SamplesPerSecond / 15;
            SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond*SoundOutput.BytesPerSample;

            Win32InitDSound(Window, SoundOutput.SamplesPerSecond, SoundOutput.SecondaryBufferSize);
            Win32ClearBuffer(&SoundOutput);
            GlobalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);

            GlobalRunning = true;

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
   
               LARGE_INTEGER LastCounter;
               QueryPerformanceCounter(&LastCounter);
   
               uint64 LastCycleCount = __rdtsc();
   
               while(GlobalRunning)
               {
                   game_controller_input *KeyboardController = &NewInput->Controllers[0];
                   game_controller_input ZeroController = {};
                   *KeyboardController = ZeroController;

                   Win32ProcessPendingMessages(KeyboardController);

                   DWORD MaxControllerCount = XUSER_MAX_COUNT;
                   if(MaxControllerCount > ArrayCount(NewInput->Controllers))
                   {
                      MaxControllerCount = ArrayCount(NewInput->Controllers);
                   }
   
                   for(DWORD ControllerIndex = 0;
                       ControllerIndex < MaxControllerCount;
                       ++ControllerIndex)
                   {
                      game_controller_input *OldController = &OldInput->Controllers[ControllerIndex];
                      game_controller_input *NewController = &NewInput->Controllers[ControllerIndex];
   
                      XINPUT_STATE ControllerState;
                      if(XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS)
                      {
                         XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;
           
                         bool32 Up = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                         bool32 Down = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                         bool32 Left = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                         bool32 Right = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
   
                         NewController->IsAnalog = true;
                         NewController->StartX = OldController->EndX;
                         NewController->StartY = OldController->EndY;
   
                         real32 X;
                         if(Pad->sThumbLX < 0)
                         {
                            X = (real32)Pad->sThumbLX / 32768.0f;
                         }
                         else
                         {
                            X = (real32)Pad->sThumbLX / 32767.0f;
                         }
                         NewController->MinX = NewController->MaxX = NewController->EndX = X;
   
                         real32 Y;
                         if(Pad->sThumbLY < 0)
                         {
                            Y = (real32)Pad->sThumbLY / 32768.0f;
                         }
                         else
                         {
                            Y = (real32)Pad->sThumbLY / 32767.0f;
                         }
   
                         NewController->MinY = NewController->MaxY = NewController->EndY = Y;
   
                         Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                         &OldController->Down, XINPUT_GAMEPAD_A,
                                                         &NewController->Down);
                         Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                         &OldController->Right, XINPUT_GAMEPAD_B,
                                                         &NewController->Right);
                         Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                         &OldController->Left, XINPUT_GAMEPAD_X,
                                                         &NewController->Left);
                         Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                         &OldController->Up, XINPUT_GAMEPAD_Y,
                                                         &NewController->Up);
                         Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                         &OldController->LeftShoulder, XINPUT_GAMEPAD_LEFT_THUMB,
                                                         &NewController->LeftShoulder);
                         Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                         &OldController->RightShoulder, XINPUT_GAMEPAD_RIGHT_THUMB,
                                                         &NewController->RightShoulder);
   
                         // bool32 Start = (Pad->wButtons & XINPUT_GAMEPAD_START);
                         // bool32 Back = (Pad->wButtons & XINPUT_GAMEPAD_BACK);
                      }
                      else
                      {
                         // NOTE(maximqa): The controller is not available
                      }
                   }
   
   
                   DWORD ByteToLock = 0;
                   DWORD TargetCursor = 0;
                   DWORD BytesToWrite = 0;
                   DWORD PlayCursor = 0;
                   DWORD WriteCursor = 0;
                   bool32 SoundIsValid = false;
                   if(SUCCEEDED(GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor)))
                   {
                       ByteToLock = ((SoundOutput.RunningSampleIndex * SoundOutput.BytesPerSample) %
                                            SoundOutput.SecondaryBufferSize);
                       TargetCursor = ((PlayCursor +
                                            (SoundOutput.LatencySampleCount * SoundOutput.BytesPerSample)) %
                                             SoundOutput.SecondaryBufferSize);
                       if(ByteToLock > TargetCursor)
                       {
                          BytesToWrite = SoundOutput.SecondaryBufferSize - ByteToLock;
                          BytesToWrite += TargetCursor;
                       }
                       else
                       {
                          BytesToWrite = TargetCursor - ByteToLock;
                       }
                       SoundIsValid = true;
                   }
   
                   game_sound_output_buffer SoundBuffer = {};
                   SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
                   SoundBuffer.SampleCount = BytesToWrite / SoundOutput.BytesPerSample;
                   SoundBuffer.Samples = Samples;
   
                   game_offscreen_buffer Buffer = {};
                   Buffer.Memory = GlobalBackBuffer.Memory;
                   Buffer.Width = GlobalBackBuffer.Width;
                   Buffer.Height = GlobalBackBuffer.Height;
                   Buffer.Pitch = GlobalBackBuffer.Pitch;
   
                   GameUpdateAndRender(&GameMemory, NewInput, &Buffer, &SoundBuffer);
   
                   if(SoundIsValid)
                   {
                       Win32FillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite, &SoundBuffer);
                   }
   
   
                   win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                   Win32DisplayBufferInWindow(&GlobalBackBuffer, DeviceContext,
                                              Dimension.Width, Dimension.Height,
                                              0, 0, Dimension.Width, Dimension.Height);
                   uint64 EndCycleCount = __rdtsc();
   
                   LARGE_INTEGER EndCounter;
                   QueryPerformanceCounter(&EndCounter);
   
   
                   uint64 CyclesElapsed = EndCycleCount - LastCycleCount;
                   int64 CounterElapsed = EndCounter.QuadPart - LastCounter.QuadPart;
                   real64 MSPerFrame = (real64)((1000.0f*(real64)CounterElapsed) / (real64)PerfCountFrequency);
                   real64 FPS = (real64)PerfCountFrequency / (real64)CounterElapsed;
                   real64 MCPF = (real64)(CyclesElapsed / (1000.0f * 1000.0f));
   #if 0
                   char Buffer[256];
                   sprintf(Buffer, "%.02fms/f, %.02fFPS - %.02fmc/f\n", MSPerFrame, FPS, MCPF);
                   OutputDebugStringA(Buffer);
   #endif
                   LastCounter = EndCounter;
                   LastCycleCount = EndCycleCount;
   
                   game_input *Temp = NewInput;
                   NewInput = OldInput;
                   OldInput = Temp;
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
