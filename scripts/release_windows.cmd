@echo off
setlocal enabledelayedexpansion

rem Rclone Browser NG — local Windows x64 release build script.
rem Matches the CI release workflow; see .github/workflows/release.yml.
rem
rem Usage: release_windows.cmd
rem
rem Requirements:
rem   - Visual Studio 2022 (Build Tools or Community) with C++ workload
rem   - Qt 6.8.8+ or 6.11.1+ MSVC 64-bit (set QT env var or edit the default below)
rem   - CMake on PATH
rem   - Inno Setup 6 (for installer, optional)
rem   - 7-Zip (for zip archive, optional)

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

rem --- Version and paths ------------------------------------------------------
set ROOT="%~dp0.."
set /p VERSION=<"%ROOT%\VERSION"
for /f "tokens=*" %%t in ('git rev-parse --short HEAD 2^>nul') do set "COMMIT=%%t"
if defined COMMIT (
  set "FULLVER=%VERSION%-!COMMIT!"
) else (
  set "FULLVER=%VERSION%"
)

set "NAME=RcloneBrowserNG-%FULLVER%-windows-x64"
set "TARGET=%~dp0..\release\%NAME%"
set BUILD="%~dp0..\build\build\Release"

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
dir /b "%~dp0..\release\%NAME%*"
