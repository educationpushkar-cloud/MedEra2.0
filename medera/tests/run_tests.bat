@echo off
setlocal
cd /d "%~dp0.."
where g++ >nul 2>nul
if errorlevel 1 (
  echo g++ was not found. Install MinGW-w64 UCRT64 and add its bin folder to PATH.
  exit /b 1
)

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude tests\ranking_tests.cpp src\ranking.cpp -o tests\ranking_tests.exe
if errorlevel 1 exit /b 1

tests\ranking_tests.exe
exit /b %errorlevel%
