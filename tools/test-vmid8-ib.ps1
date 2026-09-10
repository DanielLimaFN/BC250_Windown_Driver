<#
    Ensaio de laboratorio: um INDIRECT sob VMID 8 e executado?

    POR QUE ESTE ENSAIO EXISTE

    Sete corridas do driver WDDM de producao terminaram no mesmo ponto: a SDMA0
    aposenta o pacote INDIRECT (RPTR de writeback = 96, exatamente o fim do
    pacote), a engine fica OCIOSA (STATUS1 & 0x3FF == 0x3FF), e nem o IB nem os
    pacotes seguintes do anel executam. A fence fica no poison.

    Cada uma dessas corridas custou tela preta, BugCheck 0x119, reboot,
    recuperacao e boot frio -- para UM ponto de dado. E a pergunta nao tem
    conteudo WDDM nenhum: e pergunta de engine.

    Aqui a mesma pergunta e feita pelo driver de laboratorio. Nos montamos as
    page tables, o IB e o destino; nos emitimos o INDIRECT; nos conferimos a
    fence e os bytes. Sem VidMm, sem dxgkrnl, sem display.

    O QUE ESTE ENSAIO NAO RESPONDE

    Se as page tables DO VIDMM e a colocacao de buffer dele funcionam. Isso
    continua exigindo o driver de producao. Passar aqui nao garante passar la.

    RISCO

    Nao e zero. Este script escreve nos registradores do VMID 8 da GCVM e
    submete pacotes no anel da SDMA0. O repositorio ja mediu que SDMA quebrada
    nesta placa nao volta por software -- o custo de erro e um reboot. O que
    some em relacao ao caminho de producao e a tela preta, o BugCheck, o
    fusivel e a restauracao do BasicDisplay.

    O VMID 8 nao e usado pelo sistema em execucao: o display herdado do GOP e
    scanout puro e nao passa por contexto da GCVM. O driver de producao ja
    escreve estes mesmos registradores em toda corrida.

    A PROVA CAUSAL QUE SO EXISTE AQUI

    A variante DIRTY reproduz de proposito o defeito corrigido em 05/09: PDE
    com flags 0x61, onde o bit 6 e o LSB do campo de endereco [47:6] e desloca
    a tabela folha em 64 bytes. Rodar DIRTY e CLEAN no mesmo boot transforma
    "mudamos cinco coisas e o comportamento mudou" em prova causal.
#>

