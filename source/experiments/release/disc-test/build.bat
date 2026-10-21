@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set CM="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NJ=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja
set PATH=%NJ%;%PATH%
%CM% -S "%~dp0." -B "%~dp0..\..\..\build\disc-test" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
%CM% --build "%~dp0..\..\..\build\disc-test" --target wiids_disc_tool wiids_art_tool || exit /b 1
