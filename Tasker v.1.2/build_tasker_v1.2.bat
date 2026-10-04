@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
)
if defined VSINSTALL if exist "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
where cl >nul 2>nul
if errorlevel 1 (
  echo C++ compiler not found. Install Desktop development with C++ in Visual Studio Installer.
  exit /b 1
)
cl /nologo /EHsc /W4 /std:c++17 /c Tasker_v1.2.cpp /FoTasker_v1.2.obj
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /std:c++17 /c mcc_usb1608fsplus.cpp /Fomcc_usb1608fsplus_v1.2.obj
if errorlevel 1 exit /b 1
link /nologo /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib comdlg32.lib Tasker_v1.2.obj mcc_usb1608fsplus_v1.2.obj /OUT:Tasker_v1.2.exe
if errorlevel 1 exit /b 1
exit /b 0



