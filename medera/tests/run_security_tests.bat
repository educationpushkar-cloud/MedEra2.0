@echo off
setlocal
cd /d "%~dp0.."
where g++ >nul 2>nul
if errorlevel 1 (
  echo g++ was not found. Install MinGW-w64 UCRT64 and add its bin folder to PATH.
  exit /b 1
)

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude tests\security_tests.cpp src\security.cpp -o tests\security_tests.exe -lbcrypt
if errorlevel 1 exit /b 1

tests\security_tests.exe
exit /b %errorlevel%
