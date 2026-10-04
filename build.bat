@echo off
setlocal
rem Initialize Visual Studio's C++ compiler environment when available.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
)
if defined VSINSTALL if exist "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" (
  call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
)
where cl >nul 2>nul
if %errorlevel%==0 (
  cl /nologo /EHsc /W4 hello_world.cpp /link /SUBSYSTEM:WINDOWS user32.lib /OUT:HelloWorld.exe
  exit /b %errorlevel%
)
where g++ >nul 2>nul
if %errorlevel%==0 (
  g++ -std=c++17 -Wall -Wextra -municode -mwindows hello_world.cpp -o HelloWorld.exe -luser32
  exit /b %errorlevel%
)
echo No C++ compiler found. Open Visual Studio Installer, choose Modify, and install Desktop development with C++.
exit /b 1
