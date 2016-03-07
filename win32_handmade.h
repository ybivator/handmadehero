#ifndef WIN32_HANDMADE_H 
#define WIN32_HANDMADE_H

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

struct win32_sound_output
{
   DWORD SafetyBytes;
   int SamplesPerSecond;
   uint32 RunningSampleIndex;
   int BytesPerSample;
   DWORD SecondaryBufferSize;
};

struct win32_debug_time_marker
{
   DWORD ByteToLock;
   DWORD EndBufferByte;
   DWORD FrameBeforeBoundary;
   DWORD FrameBoundaryByte;
   DWORD PlayCursor;
   DWORD WriteCursor;

   DWORD FlipPlayCursor;
   DWORD FlipWriteCursor;
};

struct win32_game_code
{
   HMODULE GameCodeDLL;
   FILETIME LastWriteTime;
   game_update_and_render *UpdateAndRender;
   game_get_sound_samples *GetSoundSamples;
   bool32 IsValid;
};




#endif
