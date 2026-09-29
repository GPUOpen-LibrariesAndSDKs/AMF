@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"

rem Remove trailing backslash
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

rem Project root = ..\..
for %%I in ("%SCRIPT_DIR%\..\..") do set "BASE=%%~fI"

set "ROOT=%BASE%\openssl"
set "WORK=%ROOT%\_build"
set "ZLIB_ROOT=%BASE%\zlib"
set "NASM_DIR=C:\Program Files\NASM"

if not exist "%WORK%" mkdir "%WORK%"
cd /d "%WORK%"

where perl >nul 2>nul
if errorlevel 1 (
    echo ERROR: Perl not found. Install Strawberry Perl and add it to PATH.
    exit /b 1
)

where nasm >nul 2>nul
if errorlevel 1 (
    if exist "%NASM_DIR%\nasm.exe" (
        set "PATH=%PATH%;%NASM_DIR%"
    ) else (
        echo ERROR: NASM not found. Install NASM and add it to PATH.
        exit /b 1
    )
)

set "VSWHERE=vswhere"
where vswhere >nul 2>nul
if errorlevel 1 set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"

if not defined VSINSTALL (
    echo ERROR: Visual Studio with C++ tools not found.
    exit /b 1
)

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "%VCVARS%" (
    echo ERROR: vcvarsall.bat not found: %VCVARS%
    exit /b 1
)

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

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop';" ^
  "$url='https://openssl-library.org/source/';" ^
  "$html=(Invoke-WebRequest $url -UseBasicParsing).Content;" ^
  "$rx='(?is)<tr>\s*<td>(?<series>[^<]+)</td>\s*<td>\s*<a[^>]*>openssl-(?<version>[0-9]+\.[0-9]+\.[0-9]+)\.tar\.gz</a>.*?</td>\s*<td>[^<]*</td>\s*<td>(?<release>[^<]+)</td>\s*<td>(?<eol>[^<]+)</td>';" ^
  "$items=@(); foreach($m in [regex]::Matches($html,$rx)){" ^
  "  $series=[System.Net.WebUtility]::HtmlDecode($m.Groups['series'].Value).Trim();" ^
  "  $items += [pscustomobject]@{Series=$series;Version=$m.Groups['version'].Value;Release=$m.Groups['release'].Value.Trim();EOL=$m.Groups['eol'].Value.Trim();LTS=($series -match '\[LTS\]')}" ^
  "}" ^
  "if(-not $items){throw 'Cannot parse OpenSSL versions'};" ^
  "$latestLts=$items | Where-Object LTS | Sort-Object {[version]$_.Version} -Descending | Select-Object -First 1;" ^
  "if(-not $latestLts){throw 'Cannot detect latest OpenSSL LTS version'};" ^
  "Write-Host '';" ^
  "Write-Host 'Available OpenSSL versions:';" ^
  "$items | ForEach-Object { $tag=if($_.LTS){' [LTS]'}else{''}; Write-Host ('  ' + $_.Version + $tag + ' - released: ' + $_.Release + ', support until: ' + $_.EOL) };" ^
  "Write-Host '';" ^
  "$inputVer=Read-Host ('Enter OpenSSL version in format X.Y.Z, e.g. ' + $latestLts.Version + '. Press Enter for default ' + $latestLts.Version + ' [LTS]');" ^
  "if($inputVer -match '^[0-9]+\.[0-9]+\.[0-9]+$'){" ^
  "  $selected=$items | Where-Object { $_.Version -eq $inputVer } | Select-Object -First 1;" ^
  "  if(-not $selected){throw ('OpenSSL version ' + $inputVer + ' was not found on the official source page. Please enter one of the listed versions, for example ' + $latestLts.Version)}" ^
  "} else {" ^
  "  if($inputVer){ Write-Host 'Invalid format. Expected X.Y.Z, for example 3.5.7. Using latest LTS.' }" ^
  "  $selected=$latestLts" ^
  "}" ^
  "$v=$selected.Version;" ^
  "Set-Content -Path 'latest_openssl_lts_version.txt' -Value $v;" ^
  "$dl='https://www.openssl.org/source/openssl-' + $v + '.tar.gz';" ^
  "$out='openssl-' + $v + '.tar.gz';" ^
  "$tag=if($selected.LTS){' [LTS]'}else{''};" ^
  "Write-Host ('Selected OpenSSL ' + $v + $tag + ', released: ' + $selected.Release + ', support until: ' + $selected.EOL);" ^
  "Write-Host ('Downloading ' + $dl);" ^
  "Invoke-WebRequest -Uri $dl -OutFile $out"

if errorlevel 1 (
    echo ERROR: Failed to download OpenSSL.
    exit /b 1
)

set /p OSSL_VER=<latest_openssl_lts_version.txt
set "SRC=%WORK%\openssl-%OSSL_VER%"

