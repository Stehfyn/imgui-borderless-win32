@echo off
setlocal

rem Regenerates the Dear Bindings C wrappers in generated\dear_bindings from
rem the imgui submodule. Invoked by the GenerateDearBindings MSBuild target
rem before compilation; can also be run standalone.

set "SCRIPT_DIR=%~dp0"
set "DEAR_BINDINGS=%SCRIPT_DIR%..\external\dear_bindings"
set "IMGUI=%SCRIPT_DIR%imgui"
set "OUT=%SCRIPT_DIR%generated\dear_bindings"

where python >nul 2>nul
if errorlevel 1 (
    if exist "%OUT%\dcimgui.h" (
        echo warning: python not found on PATH; using existing dear_bindings output, which may be stale relative to the imgui submodule
        exit /b 0
    )
    echo error: python not found on PATH and no generated bindings exist at "%OUT%"
    exit /b 1
)

if not exist "%OUT%\backends" mkdir "%OUT%\backends"

echo Generating dcimgui
python "%DEAR_BINDINGS%\dear_bindings.py" -o "%OUT%\dcimgui" "%IMGUI%\imgui.h"
if errorlevel 1 exit /b 1

echo Generating dcimgui_internal
python "%DEAR_BINDINGS%\dear_bindings.py" -o "%OUT%\dcimgui_internal" --include "%IMGUI%\imgui.h" "%IMGUI%\imgui_internal.h"
if errorlevel 1 exit /b 1

echo Generating dcimgui_impl_win32
python "%DEAR_BINDINGS%\dear_bindings.py" --backend --include "%IMGUI%\imgui.h" --imconfig-path "%IMGUI%\imconfig.h" -o "%OUT%\backends\dcimgui_impl_win32" "%IMGUI%\backends\imgui_impl_win32.h"
if errorlevel 1 exit /b 1

echo Generating dcimgui_impl_opengl3
python "%DEAR_BINDINGS%\dear_bindings.py" --backend --include "%IMGUI%\imgui.h" --imconfig-path "%IMGUI%\imconfig.h" -o "%OUT%\backends\dcimgui_impl_opengl3" "%IMGUI%\backends\imgui_impl_opengl3.h"
if errorlevel 1 exit /b 1

exit /b 0
