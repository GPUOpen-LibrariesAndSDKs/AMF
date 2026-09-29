@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"

rem Remove trailing backslash
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

rem Project root = ..\..
for %%I in ("%SCRIPT_DIR%\..\..") do set "BASE=%%~fI"

set "ROOT=%BASE%\zlib"
set "WORK=%ROOT%\_build"
set "OUT=%ROOT%"

if not exist "%WORK%" mkdir "%WORK%"
cd /d "%WORK%"

set "VSWHERE=vswhere"

where vswhere >nul 2>nul
if errorlevel 1 set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo vswhere.exe not found.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"

if not defined VSINSTALL (
    echo Visual Studio with C++ tools not found.
    exit /b 1
)

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat"

echo.
echo Select architecture to build:
echo   x64
echo   x86
echo   all
echo   Enter - all
echo.

set "BUILD_ARCH="
set /p BUILD_ARCH=Selection: 

if not defined BUILD_ARCH set "BUILD_ARCH=all"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop';" ^
  "$html=(Invoke-WebRequest 'https://zlib.net/' -UseBasicParsing).Content;" ^
  "$latest=[regex]::Match($html,'zlib\s+([0-9]+(?:\.[0-9]+)+)').Groups[1].Value;" ^
  "if(-not $latest){throw 'Cannot detect latest zlib version'};" ^
  "$links=(Invoke-WebRequest 'https://zlib.net/fossils/' -UseBasicParsing).Links.href;" ^
  "$versions=$links | Where-Object {$_ -match '^zlib-[0-9]+(\.[0-9]+)+\.tar\.gz$'} | ForEach-Object { [regex]::Match($_,'zlib-([0-9]+(?:\.[0-9]+)+)\.tar\.gz').Groups[1].Value } | Sort-Object {[version]$_} -Descending;" ^
  "$versions=@($latest) + @($versions | Where-Object {$_ -ne $latest});" ^
  "$versions=$versions | Select-Object -Unique;" ^
  "Set-Content -Path 'latest_zlib_version.txt' -Value $latest;" ^
  "Set-Content -Path 'available_zlib_versions.txt' -Value $versions"

if errorlevel 1 (
    echo Failed to detect zlib versions.
    exit /b 1
)

set /p ZVER=<latest_zlib_version.txt

echo.
echo Available versions:
powershell -NoProfile -Command "Get-Content available_zlib_versions.txt | Select-Object -First 5 | ForEach-Object {Write-Host '  ' $_}"
echo.
echo Press Enter to build the latest version %ZVER% or type another version.
echo.

set "USERVER="
set /p USERVER=Select version (Enter = %ZVER%): 

if defined USERVER set "ZVER=%USERVER%"

echo.
echo Selected zlib version: %ZVER%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop';" ^
  "$v='%ZVER%';" ^
  "$url='https://zlib.net/zlib-' + $v + '.tar.gz';" ^
  "$out='zlib-' + $v + '.tar.gz';" ^
  "if(Test-Path $out){Remove-Item $out -Force};" ^
  "Invoke-WebRequest -Uri $url -OutFile $out"

if errorlevel 1 (
    echo Failed to download zlib %ZVER%.
    exit /b 1
)

echo Building zlib %ZVER%

if exist "%WORK%\zlib-%ZVER%" rmdir /s /q "%WORK%\zlib-%ZVER%"

tar -xzf zlib-%ZVER%.tar.gz

if errorlevel 1 (
    echo Failed to extract zlib archive.
    exit /b 1
)

set "SRC=%WORK%\zlib-%ZVER%"

if not exist "%SRC%\win32\Makefile.msc" (
    echo zlib sources were not extracted correctly: %SRC%
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$mf='%SRC%\win32\Makefile.msc'; " ^
  "(Get-Content $mf) -replace '-MD','-MT' | Set-Content $mf; " ^
  "$zc='%SRC%\zconf.h'; " ^
  "(Get-Content $zc) | Where-Object {$_ -notmatch '^\s*#\s*include\s*[<""]unistd\.h[>""]'} | Set-Content $zc"

if errorlevel 1 (
    echo Failed to patch zlib files.
    exit /b 1
)

set "BUILT_X64=0"
set "BUILT_X86=0"

if "%BUILD_ARCH%"=="x64" (
    call :build x64 amd64
    if errorlevel 1 exit /b 1
) else if "%BUILD_ARCH%"=="x86" (
    call :build x86 x86
    if errorlevel 1 exit /b 1
) else (
    call :build x64 amd64
    if errorlevel 1 exit /b 1

    call :build x86 x86
    if errorlevel 1 exit /b 1
)

echo.
echo ========================================
echo Done
echo.
echo zlib version: %ZVER%
echo.
echo Built targets:

if /I "%BUILD_ARCH%"=="x64" (
    echo   [OK] x64 : %OUT%\x64
) else if /I "%BUILD_ARCH%"=="x86" (
    echo   [OK] x86 : %OUT%\x86
) else (
    echo   [OK] x64 : %OUT%\x64
    echo   [OK] x86 : %OUT%\x86
)

echo.
echo Build type: static library, MSVC, /MT
echo ASM: disabled
echo ========================================
echo.

exit /b 0


:build
setlocal

set "INCLUDE="
set "LIB="
set "LIBPATH="

set "ARCH=%~1"
set "VCARCH=%~2"

echo.
echo ===== Building %ARCH% =====

call "%VCVARS%" %VCARCH%
if errorlevel 1 (
    echo Failed to initialize Visual Studio environment for %ARCH%
    endlocal
    exit /b 1
)

cd /d "%SRC%"

nmake -f win32/Makefile.msc clean
nmake -f win32/Makefile.msc

if errorlevel 1 (
    echo Build failed for %ARCH%
    endlocal
    exit /b 1
)

if not exist "%OUT%\%ARCH%" mkdir "%OUT%\%ARCH%"

copy /Y zlib.lib "%OUT%\%ARCH%\zlib.lib"
copy /Y zlib.h "%OUT%\%ARCH%\zlib.h"
copy /Y zconf.h "%OUT%\%ARCH%\zconf.h"

if errorlevel 1 (
    echo Failed to copy files for %ARCH%
    endlocal
    exit /b 1
)

echo Installed to %OUT%\%ARCH%

endlocal
exit /b 0