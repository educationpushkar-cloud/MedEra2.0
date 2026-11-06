@echo off
setlocal
cd /d "%~dp0.."
where g++ >nul 2>nul
if errorlevel 1 (
  echo g++ was not found. Install MinGW-w64 UCRT64 and add its bin folder to PATH.
  exit /b 1
)

set "PORT=%~1"
if "%PORT%"=="" set "PORT=8080"

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic tests\api_regression_tests.cpp -o tests\api_regression_tests.exe -lws2_32
if errorlevel 1 exit /b 1

echo Make sure MedEra is already running on port %PORT%.
tests\api_regression_tests.exe %PORT%
exit /b %errorlevel%