[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptTheRisk,
    [switch]$UpstreamSmokeTest,
    [switch]$ObservableFaults,
    [switch]$UseMtype,
    [switch]$GlobalMmu,
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli  = Join-Path $root 'studio\output\bc250ctl.exe'

# ---------------------------------------------------------------------------
# Mapa de memoria na VRAM.
#
# Fica longe do anel da SDMA (0x0FF00000), do alvo do ptbuild legado
# (0x0FD00000) e do scanout do GOP (offset 0). Tudo alinhado a 4 KiB; a root
# precisa de 64 KiB por causa dos 13 bits de indice.
# ---------------------------------------------------------------------------
$VRAM_ROOT  = 0x0F000000   # 8192 entradas x 8 B = 64 KiB
$VRAM_LEAF  = 0x0F010000   # 512 entradas x 8 B = 4 KiB
$VRAM_IB    = 0x0F020000   # o indirect buffer
$VRAM_DEST_PHYS   = 0x0F030000   # destino do PHYS (4 dwords DEADBEE1..4)
$VRAM_DEST_EXEC   = 0x0F070000   # destino do EXEC (4 dwords BC25E001..4, isolado 256 KB acima)
$VRAM_DEST_NOEXEC = 0x0F080000   # destino do NOEXEC (4 dwords BC25A001..4, isolado)
$VRAM_DEST_DIRTY  = 0x0F090000   # destino do DIRTY (4 dwords BC25D001..4, isolado)
$VRAM_MARK  = 0x0F050000   # marcador do WRITE_LINEAR que segue o INDIRECT
$VRAM_FENCE = 0x0F060000   # fence gravada pela GPU apos GCR rinse
$DECOY_VRAM = 0x00202000   # endereco que seria escrito se a GPU tratasse o VA como fisico cru

# ---------------------------------------------------------------------------
# Enderecos virtuais. Espaco de 34 bits: VA[33:21] indexa a root (13 bits),
# VA[20:12] indexa a folha (9 bits), VA[11:0] e offset na pagina.
# Ambos caem na MESMA folha (indice 1 da root) de proposito: assim um unico
# PDE cobre o IB e o destino, e o teste isola o PDE.
# ---------------------------------------------------------------------------
$VA_IB   = 0x00201000      # root 1, folha 1
$VA_DEST = 0x00202000      # root 1, folha 2

# Sufixo L obrigatorio: em PowerShell 5.1 o literal 0xCAFEBABE e lido como
# Int32 e vira -889275714, que nao cabe num parametro [uint32].
$MC_BASE      = [uint64]0xF400000000L   # base fisica de VRAM do laboratorio
$FILL_PATTERN     = [uint32]0xCAFEBABEL
$UPSTREAM_PATTERN = [uint32]0xDEADBEEFL
# Idem para o marcador: 0xFEEDFACE lido como Int32 vira -17958194, e a
# comparacao contra o [uint32] lido da VRAM dava SEMPRE falso. Foi o que
# inverteu a leitura do marcador na corrida de 05/09 11:26.
<#
    MARCADOR UNICO POR VARIANTE.

    Ate 05/09 11:35 todas as variantes escreviam 0xFEEDFACE no MESMO endereco.
    O 'sdma raw' devolve quando o RPTR do REGISTRADOR alcanca o WPTR
    (dbg/hw.c:1672 cai para ele quando o writeback nao alcanca), o que pode
    acontecer ANTES da execucao terminar. Entao a escrita de uma variante podia
    chegar depois que a proxima ja tinha envenenado o endereco -- e a proxima
    lia 0xFEEDFACE e creditava a si mesma.

    Foi o que produziu duas corridas consecutivas discordando com entradas
    identicas: 11:26 deu EXEC e DIRTY "gravou", 11:35 deu tudo "nao gravou".

    Com um valor por variante, uma leitura cruzada e detectavel em vez de
    silenciosa.
#>
$MARK_BASE    = [uint32]0xFEED0000L
$MARK_VALUE   = $MARK_BASE   # sobrescrito por variante
$POISON       = 0x43DAFFFE

# Registradores. Offsets do catalogo docs/03_CATALOGO_DE_REGISTRADORES.md e de
# driver/core/bc250_gcvm_vmid8_transaction.h.
$REG_FB_OFFSET   = 0x0A5AC
$REG_CTX8_CNTL   = 0x0A220
$REG_CTX8_BASE_LO = 0x0A3EC
$REG_CTX8_BASE_HI = 0x0A3F0
$REG_CTX8_START_LO = 0x0A46C
$REG_CTX8_START_HI = 0x0A470
$REG_CTX8_END_LO = 0x0A4EC
$REG_CTX8_END_HI = 0x0A4F0
$REG_SDMA_STATUS2  = 0x04A60   # catalogo linha 453: dw 0x0038 -> 0x04A60
$REG_SDMA_STATUS1  = 0x04A18
$REG_FAULT_STATUS  = 0x0A130   # GCVM_L2_PROTECTION_FAULT_STATUS
$REG_FAULT_ADDR_LO = 0x0A134   # GCVM_L2_PROTECTION_FAULT_ADDR_LO32
$REG_FAULT_ADDR_HI = 0x0A138   # GCVM_L2_PROTECTION_FAULT_ADDR_HI32
$REG_FAULT_CNTL    = 0x0A120   # GCVM_L2_PROTECTION_FAULT_CNTL
# Bloco GFX_IB_*: derivacao cercada pelas ancoras IB_CNTL (0x04BA8) e DOORBELL
# (0x04BC8), registrada em docs/03_CATALOGO_DE_REGISTRADORES.md secao
# "Bloco SDMA0_GFX_IB_* -- derivacao cercada por ancoras". SOMENTE LEITURA:
# quem carrega estes registradores e o microcodigo do F32 ao consumir o
# INDIRECT. Por isso servem de diagnostico do salto.
$REG_SDMA_CNTL       = 0x049F0
$REG_IB_CNTL         = 0x04BA8
$REG_IB_RPTR         = 0x04BAC
$REG_IB_OFFSET       = 0x04BB0
$REG_IB_BASE_LO      = 0x04BB4
$REG_IB_BASE_HI      = 0x04BB8
$REG_IB_SIZE         = 0x04BBC
$REG_SKIP_CNTL       = 0x04BC0
$REG_CONTEXT_STATUS  = 0x04BC4
$REG_IB_SUB_REMAIN   = 0x04C3C
$F_CNTL_MIDCMD       = 0x00000020

if ($ObservableFaults) {
    # 0x007FFE03: habilita as 7 classes de fault interrupt (BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE)
    # declarada em driver/core/bc250_gcvm_vmid8_transaction.h:102
    $CTX8_CNTL_VALUE = 0x007FFE03
} else {
    $CTX8_CNTL_VALUE = 0x00555403  # ENABLE | DEPTH=1 | BLOCK=0 | defaults de fault
}
$CTX8_END_LO     = 0x003FFFFF  # max_pfn-1 para 34 bits

# Flags de entrada. Layout do Navi 10 (gmc_v10_0.c / amdgpu_vm.h):
#   11:7 fragment | 6 write | 5 read | 4 exe | 3 Z | 2 snooped | 1 system | 0 valid
# EXEC e o bit 4. O bit 7 e FRAG[0] -- era ali que o projeto o colocava.
$PTE_VALID = 0x01
$PTE_EXEC  = 0x10
$PTE_READ  = 0x20
$PTE_WRITE = 0x40

if (-not $Execute) {
    Write-Host 'Modo preflight. Nada foi escrito no hardware.'
    Write-Host 'Para executar: -Execute -IAcceptTheRisk'
    Write-Host ''
    Write-Host 'O ensaio vai:'
    Write-Host '  0. rodar o parsetest (a engine decodifica pacotes do anel?)'
    Write-Host '  1. montar root + folha de 2 niveis na VRAM'
    Write-Host '  2. programar o VMID 8 e invalidar a TLB'
    Write-Host '  3. emitir INDIRECT sob VMID 8 apontando para um IB traduzido'
    Write-Host '  4. conferir fence e bytes do destino'
    Write-Host '  5. repetir com o PDE deliberadamente corrompido (prova causal)'
    Write-Host '  6. desligar o VMID 8 e devolver o anel'
    exit 0
}
if (-not $IAcceptTheRisk) {
    throw 'Faltou -IAcceptTheRisk. Leia o cabecalho deste arquivo antes.'
}
if (-not (Test-Path $cli)) {
    throw "bc250ctl.exe nao encontrado em $cli. Rode studio\build.bat."
}

if (-not $OutDir) {
    $stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
    $OutDir = Join-Path $root "docs\evidence\$stamp-vmid8-ib-lab"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$transcript = Join-Path $OutDir 'run.txt'
Start-Transcript -Path $transcript | Out-Null

function Ctl {
    # NAO usar $Args como nome de parametro: e variavel automatica do PowerShell.
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$CmdArgs)
    $out = & $cli @CmdArgs 2>&1
    $out | ForEach-Object { Write-Host "    $_" }
    return ,$out
}

function RegRead {
    param([int]$Offset)
    $out = & $cli reg r ("0x{0:X}" -f $Offset) 2>&1
    foreach ($line in $out) {
        if ($line -match '=\s*0x([0-9A-Fa-f]{8})') { return [uint32]("0x" + $Matches[1]) }
    }
    throw "nao consegui ler o registrador 0x$('{0:X}' -f $Offset): $out"
}

function RegWrite {
    param([int]$Offset, [uint32]$Value)
    $out = & $cli reg w ("0x{0:X}" -f $Offset) ("0x{0:X8}" -f $Value) 2>&1
    $joined = ($out -join ' ')
    if ($joined -match 'NAO GUARDOU') {
        Write-Host "    AVISO: 0x$('{0:X}' -f $Offset) nao guardou 0x$('{0:X8}' -f $Value)"
        return $false
    }
    if ($joined -match 'NEGADO') { throw "driver em readonly; rode 'bc250ctl arm write'" }
    return $true
}

function VramRead {
    param([int]$Offset)
    $out = & $cli vram r ("0x{0:X}" -f $Offset) 2>&1
    # A linha e 'vram 0xOFFSET = 0xVALOR   (GPU 0x...)'. Ancorar no '=' -- um
    # regex ancorado no fim da linha pegaria o endereco GPU, nao o valor.
    foreach ($line in $out) {
        if ($line -match '=\s*0x([0-9A-Fa-f]{8})') { return [uint32]("0x" + $Matches[1]) }
    }
    throw "nao consegui ler VRAM 0x$('{0:X}' -f $Offset): $out"
}

function VramWrite {
    param([int]$Offset, [uint32]$Value)
    & $cli vram w ("0x{0:X}" -f $Offset) ("0x{0:X8}" -f $Value) | Out-Null
}

<#
    PowerShell 5.1 NAO faz operacao bit-a-bit correta em 64 bits sem sinal.
    Medido nesta maquina:

        [uint64]0x45F010001 -band 0xFFFFFFFF  ->  18773770241  (INTACTO)
        [uint64]0xFFFFFFFF                    ->  erro: -1 nao cabe em UInt64

    Por isso o corte usa aritmetica, nao mascara. Os valores aqui ficam perto
    de 2^35, muito abaixo de 2^53, entao a divisao e exata.
#>
function Lo32 {
    param([uint64]$Value)
    return [uint32]($Value % 4294967296)
}
function Hi32 {
    param([uint64]$Value)
    return [uint32]($Value -shr 32)
}

<#
    WPTR atual do anel, em dwords.

    Necessario porque 'sdma raw' emite onde o anel estiver, e o SDMA 5.0 exige
    que o pacote INDIRECT de 6 DW TERMINE em fronteira de 8 DW
    (sdma_v5_0_ring_emit_ib: insere (2 - wptr_dw) & 7 NOPs antes).

    Na primeira execucao deste ensaio eu ignorei isso: das tres variantes, so
    EXEC caiu alinhada por acaso (termina em DW 8). NOEXEC terminou em DW 9 e
    DIRTY em DW 20 -- as duas violando a regra. As variantes nao eram
    comparaveis entre si, e nenhum veredito daquela corrida vale.
#>
<#
    O 'sdma raw' do laboratorio NAO e fonte confiavel de progresso.

    dbg/hw.c:1672 faz:
        rptrAfter = (rptrWb == wptr) ? rptrWb : rptrReg;

    Ou seja: quando o writeback NAO alcanca o WPTR, ele troca a medicao pelo
    registrador MMIO -- justamente o que o comentario logo acima diz que apenas
    acompanha o WPTR. Entao "RPTR alcancou WPTR" impresso pelo 'sdma raw' pode
    ser o registrador espelhando o WPTR, e nao aposentadoria real.

    Aqui lemos os dois pelo 'sdma status' e reportamos SEPARADOS, sem fallback.
    O amdgpu le o writeback (sdma_v5_0_ring_get_rptr); e ele que vale.
#>
function Get-RingPointers {
    $out = & $cli sdma status 2>&1
    $r = @{ RptrWb = -1; RptrReg = -1; WptrShadow = -1 }
    foreach ($line in $out) {
        if ($line -match 'RPTR \(writeb\.\)\s*:\s*0x([0-9A-Fa-f]+)') { $r.RptrWb = [int]("0x" + $Matches[1]) }
        elseif ($line -match 'RPTR \(reg\)\s*:\s*0x([0-9A-Fa-f]+)')   { $r.RptrReg = [int]("0x" + $Matches[1]) }
        elseif ($line -match 'WPTR \(shadow\)\s*:\s*0x([0-9A-Fa-f]+)') { $r.WptrShadow = [int]("0x" + $Matches[1]) }
    }
    return $r
}

function Get-WptrDwords {
    $out = & $cli sdma status 2>&1
    foreach ($line in $out) {
        if ($line -match 'WPTR \(shadow\)\s*:\s*0x([0-9A-Fa-f]+)') {
            return ([int]("0x" + $Matches[1])) / 4
        }
    }
    throw "nao consegui ler o WPTR shadow: $out"
}

function VramWrite64 {
    param([int]$Offset, [uint64]$Value)
    VramWrite $Offset       (Lo32 $Value)
    VramWrite ($Offset + 4) (Hi32 $Value)
}

# ---------------------------------------------------------------------------

Write-Host ''
Write-Host '=== VMID 8 / INDIRECT — ensaio de laboratorio ==='
Write-Host ''

Write-Host '[0] Armando o driver para escrita...'
Ctl arm write | Out-Null

<#
    ORDEM DAS OPERACOES -- MEDIDO EM 05/09, NAO REORDENAR SEM EVIDENCIA.

    Sequencia que PASSOU (sdma verify isolado, 09:56):
        arm write -> sdma init -> verify        => WRITE_LINEAR, CONST_FILL e
                                                   COPY_LINEAR, todos com
                                                   leitura de volta conferida.

    Sequencia que FALHOU (este harness, 09:58, dois minutos depois):
        psp init -> psp loadfw -> sdma f32 -> sdma init -> PARSETEST -> controle

    Duas coisas que o harness fazia e o caminho bom nao fazia:

    1. RECARREGAR O FIRMWARE. O UCODE_CHECKSUM mudou entre cargas
       (0x56C60B8C -> 0x55B182E3), e o caminho que funcionou usou o firmware
       que o POST ja tinha deixado. Nao recarregamos mais: se o F32 estiver
       sadio, nao ha o que consertar.

    2. RODAR O PARSETEST ANTES. Ele emite um NOP anunciando 8 dwords extras e
       manda so o cabecalho. Lendo o codigo do CLI: os 8 dwords que faltam so
       sao completados no ramo em que o RPTR PARA. No nosso caso o RPTR
       alcancou o WPTR, entao ele retorna deixando um pacote incompleto
       pendente no anel. Passou para o fim, e so roda se o controle passar.

    O CONTROLE POSITIVO agora e a PRIMEIRA coisa depois do init.
#>
Write-Host ''
Write-Host '[0] Saude do microcontrolador F32 (leitura pura).'
Write-Host '    Criterio medido neste projeto: firmware sadio cobre 0x0140..0x03E0;'
Write-Host '    firmware travado oscila entre 0x000E e 0x0010.'
Ctl sdma f32 | Out-Null

Write-Host ''
Write-Host '[0] Inicializando o anel da SDMA0...'
Ctl sdma init | Out-Null
$cntl = RegRead $REG_SDMA_CNTL
Write-Host ("    SDMA0_CNTL apos init: 0x{0:X8} (MIDCMD={1})" -f $cntl, (($cntl -band $F_CNTL_MIDCMD) -ne 0))
if (($cntl -band $F_CNTL_MIDCMD) -ne 0) {
    Write-Host "    AVISO: MIDCMD_PREEMPT_ENABLE (bit 5) estava ligado. Desarmando..."
    RegWrite $REG_SDMA_CNTL ($cntl -band (-bnot $F_CNTL_MIDCMD)) | Out-Null
    $cntl = RegRead $REG_SDMA_CNTL
    Write-Host ("    SDMA0_CNTL agora: 0x{0:X8}" -f $cntl)
}

<#
    CONTROLE POSITIVO -- a primeira submissao da sessao, antes de qualquer
    coisa que possa deixar o anel inconsistente.
#>
Write-Host ''
Write-Host '[0] CONTROLE POSITIVO: WRITE_LINEAR num scratch de VRAM.'
Write-Host '    Se isto falhar, o anel nao executa nada e o resto do ensaio'
Write-Host '    nao mede VMID 8 -- mede o anel quebrado.'
$ctrl = Ctl sdma write 0x0F040000 0x11111111 0x22222222 0x33333333 0x44444444
$ctrlTexto = ($ctrl -join ' ')
$controleOk = ($ctrlTexto -notmatch 'DIVERGIU') -and ($ctrlTexto -notmatch 'TIMEOUT')
if ($controleOk) {
    Write-Host '    ** CONTROLE OK: o anel executa e a fence e gravada pela GPU.'
} else {
    Write-Host '    ** CONTROLE FALHOU. Tudo abaixo mede o anel, nao o VMID 8.'
}

<#
    Estado da GCVM antes de mexer. Importa para a variante PHYS: ela so mede
    "o INDIRECT executa sem traducao" se o VMID 0 estiver mesmo em bypass, que
    e o que o POST deixa segundo tools/test-gcvm-translate.ps1. Se CONTEXT0
    estiver habilitado com uma page table de verdade, PHYS tambem passa por
    traducao e o discriminador perde o sentido.
#>
Write-Host ''
Write-Host '[1] Estado da GCVM (leitura pura) -- o VMID 0 esta em bypass?'
# Arquivado, nao descartado: sem este estado a variante PHYS nao e
# autocontida -- ela so mede "sem traducao" se CONTEXT0 estiver em bypass.
Ctl gcvm dump | Out-Null

Write-Host ''
Write-Host '[1] Lendo a base fisica da VRAM (GCMC_VM_FB_OFFSET)...'
$fbOffsetReg = RegRead $REG_FB_OFFSET
$vramBase = [uint64]$fbOffsetReg -shl 24
Write-Host ("    FB_OFFSET = 0x{0:X8}  ->  vram_base_offset = 0x{1:X}" -f $fbOffsetReg, $vramBase)

$paRoot = $vramBase + $VRAM_ROOT
$paLeaf = $vramBase + $VRAM_LEAF
$paIb   = $vramBase + $VRAM_IB

$rootIndex = ($VA_IB -shr 21) -band 0x1FFF
$leafIb    = ($VA_IB -shr 12) -band 0x1FF
$leafDest  = ($VA_DEST -shr 12) -band 0x1FF
Write-Host ("    root[{0}]  folha_ib[{1}]  folha_dest[{2}]" -f $rootIndex, $leafIb, $leafDest)

# ---------------------------------------------------------------------------
# O IB: um CONST_FILL de 4 KiB no VA de destino, com NOPs de cauda ate 8 DW.
# Cabecalho 0x0000020B copiado byte a byte do que o encoder de producao emite.
# ---------------------------------------------------------------------------
<#
    Cabecalho do CONST_FILL.

    O caminho do laboratorio que PASSOU com prova byte a byte monta assim
    (dbg/hw.c, BC250DBG_SDMA_OP_FILL):

        SDMA_PKT_HEADER(op, subop) = (op & 0xFF) | ((subop & 0xFF) << 8)
        pkt = SDMA_PKT_HEADER(CONST_FILL, 0) | ((FillSize & 3) << 30)

    Ou seja: sub_op = 0 e FILLSIZE nos bits 31:30. Fill de dword = 0x8000000B.

    O encoder de producao emite 0x0000020B, que coloca o 2 em sub_op e deixa
    FILLSIZE = 0 -- e com FILLSIZE 0 a SDMA replica apenas o BYTE BAIXO do
    padrao, conforme o comentario medido em dbg/hw.c.
#>
<#
    Monta o IB: um unico CONST_FILL de 4 KiB no endereco de destino dado.

    O endereco muda conforme a variante:
      - sob VMID 8 e uma GPUVA, que a page table tem de resolver;
      - sob VMID 0 e o endereco FISICO MC (BC250_VRAM_GPU_BASE + offset), o
        mesmo dominio que os blts provados por byte proof usam.

    Cabecalho: sub_op = 0 e FILLSIZE nos bits 31:30 (0x8000000B para dword),
    como dbg/hw.c monta no caminho que passou com prova byte a byte. O encoder
    de producao emite 0x0000020B, que poe o 2 em sub_op e deixa FILLSIZE = 0.
#>
$IB_DWORDS = 8

function Build-Ib {
    param(
        [uint64]$DestAddress,
        [uint32]$DestVramOffset,
        [uint32[]]$Pattern,
        [string]$Label
    )

    if ($Pattern.Count -eq 4) {
        # SDMA WRITE_LINEAR de 4 dwords (count - 1 = 3) -> exatamente 8 DW
        $ib = @(
            [uint32]0x00000002L,
            (Lo32 $DestAddress),
            (Hi32 $DestAddress),
            0x00000003,
            $Pattern[0],
            $Pattern[1],
            $Pattern[2],
            $Pattern[3]
        )
    } else {
        # SDMA WRITE_LINEAR de 1 dword + 3 NOPs -> 8 DW
        $ib = @(
            [uint32]0x00000002L,
            (Lo32 $DestAddress),
            (Hi32 $DestAddress),
            0x00000000,
            $Pattern[0],
            0x00000000,
            0x00000000,
            0x00000000
        )
    }

    for ($i = 0; $i -lt $ib.Count; $i++) {
        VramWrite ($VRAM_IB + $i * 4) $ib[$i]
    }
    for ($i = 0; $i -lt 4; $i++) {
        VramWrite ($DestVramOffset + $i * 4) $POISON
    }
    Ctl hdp | Out-Null

    # PRE-READBACK OBRIGATÓRIO (Anti-Falso-Positivo):
    # 1. Conferir se o buffer do IB gravado pela CPU via BAR0 assentou na VRAM física
    for ($i = 0; $i -lt $ib.Count; $i++) {
        $lido = VramRead ($VRAM_IB + $i * 4)
        if ($lido -ne $ib[$i]) {
            throw "Abortando: IB divergiu na VRAM fisica apos escrita da CPU!"
        }
    }
    # 2. Conferir se o DESTINO esta de fato envenenado antes do kick
    for ($i = 0; $i -lt 4; $i++) {
        $lido = VramRead ($DestVramOffset + $i * 4)
        if ($lido -ne $POISON) {
            throw ("Abortando: Destino 0x{0:X} nao guardou poison 0x{1:X8}! Lido=0x{2:X8}" -f `
                ($DestVramOffset + $i * 4), $POISON, $lido)
        }
    }

    Write-Host ("    IB em VRAM 0x{0:X} -> destino 0x{1:X} ({2}): [READBACK CONFERIDO]" -f $VRAM_IB, $DestAddress, $Label)
    Write-Host ("      " + (($ib | ForEach-Object { '{0:X8}' -f $_ }) -join ' '))
    Write-Host ("    Destino VRAM 0x{0:X} pre-envenenado: [READBACK CONFERIDO POISON=0x{1:X8}]" -f $DestVramOffset, $POISON)
}

function Build-PageTables {
    param(
        [uint32]$PdeFlags,
        [uint32]$IbPteFlags,
        [uint32]$DestVramOffset,
        [string]$Label
    )

    Write-Host ("[2] Montando page tables ({0}): PDE flags = 0x{1:X2}" -f $Label, $PdeFlags)

    # Root: zera so a entrada que usamos; o resto fica invalido por construcao
    for ($i = 0; $i -lt 8; $i++) {
        VramWrite64 ($VRAM_ROOT + $i * 8) 0
    }
    VramWrite64 ($VRAM_ROOT + $rootIndex * 8) ([uint64]$paLeaf -bor $PdeFlags)

    $mtype = [uint64]0
    if ($UseMtype) {
        $mtype = [uint64]0x0003000000000000L  # MTYPE_UC (bits 50:48 = 3)
        Write-Host "    [MTYPE] Aplicando MTYPE_UC (0x3 nos bits 50:48) nas PTEs da folha"
    }

    $paDestVariant = $vramBase + $DestVramOffset

    # Folha: PTE do IB (leitura + execucao) e PTE do destino (leitura+escrita).
    for ($i = 0; $i -lt 8; $i++) {
        VramWrite64 ($VRAM_LEAF + $i * 8) 0
    }
    VramWrite64 ($VRAM_LEAF + $leafIb * 8) ([uint64]$paIb -bor $IbPteFlags -bor $mtype)
    VramWrite64 ($VRAM_LEAF + $leafDest * 8) `
        ([uint64]$paDestVariant -bor $PTE_VALID -bor $PTE_READ -bor $PTE_WRITE -bor $mtype)

    Write-Host ("    PDE  em VRAM 0x{0:X} = 0x{1:X16}" -f ($VRAM_ROOT + $rootIndex * 8), ([uint64]$paLeaf -bor $PdeFlags))
    Write-Host ("    PTE  IB   em VRAM 0x{0:X} -> PA 0x{1:X}  flags 0x{2:X2} (raw 0x{3:X16})" `
        -f ($VRAM_LEAF + $leafIb * 8), $paIb, $IbPteFlags, ([uint64]$paIb -bor $IbPteFlags -bor $mtype))
    Write-Host ("    PTE  DEST em VRAM 0x{0:X} -> PA 0x{1:X} (raw 0x{2:X16})" `
        -f ($VRAM_LEAF + $leafDest * 8), $paDestVariant, ([uint64]$paDestVariant -bor $PTE_VALID -bor $PTE_READ -bor $PTE_WRITE -bor $mtype))
}

function Program-Vmid8 {
    Write-Host '[3] Programando o VMID 8 e invalidando a TLB...'
    # O ponteiro de root carrega o bit VALID, como amdgpu_gmc_pd_addr() faz
    # para Vega10+.
    $base = [uint64]$paRoot -bor $PTE_VALID
    RegWrite $REG_CTX8_BASE_LO  (Lo32 $base) | Out-Null
    RegWrite $REG_CTX8_BASE_HI  (Hi32 $base) | Out-Null
    RegWrite $REG_CTX8_START_LO 0 | Out-Null
    RegWrite $REG_CTX8_START_HI 0 | Out-Null
    RegWrite $REG_CTX8_END_LO   $CTX8_END_LO | Out-Null
    RegWrite $REG_CTX8_END_HI   0 | Out-Null
    RegWrite $REG_CTX8_CNTL     $CTX8_CNTL_VALUE | Out-Null
    Write-Host ("    PT_BASE = 0x{0:X}   CNTL = 0x{1:X8}" -f $base, $CTX8_CNTL_VALUE)

    <#
        READBACK. A producao faz; o laboratorio nao fazia. Sem reler, "o VMID 8
        esta programado" era hipotese: a caminhada da CPU le VRAM, nao a TLB
        nem os registradores de contexto.
    #>
    $rbBaseLo = RegRead $REG_CTX8_BASE_LO
    $rbBaseHi = RegRead $REG_CTX8_BASE_HI
    $rbCntl   = RegRead $REG_CTX8_CNTL
    $rbEndLo  = RegRead $REG_CTX8_END_LO
    $okBase = ($rbBaseLo -eq (Lo32 $base)) -and ($rbBaseHi -eq (Hi32 $base))
    $okCntl = ($rbCntl -eq $CTX8_CNTL_VALUE)
    Write-Host ("    readback: BASE_LO=0x{0:X8} BASE_HI=0x{1:X8} CNTL=0x{2:X8} END_LO=0x{3:X8} -> {4}" `
        -f $rbBaseLo, $rbBaseHi, $rbCntl, $rbEndLo,
        $(if ($okBase -and $okCntl) { 'CONFERE' } else { '*** DIVERGIU ***' }))

    <#
        INVALIDAR A TLB DO VMID 8, NAO DO VMID 0.

        Erro medido na corrida de 05/09 10:49 e apontado por tres revisoes
        externas independentes: 'gcvm invalidate' sem argumentos usa o default
        vmid=0 (studio/cli/bc250ctl.c:3549). O log mostrava
        'vmid / flush type : 0 / 0' e REQ 0x00F80001 -- mascara do VMID 0.

        A TLB do VMID 8 NUNCA foi invalidada em nenhuma variante. As tres
        compartilhavam TLB velha, e falharem igual era o esperado mesmo com
        PDE e PTE perfeitos. A conclusao "nem execucao nem PDE eram a parede"
        NAO se sustentava.
    #>
    Ctl gcvm invalidate 8 0 | Out-Null
}

function Submit-Indirect {
    param([string]$Label, [uint64]$IbAddress, [uint32]$Vmid, [uint32]$MarkValue)

    Write-Host ("[4] Emitindo INDIRECT sob VMID {0} para 0x{1:X} ({2})..." -f $Vmid, $IbAddress, $Label)

    # Pacote INDIRECT do SDMA 5.0, identico ao que Bc250SdmaEmitIndirect monta:
    #   dw0 header = OP_INDIRECT(4) | vmid << 16
    #   dw1 base_lo mascarado com 0xFFFFFFE0 (alinhamento de 32 bytes)
    #   dw2 base_hi
    #   dw3 ib_size em dwords
    #   dw4/dw5 CSA (0 = sem context save area, o caso normal sem MCBP)
    # Alinhamento: o INDIRECT precisa TERMINAR em fronteira de 8 DW.
    $wptrDw = Get-WptrDwords
    $pad = (2 - $wptrDw) % 8
    if ($pad -lt 0) { $pad += 8 }
    $fim = $wptrDw + $pad + 6
    Write-Host ("    alinhamento: WPTR=DW{0}  pad={1} NOPs  INDIRECT termina em DW{2} (mod 8 = {3})" `
        -f $wptrDw, $pad, $fim, ($fim % 8))

    $hdr = 0x00000004 -bor ([int]$Vmid -shl 16)
    $pkt = @()
    for ($p = 0; $p -lt $pad; $p++) { $pkt += '0x00000000' }
    $pkt += @(
        ("0x{0:X8}" -f $hdr),
        ("0x{0:X8}" -f ((Lo32 $IbAddress) -band 0xFFFFFFE0)),
        ("0x{0:X8}" -f (Hi32 $IbAddress)),
        ("0x{0:X8}" -f $IB_DWORDS),
        '0x00000000',
        '0x00000000'
    )

    <#
        MARCADOR NO ANEL, DEPOIS DO INDIRECT.

        WRITE_LINEAR de 1 dword num endereco fisico, emitido no MESMO lote,
        logo apos o INDIRECT. Formato copiado de dbg/hw.c
        (BC250DBG_SDMA_OP_WRITE): header op=2/subop=0, dst_lo, dst_hi,
        count-1, dado.

        Ele separa duas coisas que ate agora estavam juntas:
          gravou   -> a engine passou pelo INDIRECT e continuou o anel; o que
                      falhou foi so o conteudo do IB.
          nao grav -> a engine PARA no INDIRECT. E exatamente o sintoma das
                      sete corridas de producao, onde a fence emitida depois
                      do INDIRECT nunca rodou.
    #>
    $mcMark  = $MC_BASE + $VRAM_MARK
    $mcFence = $MC_BASE + $VRAM_FENCE
    VramWrite $VRAM_MARK  $POISON
    VramWrite $VRAM_FENCE $POISON
    $pkt += @(
        '0x00000002',
        ("0x{0:X8}" -f (Lo32 $mcMark)),
        ("0x{0:X8}" -f (Hi32 $mcMark)),
        '0x00000000',
        ("0x{0:X8}" -f $MarkValue)
    )
    Write-Host ("    marcador: WRITE_LINEAR 0x{0:X8} em MC 0x{1:X} (poison antes)" -f $MarkValue, $mcMark)
    $script:marcadorEsperado = $MarkValue

    <#
        PACOTE GCR (rinse de cache L2) + PACOTE FENCE no mesmo submit.
        Sem GCR a escrita fica suja na L2 do GC e nao assenta na VRAM
        pela abertura do BAR0 (medido em dbg/hw.c:2443).
        A fence confirma conclusao de toda a fila (INDIRECT + MARCADOR + GCR).
    #>
    $fenceVal = [uint32](0xBC250000L -bor $MarkValue)
    $script:fenceEsperado = $fenceVal
    $pkt += @(
        # GCR rinse (5 DW): OP 17 (0x11), base=0, cntl=0xC3A10000, hi=0, vmid=0
        '0x00000011',
        '0x00000000',
        '0xC3A10000',
        '0x00000000',
        '0x00000000',
        # FENCE (4 DW): OP 5 (0x05) | (MTYPE_UC=3 << 16), dst_lo, dst_hi, fenceVal
        '0x00030005',
        ("0x{0:X8}" -f (Lo32 $mcFence)),
        ("0x{0:X8}" -f (Hi32 $mcFence)),
        ("0x{0:X8}" -f $fenceVal)
    )
    Write-Host ("    fence:    FENCE 0x{0:X8} em MC 0x{1:X} (pos-GCR, MTYPE_UC)" -f $fenceVal, $mcFence)

    <#
        COERENCIA HDP ANTES DE SUBMETER -- passo que faltava desde o inicio.

        BcVramWriteRaw (dbg/hw.c:349) NAO faz flush: ele so escreve no BAR0.
        Quem invalida e a LEITURA. Entao tudo que este script monta por
        'vram w' -- page tables, IB, poison -- podia estar parado no buffer de
        escrita do HDP quando a GPU foi ler.

        Medido em 05/09 11:51, com o bloco GFX_IB_* ja instrumentado:
            IB_BASE  = exatamente o endereco emitido
            IB_RPTR  = 0x20 = 8 dwords
            IB_SIZE  = 0
        A engine decodificou o INDIRECT, saltou e LEU O IB INTEIRO -- mas o
        CONST_FILL nao teve efeito. Ler lixo residual da VRAM e executa-lo como
        pacote explica tanto o "nao escreveu" quanto a engine parar em seguida.
    #>
    Ctl hdp | Out-Null

    Ctl sdma raw @pkt

    <#
        ESPERAR A FENCE, NAO UM PRAZO FIXO.
        A fence garante que o lote terminou e que a L2 foi drenada pelo GCR.
    #>
    Ctl hdp | Out-Null
    $limite = [Diagnostics.Stopwatch]::StartNew()
    while ($limite.ElapsedMilliseconds -lt 2000) {
        if ((VramRead $VRAM_FENCE) -eq $fenceVal) { break }
        Start-Sleep -Milliseconds 20
    }
    $fenceLida = VramRead $VRAM_FENCE
    Write-Host ("    espera da fence: {0} ms (FENCE=0x{1:X8} -> {2})" -f `
        $limite.ElapsedMilliseconds, $fenceLida,
        $(if ($fenceLida -eq $fenceVal) { 'ALCANCADA' } else { 'TIMEOUT' }))
}

function Check-Result {
    param(
        [string]$Label,
        [uint32]$DestVramOffset,
        [uint32[]]$ExpectedPattern
    )

    $status1 = RegRead $REG_SDMA_STATUS1
    $status2 = RegRead $REG_SDMA_STATUS2

    <#
        SDMA0_STATUS2_REG NAO e um registrador de opcode nesta placa.

        O laboratorio ja tinha isto provado e eu nao olhei antes de decodificar:
        'bc250ctl sdma f32' extrai o PROGRAM COUNTER do F32 como
        (STATUS2 >> 2) & 0x3FFF, com criterio de saude medido -- firmware sadio
        cobre 0x0140..0x03E0, firmware travado oscila entre 0x000E e 0x0010.

        Ou seja: nem a mascara do nosso catalogo (CMD_OP, shift 4) nem a
        proposta pela revisao externa (shift 16) valem aqui. Os "opcodes"
        0x41/0x38/0x6F/0xF8 que este script imprimiu nas corridas anteriores
        eram o PC do microcontrolador andando. Descarte-os.
    #>
    $pc = ($status2 -shr 2) -band 0x3FFF

    Write-Host ("[5] Resultado ({0}):" -f $Label)
    Write-Host ("    STATUS1 = 0x{0:X8}  ({1})" -f $status1,
        $(if (($status1 -band 0x3FF) -eq 0x3FF) { 'engine OCIOSA' } else { 'engine OCUPADA' }))
    Write-Host ("    STATUS2 = 0x{0:X8}  ->  F32 PC = 0x{1:X4}  ({2})" -f $status2, $pc,
        $(if ($pc -ge 0x0140 -and $pc -le 0x03E0) { 'faixa de firmware sadio' }
          elseif ($pc -le 0x0010) { 'FAIXA DE FIRMWARE TRAVADO' }
          else { 'fora das faixas conhecidas' }))

    <#
        Estado interno do IB. Estes registradores sao carregados pelo F32 ao
        consumir o INDIRECT -- o software nunca os escreve. Sao a unica forma
        de saber se a engine chegou a carregar o descritor do IB ou se parou
        antes de decodificar o pacote.
    #>
    $ibCntl = RegRead $REG_IB_CNTL
    $ibBaseLo = RegRead $REG_IB_BASE_LO
    $ibBaseHi = RegRead $REG_IB_BASE_HI
    $ibSize = RegRead $REG_IB_SIZE
    $ibRptr = RegRead $REG_IB_RPTR
    $ibOffset = RegRead $REG_IB_OFFSET
    $ctxStatus = RegRead $REG_CONTEXT_STATUS
    $skipCntl = RegRead $REG_SKIP_CNTL
    $ibSubRemain = RegRead $REG_IB_SUB_REMAIN
    $subRemainDw = $ibSubRemain -band 0x3FFF
    Write-Host ("    IB_CNTL=0x{0:X8} (IB_ENABLE={1})  IB_BASE=0x{2:X8}{3:X8}  IB_SIZE=0x{4:X}  IB_RPTR=0x{5:X}  IB_OFFSET=0x{6:X}" `
        -f $ibCntl, ($ibCntl -band 1), $ibBaseHi, $ibBaseLo, $ibSize, $ibRptr, $ibOffset)
    Write-Host ("    CONTEXT_STATUS=0x{0:X8}  SKIP_CNTL=0x{1:X8}  IB_SUB_REMAIN=0x{2:X8} ({3} dw restantes) -> {4}" `
        -f $ctxStatus, $skipCntl, $ibSubRemain, $subRemainDw,
        $(if ($ibBaseLo -ne 0 -or $ibBaseHi -ne 0) { 'a engine CARREGOU o descritor do IB' }
          else { 'IB_BASE zerado: a engine NAO carregou o descritor' }))

    # Observabilidade de FAULT da GCVM (GFXHUB)
    $faultStatus = RegRead $REG_FAULT_STATUS
    $faultAddrLo = RegRead $REG_FAULT_ADDR_LO
    $faultAddrHi = RegRead $REG_FAULT_ADDR_HI
    $faultAddr = ([uint64]$faultAddrHi -shl 32) -bor [uint64]$faultAddrLo
    Write-Host ("    FAULT_STATUS  = 0x{0:X8}  FAULT_ADDR = 0x{1:X10} (page VA 0x{2:X})" -f `
        $faultStatus, $faultAddr, ($faultAddr -shl 12))
    if ($faultStatus -ne 0) {
        $fVmid = ($faultStatus -shr 20) -band 0xF
        $fRw = ($faultStatus -shr 18) -band 1
        $fWalker = ($faultStatus -shr 1) -band 7
        $fPerm = ($faultStatus -shr 4) -band 0xF
        $fMap = ($faultStatus -shr 8) -band 1
        $fCid = ($faultStatus -shr 9) -band 0x1FF
        Write-Host ("      -> FAULT DETECTADO: VMID={0} RW={1} ({2}) WALKER_ERROR={3} PERM_FAULT=0x{4:X} MAP_ERR={5} CID=0x{6:X}" -f `
            $fVmid, $fRw, $(if ($fRw -eq 1) { 'WRITE' } else { 'READ' }), $fWalker, $fPerm, $fMap, $fCid)
    }

    $ptr = Get-RingPointers
    Write-Host ("    ponteiros: RPTR_writeback=0x{0:X}  RPTR_reg=0x{1:X}  WPTR_shadow=0x{2:X}  ->  {3}" `
        -f $ptr.RptrWb, $ptr.RptrReg, $ptr.WptrShadow,
        $(if ($ptr.RptrWb -eq $ptr.WptrShadow) { 'writeback alcancou o WPTR' }
          else { ('writeback {0} dwords atras -- NAO e sinal de falha por si so' -f `
                  (($ptr.WptrShadow - $ptr.RptrWb) / 4)) }))

    $mark = VramRead $VRAM_MARK
    $script:ultimoMarcador = ($mark -eq $script:marcadorEsperado)
    $cruzado = (-not $script:ultimoMarcador) -and ($mark -ne $POISON)
    Write-Host ("    marcador pos-INDIRECT = 0x{0:X8} (esperado 0x{1:X8})  ->  {2}" -f `
        $mark, $script:marcadorEsperado,
        $(if ($script:ultimoMarcador) { 'GRAVOU: a engine passou pelo INDIRECT e continuou o anel' }
          elseif ($cruzado) { '*** VALOR DE OUTRA VARIANTE: escrita atrasada, leitura invalida ***' }
          else { 'NAO GRAVOU: a engine PAROU no INDIRECT' }))

    $fenceLida = VramRead $VRAM_FENCE
    Write-Host ("    fence pos-GCR         = 0x{0:X8} (esperado 0x{1:X8})  ->  {2}" -f `
        $fenceLida, $script:fenceEsperado,
        $(if ($fenceLida -eq $script:fenceEsperado) { 'GRAVADA PELA GPU: lote e GCR concluidos' }
          else { 'NAO GRAVADA: a GPU nao alcancou o fim do lote' }))

    Ctl hdp | Out-Null
    $dest = @(0..3 | ForEach-Object { VramRead ($DestVramOffset + $_ * 4) })
    Write-Host ("    destino  = {0}" -f (($dest | ForEach-Object { '0x{0:X8}' -f $_ }) -join ' '))

    $ok = $true
    for ($i = 0; $i -lt $ExpectedPattern.Count; $i++) {
        if ($dest[$i] -ne $ExpectedPattern[$i]) {
            $ok = $false
            break
        }
    }
    if ($ok) {
        Write-Host ("    ** BYTE PROOF ({0}): todos os {1} dwords gravados no destino 0x{2:X} com sucesso! **" -f `
            $Label, $ExpectedPattern.Count, $DestVramOffset)
    } else {
        Write-Host ("    byte proof FALHOU: o poison continua ou dados divergiram, o IB {0} nao escreveu no destino 0x{1:X}." -f `
            $Label, $DestVramOffset)
    }

    # CHECAGEM DO DECOY (0x00202000):
    if ($null -ne $script:decoyInitial) {
        $decoyNow = @(0..3 | ForEach-Object { VramRead ($DECOY_VRAM + $_ * 4) })
        $decoyIntacto = ($decoyNow[0] -eq $script:decoyInitial[0] -and `
                         $decoyNow[1] -eq $script:decoyInitial[1] -and `
                         $decoyNow[2] -eq $script:decoyInitial[2] -and `
                         $decoyNow[3] -eq $script:decoyInitial[3])
        Write-Host ("    decoy VRAM 0x{0:X} = {1} -> {2}" -f `
            $DECOY_VRAM, `
            (($decoyNow | ForEach-Object { '0x{0:X8}' -f $_ }) -join ' '), `
            $(if ($decoyIntacto) { 'INTACTO (a GPU NAO escreveu cru)' } else { '*** MODIFICADO: ESCRITA CRUA DETECTADA! ***' }))
    }

    return $ok
}

function Disable-Vmid8 {
    Write-Host '[6] Desligando o VMID 8...'
    RegWrite $REG_CTX8_CNTL 0 | Out-Null
    Ctl gcvm invalidate 8 0 | Out-Null
}

# ---------------------------------------------------------------------------
# A matriz. Cada variante muda UMA coisa em relacao a EXEC, e todas rodam no
# mesmo boot -- que e o unico motivo de este ensaio existir no laboratorio.
# ---------------------------------------------------------------------------
$PTE_RWX = $PTE_VALID -bor $PTE_READ -bor $PTE_WRITE -bor $PTE_EXEC
$PTE_RW  = $PTE_VALID -bor $PTE_READ -bor $PTE_WRITE

$patternPhys   = @([uint32]0xDEADBEE1L, [uint32]0xDEADBEE2L, [uint32]0xDEADBEE3L, [uint32]0xDEADBEE4L)
$patternExec   = @([uint32]0xBC25E001L, [uint32]0xBC25E002L, [uint32]0xBC25E003L, [uint32]0xBC25E004L)
$patternNoExec = @([uint32]0xBC25A001L, [uint32]0xBC25A002L, [uint32]0xBC25A003L, [uint32]0xBC25A004L)
$patternDirty  = @([uint32]0xBC25D001L, [uint32]0xBC25D002L, [uint32]0xBC25D003L, [uint32]0xBC25D004L)

$variantes = @(
    @{ Nome = 'EXEC';   Pde = $PTE_VALID; IbPte = $PTE_RWX; Dest = $VRAM_DEST_EXEC; Pattern = $patternExec;
       Nota = 'destino isolado 0x0F070000, 4 dwords unicos 0xBC25E001..4, PDE so VALID' }
    @{ Nome = 'NOEXEC'; Pde = $PTE_VALID; IbPte = $PTE_RW;  Dest = $VRAM_DEST_NOEXEC; Pattern = $patternNoExec;
       Nota = 'destino isolado 0x0F080000, 4 dwords unicos 0xBC25A001..4, PTE sem bit 4 (exe)' }
    @{ Nome = 'DIRTY';  Pde = ($PTE_VALID -bor $PTE_READ -bor $PTE_WRITE); IbPte = $PTE_RWX; Dest = $VRAM_DEST_DIRTY; Pattern = $patternDirty;
       Nota = 'destino isolado 0x0F090000, 4 dwords 0xBC25D001..4, PDE 0x61 corrompido (prova causal)' }
)

<#
    DISCRIMINADOR PHYS -- o INDIRECT funciona, em qualquer forma?

    Mesma engine, mesmo anel, mesmo IB, mesmo destino fisico. A UNICA
    diferenca para as variantes seguintes e que aqui nao ha traducao: VMID 0 e
    o IB e enderecado pelo MC fisico (BC250_VRAM_GPU_BASE + offset), o mesmo
    dominio dos blts provados por byte proof.

      PASSA  -> o pacote INDIRECT esta correto e a engine o executa. O que
                falha e especificamente a traducao sob VMID 8.
      FALHA  -> o INDIRECT nao executa nesta engine em forma nenhuma, e as
                sete corridas de producao perseguiram a pergunta errada.
#>
$mcIb   = $MC_BASE + $VRAM_IB
$mcDest = $MC_BASE + $VRAM_DEST_PHYS

Write-Host ''
Write-Host '--- VARIANTE PHYS: INDIRECT sob VMID 0, IB endereçado pelo MC fisico ---'
Write-Host ("    IB   em MC 0x{0:X}" -f $mcIb)
Write-Host ("    DEST em MC 0x{0:X} (VRAM 0x{1:X})" -f $mcDest, $VRAM_DEST_PHYS)

# Salva o estado inicial do Decoy (0x00202000) antes de qualquer submissao
$script:decoyInitial = @(0..3 | ForEach-Object { VramRead ($DECOY_VRAM + $_ * 4) })
Write-Host ("    Decoy VRAM 0x{0:X} inicial: {1}" -f $DECOY_VRAM, (($script:decoyInitial | ForEach-Object { '0x{0:X8}' -f $_ }) -join ' '))

Build-Ib -DestAddress $mcDest -DestVramOffset $VRAM_DEST_PHYS -Pattern $patternPhys -Label 'PHYS'
$physOk = $false
Submit-Indirect -Label 'PHYS' -IbAddress $mcIb -Vmid 0 -MarkValue ($MARK_BASE + 1) | Out-Null
$physOk = Check-Result -Label 'PHYS' -DestVramOffset $VRAM_DEST_PHYS -ExpectedPattern $patternPhys
$physMark = $script:ultimoMarcador

$resultados = @{}
$marcadores = @{}
$script:idxVariante = 0
$globalMmuConfigured = $false

try {
    if ($GlobalMmu) {
        Write-Host ''
        Write-Host '[2.5] Configurando estagios globais da MMU (GFXHUB)...'
        Write-Host '      Tirando snapshot transacional dos 44 registradores GCVM...'
        $snap = Ctl gcvm txn snapshot
        $snapText = ($snap -join ' ')
        if ($snapText -match 'FALHOU' -or $snapText -match 'RECUSADO') {
            throw "Snapshot GCVM falhou: $snapText"
        }
        $globalMmuConfigured = $true

        Write-Host '      Aplicando estagio system (abertura e fault default)...'
        Ctl gcvm program system -dummy 0x0F050000 | Out-Null
        Write-Host '      Aplicando estagio identity...'
        Ctl gcvm program identity | Out-Null
        Write-Host '      Aplicando estagio invrange (engines 0 e 17)...'
        Ctl gcvm program invrange | Out-Null
        Write-Host '      Aplicando estagio tlb (SYSTEM_ACCESS_MODE=3, ENABLE_ADVANCED_DRIVER_MODEL=1)...'
        Ctl gcvm program tlb -danger | Out-Null
        Write-Host '      Aplicando estagio cache (L2 ligada, VMC_TAP_PDE/PTE_REQUEST_PHYSICAL=0)...'
        Ctl gcvm program cache -danger | Out-Null
    }

    foreach ($v in $variantes) {
        Write-Host ''
        Write-Host ("--- VARIANTE {0}: {1} ---" -f $v.Nome, $v.Nota)
        Build-PageTables -PdeFlags $v.Pde -IbPteFlags $v.IbPte -DestVramOffset $v.Dest -Label $v.Nome
        Build-Ib -DestAddress ([uint64]$VA_DEST) -DestVramOffset $v.Dest -Pattern $v.Pattern -Label $v.Nome
        Program-Vmid8
        $script:idxVariante++
        Submit-Indirect -Label $v.Nome -IbAddress ([uint64]$VA_IB) -Vmid 8 `
            -MarkValue ($MARK_BASE + 1 + $script:idxVariante) | Out-Null
        $resultados[$v.Nome] = Check-Result -Label $v.Nome -DestVramOffset $v.Dest -ExpectedPattern $v.Pattern
        $marcadores[$v.Nome] = $script:ultimoMarcador
        Disable-Vmid8
    }
} finally {
    if ($globalMmuConfigured) {
        Write-Host ''
        Write-Host '[6.5] Restaurando estagios globais da MMU (GFXHUB) ao snapshot do POST...'
        Ctl gcvm txn restore | Out-Null
    }
}

Write-Host ''
Write-Host '=== VEREDITO ==='
Write-Host ''
Write-Host '  variante | byte proof | marcador pos-INDIRECT'
Write-Host '  ---------+------------+----------------------'
Write-Host ("  PHYS     | {0,-10} | {1}" -f `
    $(if ($physOk) { 'OK' } else { 'falhou' }),
    $(if ($physMark) { 'gravou (passou)' } else { 'nao gravou (parou)' }))
foreach ($v in $variantes) {
    Write-Host ("  {0,-8} | {1,-10} | {2}" -f $v.Nome,
        $(if ($resultados[$v.Nome]) { 'OK' } else { 'falhou' }),
        $(if ($marcadores[$v.Nome]) { 'gravou (passou)' } else { 'nao gravou (parou)' }))
}
Write-Host ''
Write-Host '  Marcador = WRITE_LINEAR emitido no anel logo apos o INDIRECT.'
Write-Host '  gravou -> a engine decodificou o INDIRECT e seguiu o anel; o que'
Write-Host '            falhou foi o CONTEUDO do IB.'
Write-Host '  parou  -> a engine nao passou do INDIRECT.'
Write-Host ''
Write-Host '  NAO use STATUS2 como CMD_OP: nesta placa ele e o PC do F32.'
Write-Host '  NAO use "RPTR alcancou WPTR" do sdma raw: dbg/hw.c:1672 cai para o'
Write-Host '  registrador MMIO quando o writeback nao alcanca. Vale o writeback.'
Write-Host ''
Write-Host '  Nada aqui prova o caminho do VidMm. Isso continua exigindo o driver'
Write-Host '  de producao. NAO VALIDADO EM HARDWARE: paginacao dirigida pelo VidMm.'
Write-Host ''

<#
    PARSETEST POR ULTIMO, e so se o anel estiver comprovadamente bom.

    Ele deixa um pacote incompleto pendente quando o RPTR alcanca o WPTR --
    por isso nao pode rodar antes de nada que dependa do anel.
#>
if ($controleOk) {
    Write-Host ''
    Write-Host '[7] PARSETEST (por ultimo: pode deixar o anel inconsistente).'
    Write-Host '    A engine decodifica pacotes, ou so arrasta o RPTR ate o WPTR?'
    Ctl sdma parsetest | Out-Null
} else {
    Write-Host ''
    Write-Host '[7] PARSETEST pulado: o controle falhou, o resultado nao valeria.'
}

Write-Host ''
Write-Host '[8] Devolvendo o anel...'
Ctl sdma rollback | Out-Null

Stop-Transcript | Out-Null
Write-Host "Evidencia arquivada em: $OutDir"
