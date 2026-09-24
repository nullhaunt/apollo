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
$geometryInclude = (Join-Path $PSScriptRoot 'MiiGeometryProbe.inl').Replace('\', '/')
$textureInclude = (Join-Path $PSScriptRoot 'MiiTextureProbe.inl').Replace('\', '/')
$staticInclude = (Join-Path $PSScriptRoot 'MiiStaticTextureProbe.inl').Replace('\', '/')
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
$textureResource = 'MII_RESOURCE_TEXTURE_PREFIX "MidSRGB.dat"'
if (!$source.Contains($textureResource)) {
  throw 'The sample texture resource path has changed; review the SDK sample before continuing.'
}

$source = $source.Replace($textureResource, 'MII_RESOURCE_TEXTURE_PREFIX "LowSRGB.dat"')
$hook = '    SetupConstantBuffers();'
if ($source.IndexOf($hook, [StringComparison]::Ordinal) -ne $source.LastIndexOf($hook, [StringComparison]::Ordinal)) {
  throw 'The sample setup hook has changed; review the SDK sample before continuing.'
}

$entryMarker = 'extern "C" void nnMain()'
$charInfoHook = '    LoadCharInfo();'
if (!$source.Contains($hook) -or !$source.Contains($entryMarker) -or !$source.Contains($charInfoHook)) {
  throw 'The sample entry point has changed; review the SDK sample before continuing.'
}

$includes = "#include `"$geometryInclude`"`r`n#include `"$textureInclude`"`r`n#include `"$staticInclude`"`r`n`r`n"
$source = $source.Replace($entryMarker, "$includes$entryMarker")
$selectDefault = "$charInfoHook`r`n    if (!ApolloSelectDefaultMii())`r`n    {`r`n        std::abort();`r`n    }"
$source = $source.Replace($charInfoHook, $selectDefault)
$exportHook = @'
    if (!ApolloExportMiiGeometry() || !ApolloExportGeneratedTextures() || !ApolloExportViewTextures())
    {
        std::abort();
    }
'@
$source = $source.Replace($hook, "$hook`r`n$exportHook")
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
