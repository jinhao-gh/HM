@echo off
setlocal enabledelayedexpansion
rem ============================================================================
rem  HM release pipeline: build -> unit tests -> self test -> sync -> zip
rem
rem  Takes about 5-6 minutes because the two builds are full clean rebuilds
rem  (this machine's Ninja does not track header dependencies; see
rem  HMDAW\docs\05-build-env.md). Any failing step stops the run with exit 1.
rem
rem  Outputs:
rem    outputs\HM\          dev delivery  (HM.exe + README.md + screenshots\)
rem    outputs\HM-win64\    package for other people (HM.exe + 使用说明.txt)
rem    outputs\HM-win64.zip the thing to actually send
rem
rem  Close HM before running: a running exe cannot be overwritten.
rem ============================================================================

for %%i in ("%~dp0..") do set "ROOT=%%~fi"
set "WORK=%ROOT%\work"
set "OUTDEV=%ROOT%\outputs\HM"
set "OUTPKG=%ROOT%\outputs\HM-win64"
set "ZIP=%ROOT%\outputs\HM-win64.zip"
set "APPEXE=%WORK%\app-build5\HMDAW_artefacts\Release\HM.exe"
set "TESTPLUGIN=%WORK%\app-build5\HMDAWTestInstrument_artefacts\Release\VST3\HMTestInstrument.vst3"
set "TESTEFFECT=%WORK%\app-build5\HMDAWTestEffect_artefacts\Release\VST3\HMTestEffect.vst3"
set "SELFDIR=%WORK%\release-selftest"
set "TESTLOG=%WORK%\release-tests.log"

rem Restricted sandboxes (Codex/CI) only allow writes inside the workspace, and
rem CMake, the compiler, the tests, the self test and Compress-Archive all use
rem TEMP.  Give this run its own folder under work/ and point TEMP/TMP at it; a
rem normal Windows machine is unaffected.  Every exit path removes it again.
set "TMPROOT=%WORK%\tmp-release-%RANDOM%%RANDOM%"
if not exist "%TMPROOT%" mkdir "%TMPROOT%"
set "TEMP=%TMPROOT%"
set "TMP=%TMPROOT%"

if not exist "%OUTDEV%\screenshots" mkdir "%OUTDEV%\screenshots"
if not exist "%OUTPKG%" mkdir "%OUTPKG%"
if not exist "%SELFDIR%" mkdir "%SELFDIR%"

echo [1/5] building the app (full clean rebuild) ...
call "%WORK%\build_app.bat" HMDAW clean
if errorlevel 1 goto :fail

echo.
echo [1b] signing the app with the local certificate ...
if exist "%WORK%\signing\run_sign.ps1" if exist "%WORK%\signing\thumbprint.txt" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%WORK%\signing\run_sign.ps1" "%APPEXE%"
  if errorlevel 1 goto :signfailed
) else (
  echo    no local signing certificate - the exe stays unsigned
)

echo.
echo [2/5] building the unit tests (full clean rebuild) ...
call "%WORK%\build_tests22.bat" clean
if errorlevel 1 goto :fail

echo.
echo [3/5] running the unit tests ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%WORK%\run_tests22.ps1" > "%TESTLOG%" 2>&1
type "%TESTLOG%"
findstr /c:"ALL TESTS PASSED" "%TESTLOG%" >nul
if errorlevel 1 goto :testsfailed

echo.
echo [4/5] running the offscreen self test ...
rem The test VST3 instrument is passed in so the self test really loads a plugin,
rem plays a note through it and stores its state.
rem The test VST3 effect does the same for the insert chains.
rem The self test's own folder is cleared first: it is where the fresh snapshots
rem land, and a picture of a feature that has since gone (the melody generator's
rem two, say) would otherwise be copied into the delivery for ever.
rem The self test's own folder is cleared completely, not just its pictures: it also
rem holds the preset library and the project files the run writes, and a library left
rem over from an earlier run was seen to change what the wavetable section counts
rem (three custom tables instead of two). Only this run's snapshots and log belong
rem in the delivery.
if exist "%SELFDIR%" rd /s /q "%SELFDIR%"
mkdir "%SELFDIR%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%WORK%\run_selftest.ps1" -Out "%SELFDIR%" -Exe "%APPEXE%" -Plugin "%TESTPLUGIN%" -Effect "%TESTEFFECT%"
if errorlevel 1 goto :selffailed
findstr /c:"SELFTEST PASSED" "%SELFDIR%\selftest.log" >nul
if errorlevel 1 goto :selffailed

echo.
echo [5/5] syncing the delivery folders and rebuilding the zip ...
copy /y "%APPEXE%" "%OUTDEV%\HM.exe" >nul
if errorlevel 1 goto :locked
rem The self test's snapshots are the delivery's snapshots: clear the old ones first,
rem so a picture of a feature that has gone cannot stay in the folder for ever.
del /q "%OUTDEV%\screenshots\*.png" >nul 2>&1
for %%f in ("%SELFDIR%\*.png") do copy /y "%%f" "%OUTDEV%\screenshots\" >nul
copy /y "%SELFDIR%\selftest.log" "%OUTDEV%\screenshots\selftest.log" >nul
copy /y "%APPEXE%" "%OUTPKG%\HM.exe" >nul
if errorlevel 1 goto :locked
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%OUTPKG%' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 goto :fail

if defined TMPROOT rd /s /q "%TMPROOT%" >nul 2>&1

echo.
echo ============================================================
echo  RELEASE DONE
echo    exe          : %APPEXE%
echo    dev delivery : %OUTDEV%
echo    package      : %OUTPKG%
echo    zip to send  : %ZIP%
echo ============================================================
exit /b 0

:signfailed
if defined TMPROOT rd /s /q "%TMPROOT%" >nul 2>&1
echo.
echo *** CODE SIGNING FAILED - see work\signing\ (run new_hm_signing_cert.ps1 again)
exit /b 1

:testsfailed
if defined TMPROOT rd /s /q "%TMPROOT%" >nul 2>&1
echo.
echo *** UNIT TESTS FAILED - see %TESTLOG%
exit /b 1

:selffailed
if defined TMPROOT rd /s /q "%TMPROOT%" >nul 2>&1
echo.
echo *** SELF TEST FAILED - see %SELFDIR%\selftest.log
exit /b 1

:locked
if defined TMPROOT rd /s /q "%TMPROOT%" >nul 2>&1
echo.
echo *** Could not replace an output exe.
echo     Close HM (it may still be running) and run this script again.
exit /b 1

:fail
if defined TMPROOT rd /s /q "%TMPROOT%" >nul 2>&1
echo.
echo *** RELEASE FAILED
exit /b 1
