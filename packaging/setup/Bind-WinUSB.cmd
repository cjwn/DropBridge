@echo off
setlocal EnableExtensions
title DropBridge - bind the network card's Wi-Fi interface to WinUSB
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 (
  echo Requesting administrator rights...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs" >nul 2>&1
  exit /b
)

echo ----------------------------------------------------------------
echo  DropBridge one-time setup: the RTL8821CU Wi-Fi interface
echo ----------------------------------------------------------------
echo.
echo %DATE% %TIME% > setup.log
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Binding.ps1"
set "CODE=%ERRORLEVEL%"
echo check exit=%CODE% >> setup.log

if "%CODE%"=="3" (
  echo.
  echo Plug the card into a rear USB port, then run this file again.
  echo.
  pause
  exit /b 3
)
if "%CODE%"=="0" (
  echo.
  echo Nothing to do. Double-click DropBridge.exe in the folder above.
  echo.
  pause
  exit /b 0
)

echo.
echo Zadig will open now. In Zadig:
echo   1. Options -^> List All Devices
echo   2. pick   RTL8821CU / 802.11ac NIC   (USB ID 0BDA C820, interface MI_02)
echo        do NOT pick "USB Composite Device", do NOT pick anything "Bluetooth"
echo   3. target driver stays   WinUSB
echo   4. press   Replace Driver   and wait for it to finish
echo.
echo Press any key to open Zadig...
pause >nul
start "" "%~dp0zadig-2.9.exe"

echo.
echo After Zadig reports the driver was installed, press any key to verify...
pause >nul

for /l %%i in (1,1,10) do (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Binding.ps1" -Quiet
  if not errorlevel 1 goto verified
  echo   ...waiting for the card to come back (%%i/10)
  timeout /t 3 >nul
)

echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Binding.ps1"
echo FAILED: unbind or replug the card and run this file again. >> setup.log
echo.
echo FAILED. Unplug and replug the card, then run this file again.
echo If it still fails, send setup.log and the text above for help.
echo.
pause
exit /b 1

:verified
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Binding.ps1"
echo verified >> setup.log
echo.
echo Done. Double-click DropBridge.exe in the folder above, then send a file
echo from the iPhone's AirDrop list (it shows up as "DropBridge").
echo.
pause
endlocal
