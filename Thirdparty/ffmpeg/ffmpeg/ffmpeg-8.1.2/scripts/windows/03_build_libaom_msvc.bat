@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"

rem Remove trailing backslash
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

rem Project root = ..\..
for %%I in ("%SCRIPT_DIR%\..\..") do set "BASE=%%~fI"

set "ROOT=%BASE%\aom"
set "WORK=%ROOT%\_build"
set "SRC=%ROOT%\_src\aom"

if not exist "%ROOT%" mkdir "%ROOT%"
if not exist "%WORK%" mkdir "%WORK%"
if not exist "%ROOT%\_src" mkdir "%ROOT%\_src"

where git >nul 2>nul || (
    echo ERROR: git not found.
    exit /b 1
)

set "VSWHERE=vswhere"
where vswhere >nul 2>nul
if errorlevel 1 set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do set "VSVERSION=%%i"

if not defined VSINSTALL (
    echo ERROR: Visual Studio with C++ tools not found.
    exit /b 1
)

for /f "tokens=1 delims=." %%a in ("%VSVERSION%") do set "VSMAJOR=%%a"

set "GENERATOR="
if "%VSMAJOR%"=="18" set "GENERATOR=Visual Studio 18 2026"
if "%VSMAJOR%"=="17" set "GENERATOR=Visual Studio 17 2022"
if "%VSMAJOR%"=="16" set "GENERATOR=Visual Studio 16 2019"

if not defined GENERATOR (
    echo ERROR: Unsupported Visual Studio version: %VSVERSION%
    exit /b 1
)

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    echo ERROR: vcvarsall.bat not found: %VCVARS%
    exit /b 1
)

set "CMAKE_EXE=cmake"
where cmake >nul 2>nul
if errorlevel 1 set "CMAKE_EXE="

set "VS_CMAKE=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if defined CMAKE_EXE (
    "%CMAKE_EXE%" --help | findstr /C:"%GENERATOR%" >nul
    if errorlevel 1 (
        if exist "%VS_CMAKE%" (
            "%VS_CMAKE%" --help | findstr /C:"%GENERATOR%" >nul
            if errorlevel 1 (
                echo ERROR: Neither system CMake nor Visual Studio bundled CMake supports generator: %GENERATOR%
                exit /b 1
            )
            set "CMAKE_EXE=%VS_CMAKE%"
        ) else (
            echo ERROR: System CMake does not support generator: %GENERATOR%
            echo ERROR: Visual Studio bundled CMake not found: %VS_CMAKE%
            exit /b 1
        )
    )
) else (
    if exist "%VS_CMAKE%" (
        "%VS_CMAKE%" --help | findstr /C:"%GENERATOR%" >nul
        if errorlevel 1 (
            echo ERROR: Visual Studio bundled CMake does not support generator: %GENERATOR%
            exit /b 1
        )
        set "CMAKE_EXE=%VS_CMAKE%"
    ) else (
        echo ERROR: cmake not found.
        exit /b 1
    )
)

echo Visual Studio: %VSINSTALL%
echo Visual Studio version: %VSVERSION%
echo CMake generator: %GENERATOR%
echo CMake executable: %CMAKE_EXE%


echo.
echo Select architecture to build:
echo   x64 - build x64
echo   x86 - build x86
echo   all - build x64 and x86
echo   Enter - all
echo.

set "BUILD_ARCH="
set /p BUILD_ARCH=Selection: 

if not defined BUILD_ARCH set "BUILD_ARCH=all"

if /I not "%BUILD_ARCH%"=="x64" if /I not "%BUILD_ARCH%"=="x86" if /I not "%BUILD_ARCH%"=="all" (
    echo.
    echo ERROR: Invalid architecture: %BUILD_ARCH%
    echo Expected: x64, x86 or all.
    exit /b 1
)

if not exist "%SRC%\.git" (
    echo Cloning libaom...
    git clone --depth 1 https://aomedia.googlesource.com/aom "%SRC%"
    if errorlevel 1 exit /b 1
) else (
    echo Updating libaom...
    cd /d "%SRC%"
    git fetch --depth 1 origin
    git reset --hard origin/main
    if errorlevel 1 exit /b 1
)

