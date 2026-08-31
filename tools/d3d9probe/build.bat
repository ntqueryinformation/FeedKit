// d3d9probe build script (32-bit)
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
cd /d "%~dp0"
cl /nologo /EHsc /O2 d3d9probe.cpp d3d9.lib user32.lib gdi32.lib /Fe:d3d9probe.exe /link /SUBSYSTEM:WINDOWS
echo build exit: %ERRORLEVEL%
