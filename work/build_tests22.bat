@echo off
setlocal
rem Usage: build_tests22.bat [clean]
rem   "clean" throws the build directory away first (make_release.bat does that;
rem   normal development builds are incremental).
rem
rem Header dependencies work again: fix_showincludes_prefix.ps1 repairs the
rem /showIncludes prefix CMake recorded as mojibake (see docs/05-build-env.md).
set VSLANG=1033
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH set "VSPATH=C:\Program Files\Microsoft Visual Studio\18\Enterprise"
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" (
  echo MSVC_NOT_FOUND: install Visual Studio with the Desktop C++ workload
  exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
set "SRC=%~dp0testproj"
set "BLD=%~dp0test-build22"
if /I "%1"=="clean" (
  if exist "%BLD%\build.ninja" (
    echo %BLD% | findstr /C:"\work\test-build" > nul || (echo REFUSING_TO_CLEAN & exit /b 1)
    rmdir /s /q "%BLD%"
  )
)
if not exist "%BLD%" mkdir "%BLD%"
if not exist "%BLD%\build.ninja" (
  cmake -S "%SRC%" -B "%BLD%" -G Ninja -DCMAKE_BUILD_TYPE=Release > "%BLD%\configure.log" 2>&1
  if errorlevel 1 ( echo CONFIGURE_FAILED & findstr /C:"Error" "%BLD%\configure.log" & exit /b 1 )
)
set PREFIX_STATE=OK
for /f "delims=" %%r in ('powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fix_showincludes_prefix.ps1" "%BLD%"') do set PREFIX_STATE=%%r
if /I not "%PREFIX_STATE%"=="OK" (
  rem The existing dependency database was recorded with the broken prefix.
  cmake --build "%BLD%" --target clean >nul 2>&1
)
cmake -S "%SRC%" -B "%BLD%" -G Ninja -DCMAKE_BUILD_TYPE=Release > "%BLD%\configure.log" 2>&1
if errorlevel 1 ( echo CONFIGURE_FAILED & findstr /C:"Error" "%BLD%\configure.log" & exit /b 1 )
echo CONFIGURE_OK
cmake --build "%BLD%" --target HMDAWTests > "%BLD%\build.log" 2>&1
rem The log is checked as well as the exit code: a failing vs_link_exe comes back as
rem -1, which "if errorlevel 1" treats as success.
set BUILD_STATUS=0
if errorlevel 1 set BUILD_STATUS=1
findstr /C:"ninja: build stopped" /C:": error " /C:"fatal error" /C:"FAILED:" "%BLD%\build.log" > nul
if not errorlevel 1 set BUILD_STATUS=1
if "%BUILD_STATUS%"=="1" ( echo BUILD_FAILED & findstr /C:": error " /C:"fatal error" /C:"FAILED:" "%BLD%\build.log" & exit /b 1 )
rem A warning-free build is the standard now (see docs/06-testing.md). Warnings
rem that point at HMDAW's own Source fail the build; warnings from external/JUCE
rem are not our business here.
rem
rem Two steps, no pipe: a piped findstr swallowed the exit code, so a warning in
rem Source could go straight past this gate. The path pattern has no trailing
rem backslash on purpose - inside cmd, a pattern that ends with one right before
rem its closing quote is not passed intact.
findstr /C:"warning C" "%BLD%\build.log" > "%BLD%\warnings.txt"
findstr /L /C:"HMDAW\Source" "%BLD%\warnings.txt" > "%BLD%\source-warnings.txt"
if errorlevel 1 goto :nowarnings
echo WARNINGS_IN_SOURCE_CODE
type "%BLD%\source-warnings.txt"
exit /b 1
:nowarnings
echo BUILD_OK
endlocal
exit /b 0
