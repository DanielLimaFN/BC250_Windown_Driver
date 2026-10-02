[CmdletBinding()]
param()

# Apenas leitura: fixa o candidato T3.8 (doc 534) para a Corrida 178 (600 s).
# Mudancas:
# 1. UMD: Clamping de subrecursos da SRV (MostDetailedMip e FirstArraySlice) para os limites
#    reais do recurso alocado em vez de recusar com SRV_ALEM_DO_SUBRECURSO_CARREGADO,
#    permitindo que o Direct2D no Explorer execute Draw com sucesso para shell brushes,
#    gradientes e icones sem abortar o contexto de renderizacao.
# 2. UMD: Extracao de geometria (Width, Height, RowPitch, Format) e Token na DDI
#    Bc250Umd10OpenResource a partir do blob privado da ABI de alocacao compartilhada.
# 3. UMD: Textura de fallback com pixel opaco neutro tema escuro (0xFF202020 = RGB 32,32,32, A=255)
#    em vez de preto transparente (0x00000000), prevenindo janelas pretas transparentes
#    durante a amostragem pelo DWM.
# 4. KMD: Habilitacao de EvictionSegmentSet = 2u para alocacoes nao-primarias no Active Miniport,
#    permitindo que o VidMm evite thrashing de paginacao (66.326 transferencias medidas na Corrida 177)
#    quando o Segmento 1 (256 MB) sofre pressao de memoria. Primaria segue com EvictionSegmentSet = 0u.
# Mantem todas as correcoes de T3.7, T3.6, T3.5, T3.4, T3.3, HDP.1, T3.1, EVT.1, X1, X2a, X3 e X4.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = 'A9991F6DF38CF4E21FAC70F6EC5D67EA48867683DC3AE8D4A516CD80FDDAF0E2'
    (Join-Path $package 'bc250mcdm.inf') = 'EEB52AB5210C71510D5602CDE08AA15FA22927B94CE7BA26A87C5ECD4CE0033A'
    (Join-Path $package 'bc250mcdm.cat') = '834A71E3FDD74D404D506AF0F68756BBE3838379ADA5C0F4B634399080088E7C'
    (Join-Path $package 'bc250umd.dll') = '59FE4A823D78EE9780AEF4013F437C0733512860DFE1CF1C8D14E2D77BD8EF36'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 178 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 178 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/29/2026,3.323.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato T3.8 controlado.'
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
Write-Host "Preflight T3.8 (Corrida 178) aprovado: pacote 3.323.1.0 selado." -ForegroundColor Green
