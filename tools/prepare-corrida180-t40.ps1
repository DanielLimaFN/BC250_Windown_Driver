[CmdletBinding()]
param()

# Apenas leitura: fixa o candidato T4.0 para a Corrida 180 (600 s).
# Mudancas:
# 1. UMD: Bc250Umd10OpenResource agora extrai KernelAllocation e GpuVirtualAddress dos
#    argumentos pOpenAllocationInfo2 do runtime WDDM quando disponiveis.
# 2. UMD: Bc250UmdBridgeGpuBlt implementada via escape BC250_ESCAPE_OP_COMPOSITE_BLT para
#    transferencia direta GPU-to-GPU por SDMA no Node 0 entre espacos de alocacao do processo.
# 3. UMD: Bc250WinsysReadAllocation agora utiliza lockArgs.Flags.ReadOnly = 1u.
# 4. UMD: Bc250UmdBuildVkDescriptorSet implementa estrategia em camadas para texturas de
#    OpenResource (Tier 1: Read/Upload CPU; Tier 2: SDMA GPU-to-GPU staging; Tier 3: fallback
#    seguro com dimensoes reais), eliminando o quadrado cinza solido 1x1 dos blocos do menu Iniciar.
# Mantem todas as correcoes de T3.9, T3.8, T3.7, T3.6, T3.5, T3.4, T3.3, HDP.1, T3.1, EVT.1, X1, X2a, X3 e X4.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = 'F082BA0BC02E1736D22E032C6A175F38F92ECC81F6C6E133C2537E4265C6323C'
    (Join-Path $package 'bc250mcdm.inf') = '289B6B9E69593879C6AA0F8F91104E58D109A692FE674D65FD13EE58AE994CC9'
    (Join-Path $package 'bc250mcdm.cat') = '279A3F4E5A323872D1FCCE65FE4015C5E616DE59FAC56544E886A5DA3E3DF659'
    (Join-Path $package 'bc250umd.dll') = '79C07B3DEEAE03980B0E799D71F3723D19DA27F1612C47448F804161901F25B5'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 180 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 180 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/29/2026,3.325.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato T4.0 controlado.'
}
$blockers = @()
# DX.86: depurador de kernel habilitado muda o tempo do experimento; so' com pedido explicito.
$startOptions = (Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control').SystemStartOptions
Write-Host "SystemStartOptions (sem chave): $($startOptions -replace 'ENCRYPTION_KEY=\S+','ENCRYPTION_KEY=***')"
if ($startOptions -match '(^|\s)DEBUG(\s|$)' -and $env:BC250_ALLOW_KERNEL_DEBUG -ne '1') {
    $blockers += 'depurador de kernel habilitado no boot (bcdedit /debug off e reiniciar, ou BC250_ALLOW_KERNEL_DEBUG=1)'
}
if (Get-Process -Name 'RadeonSoftware' -ErrorAction SilentlyContinue) { $blockers += 'RadeonSoftware.exe em execucao' }
$startCn = Get-ScheduledTask -TaskName 'StartCN' -ErrorAction SilentlyContinue
if ($startCn -and $startCn.State -ne 'Disabled') { $blockers += "StartCN $($startCn.State)" }
$events = Get-Service -Name 'AMD External Events Utility' -ErrorAction SilentlyContinue
if ($events -and ($events.Status -ne 'Stopped' -or $events.StartType -ne 'Disabled')) {
    $blockers += "AMD External Events Utility $($events.Status)/$($events.StartType)"
}
if ($blockers.Count -gt 0) { throw ('Preflight bloqueado: ' + ($blockers -join '; ')) }
foreach ($script in @('audit-bc250mcdm-safety.py', 'audit-bc250mcdm-lab-package.ps1')) {
    $path = Join-Path $PSScriptRoot $script
    if ($script.EndsWith('.py')) { & python $path | Out-Null }
    else { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $path | Out-Null }
    if ($LASTEXITCODE -ne 0) { throw "Preflight reprovou: $script exit=$LASTEXITCODE" }
}
$crash = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl'
if ([int]$crash.CrashDumpEnabled -ne 2 -or [int]$crash.AlwaysKeepMemoryDump -ne 1) {
    throw 'Dump de kernel nao configurado para eventual travamento.'
}
# Corrida 145: atalho alternativo de dump (qualquer Ctrl + Scroll Lock 2x).
foreach ($name in @('i8042prt', 'kbdhid')) {
    $params = "HKLM:\SYSTEM\CurrentControlSet\Services\$name\Parameters"
    if (Test-Path -LiteralPath $params) {
        $val = (Get-ItemProperty -LiteralPath $params -Name 'CrashOnCtrlScroll' -ErrorAction SilentlyContinue).CrashOnCtrlScroll
        if ($null -eq $val -or [int]$val -ne 1) {
            Write-Warning "CrashOnCtrlScroll ausente ou desligado em $name; atalho de teclado pode nao funcionar."
        }
    }
}
Write-Host "Preflight T4.0 (Corrida 180) aprovado: pacote 3.325.1.0 selado." -ForegroundColor Green
