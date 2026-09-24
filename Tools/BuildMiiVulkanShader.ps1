param(
  [Parameter(Mandatory = $true)][string]$Source,
  [Parameter(Mandatory = $true)][string]$OutputDirectory,
  [Parameter(Mandatory = $true)][string]$VulkanSdk
)

$ErrorActionPreference = 'Stop'

$dxc = Join-Path $VulkanSdk 'Bin\dxc.exe'
$validator = Join-Path $VulkanSdk 'Bin\spirv-val.exe'
if (!(Test-Path -LiteralPath $dxc) -or !(Test-Path -LiteralPath $validator)) {
  throw 'Vulkan DXC and SPIR-V validator are required.'
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$vertexPath = Join-Path $OutputDirectory 'MiiHead.vs.spv'
$pixelPath = Join-Path $OutputDirectory 'MiiHead.ps.spv'

& $dxc @('-spirv', '-fspv-target-env=vulkan1.0', '-T', 'vs_6_0', '-E', 'VSMain', '-Fo', $vertexPath, $Source)
if ($LASTEXITCODE -ne 0) { throw 'Mii Vulkan vertex shader compilation failed.' }
& $dxc @('-spirv', '-fspv-target-env=vulkan1.0', '-T', 'ps_6_0', '-E', 'PSMain', '-Fo', $pixelPath, $Source)
if ($LASTEXITCODE -ne 0) { throw 'Mii Vulkan fragment shader compilation failed.' }
& $validator --target-env vulkan1.0 $vertexPath
if ($LASTEXITCODE -ne 0) { throw 'Mii Vulkan vertex SPIR-V validation failed.' }
& $validator --target-env vulkan1.0 $pixelPath
if ($LASTEXITCODE -ne 0) { throw 'Mii Vulkan fragment SPIR-V validation failed.' }

$builder = [System.Text.StringBuilder]::new()
[void]$builder.AppendLine('#pragma once')
[void]$builder.AppendLine('namespace apollo::render::generated {')
foreach ($shader in @(@('MiiHeadVsSpirv', $vertexPath), @('MiiHeadPsSpirv', $pixelPath))) {
  [byte[]]$bytes = [IO.File]::ReadAllBytes($shader[1])
  [void]$builder.AppendLine("alignas(8) inline constexpr unsigned char $($shader[0])[] = {")
  for ($offset = 0; $offset -lt $bytes.Length; $offset += 16) {
    $last = [Math]::Min($offset + 15, $bytes.Length - 1)
    $values = for ($index = $offset; $index -le $last; ++$index) { '0x{0:X2}' -f $bytes[$index] }
    [void]$builder.AppendLine('  ' + ($values -join ', ') + ',')
  }
  [void]$builder.AppendLine('};')
}
[void]$builder.AppendLine('} // namespace apollo::render::generated')
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'MiiHeadVulkanShader.hpp'), $builder.ToString(), [Text.Encoding]::ASCII)
