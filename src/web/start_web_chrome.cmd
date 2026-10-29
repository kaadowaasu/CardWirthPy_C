@echo off
setlocal
set "WEB_URL=https://127.0.0.1:8001/"
start "CardWirthPy_C Web Server" powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_local_server.ps1" -Port 8001 -Mode Https -NoBrowser
timeout /t 2 /nobreak >nul

set "CHROME=%ProgramFiles%\Google\Chrome\Application\chrome.exe"
if exist "%CHROME%" goto open_chrome
set "CHROME=%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe"
if exist "%CHROME%" goto open_chrome
set "CHROME=%LocalAppData%\Google\Chrome\Application\chrome.exe"
if exist "%CHROME%" goto open_chrome
start "" "%WEB_URL%"
exit /b 0

:open_chrome
start "" "%CHROME%" "%WEB_URL%"
exit /b 0
