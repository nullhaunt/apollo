param(
  [string] $SdkRoot = $env:NINTENDO_SDK_ROOT,
  [string] $Output = '',
  [int] $DefaultIndex = -1
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
  throw 'Set NINTENDO_SDK_ROOT or pass -SdkRoot.'
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$probeRoot = Join-Path $projectRoot 'Build\MiiGeometryProbe'
if ($DefaultIndex -lt -1 -or $DefaultIndex -ge 6) {
  throw 'DefaultIndex must be 0 through 5, or -1 to cook all six defaults.'
}
if ($DefaultIndex -eq -1 -and ![string]::IsNullOrWhiteSpace($Output)) {
  throw '-Output requires a single -DefaultIndex.'
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

$probe = Join-Path $probeRoot 'MiiSimple.exe'
$indices = if ($DefaultIndex -eq -1) { 0..5 } else { @($DefaultIndex) }
$priorIndex = $env:APOLLO_MII_DEFAULT_INDEX
try {
  foreach ($index in $indices) {
    foreach ($name in $probeFiles) {
      $path = Join-Path $probeRoot $name
      if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
      }
    }

    $env:APOLLO_MII_DEFAULT_INDEX = [string]$index
    Push-Location $probeRoot
    try {
      & $probe
      $probeExitCode = $LASTEXITCODE
    }
    finally {
      Pop-Location
    }
    if ($probeExitCode -ne 0) {
      throw "Mii probe failed for default $index with exit code $probeExitCode."
    }

    $destination = if ([string]::IsNullOrWhiteSpace($Output)) { Join-Path $probeRoot "Default$index.apmp" } else { $Output }
    & python (Join-Path $PSScriptRoot 'PackMiiPreview.py') --input-dir $probeRoot --sdk-root $SdkRoot --output $destination --default-index $index
    if ($LASTEXITCODE -ne 0) {
      throw "Mii preview packing failed for default $index with exit code $LASTEXITCODE."
    }
  }
}
finally {
  $env:APOLLO_MII_DEFAULT_INDEX = $priorIndex
}
