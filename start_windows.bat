@echo off
setlocal
cd /d "%~dp0"
echo ===============================================
echo   ECHO - C DSA Music Management Application
echo ===============================================
echo.
where gcc >nul 2>nul
if errorlevel 1 (
  echo GCC compiler not found.
  echo Install MSYS2 / MinGW-w64, add its bin folder to PATH, then rerun.
  echo Download MSYS2 at https://www.msys2.org/
  pause
  exit /b 1
)
echo Compiling C backend...
gcc -std=c11 -O2 -o echo_server.exe backend\server.c backend\music.c backend\json.c -lws2_32
if errorlevel 1 (
  echo.
  echo Compilation failed. Review errors above.
  pause
  exit /b 1
)
echo.
echo Starting local server on http://localhost:8000
start "" "http://localhost:8000"
echo Close this window or press Ctrl+C to stop.
echo.
echo_server.exe
pause
