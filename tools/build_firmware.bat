@echo off
REM =====================================================
REM OC810 Firmware Build and Merge Tool
REM Features:
REM   1. Keil After Build auto merge
REM   2. Manual HEX/BIN merge
REM   3. One-click build and merge
REM
REM Usage:
REM   build_firmware.bat [hex|bin|all|auto]
REM
REM Examples:
REM   build_firmware.bat                    # Auto mode (Keil After Build)
REM   build_firmware.bat all                # Manual: merge HEX+BIN
REM   build_firmware.bat hex                # Manual: merge HEX only
REM   build_firmware.bat bin                # Manual: merge BIN only
REM =====================================================

setlocal enabledelayedexpansion

REM Parse parameter
set MERGE_TYPE=%1
if "%MERGE_TYPE%"=="" set MERGE_TYPE=auto

REM Auto-detect running mode
cd /d "%~dp0"
set SCRIPT_DIR=%CD%

REM [DEBUG] Output current directory
REM echo [DEBUG] Current dir: %CD%
REM echo [DEBUG] Checking for Project.axf...

REM Detect running environment (by priority)
if exist ".\Project.axf" (
    REM Mode 1: Keil After Build (in APP output directory)
    REM echo [DEBUG] Mode 1: Keil After Build
    set APP_OUTPUT=%CD%
    set PROJECT_DIR=%CD%\..\..
    set BOOTLOADER_FIRMWARE=%CD%\..\..\..\firmware
) else if exist "..\firmware\Bootloader.hex" (
    REM Mode 2: Running in tools directory
    REM echo [DEBUG] Mode 2: tools directory
    for %%i in ("%~dp0..") do set PROJECT_DIR=%%~fi
    for %%i in ("%~dp0..\project\OC810\MDK-ARM\output") do set APP_OUTPUT=%%~fi
    for %%i in ("%~dp0..\firmware") do set BOOTLOADER_FIRMWARE=%%~fi
) else if exist ".\firmware\Bootloader.hex" (
    REM Mode 3: Running in project root directory
    REM echo [DEBUG] Mode 3: project root
    set PROJECT_DIR=%CD%
    set APP_OUTPUT=%PROJECT_DIR%\project\OC810\MDK-ARM\output
    set BOOTLOADER_FIRMWARE=%CD%\firmware
) else (
    echo [ERROR] Cannot detect project structure
    echo Current dir: %CD%
    echo.
    echo Checking paths:
    echo   Project.axf: %CD%\Project.axf
    if exist "%CD%\Project.axf" (echo     - EXISTS) else (echo     - NOT FOUND)
    echo   ..\firmware\Bootloader.hex: %CD%\..\firmware\Bootloader.hex
    if exist "%CD%\..\firmware\Bootloader.hex" (echo     - EXISTS) else (echo     - NOT FOUND)
    echo   .\firmware\Bootloader.hex: %CD%\firmware\Bootloader.hex
    if exist "%CD%\firmware\Bootloader.hex" (echo     - EXISTS) else (echo     - NOT FOUND)
    echo Firmware directory or Bootloader.hex not found
    echo Please build Bootloader project first
    exit /b 1
)

REM File paths
set BL_HEX=%BOOTLOADER_FIRMWARE%\Bootloader.hex
set BL_BIN=%BOOTLOADER_FIRMWARE%\Bootloader.bin
set APP_HEX=%APP_OUTPUT%\Project.hex
set APP_BIN=%APP_OUTPUT%\Project.bin

REM Output directory: always to firmware
set MERGED_HEX=%BOOTLOADER_FIRMWARE%\merged_full.hex
set MERGED_BIN=%BOOTLOADER_FIRMWARE%\merged_full.bin

echo ========================================
echo  OC810 Firmware Build Tool
echo ========================================
echo.

