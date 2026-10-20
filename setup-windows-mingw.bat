@echo off
setlocal

if "%MSYS2_ROOT%"=="" set "MSYS2_ROOT=C:\msys64"
if not exist "%MSYS2_ROOT%\usr\bin\bash.exe" (
    echo MSYS2 was not found at "%MSYS2_ROOT%".
    echo Install MSYS2 from https://www.msys2.org/ or set MSYS2_ROOT to its install folder.
    exit /b 1
)

"%MSYS2_ROOT%\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-pkgconf mingw-w64-x86_64-gtk3 mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_mixer mingw-w64-x86_64-fluidsynth mingw-w64-x86_64-lua54 mingw-w64-x86_64-nlohmann-json mingw-w64-x86_64-libepoxy"
if errorlevel 1 (
    echo Dependency installation failed. Review the pacman output above.
    exit /b 1
)

echo Dependencies are ready. Open an MSYS2 MINGW64 shell in this folder and run: make
exit /b 0
