# Apenas leitura: Corrida 218 = 3.366.1.0 = 3.365.1.0 + UC.1 (PTE de RAM da GART com MTYPE_UC, como gart_pte_flags do Linux; na 217 a fence do SDMA0 na RAM ficou invisivel com o anel drenado), doc 592. UMD e RADV identicas.
# Apenas leitura: Corrida 217 = 3.365.1.0 = 3.364.1.0 + LC.1 (recusa por pontos de divisao conta so' o buffer atual; sem o laco do rollback da 216) + V0.1 (uma tabela so' para a VMID 0: o MEC escreve as PTEs dele na GART e nao programa o contexto 0; anel/Work da D2 nas entradas 32/33; rollback do TDR mantem o contexto 0), doc 590. UMD e RADV identicas.
# Apenas leitura: Corrida 216 = 3.364.1.0 = 3.363.1.0 (D2 por GART) + foto do contexto 0 e da tabela no timeout e no TDR + TDR que volta o anel para a VRAM se a GART falhar (tela azul da 215), doc 588. UMD e RADV identicas.
# Apenas leitura: Corrida 215 = 3.363.1.0 = D2 por GART (doc 587): anel e Work do SDMA0 na RAM pela GART da VMID 0 (contexto 0, tabela de 4 KB em 0xFF02000), como o Linux; AGP desligada (refutada na 214). LOG.1 mantida. UMD e RADV identicas.
# Apenas leitura: Corrida 214 = 3.362.1.0 = 3.361.1.0 (D2) + LOG.1: a thread do rastro drena o anel de log em RAM, entao um StartDevice que falha deixa o log no disco (Corrida 213 ficou sem uma linha), doc 585. D2 identica; UMD e RADV identicas.
# Apenas leitura: Corrida 213 = D2 com 3.361.1.0 = 3.360.1.0 + anel e Work do SDMA0 em memoria de sistema (RAM contigua nao cacheada, abertura AGP depois da FB), doc 583. UMD e RADV identicas as da 212.
# Variavel: onde a CPU consulta o anel e as fences do SDMA0 (VRAM pela BAR -> RAM). M1b.1 e 4c.4 iguais a 212.
# Apenas leitura: Corrida 212 = M1b.1 com 3.360.1.0 = 3.358.1.0 + IB dividido escrito em PEDACOS de anel, sem recusa por tamanho (tela azul da Corrida 211: entrada de 1.018 dw recusada), doc 580. UMD e RADV identicas as da 211.
# Apenas leitura: Corrida 211 = M1b.1 ligada com 3.358.1.0 = 3.357.1.0 + RADV no segmento 2 (FUSION, tamanhos reais, dominio GTT) + KMD com transbordo para o segmento 2 + buffer do Present local e mapeavel, doc 579.
# Variavel: onde moram as alocacoes da RADV. 4c.4 igual a 210. UMD e RADV NOVAS (interruptores M1b).
# Apenas leitura: Corrida 210 = 4c.4 corrigida com 3.357.1.0 = 3.356.1.0 + ponto de divisao casando SO' com o IB que o contem (defeito da Corrida 209: o VidMm monta o proximo paging buffer antes de enviar o anterior), doc 576.
# Mesma variavel da 209 (PTE pelo SDMA); a correcao e' de defeito, nao experimento novo. UMD identica.
# Apenas leitura: Corrida 209 = 4c.4 com 3.356.1.0 = 3.355.1.0 + PTE pelo SDMA (GPUMMUCAPS=GPU_VIRTUAL, UpdatePageTable por WRITE_LINEAR no paging buffer) e IB de paginacao dividido no anel em cada FlushTlb, mais o journal falhando fechado no NO_SPACE (doc 575).
# Variavel de comportamento: quem escreve a PTE (CPU no build -> SDMA em ordem). L1, BPG1, 4c.3 iguais. UMD identica a da 208.
# Apenas leitura: Corrida 208 = 4c.3 com 3.355.1.0 = 3.354.1.0 + gates de submissao pelo journal de software, sem caminhar tabela de pagina nem ler o IB pela BAR (PtWalkOnSubmit=0), doc 573.
# Uma variavel: as leituras da CPU sobre tabela de pagina/IB no submit. L1, BPG1 e 4c.2 iguais. UMD identica a da 207.
# Apenas leitura: Corrida 207 = 3.354.1.0 = 3.353.1.0 (4c.2) + L1 (buffer com VkBuffer sem Lock de upload na alocacao WDDM; gatilho do 0x10E/0x34 das 197 e 206) + I.PG (BPG1 no rastro) + I.LK (duracao do Lock), doc 571.
# Variavel de comportamento: so o L1. I.PG e I.LK so medem. 4c.2 continua ARMADA.
# Apenas leitura: Corrida 206 = 4c.2 com 3.353.1.0 = 3.352.1.0 + FlushTlb pelo SDMA NO ANEL antes do INDIRECT, sem MMIO de invalidacao no build (doc 569).
# Uma variavel: o momento e o executor da invalidacao da TLB do FlushTlb. PTE ainda pela CPU no build. Ensaio de boot so com a passada do anel (porta do flush).
# Apenas leitura: Corrida 205 = 4c etapa 1 com 3.352.1.0 = 3.351.1.0 + ensaio de boot dos pacotes de memoria virtual pelo SDMA0 (doc 567).
# Uma variavel: o ensaio no StartDevice (WRITE_LINEAR + invalidacao de TLB pelo SDMA, no anel e dentro de IB, engine 1). Runtime identico ao da 204: PTE e flush pela CPU.
# Apenas leitura: Corrida 204 = 4b com 3.351.1.0 = 3.350.1.0 + UMD sem leitura do buffer linear de Present pela CPU (BAR), doc 564.
# Uma variavel: o acesso da CPU ao buffer linear. KMD com X2b e CG.1 identicos em comportamento, CMOS 256, RADV identica.
# Apenas leitura: Corrida 201 = candidato 3.348.1.0 = 3.347.1.0 (Corrida 200) + CG.1 (SDMA0_CLK_CTRL volta aos overrides do POST depois do anel provado), doc 557.
# Unica variavel: o clock gating do SDMA0. Instrumentacao (BPS1/BSQ1/BCG1), CMOS 256, UMD e RADV identicas as da 200.
# Apenas leitura: Corrida 200 = candidato 3.347.1.0 = 3.346.1.0 (Corrida 199) + I.SDMA0 (estado do SDMA0 a cada 100 ms, BSQ1) + I.CG (clock gating a cada 10 s, BCG1), doc 556.
# Unica variavel: instrumentacao somente leitura. CMOS em 256 (exigida abaixo). UMD e RADV identicas as da 199.
# Apenas leitura: Corrida 199 = candidato 3.346.1.0 (= 3.345.1.0 reempacotado: o fonte da UMD auditada mudou com a M1b.1 dormente, doc 554; comportamento identico, interruptores em 0).
# Antes: candidato 3.345.1.0 (= 3.344.1.0 + instrumentacao I.PLAT, doc 553) com a CMOS em UMA_SIZE=256 (doc 551).
# I.PLAT so' MEDE: registro BPS1 (Tctl + estado do SDMA1) a cada 1 s no rastro, e motivo do ultimo reset (FCH) lido e limpo pelo -Ler.
# Antes: Corrida 199 = MESMO pacote 3.344.1.0 da 198 com a CMOS em UMA_SIZE=256 (decisao do Daniel, doc 551: funcionar com o minimo).
# Unica variavel: o tamanho da VRAM. Com 256 MB a VRAM inteira fica dentro do BAR e o segmento 3 deixa de existir sozinho (driver dinamico).
# Apenas leitura: fixa o candidato M2 completo (3.344.1.0) para a Corrida 198: imagens da RADV sem CPU podem morar no segmento 3 (unica variavel).
# Corrida 197: Lock2 fechado (zero falhas na corrida inteira), V-ok com 4096 MB, e BugCheck 0x10E/0x34 aos ~2 min num Lock da UMD na Pesquisa (doc 548 secao 10).
[CmdletBinding()]
param()

