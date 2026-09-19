param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath,
    [switch]$Descriptors
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dll = (Resolve-Path -LiteralPath $DllPath).Path
if (-not [IO.Path]::IsPathRooted($dll)) { throw 'DllPath must resolve to an absolute path.' }

$fxc = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\fxc.exe'
$harness = Join-Path $root 'driver\umd\output\vkd3d_shader_real_smoke.exe'
if (-not (Test-Path -LiteralPath $fxc)) { throw "fxc not found: $fxc" }
if (-not (Test-Path -LiteralPath $harness)) { throw 'Build driver\umd\build-umd.bat first.' }

$run = Join-Path ([IO.Path]::GetTempPath()) ('bc250-vkd3d-smoke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
try {
    $vs = Join-Path $run 'smoke-vs.cso'
    $ps = Join-Path $run 'smoke-ps.cso'
    $vsSource = if ($Descriptors) { 'driver\tests\shaders\descriptor_vs.hlsl' } else { 'driver\tests\shaders\vkd3d_smoke_vs.hlsl' }
    $psSource = if ($Descriptors) { 'driver\tests\shaders\descriptor_ps.hlsl' } else { 'driver\tests\shaders\vkd3d_smoke_ps.hlsl' }
    & $fxc /nologo /T vs_4_0 /E main /Fo $vs (Join-Path $root $vsSource)
    if ($LASTEXITCODE -ne 0) { throw "fxc VS failed: $LASTEXITCODE" }
    & $fxc /nologo /T ps_4_0 /E main /Fo $ps (Join-Path $root $psSource)
    if ($LASTEXITCODE -ne 0) { throw "fxc PS failed: $LASTEXITCODE" }
    $identity = Get-FileHash -Algorithm SHA256 -LiteralPath $dll
    Write-Host ("PROVIDER_SHA256=" + $identity.Hash)
    if ($Descriptors) { & $harness $dll $vs $ps descriptors }
    else { & $harness $dll $vs $ps }
    if ($LASTEXITCODE -ne 0) { throw "real shader smoke failed: $LASTEXITCODE" }
} finally {
    if (Test-Path -LiteralPath $run) { Remove-Item -LiteralPath $run -Recurse -Force }
}
