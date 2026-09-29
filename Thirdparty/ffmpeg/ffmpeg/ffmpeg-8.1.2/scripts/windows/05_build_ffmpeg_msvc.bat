@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"

rem Remove trailing backslash
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

rem Project root = ..\..
for %%I in ("%SCRIPT_DIR%\..\..") do set "BASE=%%~fI"

set "FFMPEG_ROOT=%BASE%"
set "MSYS2_ROOT=C:\msys64"
set "ZLIB_ROOT=%BASE%\zlib"
set "OPENSSL_ROOT=%BASE%\openssl"
set "AOM_ROOT=%BASE%\aom"
set "NASM_DIR=C:\Program Files\NASM"

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

if not exist "%MSYS2_ROOT%\msys2_shell.cmd" (
    echo ERROR: MSYS2 shell not found: %MSYS2_ROOT%\msys2_shell.cmd
    exit /b 1
)

if not exist "%FFMPEG_ROOT%\ffmpeg-build-win" (
    echo ERROR: ffmpeg-build-win not found in %FFMPEG_ROOT%
    exit /b 1
)

set "FFMPEG_SRC_DIR="
for /d %%D in ("%FFMPEG_ROOT%\ffmpeg-*") do (
    if exist "%%~fD\configure" (
        set "FFMPEG_SRC_DIR=%%~fD"
    )
)

if not defined FFMPEG_SRC_DIR (
    echo ERROR: FFmpeg source directory with configure file was not found in %FFMPEG_ROOT%.
    echo.
    echo Please run get_sourcecode manually first, then patch FFmpeg configure if needed.
    echo Example:
    echo   C:\msys64\msys2_shell.cmd -mingw64 -use-full-path
    echo   cd /c/ffmpeg_new/ffmpeg-8.1.2
    echo   ./get_sourcecode
    echo.
    echo After get_sourcecode finishes, patch FFmpeg configure for static OpenSSL MSVC build.
    echo.
    echo Open:
    echo   %FFMPEG_ROOT%\ffmpeg-^<version^>\configure
    echo.
    echo Find the OpenSSL check block:
    echo   enabled openssl
    echo.
    echo In that block, add this check_lib line before the generic -lssl -lcrypto checks:
    echo.
    echo   check_lib openssl openssl/ssl.h DTLS_get_data_mtu -llibssl -llibcrypto -lzlib -lws2_32 -lgdi32 -luser32 -ladvapi32 -lcrypt32 ^|^|
    echo.
    echo This is needed so FFmpeg configure can detect static OpenSSL built with MSVC.
    echo This change is not needed for MinGW builds.
    exit /b 1
)

echo Using FFmpeg source:
echo %FFMPEG_SRC_DIR%

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

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    echo ERROR: vcvarsall.bat not found: %VCVARS%
    exit /b 1
)

echo Visual Studio: %VSINSTALL%
echo Visual Studio version: %VSVERSION%


if /I "%BUILD_ARCH%"=="x64" (
    call :build_one x64 amd64 mingw64 64 x64
    if errorlevel 1 exit /b 1
) else if /I "%BUILD_ARCH%"=="x86" (
    call :build_one x86 x86 mingw32 32 x86
    if errorlevel 1 exit /b 1
) else if /I "%BUILD_ARCH%"=="all" (
    call :build_one x64 amd64 mingw64 64 x64
    if errorlevel 1 exit /b 1

    call :build_one x86 x86 mingw32 32 x86
    if errorlevel 1 exit /b 1
) else (
    echo ERROR: Internal error: unsupported architecture=%BUILD_ARCH%
    exit /b 1
)

echo.
echo Done.
exit /b 0


:build_one
set "INCLUDE="
set "LIB="
set "LIBPATH="
setlocal

set "NAME=%~1"
set "VCARCH=%~2"
set "MSYS_FLAVOR=%~3"
set "FF_BITS=%~4"
set "DEP_ARCH=%~5"

echo.
echo ===== Building FFmpeg %NAME% =====

