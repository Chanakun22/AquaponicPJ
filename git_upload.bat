@echo off
cd /d "%~dp0"

echo ==============================================
echo  Aquaponics Project - GitHub Uploader
echo ==============================================

REM Check if git exists at common location
if exist "C:\Program Files\Git\cmd\git.exe" (
    set GIT_CMD="C:\Program Files\Git\cmd\git.exe"
) else (
    set GIT_CMD=git
)

echo using git: %GIT_CMD%

echo.
echo [1/3] Configuring Remote URL...
%GIT_CMD% remote add origin https://github.com/Chanakun22/AquaponicPJ.git
%GIT_CMD% remote set-url origin https://github.com/Chanakun22/AquaponicPJ.git

echo.
echo [2/3] Setting branch to main...
%GIT_CMD% branch -M main

echo.
echo [3/3] Pushing to GitHub...
echo.
echo ********************************************************
echo * PLEASE LOOK FOR A POPUP WINDOW TO LOGIN TO GITHUB... *
echo ********************************************************
echo.
%GIT_CMD% push -u origin main

echo.
if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Upload Complete! verify at: https://github.com/Chanakun22/AquaponicPJ
) else (
    echo [ERROR] Upload Failed. Please check the error message above.
)
echo.
pause
