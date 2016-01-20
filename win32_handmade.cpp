
#include <windows.h>
#include <stdint.h>
#include <xinput.h>
#include <dsound.h>

#define internal static
#define local_persist static
#define global_variable static

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int32 bool32;

struct win32_offscreen_buffer
{
    BITMAPINFO Info;
    void *Memory;
    int Width;
    int Height;
    int Pitch;
    int BytesPerPixel;
};

struct win32_window_dimension
{
   int Width;
   int Height;
};

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

global_variable bool GlobalRunning;
global_variable win32_offscreen_buffer GlobalBackBuffer;

internal void
Win32LoadXInput()
{
   HMODULE XInputLibrary = LoadLibraryA("xinput1_4.dll");
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

         LPDIRECTSOUNDBUFFER SecondaryBuffer;
         HRESULT Error = DirectSound->CreateSoundBuffer(&BufferDescription, &SecondaryBuffer, 0);
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
RenderWeirdGradient(win32_offscreen_buffer *Buffer, int XOffset, int YOffset)
{
    uint8 *Row = (uint8 *)Buffer->Memory;
    for(int Y = 0;
        Y < Buffer->Height;
        ++Y)
    {
       uint32 *Pixel = (uint32 *)Row;
       for(int X = 0;
           X < Buffer->Width;
           ++X)
       {
          uint8 Blue = (X + XOffset);
          uint8 Green = (Y + YOffset);

          *Pixel++ = ((Green << 8) | Blue);
       }
       Row += Buffer->Pitch;
    }
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
           uint32 VCode = WParam;
           bool WasDown = ((LParam & (1 << 30)) != 0);
           bool IsDown = ((LParam & (1 << 31)) == 0);

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
               }
               else if(VCode == 'E')
               {
               }
               else if(VCode == VK_UP)
               {
               }
               else if(VCode == VK_LEFT)
               {
               }
               else if(VCode == VK_DOWN)
               {
               }
               else if(VCode == VK_RIGHT)
               {
               }
               else if(VCode == VK_ESCAPE)
               {
                  OutputDebugString("ESCAPE: ");
                  if(WasDown)
                  {
                     OutputDebugString("WasDown");
                  }
                  if(IsDown)
                  {
                     OutputDebugString(" IsDown");
                  }
                  OutputDebugString("\n");
               }
               else if(VCode == VK_SPACE)
               {
               }
           }
           bool32 AltKeyWasDown = (LParam & (1 << 29));
           if((VCode == VK_F4) && AltKeyWasDown)
           {
              GlobalRunning = false;
           }

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
            Result = DefWindowProc(Window, Message, WParam, LParam);
        } break;
    }

    return Result;
}



int CALLBACK 
WinMain(HINSTANCE Instance,
        HINSTANCE PrevInstance,
        LPSTR     CommandLine,
        int       ShowCode)
{
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

            int XOffset = 0;
            int YOffset = 0;

            Win32InitDSound(Window, 48000, 48000*sizeof(int16)*2);
            GlobalRunning = true;
            while(GlobalRunning)
            {
                MSG Message;
                while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
                {
                    if(Message.message == WM_QUIT)
                    {
                       GlobalRunning = false;
                    }
                    TranslateMessage(&Message);
                    DispatchMessageA(&Message);
                }
                for(DWORD ControllerIndex = 0;
                    ControllerIndex < XUSER_MAX_COUNT;
                    ++ControllerIndex)
                {
                   XINPUT_STATE ControllerState;
                   if(XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS)
                   {
                      XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;

                      bool Up = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                      bool Down = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                      bool Left = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                      bool Right = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
                      bool Start = (Pad->wButtons & XINPUT_GAMEPAD_START);
                      bool Back = (Pad->wButtons & XINPUT_GAMEPAD_BACK);
                      bool LeftThumb = (Pad->wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
                      bool RightThumb = (Pad->wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
                      bool AButton = (Pad->wButtons & XINPUT_GAMEPAD_A);
                      bool BButton = (Pad->wButtons & XINPUT_GAMEPAD_B);
                      bool XButton = (Pad->wButtons & XINPUT_GAMEPAD_X);
                      bool YButton = (Pad->wButtons & XINPUT_GAMEPAD_Y);

                      int16 StickX = Pad->sThumbLX;
                      int16 StickY = Pad->sThumbLY;

                      XOffset -= StickX >> 12;
                      YOffset += StickY >> 12;
                   }
                   else
                   {
                      // NOTE(maximqa): The controller is not available
                   }
                }

                RenderWeirdGradient(&GlobalBackBuffer, XOffset, YOffset);

                win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                Win32DisplayBufferInWindow(&GlobalBackBuffer, DeviceContext,
                                           Dimension.Width, Dimension.Height,
                                           0, 0, Dimension.Width, Dimension.Height);
                ReleaseDC(Window, DeviceContext);

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
