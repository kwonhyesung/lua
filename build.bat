@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist out mkdir out

if "%1"=="test" (
  cl /nologo /utf-8 /EHsc /std:c++17 /DSELFTEST hook.cpp /Fe:out\selftest.exe /Foout\ || exit /b 1
  out\selftest.exe
  exit /b %errorlevel%
)

cl /nologo /utf-8 /O2 /EHsc /std:c++17 /LD hook.cpp minhook\src\*.c minhook\src\hde\*.c /I minhook\include /Fe:out\hook.dll /Foout\ || exit /b 1
cl /nologo /utf-8 /O2 /EHsc /std:c++17 injector.cpp /Fe:out\injector.exe /Foout\ || exit /b 1
copy /Y rules.txt out\ >nul
echo BUILD OK
