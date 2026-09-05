param(
  [Parameter(Mandatory = $true)]
  [string]$QtDir
)

# Thin wrapper. The policy itself lives in validate_qt_version.py so the
# Windows, AppImage and macOS release lanes all enforce the same table
# instead of three copies drifting apart.

$ErrorActionPreference = 'Stop'

$qmake = Join-Path $QtDir 'bin/qmake6.exe'
if (-not (Test-Path $qmake)) {
  $qmake = Join-Path $QtDir 'bin/qmake.exe'
}
if (-not (Test-Path $qmake)) {
  Write-Error "qmake was not found under '$QtDir'. Set QtDir/QT to a Qt MSVC install root."
  exit 1
}

$validator = Join-Path $PSScriptRoot 'validate_qt_version.py'
if (-not (Test-Path $validator)) {
  Write-Error "validate_qt_version.py is missing next to this script."
  exit 1
}

$python = $null
foreach ($candidate in @('py', 'python3', 'python')) {
  $resolved = Get-Command $candidate -ErrorAction SilentlyContinue
  if ($resolved) { $python = $resolved.Source; break }
}
if (-not $python) {
  Write-Error "Python 3 is required to validate the Qt version. Install it or run scripts/validate_qt_version.py directly."
  exit 1
}

& $python $validator --qmake $qmake
exit $LASTEXITCODE
