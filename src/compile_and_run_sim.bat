@echo off
echo Setting up Visual Studio environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo.
echo Compiling lab1_sim.cpp...
cl /EHsc /std:c++17 /O2 lab1_sim.cpp /Fe:lab1_sim.exe

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Compilation successful! Running the simulation program...
    echo.
    echo ============================================
    echo Running with default parameters (1MHz, 2 threads, 10 seconds)
    echo ============================================
    lab1_sim.exe
    echo.
    echo ============================================
    echo Running with higher sampling rate (5MHz, 4 threads, 15 seconds)
    echo ============================================
    lab1_sim.exe 5e6 4 15
) else (
    echo.
    echo Compilation failed. Please check the errors above.
)

pause
