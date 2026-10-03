@echo off
setlocal
cd /d "%~dp0"
where g++ >nul 2>nul
if errorlevel 1 (
  echo g++ was not found. Install MinGW-w64 UCRT64 and add its bin folder to PATH.
  exit /b 1
)
set "OUTPUT_NAME=medera.exe"
if not "%~1"=="" set "OUTPUT_NAME=%~1"
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -Wall -Wextra -Wpedantic -Iinclude src\main.cpp src\security.cpp src\ranking.cpp -o "%OUTPUT_NAME%" -lws2_32 -lbcrypt -lcrypt32
if errorlevel 1 exit /b 1
echo.
echo Built %OUTPUT_NAME%
echo Run it from this folder: %OUTPUT_NAME% [port]
echo Open http://127.0.0.1:8080, or use your chosen port.
