@echo off
setlocal
cd /d "%~dp0"

echo ==============================================
echo Pushing Galaxy Android to GitHub
echo ==============================================
echo.
echo Target: https://github.com/OpenPort-Labs/Galaxy-Android
echo.

git push -u origin main

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Push failed or was canceled.
    echo.
    pause
    exit /b 1
)

echo.
echo ==============================================
echo [SUCCESS] Successfully pushed to GitHub!
echo View your repository: https://github.com/OpenPort-Labs/Galaxy-Android
echo ==============================================
echo.
pause
