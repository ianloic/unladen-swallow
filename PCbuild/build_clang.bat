@echo off

set CONFIGURATION=%1
set PLATFORM=%2
set INPUTDIR=%3
set INPUT=%4
set BITCODE=%5
set OUTPUT=%6
set INTDIRDEP=%7

set CLANG="%CD%\..\Util\llvm\obj\bin\Release\clang"
set LLC="%CD%\..\Util\llvm\obj\bin\Release\llc"

set CFLAGS=-I.. -I..\Include -I..\PC -D_USRDLL -DPy_BUILD_CORE -DPy_ENABLE_SHARED -DWIN32 -D_WIN32
if "%CONFIGURATION%"=="Debug" set CFLAGS=%CFLAGS% -D_DEBUG
if "%PLATFORM%"=="x64" set CFLAGS=%CFLAGS% -D_WIN64
for /F "delims=;" %%I in ("%INCLUDE%") do set CFLAGS=%CFLAGS% -I"%%I"

cd %INPUTDIR%
if ERRORLEVEL 1 goto end

%CLANG% -O3 -emit-llvm -c %CFLAGS% %INPUT% -o %BITCODE%
if ERRORLEVEL 1 goto end

%LLC% %BITCODE% -march=cpp -cppgen=contents -cppfname=FillInitialGlobalModule -o %OUTPUT%.tmp -f
if ERRORLEVEL 1 goto end

copy /B %OUTPUT%.header + %OUTPUT%.tmp %OUTPUT%
if ERRORLEVEL 1 goto end

del %OUTPUT%.tmp 2> NUL

echo Last built for %CONFIGURATION%^|%PLATFORM%> %OUTPUT%.dep
echo.> %INTDIRDEP%

:end
