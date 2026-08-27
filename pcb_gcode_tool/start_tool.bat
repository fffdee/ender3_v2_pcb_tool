@echo off
setlocal
cd /d "%~dp0"
set "PYTHON=C:\Users\admin\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if not exist "%PYTHON%" (
  echo Python runtime not found: %PYTHON%
  pause
  exit /b 1
)
"%PYTHON%" main.py
if errorlevel 1 pause
