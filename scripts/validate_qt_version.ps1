param(
  [Parameter(Mandatory = $true)]
  [string]$QtDir
)

$ErrorActionPreference = 'Stop'

$qmake = Join-Path $QtDir 'bin/qmake6.exe'
if (-not (Test-Path $qmake)) {
  $qmake = Join-Path $QtDir 'bin/qmake.exe'
}
if (-not (Test-Path $qmake)) {
  Write-Error "qmake was not found under '$QtDir'. Set QtDir/QT to a Qt MSVC install root."
  exit 1
}

$version = (& $qmake -query QT_VERSION).Trim()
Write-Host "Qt version: $version"

$parts = $version -split '\.'
if ($parts.Count -lt 3) {
  Write-Error "Could not parse Qt version '$version'."
  exit 1
}

$major = [int]$parts[0]
$minor = [int]$parts[1]
$patch = [int]$parts[2]

# CVE-2026-6210 is fixed in Qt 6.8.8+ and 6.11.1+.
$safe = ($major -gt 6) -or
        ($major -eq 6 -and $minor -eq 8 -and $patch -ge 8) -or
        ($major -eq 6 -and $minor -eq 11 -and $patch -ge 1) -or
        ($major -eq 6 -and $minor -gt 11)

if (-not $safe) {
  Write-Error "Qt $version is in the CVE-2026-6210 vulnerable range. Use Qt 6.8.8+ or 6.11.1+ for release packaging."
  exit 1
}
