@echo off
setlocal enabledelayedexpansion

rem Rclone Browser NG — local Windows x64 release build script.
rem This is the local Windows release path; no CI release workflow is included.
rem
rem Usage: release_windows.cmd
rem
rem Requirements:
rem   - Visual Studio 2022 (Build Tools or Community) with C++ workload
rem   - Qt 6.8.8+ or 6.11.1+ MSVC 64-bit (set QT env var or edit the default below)
rem   - CMake on PATH
rem   - Inno Setup 6 (for installer, optional)
rem   - 7-Zip (for zip archive, optional)

set "DRY_RUN=0"
if /I "%~1"=="--dry-run" set "DRY_RUN=1"
if not "%~1"=="" if /I not "%~1"=="--dry-run" (
  echo ERROR: Unknown argument "%~1". Use --dry-run or no arguments.
  exit /b 2
)

rem --- Version and paths ------------------------------------------------------
set "ROOT=%~dp0.."
set /p VERSION=<"%ROOT%\VERSION"
if "%VERSION%"=="" (
  echo ERROR: VERSION is missing or empty.
  exit /b 1
)
for /f "tokens=*" %%t in ('git -C "%ROOT%" rev-parse --short HEAD 2^>nul') do set "COMMIT=%%t"
if defined COMMIT (
  set "FULLVER=%VERSION%-!COMMIT!"
) else (
  set "FULLVER=%VERSION%"
)
set "NAME=RcloneBrowserNG-%FULLVER%-windows-x64"
set "TARGET=%ROOT%\release\%NAME%"
set "BUILD=%ROOT%\build\build\Release"

if "%DRY_RUN%"=="1" goto :dry_run

rem --- Locate Visual Studio ---------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere not found — install Visual Studio 2022 Build Tools or Community.
  exit /b 1
)
for /f "delims=" %%p in ('"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VSINSTALL=%%p"
if "%VSINSTALL%" == "" (
  echo ERROR: No Visual Studio installation with the C++ workload found.
  exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
  echo ERROR: vcvarsall.bat failed.
  exit /b 1
)

rem --- Locate Qt --------------------------------------------------------------
if "%QT%" == "" set "QT=C:\Qt\6.8.8\msvc2019_64"
set "QMAKE=%QT%\bin\qmake6.exe"
if not exist "%QMAKE%" set "QMAKE=%QT%\bin\qmake.exe"
if not exist "%QMAKE%" (
  echo ERROR: Qt not found at %QT%. Set the QT environment variable to your Qt MSVC 64-bit directory.
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0validate_qt_version.ps1" -QtDir "%QT%"
if errorlevel 1 (
  exit /b 1
)
set "PATH=%QT%\bin;%PATH%"

rem --- Check cmake ------------------------------------------------------------
where /q cmake.exe
if errorlevel 1 (
  echo ERROR: cmake.exe not found on PATH.
  exit /b 1
)

pushd "%ROOT%"

if not exist release mkdir release

if exist "%TARGET%" rd /s /q "%TARGET%"
if exist "%TARGET%.zip" del "%TARGET%.zip"
if exist "%TARGET%-setup.exe" del "%TARGET%-setup.exe"

if exist build rd /s /q build
mkdir build
cd build

cmake -A x64 -DCMAKE_CONFIGURATION_TYPES="Release" -DCMAKE_PREFIX_PATH="%QT%" ..
if errorlevel 1 (
  echo ERROR: cmake configure failed.
  popd & exit /b 1
)
cmake --build . --config Release
if errorlevel 1 (
  echo ERROR: build failed.
  popd & exit /b 1
)
popd

mkdir "%TARGET%" 2>nul

copy "%ROOT%\README.md" "%TARGET%\Readme.md"
copy "%ROOT%\CHANGELOG.md" "%TARGET%\Changelog.md"
copy "%ROOT%\LICENSE" "%TARGET%\License.txt"
copy %BUILD%\RcloneBrowser.exe "%TARGET%"
if exist %BUILD%\RcloneBrowserPassword.exe (
  copy %BUILD%\RcloneBrowserPassword.exe "%TARGET%"
)

windeployqt.exe --no-translations "%TARGET%\RcloneBrowser.exe"

if exist "%QT%\plugins\platforms\qoffscreen.dll" (
  if not exist "%TARGET%\platforms" mkdir "%TARGET%\platforms"
  copy /y "%QT%\plugins\platforms\qoffscreen.dll" "%TARGET%\platforms\qoffscreen.dll" >nul
)

rem --- Qt conf ----------------------------------------------------------------
(
echo [Paths]
echo Prefix = .
echo LibraryExecutables = .
echo Plugins = .
)>"%TARGET%\qt.conf"

rem --- Create zip archive -----------------------------------------------------
where /q 7z.exe
if not errorlevel 1 (
  "7z.exe" a -mx=9 -r -tzip "%TARGET%.zip" "%TARGET%"
) else (
  echo NOTE: 7-Zip not found — skipping zip archive. Install from https://www.7-zip.org/
)

if exist "%TARGET%.zip" (
  where /q python.exe
  if not errorlevel 1 (
    python.exe "%ROOT%\scripts\smoke_package.py" --artifact "%TARGET%.zip" --version "%VERSION%"
    if errorlevel 1 (
      echo ERROR: Packaged Windows zip smoke failed.
      exit /b 1
    )
  ) else (
    echo NOTE: Python not found — skipping packaged zip smoke.
  )
)

rem --- Create installer -------------------------------------------------------
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" (
  pushd "%~dp0"
  "%ISCC%" "/dMyAppVersion=%VERSION%" "/dMyAppId={{0AF9BF43-8D44-4AFF-AE60-6CECF1BF0D31}" "/dMyAppDir=%NAME%" "/dMyAppArch=x64" "/O..\release" "/F%NAME%-setup" rclone-browser-win-installer.iss
  popd
) else (
  echo NOTE: Inno Setup 6 not found — skipping installer. Install from https://jrsoftware.org/isinfo.php
)

echo.
echo Release artifacts:
dir /b "%ROOT%\release\%NAME%*"
exit /b 0

:dry_run
echo [DRY-RUN] release_windows.cmd would build version %VERSION%.
echo [DRY-RUN] Release directory: %ROOT%\release
echo [DRY-RUN] CMake configure: cmake -A x64 -DCMAKE_CONFIGURATION_TYPES="Release" -DCMAKE_PREFIX_PATH="%%QT%%" ..
echo [DRY-RUN] CMake build: cmake --build . --config Release
echo [DRY-RUN] Qt deployment: windeployqt.exe --no-translations "%TARGET%\RcloneBrowser.exe"
echo [DRY-RUN] Zip artifact: "%TARGET%.zip"
echo [DRY-RUN] Installer artifact: "%ROOT%\release\%NAME%-setup.exe"
exit /b 0