if /I "%BUILD_ARCH%"=="x64" (
    call :build x64 amd64 x64
    if errorlevel 1 exit /b 1
) else if /I "%BUILD_ARCH%"=="x86" (
    call :build x86 x86 Win32
    if errorlevel 1 exit /b 1
) else if /I "%BUILD_ARCH%"=="all" (
    call :build x64 amd64 x64
    if errorlevel 1 exit /b 1

    call :build x86 x86 Win32
    if errorlevel 1 exit /b 1
) else (
    echo ERROR: Internal error: unsupported architecture=%BUILD_ARCH%
    exit /b 1
)

echo.
echo ========================================
echo Done
echo.
echo.
echo Built targets:
if /I "%BUILD_ARCH%"=="x64" echo   [OK] x64 : %ROOT%\x64
if /I "%BUILD_ARCH%"=="x86" echo   [OK] x86 : %ROOT%\x86
if /I "%BUILD_ARCH%"=="all" (
    echo   [OK] x64 : %ROOT%\x64
    echo   [OK] x86 : %ROOT%\x86
)
echo.
echo Build type: static libaom, MSVC, /MT
echo ========================================
exit /b 0

:build

setlocal

set "INCLUDE="
set "LIB="
set "LIBPATH="

set "ARCH=%~1"
set "VCARCH=%~2"
set "CMAKE_ARCH=%~3"
set "BUILD=%WORK%\%ARCH%"
set "PREFIX=%ROOT%\%ARCH%"

echo.
echo ===== Building libaom %ARCH% =====

call "%VCVARS%" %VCARCH%
if errorlevel 1 (
    echo ERROR: Failed to initialize Visual Studio environment for %ARCH%
    endlocal
    exit /b 1
)

if exist "%BUILD%" rmdir /s /q "%BUILD%"
if exist "%PREFIX%" rmdir /s /q "%PREFIX%"

mkdir "%BUILD%"
mkdir "%PREFIX%"
mkdir "%PREFIX%\include"
mkdir "%PREFIX%\bin"
mkdir "%PREFIX%\lib"
mkdir "%PREFIX%\lib\pkgconfig"

echo Configuring %ARCH%...

echo ===== Generate Visual Studio solution =====

"%CMAKE_EXE%" -S "%SRC%" -B "%BUILD%" ^
    -G "%GENERATOR%" ^
    -A %CMAKE_ARCH% ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded

if errorlevel 1 (
    echo ERROR: Configure failed for %ARCH%
    endlocal
    exit /b 1
)

echo Building Release %ARCH%...

echo ===== Build Release =====

"%CMAKE_EXE%" --build "%BUILD%" --config Release

if errorlevel 1 (
    echo ERROR: Build failed for %ARCH%
    endlocal
    exit /b 1
)

echo Copying files %ARCH%...

echo ===== Copy headers =====

xcopy /E /I /Y "%SRC%\aom" "%PREFIX%\include\aom"

echo ===== Copy libraries =====

copy /Y "%BUILD%\Release\*.lib" "%PREFIX%\lib\"

echo ===== Copy binaries =====

if exist "%BUILD%\Release\*.exe" copy /Y "%BUILD%\Release\*.exe" "%PREFIX%\bin\"
if exist "%BUILD%\Release\*.dll" copy /Y "%BUILD%\Release\*.dll" "%PREFIX%\bin\"

echo ===== Copy and patch aom.pc =====

if not exist "%BUILD%\aom.pc" (
    echo ERROR: aom.pc not found in build folder: %BUILD%\aom.pc
    endlocal
    exit /b 1
)

copy /Y "%BUILD%\aom.pc" "%PREFIX%\lib\pkgconfig\aom.pc"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$pc='%PREFIX%\lib\pkgconfig\aom.pc';" ^
  "$s=Get-Content $pc -Raw;" ^
  "$prefix='%PREFIX%' -replace '\\','/';" ^
  "$prefix=$prefix -replace '^([A-Za-z]):','/$1';" ^
  "$s=$s -replace '(?m)^prefix=.*$',('prefix=' + $prefix);" ^
  "Set-Content -Path $pc -Value $s -NoNewline"

if errorlevel 1 (
    echo ERROR: Failed to patch aom.pc for %ARCH%
    endlocal
    exit /b 1
)

echo Installed to %PREFIX%

endlocal
exit /b 0
