@echo off
setlocal

set "SCRIPT_DIR=%~dp0"

rem Remove trailing backslash
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

rem Project root = ..\..
for %%I in ("%SCRIPT_DIR%\..\..") do set "BASE=%%~fI"
set "MSYS2=C:\msys64"

echo BASE=%BASE%
echo MSYS2=%MSYS2%
echo.

if not exist "%MSYS2%\msys2_shell.cmd" (
    echo ERROR: MSYS2 shell not found:
    echo   %MSYS2%\msys2_shell.cmd
    pause
    exit /b 1
)

if not exist "%BASE%\get_sourcecode" (
    echo ERROR: get_sourcecode not found:
    echo   %BASE%\get_sourcecode
    pause
    exit /b 1
)

pushd "%BASE%"

call "%MSYS2%\msys2_shell.cmd" -mingw64 -defterm -no-start -here -c "./get_sourcecode %*"

set "ERR=%ERRORLEVEL%"

popd

echo.
echo Exit code: %ERR%
pause

exit /b %ERR%