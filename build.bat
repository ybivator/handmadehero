@echo off

IF NOT EXIST ..\..\build mkdir ..\..\build
pushd ..\..\build
cl -DHANDMADE_INTERNAL=1 -DHANDMADE_SLOW=1 -DHANDMADE_WIN32=1 -FC -Zi ..\handmade\src\win32_handmade.cpp user32.lib gdi32.lib
popd

IF %ERRORLEVEL% EQU 0 (echo.
                       echo Compilation finished successfully)
