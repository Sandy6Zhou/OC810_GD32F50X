@echo off
REM =====================================================
REM Bootloader Firmware Copy Tool
REM Features:
REM   1. Auto-create firmware directory if not exists
REM   2. Copy Bootloader.hex and Bootloader.bin to firmware/
REM
REM Usage:
REM   build_bootloader.bat
REM
REM Called from: Keil Bootloader After Build (Run #3)
REM =====================================================

setlocal enabledelayedexpansion

REM Auto-detect running mode
cd /d "%~dp0"
set SCRIPT_DIR=%CD%

REM Detect project structure (check if running from tools directory)
if exist "..\project\OC810" (
    REM Running in tools directory
    for %%i in ("%~dp0..") do set PROJECT_DIR=%%~fi
    for %%i in ("%~dp0..\Bootloader\project\Objects") do set BOOTLOADER_OUTPUT=%%~fi
    for %%i in ("%~dp0..") do set FIRMWARE_DIR=%%~fi\firmware
) else (
    echo [ERROR] Cannot detect project structure
    echo Current dir: %CD%
    echo.
    echo Expected structure:
    echo   tools\build_bootloader.bat
    echo   Bootloader\project\MDK-ARM\Objects\
    echo   firmware\
    pause
    exit /b 1
)

REM File paths
set BL_HEX=%BOOTLOADER_OUTPUT%\Bootloader.hex
set BL_BIN=%BOOTLOADER_OUTPUT%\Bootloader.bin

echo ========================================
echo  Bootloader Firmware Copy Tool
echo ========================================
echo.

REM Check if files exist
if not exist "%BOOTLOADER_OUTPUT%" (
    echo [ERROR] Objects directory not found: %BOOTLOADER_OUTPUT%
    echo Please build Bootloader project first
    pause
    exit /b 1
)

if not exist "%BL_HEX%" (
    echo [ERROR] Bootloader HEX not found: %BL_HEX%
    echo Please check Keil Output directory settings
    pause
    exit /b 1
)

if not exist "%BL_BIN%" (
    echo [ERROR] Bootloader BIN not found: %BL_BIN%
    echo Please add fromelf command in Keil After Build Run #1:
    echo   fromelf --bin -o .\Objects\Bootloader.bin .\Objects\Bootloader.axf
    pause
    exit /b 1
)

REM Create firmware directory if not exists
if not exist "%FIRMWARE_DIR%" (
    echo [INFO] Creating firmware directory: %FIRMWARE_DIR%
    mkdir "%FIRMWARE_DIR%"
)

REM Copy files
echo [1/2] Copying Bootloader HEX...
copy /Y "%BL_HEX%" "%FIRMWARE_DIR%\Bootloader.hex" >nul
echo [OK] Copied to: %FIRMWARE_DIR%\Bootloader.hex

echo.
echo [2/2] Copying Bootloader BIN...
copy /Y "%BL_BIN%" "%FIRMWARE_DIR%\Bootloader.bin" >nul
echo [OK] Copied to: %FIRMWARE_DIR%\Bootloader.bin

echo.
echo ========================================
echo  Complete!
echo ========================================

REM Auto mode: exit without pause (called from Keil)
exit /b 0
