@echo off
rem CrackTwin editor control. Usage: editor.cmd status ^| start [-WaitMcp] ^| stop [-Force]
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0editor.ps1" %*
exit /b %ERRORLEVEL%
