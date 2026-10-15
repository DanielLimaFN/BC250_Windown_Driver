# Apenas leitura: Corrida 248 = lab 3.392.1.0 = 3.391.1.0 (G.1 + RLC.1) + RLC.1b (RLC_PG_CNTL bit 23 = 1, handshake RLC/SMU desligado, ramo sem GFXOFF de gfx_v10_0_rlc_smu_handshake_cntl) + LOGMIN.1 (log minimo da UMD), doc 650. P3.3=0; a MESMA RADV B4AE2E14.
# Apenas leitura: Corrida 246 = lab 3.390.1.0 = 3.389.1.0 + G.1 (34 golden settings do Cyan Skillfish como gfx_v10_0_init_golden_registers, antes do PSP/SDMA0/CP), doc 645. RLC na ROM, P3.3=0, MEC.1 e GFXRST.1 iguais; a MESMA RADV B4AE2E14. Uma variavel: os goldens.
# Apenas leitura: Corrida 245 = lab 3.389.1.0 = 3.388.1.0 com a RLC.1 DESLIGADA (RLC de volta na ROM), doc 643. P3.3=0, MEC.1 e GFXRST.1 iguais a 244; a MESMA RADV B4AE2E14. Pergunta: a trava precoce da 244 (~40 s) e' do RLC rodando?
# Apenas leitura: Corrida 244 = lab 3.388.1.0 = 3.387.1.0 com a P3.3 DESLIGADA (doc 641) + RLC.1 (firmware do RLC pelo PSP e gfx_v10_0_rlc_resume, RLC rodando) + GFXRST.1 (reset do GFX como gfx_v10_0_soft_reset, so' age em TDR do GFX), doc 642. A MESMA RADV B4AE2E14. Tres mudancas juntas por decisao do Daniel (07/10: 'estamos muito devagar'). Mesmo roteiro (so o menu).
# Apenas leitura: Corrida 243 = lab 3.387.1.0 = 3.386 (P3.3 religada) + MEC.1 (CP_MEC_DOORBELL_RANGE_UPPER 0x450 como o amdgpu, doc 640) + a MESMA RADV B4AE2E14. Unica variavel contra a 242/242b: a MEC.1. Mesmo roteiro (so o menu).
# Apenas leitura: Corrida 240 = 3.385.1.0 = 3.384.1.0 + SO instrumentacao (doc 631): RLC_CNTL/RLC_STAT/RLC_SAFE_MODE e IB2 no boot e no TDR, entradas cruas do IH de origem desconhecida (erros do SQ), descartes da P4.2 por motivo, mensagem do [P4_SDMA1]. RADV 7B82C4AE = C6BEC54B + despejo de IB no AppContainer e anel dos ultimos 256 IBs.
# Mesmo roteiro da 239 (so o menu Iniciar). Nenhuma mudanca de comportamento de GPU.
# Apenas leitura: Corrida 239 = MESMO pacote 3.384.1.0 da 236/237/238, uso = SO o menu Iniciar (abrir/fechar a cada ~30 s), doc 628.
# Bisseccao por carga: a 237 parada e a 238 com uso leve passaram 600 s; a 236 travou aos ~45 s com o menu aberto.
# Apenas leitura: Corrida 237 = MESMO pacote 3.384.1.0 da 236 (nenhum byte muda), com a maquina PARADA no desktop por 600 s, doc 625.
# Unica variavel: a carga (sem menu, Pesquisa, apps, mouse ou teclado). Pergunta: a trava precisa de uso ou so de tempo ligado?
# Apenas leitura: Corrida 236 = 3.384.1.0 = 3.382.1.0 + P4.2 (escritas de PTE do paging buffer no SDMA1, como vm_pte_scheds do amdgpu; ordem pela troca FENCE(Go)/POLL(Done) entre os aneis; paginacao e invalidacao seguem no SDMA0), doc 622. RADV igual a da 234.
# Apenas leitura: Corrida 235 = 3.382.1.0 = 3.381.1.0 + P4.1 (SDMA1 vivo e ocioso como o amdgpu: firmware pelo PSP tipo 10, anel na RAM pela GART entradas 224/225, prova por FENCE sobre poison e RPTR==WPTR, desligado antes da GART), doc 620. RADV igual a da 234.
# Apenas leitura: Corrida 234 = 3.381.1.0 = 3.380.1.0 + P6 (MTYPE_UC na folha dos BOs da RADV com GL2_BYPASS; RADV NOVA declara a flag) + EOP do CP (RELEASE_MEM do GFX com INT_SEL=2, TIME_STAMP_INT_ENABLE, CP_EOP acorda a aposentadoria do GFX), doc 618.
# Apenas leitura: Corrida 233 = 3.380.1.0 = 3.379.1.0 + P3.2 (o TRAP do SDMA0 dispara o DPC de aposentadoria; timer vira rede de 10 ms com o IH armado), doc 616.
# Apenas leitura: Corrida 228 = 3.375.1.0 = 3.374.1.0 + FB.1 (motivo da falha do blt gravado na memoria do kernel; anel de log extraido do dump pela coleta pos-trava), doc 607.
# Apenas leitura: Corrida 226 = 3.374.1.0 = 3.373.1.0 + P1b.2 (mapeamento da VRAM pela DRAM com MmMapIoSpaceEx; DxgkCbMapMemory recusou a DRAM na 225), doc 604.
# Apenas leitura: Corrida 225 = 3.373.1.0 = 3.372.1.0 + P1b.1 (prova de alias por escrita: dois padroes pela DRAM lidos pela BAR na ultima pagina, conteudo restaurado), doc 603.
# Apenas leitura: Corrida 224 = 3.372.1.0 = 3.371.1.0 + P1b (CPU na VRAM pela DRAM, GCMC_VM_FB_OFFSET<<24, sem HDP, como o amdgpu em APU), doc 602.
# Apenas leitura: Corrida 223 = 3.371.1.0 = 3.370.1.0 + P2c (abertura de doorbell do BIF ligada, RCC_DOORBELL_APER_EN, como nv_common_hw_init), doc 600.
# Apenas leitura: Corrida 222 = 3.370.1.0 = 3.369.1.0 + P2b (doorbell do SDMA0 programada dentro da reconstrucao do anel, F32 parado, como sdma_v5_0_gfx_resume), doc 599.
# Apenas leitura: Corrida 221 = 3.369.1.0 = 3.368.1.0 + P2 (WPTR do SDMA0 por doorbell, como sdma_v5_0_ring_set_wptr; BIF_SDMA0_DOORBELL_RANGE indice 0x200 SIZE 20), doc 598.
# Apenas leitura: Corrida 219 = 3.367.1.0 = 3.366.1.0 + P1 silencio da BAR (sem migalhas/eventos na VRAM, sem amostragem de VRAM no Present, sem leitura de registrador no rastro; migalhas e eventos em RAM -> rastro), doc 594.
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
    (Join-Path $package 'bc250mcdm.sys') = '5EB4F626312FFCFBB037A03A142AD8C55E9D2CF5A0668919796AD5268402D0E3'
    (Join-Path $package 'bc250mcdm.inf') = '6CE3F837CFC078C0F54A918CDC68B319496123F819805FEBAA61529EE3DF22EA'
    (Join-Path $package 'bc250mcdm.cat') = 'EC12639E895A41EBB1D028EA3AC8612976A5E59499702BFB207FEF991B04A6AA'
    (Join-Path $package 'bc250umd.dll') = 'B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66'
    $radv = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    'C:\BC250_LIB\vulkan_radeon.dll' = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    (Join-Path $package 'cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
    (Join-Path $env:SystemRoot 'System32\drivers\cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 248 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 248 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '10/08/2026,3.392.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato 3.392.1.0 controlado.'
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
# Corrida 238 (doc 627): o boot antes do bind foi reinicio, e apps reabriram sozinhos.
# Doc 625 sec. 1: depois de boot frio o RESET_REASON nao tem o bit 19 (0x00080000).
if ($cmosText -match 'RESET_REASON raw=0x([0-9A-Fa-f]{8})') {
    $reset = [Convert]::ToUInt32($Matches[1], 16)
    if (($reset -band 0x00080000) -ne 0 -and $env:BC250_ALLOW_WARM_BOOT -ne '1') {
        $blockers += "ultimo reset foi a quente (RESET_REASON=0x$($Matches[1]), bit 19): desligue (nao reinicie) e ligue de novo, ou BC250_ALLOW_WARM_BOOT=1"
    }
} else {
    Write-Warning 'RESET_REASON nao lido: nao da para conferir o boot frio.'
}
# So' DWM, Explorer e o menu podem usar a GPU nesta corrida.
# M365Copilot relanca o msedgewebview2 sozinho (medido em 07/10): feche-o antes do WebView2.
$apps = @('Claude', 'msedge', 'msedgewebview2', 'M365Copilot', 'SystemSettings', 'Taskmgr', 'chrome', 'firefox') |
    ForEach-Object { Get-Process -Name $_ -ErrorAction SilentlyContinue } | Where-Object { $_ }
if ($apps -and $env:BC250_ALLOW_APPS -ne '1') {
    $nomes = ($apps | Select-Object -ExpandProperty ProcessName -Unique) -join ', '
    $blockers += "apps abertos que carregam a GPU: $nomes (feche todos, ou BC250_ALLOW_APPS=1)"
}
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
# Corrida 237 (doc 625): nada pode desligar a tela nem suspender dentro dos 600 s, senao a carga muda sozinha.
foreach ($q in @(@('SUB_VIDEO', 'VIDEOIDLE', 'desligar o monitor'), @('SUB_SLEEP', 'STANDBYIDLE', 'suspensao'), @('SUB_SLEEP', 'HIBERNATEIDLE', 'hibernacao'))) {
    $txt = (powercfg /query SCHEME_CURRENT $q[0] $q[1]) -join "`n"
    if ($txt -match '(?i)(Alternadas Atuais|Current AC Power Setting Index):\s*0x([0-9a-f]+)') {
        $sec = [Convert]::ToInt32($Matches[2], 16)
        if ($sec -ne 0 -and $sec -le 900) { throw "Energia: $($q[2]) em $sec s (na tomada) cai dentro da corrida; ponha em Nunca." }
        Write-Host "Energia: $($q[2]) = $(if ($sec -eq 0) { 'nunca' } else { "$sec s" })"
    } else { Write-Warning "Nao li $($q[2]) do powercfg; confira a mao que nao dispara em 15 min." }
}
$ss = Get-ItemProperty -LiteralPath 'HKCU:\Control Panel\Desktop' -ErrorAction SilentlyContinue
if ($ss -and $ss.'SCRNSAVE.EXE' -and [int]$ss.ScreenSaveActive -eq 1) {
    $to = 0; [void][int]::TryParse("$($ss.ScreenSaveTimeOut)", [ref]$to)
    if ($to -gt 0 -and $to -le 900) { throw "Protecao de tela ($($ss.'SCRNSAVE.EXE')) em $to s cai dentro da corrida; desligue." }
}
Write-Host "Preflight Corrida 248 aprovado: pacote 3.392.1.0 (G.1 + RLC.1 + RLC.1b handshake SMU desligado + LOGMIN.1; P3.3=0) e RADV B4AE2E14 selados; carga = SO o menu Iniciar." -ForegroundColor Green