REM ========================================
REM Mode 1: Keil After Build auto merge
REM ========================================
if "%MERGE_TYPE%"=="auto" (
    echo [Mode] Keil After Build Auto Merge
    echo.

    REM Check if files exist
    if not exist "%BL_HEX%" (
        echo [SKIP] Bootloader HEX not found: %BL_HEX%
        goto :end
    )

    if not exist "%APP_HEX%" (
        echo [SKIP] APP HEX not found: %APP_HEX%
        goto :end
    )

    echo [1/3] Copying APP HEX to firmware...
    copy /Y "%APP_HEX%" "%BOOTLOADER_FIRMWARE%\app.hex" >nul
    echo.

    echo [2/3] Merging HEX files...
    copy /b "%BL_HEX%"+"%BOOTLOADER_FIRMWARE%\app.hex" "%MERGED_HEX%" >nul
    echo [OK] Merged HEX: %MERGED_HEX%
    echo.

    if exist "%BL_BIN%" (
        echo [3/3] Merging BIN files...

        REM Check APP BIN location (output dir or firmware dir)
        if exist "%APP_BIN%" (
            python "%SCRIPT_DIR%\merge_bin.py" "%BL_BIN%" "%APP_BIN%" 0x08040000 "%MERGED_BIN%"
        ) else if exist "%BOOTLOADER_FIRMWARE%\app.bin" (
            REM echo [INFO] Using APP BIN from firmware directory
            python "%SCRIPT_DIR%\merge_bin.py" "%BL_BIN%" "%BOOTLOADER_FIRMWARE%\app.bin" 0x08040000 "%MERGED_BIN%"
        ) else (
            echo [SKIP] APP BIN not found in output or firmware directory
            goto :end
        )

        if !errorlevel! equ 0 (
            echo [OK] Merged BIN: %MERGED_BIN%
        ) else (
            echo [WARN] BIN merge failed
        )
    ) else (
        echo [SKIP] Bootloader BIN not found
    )

    goto :end
)

REM ========================================
REM Mode 2: Manual merge (hex/bin/all)
REM ========================================
echo [Mode] Manual Merge (%MERGE_TYPE%)
echo.

REM Check if files exist
if not exist "%BL_HEX%" (
    echo [ERROR] Bootloader HEX not found: %BL_HEX%
    echo Please build Bootloader project first
    pause
    exit /b 1
)

if not exist "%APP_HEX%" (
    echo [ERROR] APP HEX not found: %APP_HEX%
    echo Please build APP project first
    pause
    exit /b 1
)

REM Merge HEX
if "%MERGE_TYPE%"=="hex" (
    echo [1/1] Merging HEX files...
    copy /b "%BL_HEX%"+"%APP_HEX%" "%MERGED_HEX%" >nul
    echo [OK] Merged HEX: %MERGED_HEX%
    goto :end
)

if not "%MERGE_TYPE%"=="bin" (
    echo [1/2] Merging HEX files...
    copy /b "%BL_HEX%"+"%APP_HEX%" "%MERGED_HEX%" >nul
    echo [OK] Merged HEX: %MERGED_HEX%
    echo.
)

REM Merge BIN
if "%MERGE_TYPE%"=="hex" goto :end

if not exist "%BL_BIN%" (
    echo [SKIP] Bootloader BIN not found: %BL_BIN%
    goto :end
)

REM Check APP BIN location (output dir or firmware dir)
if exist "%APP_BIN%" (
    REM APP BIN in output directory
) else if exist "%BOOTLOADER_FIRMWARE%\app.bin" (
    set APP_BIN=%BOOTLOADER_FIRMWARE%\app.bin
) else (
    echo [ERROR] APP BIN not found
    echo Please build APP project in Keil first
    echo Or use Keil After Build auto mode
    goto :end
)

if "%MERGE_TYPE%"=="all" (
    echo [2/2] Merging BIN files...
) else (
    echo [1/1] Merging BIN files...
)

python "%SCRIPT_DIR%\merge_bin.py" "%BL_BIN%" "%APP_BIN%" 0x08040000 "%MERGED_BIN%"

if %errorlevel% equ 0 (
    echo [OK] Merged BIN: %MERGED_BIN%
) else (
    echo [ERROR] BIN merge failed
)

:end
echo.
echo ========================================
echo  Complete!
echo ========================================
if "%MERGE_TYPE%"=="auto" (
    exit /b 0
) else (
    pause
)
