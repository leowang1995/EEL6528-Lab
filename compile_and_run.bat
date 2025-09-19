@echo off
echo Setting up Visual Studio environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo.
echo Compiling lab1.cpp in simulation mode...
cl /EHsc /std:c++17 /DSIMULATE_MODE lab1.cpp /Fe:lab1_sim.exe

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Compilation successful! Running the program...
    echo.
    lab1_sim.exe
) else (
    echo.
    echo Compilation failed. Please check the errors above.
)

pause