# Apenas leitura: fixa o candidato M2.1 (3.343.1.0) para a Corrida 197: segmento 3 DECLARADO, nenhuma alocacao nele,
# e alocacoes da RADV com CPU passam a aceitar a abertura (SegSet=0x3, preferido 1) -- regra de Lock2 do VidMm
# (allocation-usage-tracking.md). Corrida 196 (desfecho A): so' declarar o segmento 3 ja' quebrava o Lock2.
# Antes: candidato M2-diag (3.342.1.0) para a Corrida 196: segmento 3 DECLARADO, nenhuma alocacao nele.
# Corrida 195: Lock2 0xC000000D numa alocacao do segmento 1 depois de uma de 4 KB ir para o 3.
# Antes: candidato M2b (3.341.1.0) para a Corrida 195.
# Corrida 194: segmento 3 recusado (o calculo tratou o marcador opaco do proprio driver como uso).
# Antes: candidato M2 (3.340.1.0) para a Corrida 194 (600 s, ABRINDO O MENU).
# Corrida 193: sem alias nas texturas proprias a memoria nao coube (DWM em laco de 242 recriacoes).
# M2 (doc 548): segmento 3 = VRAM alem do BAR, derivado da topologia lida no boot (UMA da BIOS de
# 512 MB a 8 GB, valor escolhido pelo Daniel sem me dizer); imagens da RADV sem CPU vao para la'.
# Base PA da VRAM tambem lida da placa. UMD e RADV identicas as da 193.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '4FD6B2AD1BD57BC6F0DA0899D64E016D4C0C5111037CEF4F7E73388C76B55614'
    (Join-Path $package 'bc250mcdm.inf') = 'B3B9A7567437B4156BD69B5414F46A92AAA7F024222B4E7D1DBE1FF98A7DF88C'
    (Join-Path $package 'bc250mcdm.cat') = 'E8A5AA46C8B3F62334C604B63C8FBB9C885BFB81D418886BAF59E47F9A2FB978'
    (Join-Path $package 'bc250umd.dll') = '8782A5E54A2774548D243BE4492836B05A9D87A1559919AAC570CB41F9D4E468'
    $radv = '80EBB24837652BFD780227944585F6EAAC254799A62651B192180FD34FB47043'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 218 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 218 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '10/05/2026,3.366.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato 3.366.1.0 controlado.'
}
# doc 551/553: a ultima leitura da CMOS (feita pelo EXECUTAR logo antes) tem de mostrar 256 MB APLICADOS pela ABL.
$cmosDir = Join-Path $root 'docs\evidence\vram-cmos'
$lastRead = Get-ChildItem -LiteralPath $cmosDir -Filter '*-ler.txt' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $lastRead -or $lastRead.LastWriteTime -lt (Get-Date).AddMinutes(-15)) {
    throw 'Leitura da CMOS ausente ou com mais de 15 min: rode tools\vram-cmos\bc250-vram.ps1 -Ler.'
}
$cmosText = Get-Content -LiteralPath $lastRead.FullName -Raw
if ($cmosText -notmatch 'UMA_SIZE=0256 MB' -or $cmosText -notmatch 'valor APLICADO pelo firmware') {
    throw "CMOS nao esta em 256 MB aplicados: $($lastRead.Name)"
}
if ($cmosText -notmatch 'RESET_REASON') {
    Write-Warning 'Leitura da CMOS sem RESET_REASON: bc250-vram.exe antigo? Rode tools\vram-cmos\build.bat.'
}
Write-Host "CMOS: 256 MB aplicados ($($lastRead.Name))"
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
Write-Host "Preflight Corrida 218 aprovado: pacote 3.366.1.0 (D2 por GART com PTE MTYPE_UC + VMID 0 unica + LC.1) selado." -ForegroundColor Green