call "%VCVARS%" %VCARCH%
if errorlevel 1 (
    endlocal
    exit /b 1
)
echo DEP_ARCH=%DEP_ARCH%
echo LIB=%LIB%

set "SDKROOT=%WindowsSdkDir%"
set "SDKVER=%WindowsSDKVersion%"

if exist "%NASM_DIR%" set "PATH=%PATH%;%NASM_DIR%"

set "INCLUDE=%INCLUDE%;%ZLIB_ROOT%\%DEP_ARCH%"
set "LIB=%LIB%;%ZLIB_ROOT%\%DEP_ARCH%"

set "INCLUDE=%INCLUDE%;%OPENSSL_ROOT%\%DEP_ARCH%\include"
set "LIB=%LIB%;%OPENSSL_ROOT%\%DEP_ARCH%\lib"
set "PATH=%PATH%;%OPENSSL_ROOT%\%DEP_ARCH%\bin"

set "INCLUDE=%INCLUDE%;%AOM_ROOT%\%DEP_ARCH%\include"
set "LIB=%LIB%;%AOM_ROOT%\%DEP_ARCH%\lib"
set "PATH=%PATH%;%AOM_ROOT%\%DEP_ARCH%\bin"

set "INCLUDE=%INCLUDE%;%SDKROOT%Include\%SDKVER%ucrt"
set "INCLUDE=%INCLUDE%;%SDKROOT%Include\%SDKVER%um"
set "INCLUDE=%INCLUDE%;%SDKROOT%Include\%SDKVER%shared"

set "LIB=%LIB%;%SDKROOT%Lib\%SDKVER%ucrt\%DEP_ARCH%"
set "LIB=%LIB%;%SDKROOT%Lib\%SDKVER%um\%DEP_ARCH%"
set "LIB=%LIB%;%SDKROOT%Lib\%SDKVER%shared\%DEP_ARCH%"

if not exist "%AOM_ROOT%\%DEP_ARCH%\lib\pkgconfig\aom.pc" (
    echo ERROR: aom.pc not found: %AOM_ROOT%\%DEP_ARCH%\lib\pkgconfig\aom.pc
    endlocal
    exit /b 1
)

if not exist "%OPENSSL_ROOT%\%DEP_ARCH%\lib\libssl.lib" (
    echo ERROR: libssl.lib not found: %OPENSSL_ROOT%\%DEP_ARCH%\lib\libssl.lib
    endlocal
    exit /b 1
)

if not exist "%OPENSSL_ROOT%\%DEP_ARCH%\lib\libcrypto.lib" (
    echo ERROR: libcrypto.lib not found: %OPENSSL_ROOT%\%DEP_ARCH%\lib\libcrypto.lib
    endlocal
    exit /b 1
)

if not exist "%ZLIB_ROOT%\%DEP_ARCH%\zlib.lib" (
    echo ERROR: zlib.lib not found: %ZLIB_ROOT%\%DEP_ARCH%\zlib.lib
    endlocal
    exit /b 1
)

set "MSYS_FFMPEG_ROOT=%FFMPEG_ROOT:\=/%"
set "MSYS_FFMPEG_ROOT=%MSYS_FFMPEG_ROOT:C:=/c%"
set "PKG_CONFIG_PATH=%MSYS_FFMPEG_ROOT%/aom/%DEP_ARCH%/lib/pkgconfig"

echo PKG_CONFIG_PATH=%PKG_CONFIG_PATH%

call "%MSYS2_ROOT%\msys2_shell.cmd" -defterm -no-start -%MSYS_FLAVOR% -use-full-path -where "%FFMPEG_ROOT%" -c "export PKG_CONFIG_PATH='%PKG_CONFIG_PATH%'; ./ffmpeg-build-win release %FF_BITS% msvc"

if errorlevel 1 (
    echo ERROR: FFmpeg %NAME% build failed.
    endlocal
    exit /b 1
)

echo FFmpeg %NAME% build completed.
endlocal
exit /b 0
