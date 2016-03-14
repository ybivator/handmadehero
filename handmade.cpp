#include "handmade.h"
#include <windows.h>
#include <stdio.h>

internal void
GameOutputSound(game_state *GameState, game_sound_output_buffer *SoundBuffer, int ToneHz)
{
    int16 ToneVolume = 3000;
    int WavePeriod = SoundBuffer->SamplesPerSecond / ToneHz;

    int16 *SampleOut = SoundBuffer->Samples;

    for(int SampleIndex = 0;
        SampleIndex < SoundBuffer->SampleCount;
        ++SampleIndex)
    {
       real32 SineValue = sinf(GameState->tSine);
       int16 SampleValue = (int16)(SineValue * ToneVolume);

       *SampleOut++ = SampleValue;
       *SampleOut++ = SampleValue; 
#if 1
       GameState->tSine += (2.0f*Pi32*1.0f/(real32)WavePeriod);
       if(GameState->tSine > (2.0f*Pi32))
       {
          GameState->tSine -= (2.0f*Pi32);
       }
#endif
    }
}
    
internal void
RenderWeirdGradient(game_offscreen_buffer *Buffer, int BlueOffset, int GreenOffset)
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
         uint8 Blue = (uint8)(X + BlueOffset);
         uint8 Green = (uint8)(Y + GreenOffset);

         *Pixel++ = ((Green << 16) | Blue);
      }
      Row += Buffer->Pitch;
   }
}
internal void
RenderPlayer(game_offscreen_buffer *Buffer, int PlayerX, int PlayerY)
{
   uint32 Color = 0xFFFFFFFF;
   uint8 *EndOfBuffer = (uint8 *)Buffer->Memory + 
           (Buffer->Height * Buffer->Pitch);

   int Top = PlayerY;
   int Bottom = PlayerY + 10;
   for(int X = PlayerX;
       X < PlayerX + 10;
       ++X)
   {
      uint8 *Pixel = ((uint8 *)Buffer->Memory + 
                      X * Buffer->BytesPerPixel + 
                      Top*Buffer->Pitch);
      for(int Y = Top;
          Y < Bottom;
          ++Y)
      {
         if((Pixel >= Buffer->Memory) &&
            ((Pixel + 4) <= EndOfBuffer))
         {
            *(uint32 *)Pixel = Color;
         }
         Pixel += Buffer->Pitch;
      }
   }

}

extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{

   Assert((&Input->Controllers[0].Terminator - &Input->Controllers[0].Buttons[0]) ==
          (ArrayCount(Input->Controllers[0].Buttons)));
   Assert(sizeof(game_state) <= Memory->PermanentStorageSize);

   game_state *GameState = (game_state *)Memory->PermanentStorage;
   if(!Memory->IsInitialized)
   {
      char *Filename = __FILE__;

      debug_read_file_result File = Memory->DEBUGPlatformReadEntireFileIntoMemory(Thread, Filename);
      if(File.Contents)
      {
         Memory->DEBUGPlatformWriteEntireFileIntoMemory(Thread, "test.out", File.ContentsSize, File.Contents);
         Memory->DEBUGPlatformFreeFileMemory(Thread, File.Contents);
      }


      GameState->ToneHz = 512;
      GameState->PlayerX = 100;
      GameState->PlayerY = 100;

      Memory->IsInitialized = true;
   }


   for(int ControllerIndex = 0;
       ControllerIndex < ArrayCount(Input->Controllers);
       ++ControllerIndex)
   {
      game_controller_input *Controller = GetController(Input, ControllerIndex);
      if(Controller->IsAnalog)
      {
         GameState->BlueOffset += (int)(4.0f * Controller->StickAvarageX);
         GameState->ToneHz = 512 + (int)(128.0f * (Controller->StickAvarageY));
      }
      else
      {
         if(Controller->MoveLeft.EndedDown)
         {
            GameState->BlueOffset -= 1;
         }
         if(Controller->MoveRight.EndedDown)
         {
            GameState->BlueOffset += 1;
         }

      }
      GameState->PlayerX += (int)(4.0f * Controller->StickAvarageX);
      GameState->PlayerY -= (int)(4.0f * Controller->StickAvarageY);
      if(GameState->tJump > 0)
      {
         GameState->PlayerY += (int)(10.0f*sinf(0.5f*Pi32*GameState->tJump));
      }
      if(Controller->ActionDown.EndedDown)
      {
         GameState->tJump = 4.0f;
      }
      GameState->tJump -= 0.033f;
   }

   RenderWeirdGradient(Buffer, GameState->BlueOffset, GameState->GreenOffset);
   RenderPlayer(Buffer, GameState->PlayerX, GameState->PlayerY);

   RenderPlayer(Buffer, Input->MouseX, Input->MouseY);

   for(int ButtonIndex = 0;
       ButtonIndex < ArrayCount(Input->MouseButtons);
       ++ButtonIndex)
   {
      if(Input->MouseButtons[ButtonIndex].EndedDown)
      {
         RenderPlayer(Buffer, 10 + 20*ButtonIndex, 10);
      }
   }
}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
   game_state *GameState = (game_state *)Memory->PermanentStorage;
   GameOutputSound(GameState, SoundBuffer, GameState->ToneHz);
}
