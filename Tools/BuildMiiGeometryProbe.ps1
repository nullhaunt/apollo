param(
  [string] $SdkRoot = $env:NINTENDO_SDK_ROOT
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
  throw 'Set NINTENDO_SDK_ROOT or pass -SdkRoot.'
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sampleRoot = Join-Path $SdkRoot 'Samples\Sources\Applications\MiiSimple'
$sourcePath = Join-Path $sampleRoot 'MiiSimple-spec.Generic.autogen.cpp'
$projectPath = Join-Path $sampleRoot 'MiiSimple-Generic.autogen.vcxproj'
$includePath = (Join-Path $PSScriptRoot 'MiiGeometryProbe.inl').Replace('\', '/')
$outputRoot = Join-Path $projectRoot 'Build\MiiGeometryProbe'
$generatedSource = Join-Path $outputRoot 'MiiSimple-probe.cpp'
$generatedProject = Join-Path $outputRoot 'MiiSimple-probe.vcxproj'
$msbuild = 'D:\Dev\VisualStudio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe'

if (!(Test-Path -LiteralPath $sourcePath) -or !(Test-Path -LiteralPath $projectPath)) {
  throw 'The installed Generic Mii sample is unavailable.'
}

if (!(Test-Path -LiteralPath $msbuild)) {
  throw 'Visual Studio 2022 MSBuild is unavailable.'
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

$source = Get-Content -LiteralPath $sourcePath -Raw
$hook = '    SetupConstantBuffers();'
if ($source.IndexOf($hook, [StringComparison]::Ordinal) -ne $source.LastIndexOf($hook, [StringComparison]::Ordinal)) {
  throw 'The sample setup hook has changed; review the SDK sample before continuing.'
}

$entryMarker = 'extern "C" void nnMain()'
if (!$source.Contains($hook) -or !$source.Contains($entryMarker)) {
  throw 'The sample entry point has changed; review the SDK sample before continuing.'
}

$source = $source.Replace($entryMarker, "#include `"$includePath`"`r`n`r`n$entryMarker")
$source = $source.Replace($hook, "$hook`r`n    ApolloExportMiiGeometry();")
Set-Content -LiteralPath $generatedSource -Value $source -NoNewline

$project = Get-Content -LiteralPath $projectPath -Raw
$sampleProjectDir = $sampleRoot.TrimEnd('\') + '\'
$project = $project.Replace('$(ProjectDir)', $sampleProjectDir)
$sampleSource = $sampleProjectDir + 'MiiSimple-spec.Generic.autogen.cpp'
if (!$project.Contains($sampleSource)) {
  throw 'The sample source path has changed; review the SDK project before continuing.'
}

$project = $project.Replace($sampleSource, $generatedSource)
Set-Content -LiteralPath $generatedProject -Value $project -NoNewline

$env:NINTENDO_SDK_ROOT = $SdkRoot
& $msbuild $generatedProject /t:Build /p:Configuration=VS2022_Debug /p:Platform=x64 `
  "/p:NintendoSdkRoot=$($SdkRoot.TrimEnd('\'))\" `
  /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:VCToolsVersion=14.44.35207 `
  "/p:OutDir=$outputRoot\" "/p:IntDir=$outputRoot\obj\" /m /v:minimal /nologo

if ($LASTEXITCODE -ne 0) {
  throw "Mii geometry probe build failed with exit code $LASTEXITCODE."
}