echo Building OpenSSL %OSSL_VER%

if not exist "openssl-%OSSL_VER%.tar.gz" (
    echo ERROR: OpenSSL archive was not downloaded.
    exit /b 1
)

if exist "%SRC%" rmdir /s /q "%SRC%"

tar -xzf "openssl-%OSSL_VER%.tar.gz"

if errorlevel 1 (
    echo ERROR: Failed to extract OpenSSL archive.
    exit /b 1
)

if not exist "%SRC%\Configure" (
    echo ERROR: OpenSSL sources were not extracted correctly: %SRC%
    exit /b 1
)

if /I "%BUILD_ARCH%"=="x64" (
    call :build x64 amd64 VC-WIN64A "%ZLIB_ROOT%\x64"
    if errorlevel 1 exit /b 1
) else if /I "%BUILD_ARCH%"=="x86" (
    call :build x86 x86 VC-WIN32 "%ZLIB_ROOT%\x86"
    if errorlevel 1 exit /b 1
) else (
    call :build x64 amd64 VC-WIN64A "%ZLIB_ROOT%\x64"
    if errorlevel 1 exit /b 1

    call :build x86 x86 VC-WIN32 "%ZLIB_ROOT%\x86"
    if errorlevel 1 exit /b 1
)

echo.
echo ========================================
echo Done
echo.
echo OpenSSL version: %OSSL_VER%
echo.
echo Built targets:

if /I "%BUILD_ARCH%"=="x64" (
    echo   [OK] x64 : %ROOT%\x64
) else if /I "%BUILD_ARCH%"=="x86" (
    echo   [OK] x86 : %ROOT%\x86
) else (
    echo   [OK] x64 : %ROOT%\x64
    echo   [OK] x86 : %ROOT%\x86
)

echo.
echo Build type: static OpenSSL, MSVC, /MT
echo zlib: enabled
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
set "TARGET=%~3"
set "ZLIB_DIR=%~4"
set "PREFIX=%ROOT%\%ARCH%"
set "BUILD_SRC=%WORK%\openssl-%OSSL_VER%-%ARCH%"

echo.
echo ===== Building OpenSSL %ARCH% =====

if not exist "%ZLIB_DIR%\zlib.lib" (
    echo ERROR: zlib.lib not found: %ZLIB_DIR%\zlib.lib
    endlocal
    exit /b 1
)

if not exist "%ZLIB_DIR%\zlib.h" (
    echo ERROR: zlib.h not found: %ZLIB_DIR%\zlib.h
    endlocal
    exit /b 1
)

if exist "%BUILD_SRC%" rmdir /s /q "%BUILD_SRC%"
xcopy "%SRC%" "%BUILD_SRC%\" /E /I /Q >nul

if errorlevel 1 (
    echo ERROR: Failed to copy OpenSSL source for %ARCH%
    endlocal
    exit /b 1
)

call "%VCVARS%" %VCARCH%
if errorlevel 1 (
    echo ERROR: Failed to initialize Visual Studio environment for %ARCH%
    endlocal
    exit /b 1
)

if exist "%NASM_DIR%" set "PATH=%PATH%;%NASM_DIR%"

echo VSCMD_ARG_TGT_ARCH=%VSCMD_ARG_TGT_ARCH%
where cl
where link
where lib
where rc
where nasm
where perl

cd /d "%BUILD_SRC%"

perl Configure %TARGET% ^
    --prefix="%PREFIX%" ^
    --openssldir="%PREFIX%\ssl" ^
    no-shared ^
    no-tests ^
    -static ^
    zlib ^
    --with-zlib-include="%ZLIB_DIR%" ^
    --with-zlib-lib="%ZLIB_DIR%\zlib.lib"

if errorlevel 1 (
    echo ERROR: Configure failed for %ARCH%
    endlocal
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop';" ^
  "$mf='makefile';" ^
  "$s=Get-Content $mf -Raw;" ^
  "$s=$s -replace '/MD','/MT';" ^
  "$s=$s -replace 'CNF_CFLAGS=/Gs0 /GF /Gy','CNF_CFLAGS=/Gs0 /GF /Gy /MT /MP';" ^
  "Set-Content -Path $mf -Value $s -NoNewline"

if errorlevel 1 (
    echo ERROR: Failed to patch OpenSSL makefile for %ARCH%
    endlocal
    exit /b 1
)

nmake clean >nul 2>nul
nmake

if errorlevel 1 (
    echo ERROR: Build failed for %ARCH%
    endlocal
    exit /b 1
)

nmake install_sw

if errorlevel 1 (
    echo ERROR: Install failed for %ARCH%
    endlocal
    exit /b 1
)

echo Installed to %PREFIX%

endlocal
exit /b 0