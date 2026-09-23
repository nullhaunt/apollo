param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$Platform,
    [Parameter(Mandatory = $true)][string]$VulkanSdk,
    [string]$NintendoSdkRoot = $env:NINTENDO_SDK_ROOT
)

$ErrorActionPreference = 'Stop'

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    if (-not (Test-Path -LiteralPath $Executable)) {
        throw "Shader tool not found: $Executable"
    }
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Shader tool failed ($LASTEXITCODE): $Executable"
    }
}

function Add-ByteArray([System.Text.StringBuilder]$Builder, [string]$Name, [string]$Path) {
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    [void]$Builder.AppendLine("alignas(8) inline constexpr unsigned char $Name`[] = {")
    for ($index = 0; $index -lt $bytes.Length; $index += 16) {
        $last = [Math]::Min($index + 15, $bytes.Length - 1)
        $values = for ($item = $index; $item -le $last; ++$item) {
            '0x{0:X2}' -f $bytes[$item]
        }
        [void]$Builder.AppendLine('  ' + ($values -join ', ') + ',')
    }
    [void]$Builder.AppendLine('};')
}

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($outputPath) | Out-Null

$dxc = Join-Path $VulkanSdk 'Bin\dxc.exe'
$spirvVal = Join-Path $VulkanSdk 'Bin\spirv-val.exe'
$spirvCross = Join-Path $VulkanSdk 'Bin\spirv-cross.exe'
$vertexPath = Join-Path $outputPath 'IndexedQuad.vs.spv'
$pixelPath = Join-Path $outputPath 'IndexedQuad.ps.spv'

Invoke-Checked $dxc @('-spirv', '-fspv-target-env=vulkan1.0', '-T', 'vs_6_0', '-E', 'VSMain', '-Fo', $vertexPath, $sourcePath)
Invoke-Checked $dxc @('-spirv', '-fspv-target-env=vulkan1.0', '-T', 'ps_6_0', '-E', 'PSMain', '-Fo', $pixelPath, $sourcePath)
Invoke-Checked $spirvVal @('--target-env', 'vulkan1.0', $vertexPath)
Invoke-Checked $spirvVal @('--target-env', 'vulkan1.0', $pixelPath)

$vertexReflection = (& $spirvCross $vertexPath --reflect | Out-String | ConvertFrom-Json)
$pixelReflection = (& $spirvCross $pixelPath --reflect | Out-String | ConvertFrom-Json)
if ($LASTEXITCODE -ne 0 -or
    $vertexReflection.inputs.Count -ne 3 -or
    $vertexReflection.inputs[0].location -ne 0 -or $vertexReflection.inputs[0].type -ne 'vec2' -or
    $vertexReflection.inputs[1].location -ne 1 -or $vertexReflection.inputs[1].type -ne 'vec3' -or
    $vertexReflection.inputs[2].location -ne 2 -or $vertexReflection.inputs[2].type -ne 'vec2' -or
    $vertexReflection.outputs.Count -ne 2 -or $vertexReflection.outputs[0].location -ne 0 -or
    $vertexReflection.outputs[0].type -ne 'vec3' -or
    $vertexReflection.outputs[1].location -ne 1 -or $vertexReflection.outputs[1].type -ne 'vec2' -or
    $pixelReflection.inputs.Count -ne 2 -or $pixelReflection.inputs[0].location -ne 0 -or
    $pixelReflection.inputs[0].type -ne 'vec3' -or
    $pixelReflection.inputs[1].location -ne 1 -or $pixelReflection.inputs[1].type -ne 'vec2' -or
    $pixelReflection.outputs.Count -ne 1 -or $pixelReflection.outputs[0].location -ne 0 -or
    $pixelReflection.outputs[0].type -ne 'vec4' -or
    $pixelReflection.separate_images.Count -ne 1 -or
    $pixelReflection.separate_images[0].set -ne 0 -or $pixelReflection.separate_images[0].binding -ne 0 -or
    $pixelReflection.separate_samplers.Count -ne 1 -or
    $pixelReflection.separate_samplers[0].set -ne 0 -or $pixelReflection.separate_samplers[0].binding -ne 1) {
    throw 'Indexed quad shader interface changed; update the vertex layout before building.'
}

$builder = [System.Text.StringBuilder]::new()
[void]$builder.AppendLine('#pragma once')
[void]$builder.AppendLine('namespace apollo::render::generated {')
Add-ByteArray $builder 'IndexedQuadVsSpirv' $vertexPath
Add-ByteArray $builder 'IndexedQuadPsSpirv' $pixelPath

if ($Platform -eq 'NX64') {
    if ([string]::IsNullOrWhiteSpace($NintendoSdkRoot)) {
        throw 'Set NINTENDO_SDK_ROOT for NX64 shader conversion.'
    }
    $compiler = Join-Path $NintendoSdkRoot 'Samples\Tools\NvnSimple\NvnSimpleShaderCompiler\NvnSimpleShaderCompiler.exe'
    $nvnPath = Join-Path $outputPath 'IndexedQuad.glslc'
    Invoke-Checked $compiler @($vertexPath, $pixelPath, '-o', $nvnPath)
    Add-ByteArray $builder 'IndexedQuadNvnGlslc' $nvnPath
}
elseif ($Platform -ne 'x64') {
    throw "Unsupported shader target: $Platform"
}

[void]$builder.AppendLine('} // namespace apollo::render::generated')
$header = Join-Path $outputPath 'IndexedQuadShader.hpp'
$content = $builder.ToString()
[System.IO.File]::WriteAllText($header, $content, [System.Text.Encoding]::ASCII)
