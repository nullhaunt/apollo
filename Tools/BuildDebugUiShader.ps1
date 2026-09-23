param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$VulkanSdk,
    [string]$NintendoSdkRoot = $env:NINTENDO_SDK_ROOT
)

$ErrorActionPreference = 'Stop'
function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    if (-not (Test-Path -LiteralPath $Executable)) { throw "Shader tool not found: $Executable" }
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Shader tool failed ($LASTEXITCODE): $Executable" }
}
function Add-ByteArray([System.Text.StringBuilder]$Builder, [string]$Name, [string]$Path) {
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    [void]$Builder.AppendLine("alignas(8) inline constexpr unsigned char $Name`[] = {")
    for ($index = 0; $index -lt $bytes.Length; $index += 16) {
        $last = [Math]::Min($index + 15, $bytes.Length - 1)
        $values = for ($item = $index; $item -le $last; ++$item) { '0x{0:X2}' -f $bytes[$item] }
        [void]$Builder.AppendLine('  ' + ($values -join ', ') + ',')
    }
    [void]$Builder.AppendLine('};')
}

$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($outputPath) | Out-Null
$dxc = Join-Path $VulkanSdk 'Bin\dxc.exe'
$validator = Join-Path $VulkanSdk 'Bin\spirv-val.exe'
$reflector = Join-Path $VulkanSdk 'Bin\spirv-cross.exe'
$vs = Join-Path $outputPath 'DebugUi.vs.spv'
$ps = Join-Path $outputPath 'DebugUi.ps.spv'
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
Invoke-Checked $dxc @('-spirv','-fspv-target-env=vulkan1.0','-T','vs_6_0','-E','VSMain','-Fo',$vs,$sourcePath)
Invoke-Checked $dxc @('-spirv','-fspv-target-env=vulkan1.0','-T','ps_6_0','-E','PSMain','-Fo',$ps,$sourcePath)
Invoke-Checked $validator @('--target-env','vulkan1.0',$vs)
Invoke-Checked $validator @('--target-env','vulkan1.0',$ps)
$vr = (& $reflector $vs --reflect | Out-String | ConvertFrom-Json)
$pr = (& $reflector $ps --reflect | Out-String | ConvertFrom-Json)
if ($LASTEXITCODE -ne 0 -or
    $vr.inputs.Count -ne 3 -or
    $vr.inputs[0].location -ne 0 -or $vr.inputs[0].type -ne 'vec2' -or
    $vr.inputs[1].location -ne 1 -or $vr.inputs[1].type -ne 'vec2' -or
    $vr.inputs[2].location -ne 2 -or $vr.inputs[2].type -ne 'vec4' -or
    $pr.separate_images.Count -ne 1 -or $pr.separate_images[0].binding -ne 0 -or
    $pr.separate_samplers.Count -ne 1 -or $pr.separate_samplers[0].binding -ne 1) {
    throw 'Debug UI shader interface changed.'
}
if ([string]::IsNullOrWhiteSpace($NintendoSdkRoot)) { throw 'Set NINTENDO_SDK_ROOT for NVN shader conversion.' }
$compiler = Join-Path $NintendoSdkRoot 'Samples\Tools\NvnSimple\NvnSimpleShaderCompiler\NvnSimpleShaderCompiler.exe'
$glslc = Join-Path $outputPath 'DebugUi.glslc'
Invoke-Checked $compiler @($vs,$ps,'-o',$glslc)
$builder = [System.Text.StringBuilder]::new()
[void]$builder.AppendLine('#pragma once')
[void]$builder.AppendLine('namespace apollo::render::generated {')
Add-ByteArray $builder 'DebugUiNvnGlslc' $glslc
[void]$builder.AppendLine('} // namespace apollo::render::generated')
[System.IO.File]::WriteAllText((Join-Path $outputPath 'DebugUiShader.hpp'), $builder.ToString(), [System.Text.Encoding]::ASCII)
