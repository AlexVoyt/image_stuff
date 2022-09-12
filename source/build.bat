@echo off
setlocal ENABLEDELAYEDEXPANSION
REM Build for Visual Studio compiler. Run your copy of vcvars32.bat or vcvarsall.bat to setup command-line compiler.
set OUT_DIR=..\Build
set OUT_EXE=main
set SDL2_DIR="C:\VS_Libs\SDL2-2.0.22"
set INCLUDES=/I"imgui" /I"%SDL2_DIR%/include"
if "%1%" == "full" (
    @set SOURCES=main.cpp imgui\imgui*.cpp
    @set LIBS=/LIBPATH:%SDL2_DIR%\VisualC\x64\Release SDL2.lib SDL2main.lib opengl32.lib shell32.lib
) else (
    @set SOURCES=main.cpp
    @set LIBS=..\Build\imgui*.obj /LIBPATH:%SDL2_DIR%\VisualC\x64\Release SDL2.lib SDL2main.lib opengl32.lib shell32.lib
)
mkdir %OUT_DIR%
cl /nologo /EHsc /Zi /Od /MD %INCLUDES% /D UNICODE /D _UNICODE %SOURCES% /Fe%OUT_DIR%/%OUT_EXE%.exe /Fo%OUT_DIR%/ /link %LIBS% /subsystem:console

