@echo off
rem Builds the base for PC (Visual Studio 2022 Build Tools with its clang, CMake and Ninja).
rem The Xbox build takes build\sor1\config, musashi_gen and fonts_data.c from it.
setlocal
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=%VS%\VC\Tools\Llvm\x64\bin;%PATH%
cd /d "%~dp0..\..\referencias\Streets-Of-Rage-Recomp" || exit /b 1
cmake -S . -B build/sor1 -G Ninja -Dsor1=sor1 -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_FLAGS=-D_USE_MATH_DEFINES -D_CRT_SECURE_NO_WARNINGS" "-DCMAKE_CXX_FLAGS=-D_USE_MATH_DEFINES -D_CRT_SECURE_NO_WARNINGS" || exit /b 1
cmake --build build/sor1
