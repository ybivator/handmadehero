@echo off

mkdir ..\..\build
pushd ..\..\build
cl /FC -Zi ..\handmade\src\win32_handmade.cpp user32.lib gdi32.lib
popd

IF %ERRORLEVEL% EQU 0 (echo.
                       echo Compilation finished successfully)
