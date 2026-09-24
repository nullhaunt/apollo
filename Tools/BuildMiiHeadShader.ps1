param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Variation,
    [Parameter(Mandatory = $true)][string]$Output,
    [string]$NintendoSdkRoot = $env:NINTENDO_SDK_ROOT
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($NintendoSdkRoot)) {
    throw 'Set NINTENDO_SDK_ROOT for the NX64 Mii head shader.'
}

$compiler = Join-Path $NintendoSdkRoot 'Tools\Graphics\GraphicsTools\ShaderConverter.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    throw "ShaderConverter was not found: $compiler"
}

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$variationPath = (Resolve-Path -LiteralPath $Variation).Path
$outputPath = [System.IO.Path]::GetFullPath($Output)
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($outputPath)) | Out-Null

& $compiler -o $outputPath -s Glsl -c Binary -a Nvn `
    --vertex-shader $sourcePath --pixel-shader $sourcePath --variation $variationPath `
    --glsl-version 450 --separable --reflection

if ($LASTEXITCODE -ne 0) {
    throw "Mii head shader conversion failed ($LASTEXITCODE)."
}
