param(
  [string] $SdkRoot = $env:NINTENDO_SDK_ROOT,
  [string] $Output = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
  throw 'Set NINTENDO_SDK_ROOT or pass -SdkRoot.'
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$probeRoot = Join-Path $projectRoot 'Build\MiiGeometryProbe'
if ([string]::IsNullOrWhiteSpace($Output)) {
  $Output = Join-Path $probeRoot 'Default0.apmp'
}

& (Join-Path $PSScriptRoot 'BuildMiiGeometryProbe.ps1') -SdkRoot $SdkRoot
if ($LASTEXITCODE -ne 0) {
  throw "Mii probe build failed with exit code $LASTEXITCODE."
}

$probeFiles = @('MiiCharInfoProbe.bin', 'MiiGeometryProbe.apmg',
                'MiiFacelineProbe.aptx', 'MiiMaskProbe.aptx', 'MiiViewProbe.txt')
for ($view = 0; $view -lt 5; ++$view) {
  $probeFiles += "MiiView${view}Probe.aptx"
}

foreach ($name in $probeFiles) {
  $path = Join-Path $probeRoot $name
  if (Test-Path -LiteralPath $path) {
    Remove-Item -LiteralPath $path -Force
  }
}

$probe = Join-Path $probeRoot 'MiiSimple.exe'
Push-Location $probeRoot
try {
  & $probe
  $probeExitCode = $LASTEXITCODE
}
finally {
  Pop-Location
}

if ($probeExitCode -ne 0) {
  throw "Mii probe failed with exit code $probeExitCode."
}

& python (Join-Path $PSScriptRoot 'PackMiiPreview.py') `
  --input-dir $probeRoot --sdk-root $SdkRoot --output $Output
if ($LASTEXITCODE -ne 0) {
  throw "Mii preview packing failed with exit code $LASTEXITCODE."
}
