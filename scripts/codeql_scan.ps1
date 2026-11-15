param(
  # Where the build, the databases and the SARIF files go. /build*/ is ignored.
  [string]$Out = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build-codeql'),
  # The release lane's Qt: qtbase 6.12.0 with the CVE diffs, built by
  # scripts/build_patched_qt.py.
  [string]$QtDir = 'C:\Qt\6.12.0-patched\msvc2022_64',
  [string]$CodeQL = '',
  [int]$Ram = 4096,
  [int]$Jobs = 2,
  # Analyze the C++ database from the last run instead of tracing a new build.
  [switch]$SkipBuild
)

# Local CodeQL scan with the security-extended suites: the app's C++ from a
# traced MSVC build, and the Python release scripts. The repository runs no
# CodeQL workflow, so this is the scan. Exits 1 when either analysis reports
# anything, 2 when a step fails.
#
# CodeQL only extracts the files the traced build compiles, so the build always
# starts clean. An incremental build would leave most of the app out of the
# database and the scan would look clean for the wrong reason.

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

if (-not $CodeQL) {
  $onPath = Get-Command codeql -ErrorAction SilentlyContinue
  $CodeQL = if ($onPath) { $onPath.Source } else { Join-Path $env:LOCALAPPDATA 'Programs\CodeQL\codeql\codeql.exe' }
}
if (-not (Test-Path $CodeQL)) {
  Write-Error "codeql was not found. Install the CodeQL bundle and pass -CodeQL <path to codeql.exe>."
  exit 2
}
if (-not (Test-Path (Join-Path $QtDir 'bin'))) {
  Write-Error "Qt was not found at '$QtDir'. Pass -QtDir <Qt MSVC install root>."
  exit 2
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$build = Join-Path $Out 'build'
$cppDb = Join-Path $Out 'cpp-db'
$pyDb = Join-Path $Out 'python-db'
$cppSarif = Join-Path $Out 'codeql-cpp.sarif'
$pySarif = Join-Path $Out 'codeql-python.sarif'

function Invoke-Step([string]$Name, [scriptblock]$Command) {
  Write-Host "== $Name"
  & $Command
  if ($LASTEXITCODE -ne 0) {
    Write-Error "$Name failed with exit code $LASTEXITCODE."
    exit 2
  }
}

if (-not $SkipBuild) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vs) {
    Write-Error 'Visual Studio with the C++ tools was not found.'
    exit 2
  }
  $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
  cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
  }
  $env:PATH = (Join-Path $QtDir 'bin') + ';' + $env:PATH
  if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Write-Error 'ninja was not found on PATH (Visual Studio ships it with its CMake tools).'
    exit 2
  }

  Invoke-Step 'configure' {
    cmake -S $repo -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "-DCMAKE_PREFIX_PATH=$QtDir"
  }
  # moc and uic run before tracing so the traced build compiles only C++.
  Invoke-Step 'moc and uic' { cmake --build $build --target RcloneBrowser_autogen }
  Invoke-Step 'traced C++ build' {
    & $CodeQL database create $cppDb --language=cpp "--source-root=$repo" --overwrite "--ram=$Ram" --threads=1 `
      "--command=cmake --build `"$build`" --target RcloneBrowser --clean-first -- -j $Jobs"
  }
}

Invoke-Step 'C++ analysis' {
  & $CodeQL database analyze $cppDb codeql/cpp-queries:codeql-suites/cpp-security-extended.qls `
    --format=sarif-latest "--output=$cppSarif" "--ram=$Ram" "--threads=$Jobs"
}
Invoke-Step 'Python extraction' {
  & $CodeQL database create $pyDb --language=python "--source-root=$(Join-Path $repo 'scripts')" --overwrite "--ram=$Ram"
}
Invoke-Step 'Python analysis' {
  & $CodeQL database analyze $pyDb codeql/python-queries:codeql-suites/python-security-extended.qls `
    --format=sarif-latest "--output=$pySarif" "--ram=$Ram" "--threads=$Jobs"
}

$found = 0
foreach ($sarif in @($cppSarif, $pySarif)) {
  $results = @((Get-Content -Raw $sarif | ConvertFrom-Json).runs[0].results | Where-Object { $_ })
  Write-Host ('{0}: {1} result(s)' -f (Split-Path $sarif -Leaf), $results.Count)
  foreach ($r in $results) {
    $loc = $r.locations[0].physicalLocation
    Write-Host ('  {0} {1}:{2} {3}' -f $r.ruleId, $loc.artifactLocation.uri, $loc.region.startLine, $r.message.text)
  }
  $found += $results.Count
}
if ($found -gt 0) { exit 1 }
exit 0
