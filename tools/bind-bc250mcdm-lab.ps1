[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptBlackScreenRisk,
    [switch]$EnableShaderDispatch,
    # Fase P1: exige que o pacote tenha subido com a fila persistente ligada.
    [switch]$EnablePersistentSubmission,
    # Permite vincular sobre uma engine que ainda carrega estado da sessao
    # anterior. A telemetria resultante NAO vale como evidencia.
    [switch]$IAcceptDirtyEngine,
    # Fase V5: grava RenderNode=1, fazendo o driver registrar por
    # DxgkInitialize (WDDM display+render) em vez de
    # DxgkInitializeDisplayOnlyDriver. Lido em DriverEntry, portanto so vale a
    # partir do proximo BOOT. Alto risco de tela preta.
    [switch]$EnableRenderNode,
    # Arma GpuvaLabPromotion=1 na politica pre-bind. Sem ele
    # GpuvaMappingsOperational fica 0 e SubmitCommandVirtual no node 1 devolve
    # STATUS_NOT_SUPPORTED (bc250_active_miniport.c:6273), ou seja, nenhum
    # trabalho Vulkan chega ao anel.
    #
    # O valor e lido uma unica vez, no StartDevice
    # (bc250_active_miniport.c:3443). Por isso ele precisa estar na chave ANTES
    # do install -- e o INF de laboratorio grava esta entrada com NOCLOBBER
    # justamente para nao apagar o que a politica pre-bind escreveu.
    [switch]$EnableGpuvaLabPromotion,
    [switch]$IAcceptGpuvaLabPromotion,
    # Dispara UMA corrida de RADV_WDDM_COMPUTE_TRIPOD logo apos o bind, ainda
    # sob o watchdog armado.
    #
    # Nao e conveniencia: com o display vinculado a tela apaga, e o operador
    # nao tem como lancar o runner numa segunda janela. Ou o bind o chama, ou o
    # teste nao acontece.
    [switch]$RunRadvComputeTripod,
    # Dispara UMA corrida de RADV_WDDM_PRESENT_TRIPOD (Marco 2) logo apos o bind
    [switch]$RunRadvPresentTripod,
    # Dispara UMA corrida de VULKAN_WSI_CUBE (Marco 4) logo apos o bind
    [switch]$RunVulkanCube,
    [int]$VulkanCubeFrames = 3600,
    # Dispara UMA corrida de DXVK_D3D11 (Marco 5) logo apos o bind
    [switch]$RunDxvkD3D11,
    [int]$DxvkFrames = 1800,
    # Executa UMA transacao B7 originada pela UMD logo apos o bind, sob o
    # mesmo watchdog. E' separado do RADV para nao misturar duas experiencias
    # fisicas no mesmo candidato.
    [switch]$RunUmdNode1Tripod,
    # Dispara UMA corrida de MARCO3_GRAPHICS_DRAW (vkCmdDraw + SMPTE Color Bars)
    [switch]$RunMarco3Graphics,
    [int]$Marco3Frames = 600,
    # Dispara UMA corrida de MARCO4_GRAPHICS_CUBE (vkCmdDraw + 3D Depth Cube)
    [switch]$RunMarco4Cube,
    [int]$Marco4Frames = 600,
    # Dispara UMA corrida de D3D11_NATIVE_CONFORMANCE (bc250umd.dll -> Vulkan Bridge)
    [switch]$RunD3D11Native,
    [int]$D3D11Frames = 600,
    # Corrida 143: atraso do harness D3D11 depois da convergencia do display. As
    # travas das 139/140 vieram com o harness sobre um Explorer ja grande (N~780);
    # na 142 o harness rodou com o Explorer recem-nascido e nada reproduziu.
    # 0 = comportamento historico (sem atraso).
    [ValidateRange(0,300)][int]$D3D11StartDelaySeconds = 0,
    # Permite habilitar Plano A (fill direto no anel) para isolamento e protecao contra 0x119.
    [switch]$EnableSdmaPlanoAFallback,
    # B.1 (14/09): FOTOGRAFA o anel grafico CP_RB0 durante o bring-up. SOMENTE
    # LEITURA -- o contrato nao escreve registrador algum e o transporte MMIO
    # confirma por contador proprio; se houver UMA escrita a medicao e
    # descartada. Nao altera o veredito do StartDevice. O anel grafico e' a
    # unica engine desta placa sem medicao, e e' pre-requisito da Parede B.
    [switch]$EnableGfxRingPreflight,
    # B.1 (14/09): ensaio ESCALONADO do anel grafico. 1 configura CP_RB0 e
    # confere por readback, com as micro-engines ainda HALTED. 2 acrescenta o
    # desalt e a partida. 3 acrescenta submissao de SH_REG (shaders VS+PS).
    # 4 acrescenta isolamento de estado PM4 3D (Shaders, ColorTarget, Viewport, PrimAssembler) e Fence (SkipDraw=1).
    # 5 acrescenta pipeline 3D completo com Draw Initiator (DRAW_INDEX_AUTO) e Fence.
    # O rollback e' sempre executado e conferido, entao o
    # StartDevice termina com a placa como a encontrou.
    [ValidateRange(0,5)][int]$GfxRingBringupLevel = 0,
    # Sub-etapas de isolamento progressivo PM4 3D para Level 4:
    # 1: Sub-etapa 4.1 -- Shaders (Bloco 1) + ColorTarget 0 (Bloco 2) + TargetMask (Bloco 3) + Fence (Bloco 7)
    # 2: Sub-etapa 4.2 -- + Viewport & Scissor (Bloco 4) + Fence (Bloco 7)
    # 3: Sub-etapa 4.3 -- + Primitive Assembler (Bloco 5) + Fence (Bloco 7) (SkipDraw=1)
    # 4: Sub-etapa 4.4 -- + Draw Initiator (Bloco 6: DRAW_INDEX_AUTO) + Fence (Bloco 7)
    [ValidateRange(0,4)][int]$GfxRingStage = 1,
    # C3.9 (12/09): troca o bloqueio GLOBAL do journal no gate do Node 1 pela
    # pendencia do PROCESSO QUE SUBMETE.
    #
    # POR QUE: na corrida 13:29, com D3D11LabPromotion armado, o dwm.exe viveu
    # 3 min 13 s -- primeira vez -- mas pagina sem parar, o journal nunca drenou,
    # o gate ficou em BlockMask=0x1 (JOURNAL) e o compute do RADV foi RECUSADO.
    # Compute e desktop nao coexistiam por GRANULARIDADE do gate.
    #
    # RELAXAMENTO NAO VALIDADO EM HARDWARE: o argumento a favor e' que as raizes
    # de page table sao por processo; o argumento nao e' prova. Default desligado.
    [switch]$EnableNode1JournalGatePerProcess,
    # Promove criacao do device D3D10/11 na UMD para composicao do DWM.
    [switch]$EnableD3D11LabPromotion,
    [switch]$EnableD3D11DrawBltSubstitution,
    [switch]$EnableD3D11ShaderTranslator,
    [string]$D3D11ShaderTranslatorDllPath,
    # Arma WER LocalDumps exclusivamente para dwm.exe durante esta corrida.
    # Default desarmado: a chave global so' muda por escolha explicita do dono.
    [switch]$EnableDwmCrashCapture,
    [switch]$EnablePhysicalRenderOnly,
    # Permite o fluxo solicitado pelo operador sem rollback temporizado.
    # A recuperacao manual C:\BC250_RECOVERY.cmd continua obrigatoria.
    [switch]$DisableWatchdog,
    [switch]$IAcceptNoAutomaticRollback,
    [switch]$IAcceptAutomaticRecoveryReboot,
    [ValidateRange(30,600)][int]$WatchdogTimeoutSeconds = 180,
    # Janela do ciclo fire-and-collect do perfil physical render-only.
    #
    # Nesse perfil NAO existe caminho de confirmacao humana: o ensaio troca o
    # BasicDisplay na unica funcao PCI com saida de video, entao a tela apaga
    # por construcao e ninguem consegue rodar o confirmador. Ate o 3.79 o
    # roteiro fingia que existia confirmacao, o operador nao podia dar, o
    # guardiao expirava e o ensaio terminava em rollback de emergencia.
    # Agora o proprio binder espera esta janela, tira o retrato, faz o
    # rollback e reinicia. O fim e' o mesmo; a diferenca e' que passa a ser
    # o contrato, e nao um acidente.
    [ValidateRange(30,240)][int]$PhysicalWindowSeconds = 60,
    [switch]$RestartExplorer,
    # Aceita ligar um candidato com perfil MAIS FRACO que o do ultimo bind.
    #
    # POR QUE EXISTE (20/09, preparando a Corrida 45): ao empacotar o candidato
    # seguinte a Corrida 44 eu omiti -EnableRenderNode/-EnableShaderDispatch/
    # -EnablePersistentSubmission e produzi um pacote DISPLAY_LAB em modo
    # READ-ONLY, quando o ensaio EXIGE DISPLAY_RENDER_LAB com submissao
    # persistente. O empacotador aceitou (o perfil e' legitimo), a auditoria do
    # pacote deu PASS (ela confere o pacote, nao a INTENCAO), e nada comparava
    # com o que havia rodado antes. Um bind assim nao produz submissao GFX
    # nenhuma: a corrida inteira seria gasta medindo o nada.
    #
    # O rebaixamento pode ser deliberado -- isolar variavel e' metodo valido --,
    # entao a saida nao e' proibir, e' exigir que seja DECLARADO.
    [switch]$IAcceptProfileDowngrade
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $root 'driver\wdk\package\output_lab'
$packageInf = Join-Path $packageDir 'bc250mcdm.inf'
$packageSys = Join-Path $packageDir 'bc250mcdm.sys'
$packagePdb = Join-Path $packageDir 'bc250mcdm.pdb'
$packageCat = Join-Path $packageDir 'bc250mcdm.cat'
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$radvComputeTripod = Join-Path $root 'tools\test-radv-wddm-compute-tripod.ps1'
$radvPresentTripod = Join-Path $root 'tools\test-radv-wddm-present-tripod.ps1'
$vulkanCube = Join-Path $root 'tools\test-vulkan-wsi-cube.ps1'
$dxvkRunner = Join-Path $root 'tools\test-dxvk-d3d11.ps1'
$umdNode1Tripod = Join-Path $root 'tools\test-b7-umd-node1-tripod.ps1'
$marco3Runner = Join-Path $root 'tools\test-marco3-graphics-draw.ps1'
$marco4Runner = Join-Path $root 'tools\test-marco4-graphics-cube.ps1'
$d3d11Runner = Join-Path $root 'tools\test-d3d11-native-conformance.ps1'
$armDwmCapture = Join-Path $root 'tools\arm-dwm-crash-capture.ps1'
$restoreDwmCapture = Join-Path $root 'tools\restore-dwm-crash-capture.ps1'
# Gate proprio do laboratorio: o de producao exige SkipHwBringup=1 e por isso
# rejeita este binding sempre, por construcao (doc 95 secao B3).
$safeConfirmScript = Join-Path $root 'tools\confirm-bc250mcdm-safe-bind.ps1'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$cleanBootMarkerPath = 'C:\BC250_REQUIRE_CLEAN_BOOT.json'
$bootFusePath = 'C:\BC250_BOOT_FUSE.dat'
$recoveryPath = 'C:\BC250_RECOVERY.cmd'
$displayLogPath = 'C:\BC250_DISPLAY_LOG.txt'
$pagingTracePath = 'C:\BC250_PAGING_TRACE.bin'
$hardwareId = 'PCI\VEN_1002&DEV_13FE'

# --------------------------------------------------------------------------
# A4 — desarmar e arquivar o guardiao SYSTEM.
#
# Ate o 3.79 o unico desarme neste script era Stop-Process sobre o watchdog
# filho. C3.16 promoveu TODAS as corridas para a tarefa SYSTEM persistente.
# Um bind que falha nao pode deixar essa tarefa AtStartup armada sobre um
# estado ja' quebrado.
#
# Sempre desregistra ANTES de remover o diretorio: na ordem inversa a tarefa
# fica apontando para um script inexistente.
# --------------------------------------------------------------------------
function Copy-Bc250SharedLog([string]$Source, [string]$Destination) {
    # I.1b (30/09, Corrida 187): o Copy-Item abre a origem compartilhando so'
    # leitura; um log aberto para escrita pelo dwm.exe (o -RADV da I.1) derrubou
    # o binder com erro terminante e a corrida virou rollback sem ensaio. Aqui a
    # origem e' aberta com ReadWrite|Delete e qualquer falha so' AVISA: um log
    # preso nunca aborta uma corrida.
    try {
        $in = [System.IO.File]::Open($Source, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
        try {
            $out = [System.IO.File]::Open($Destination, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write,
                [System.IO.FileShare]::Read)
            # Corrida 248 (doc 651): a copia ficou no cache e a trava zerou os seis
            # snapshots (tamanho certo, so' zeros). Flush(true) forca os dados ao disco.
            try { $in.CopyTo($out); $out.Flush($true) } finally { $out.Dispose() }
        } finally { $in.Dispose() }
        return $true
    } catch {
        Write-Host ("  AVISO: nao copiei {0}: {1}" -f $Source, $_.Exception.Message) -ForegroundColor Yellow
        return $false
    }
}

function Get-Bc250BindUmdLogs([string[]]$Sources, [datetime]$CutoffUtc) {
    # Corrida 189 (30/09): desde 29/09 18:10 cada bind copiava E concatenava por
    # Get-Content TODOS os BC250_UMD_LOG_* das pastas Temp -- 900 arquivos antigos
    # das suites offline e de corridas passadas, 1,1 a 1,7 GB, durante os primeiros
    # ~5 min da janela fisica (a 189 travou com a copia em andamento). A coleta
    # agora so' leva o que foi gravado depois do inicio deste bind.
    $found = @()
    foreach ($src in $Sources) {
        $found += @(Get-ChildItem -LiteralPath $src -Filter 'BC250_UMD_LOG_*.txt' -File -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTimeUtc -ge $CutoffUtc })
    }
    return @($found | Sort-Object Name -Unique)
}

function Add-Bc250LogToAggregate([string]$Source, [string]$Aggregate) {
    # Concatena por fluxo, sem carregar o arquivo inteiro em memoria.
    try {
        $header = [System.Text.Encoding]::UTF8.GetBytes(("`r`n===== {0} =====`r`n" -f [System.IO.Path]::GetFileName($Source)))
        $out = [System.IO.File]::Open($Aggregate, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write,
            [System.IO.FileShare]::Read)
        try {
            $out.Write($header, 0, $header.Length)
            $in = [System.IO.File]::Open($Source, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
                [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
            try { $in.CopyTo($out); $out.Flush($true) } finally { $in.Dispose() }
        } finally { $out.Dispose() }
    } catch {
        Write-Host ("  AVISO: nao agreguei {0}: {1}" -f $Source, $_.Exception.Message) -ForegroundColor Yellow
    }
}

function Disarm-Bc250SafeGuard([string]$TaskName, [string]$EvidenceDir) {
    $guardDir = 'C:\BC250_SAFE_GUARD'
    try {
        foreach ($name in @($TaskName, 'BC250-SafeBindDeadline')) {
            Stop-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue
            Unregister-ScheduledTask -TaskName $name -Confirm:$false -ErrorAction SilentlyContinue
        }
    } catch { }
    try {
        if ($EvidenceDir -and (Test-Path -LiteralPath $EvidenceDir)) {
            $archive = Join-Path $EvidenceDir 'safe-guard-archive'
            if (-not (Test-Path -LiteralPath $archive)) {
                New-Item -ItemType Directory -Path $archive -Force | Out-Null
            }
            foreach ($name in @('armed.json','watch-bc250mcdm-safe-bind.ps1','restore-dwm-crash-capture.ps1')) {
                $source = Join-Path $guardDir $name
                if (Test-Path -LiteralPath $source -PathType Leaf) {
                    Copy-Item -LiteralPath $source -Destination $archive -Force
                }
            }
        }
        Remove-Item -LiteralPath $guardDir -Recurse -Force -ErrorAction Stop
        return $true
    } catch { return $false }
}

# --------------------------------------------------------------------------
# Execucao blindada de um .cmd nativo.
#
# No PowerShell 5.1, "& nativo ... 2>&1" com $ErrorActionPreference = 'Stop'
# transforma cada linha de stderr em NativeCommandError TERMINANTE. No fluxo
# fire-and-collect isso mataria o script entre o rollback e o reboot, que e'
# exatamente onde nao se pode morrer.
# --------------------------------------------------------------------------
function Invoke-Bc250NativeCmd([string]$Path) {
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $code = 1000   # sentinela: nem chegou a rodar
    $text = ''
    try {
        $text = (& cmd.exe /d /c $Path 2>&1 | ForEach-Object { [string]$_ }) -join "`n"
        if ($null -ne $LASTEXITCODE) { $code = [int]$LASTEXITCODE }
    } catch {
        $text = "EXCECAO AO EXECUTAR ${Path}: $($_.Exception.Message)"
    } finally { $ErrorActionPreference = $previousEap }
    return [pscustomobject]@{ exit_code = $code; output = $text }
}

function Assert-ArtifactHash([string]$Path, [string]$ExpectedHash, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Artefato ausente ($Label): $Path"
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actualHash -ne $ExpectedHash) {
        throw "Hash divergente ($Label): observado=$actualHash esperado=$ExpectedHash"
    }
}

if (-not (Test-Path -LiteralPath $packageInf) -or -not (Test-Path -LiteralPath $manifestPath)) {
    throw "Pacote de laboratorio nao encontrado. Execute tools\package-bc250mcdm-lab.ps1 primeiro."
}
if (-not (Test-Path -LiteralPath $recoveryPath)) {
    throw "C:\BC250_RECOVERY.cmd nao encontrado."
}
if ($EnableDwmCrashCapture) {
    foreach ($captureTool in @($armDwmCapture, $restoreDwmCapture)) {
        if (-not (Test-Path -LiteralPath $captureTool -PathType Leaf)) {
            throw "Captura DWM recusada: ferramenta ausente: $captureTool"
        }
    }
    if ($DisableWatchdog) {
        throw '-EnableDwmCrashCapture exige watchdog para restauracao fail-closed.'
    }
}
if ($DisableWatchdog -and -not $IAcceptNoAutomaticRollback) {
    throw '-DisableWatchdog exige -IAcceptNoAutomaticRollback.'
}
if ($IAcceptGpuvaLabPromotion -and -not $EnableGpuvaLabPromotion) {
    throw '-IAcceptGpuvaLabPromotion nao e valido sem -EnableGpuvaLabPromotion.'
}
if ($EnableGpuvaLabPromotion) {
    if (-not $IAcceptGpuvaLabPromotion) {
        throw ('-EnableGpuvaLabPromotion exige -IAcceptGpuvaLabPromotion: a promocao ' +
               'abre o gate GPUVA para submissoes reais do dxgkrnl no node 1.')
    }
    # Abrir o gate sem no de render nem fila persistente nao teria a quem
    # servir: nao existiria node 1 para submeter, e o unico efeito seria um
    # boot com uma trava a menos e nenhuma medida a mais.
    if (-not $EnableRenderNode -and -not $EnablePhysicalRenderOnly) {
        throw '-EnableGpuvaLabPromotion exige -EnableRenderNode (ou -EnablePhysicalRenderOnly).'
    }
    if (-not $EnablePersistentSubmission) {
        throw '-EnableGpuvaLabPromotion exige -EnablePersistentSubmission.'
    }
    if (-not $EnableShaderDispatch) {
        throw '-EnablePersistentSubmission exige -EnableShaderDispatch (receita F8b com CpSoftReset=1 para carga do PFP).'
    }
}
if ($EnableD3D11LabPromotion) {
    if (-not $EnableRenderNode -and -not $EnablePhysicalRenderOnly) {
        throw '-EnableD3D11LabPromotion exige -EnableRenderNode (ou -EnablePhysicalRenderOnly).'
    }
}
if ($EnableD3D11DrawBltSubstitution -and -not $EnableD3D11LabPromotion) {
    throw '-EnableD3D11DrawBltSubstitution exige -EnableD3D11LabPromotion.'
}
if ($EnableD3D11ShaderTranslator -and -not $EnableD3D11LabPromotion) {
    throw '-EnableD3D11ShaderTranslator exige -EnableD3D11LabPromotion.'
}
$activeRunners = @()
if ($RunRadvComputeTripod) { $activeRunners += 'Compute' }
if ($RunRadvPresentTripod) { $activeRunners += 'Present' }
if ($RunVulkanCube) { $activeRunners += 'VulkanCube' }
if ($RunDxvkD3D11) { $activeRunners += 'DXVK' }
if ($RunUmdNode1Tripod) { $activeRunners += 'B7_UMD_NODE1_TRIPOD' }
if ($RunMarco3Graphics) { $activeRunners += 'Marco3Graphics' }
if ($RunMarco4Cube) { $activeRunners += 'Marco4Cube' }
if ($RunD3D11Native) { $activeRunners += 'D3D11Native' }
if ($activeRunners.Count -gt 1) {
    throw ("Escolha uma unica experiencia fisica. Informados: " + ($activeRunners -join ', '))
}

if ($RunRadvComputeTripod) {
    if (-not $EnableGpuvaLabPromotion) {
        throw ('-RunRadvComputeTripod exige -EnableGpuvaLabPromotion: sem o gate ' +
               'GPUVA aberto o submit no node 1 devolve STATUS_NOT_SUPPORTED.')
    }
    if ($DisableWatchdog) {
        throw ('-RunRadvComputeTripod exige o watchdog: o runner recusa a corrida ' +
               'sem o guardiao SYSTEM em OBSERVING.')
    }
    if ($EnablePhysicalRenderOnly) {
        throw ('-RunRadvComputeTripod nao vale no perfil physical render-only: ' +
               'esse ciclo e fire-and-collect e termina em reboot.')
    }
    if ($WatchdogTimeoutSeconds -lt 90) {
        throw 'RADV_WDDM_COMPUTE_TRIPOD exige WatchdogTimeoutSeconds entre 90 e 600.'
    }
    if (-not (Test-Path -LiteralPath $radvComputeTripod -PathType Leaf)) {
        throw "Runner ausente: $radvComputeTripod"
    }
}
if ($RunRadvPresentTripod) {
    if (-not $EnableGpuvaLabPromotion) {
        throw ('-RunRadvPresentTripod exige -EnableGpuvaLabPromotion: sem o gate ' +
               'GPUVA aberto o submit no node 1 devolve STATUS_NOT_SUPPORTED.')
    }
    if ($DisableWatchdog) {
        throw ('-RunRadvPresentTripod exige o watchdog: o runner recusa a corrida ' +
               'sem o guardiao SYSTEM em OBSERVING.')
    }
    if ($EnablePhysicalRenderOnly) {
        throw ('-RunRadvPresentTripod nao vale no perfil physical render-only: ' +
               'esse ciclo e fire-and-collect e termina em reboot.')
    }
    if ($WatchdogTimeoutSeconds -lt 90) {
        throw 'RADV_WDDM_PRESENT_TRIPOD exige WatchdogTimeoutSeconds entre 90 e 600.'
    }
    if (-not (Test-Path -LiteralPath $radvPresentTripod -PathType Leaf)) {
        throw "Runner ausente: $radvPresentTripod"
    }
}
if ($RunVulkanCube) {
    if (-not $EnableGpuvaLabPromotion) {
        throw ('-RunVulkanCube exige -EnableGpuvaLabPromotion: sem o gate ' +
               'GPUVA aberto o submit no node 1 devolve STATUS_NOT_SUPPORTED.')
    }
    if ($DisableWatchdog) {
        throw ('-RunVulkanCube exige o watchdog: o runner recusa a corrida ' +
               'sem o guardiao SYSTEM em OBSERVING.')
    }
    if ($EnablePhysicalRenderOnly) {
        throw ('-RunVulkanCube nao vale no perfil physical render-only: ' +
               'esse ciclo e fire-and-collect e termina em reboot.')
    }
    if ($WatchdogTimeoutSeconds -lt 90) {
        throw 'VULKAN_WSI_CUBE exige WatchdogTimeoutSeconds entre 90 e 600.'
    }
    if (-not (Test-Path -LiteralPath $vulkanCube -PathType Leaf)) {
        throw "Runner ausente: $vulkanCube"
    }
}
if ($RunDxvkD3D11) {
    if (-not $EnableGpuvaLabPromotion) {
        throw ('-RunDxvkD3D11 exige -EnableGpuvaLabPromotion: sem o gate ' +
               'GPUVA aberto o submit no node 1 devolve STATUS_NOT_SUPPORTED.')
    }
    if ($DisableWatchdog) {
        throw ('-RunDxvkD3D11 exige o watchdog: o runner recusa a corrida ' +
               'sem o guardiao SYSTEM em OBSERVING.')
    }
    if ($EnablePhysicalRenderOnly) {
        throw ('-RunDxvkD3D11 nao vale no perfil physical render-only: ' +
               'esse ciclo e fire-and-collect e termina em reboot.')
    }
    if ($WatchdogTimeoutSeconds -lt 90) {
        throw 'DXVK_D3D11 exige WatchdogTimeoutSeconds entre 90 e 600.'
    }
    if (-not (Test-Path -LiteralPath $dxvkRunner -PathType Leaf)) {
        throw "Runner ausente: $dxvkRunner"
    }
}
if ($RunUmdNode1Tripod) {
    if ($RunRadvComputeTripod -or $RunRadvPresentTripod -or $RunVulkanCube -or $RunDxvkD3D11 -or $RunMarco3Graphics -or $RunMarco4Cube -or $RunD3D11Native) {
        throw 'Escolha uma unica experiencia fisica: RADV ou B7_UMD_NODE1_TRIPOD.'
    }
    if (-not $EnableGpuvaLabPromotion -or -not $IAcceptGpuvaLabPromotion) {
        throw ('-RunUmdNode1Tripod exige -EnableGpuvaLabPromotion e ' +
               '-IAcceptGpuvaLabPromotion para o submit Node 1 atravessar o gate KMD.')
    }
    if (-not $EnableRenderNode -or -not $EnablePersistentSubmission) {
        throw '-RunUmdNode1Tripod exige -EnableRenderNode e -EnablePersistentSubmission.'
    }
    if ($DisableWatchdog) {
        throw '-RunUmdNode1Tripod exige o watchdog de rollback ativo.'
    }
    if ($EnablePhysicalRenderOnly) {
        throw '-RunUmdNode1Tripod exige o perfil DISPLAY_RENDER_LAB.'
    }
    if ($WatchdogTimeoutSeconds -lt 90) {
        throw 'B7_UMD_NODE1_TRIPOD exige WatchdogTimeoutSeconds entre 90 e 600.'
    }
    if (-not (Test-Path -LiteralPath $umdNode1Tripod -PathType Leaf)) {
        throw "Runner ausente: $umdNode1Tripod"
    }
}
if ($RunMarco3Graphics) {
    if (-not $EnableGpuvaLabPromotion) {
        throw ('-RunMarco3Graphics exige -EnableGpuvaLabPromotion: sem o gate ' +
               'GPUVA aberto o submit no node 1 devolve STATUS_NOT_SUPPORTED.')
    }
    if ($DisableWatchdog) {
        throw ('-RunMarco3Graphics exige o watchdog: o runner recusa a corrida ' +
               'sem o guardiao SYSTEM em OBSERVING.')
    }
    if ($EnablePhysicalRenderOnly) {
        throw ('-RunMarco3Graphics nao vale no perfil physical render-only: ' +
               'esse ciclo e fire-and-collect e termina em reboot.')
    }
    if ($WatchdogTimeoutSeconds -lt 90) {
        throw 'MARCO3_GRAPHICS_DRAW exige WatchdogTimeoutSeconds entre 90 e 600.'
    }
    if (-not (Test-Path -LiteralPath $marco3Runner -PathType Leaf)) {
        throw "Runner ausente: $marco3Runner"
    }
}
if ($RunMarco4Cube) {
    if (-not $EnableGpuvaLabPromotion) {
        throw ('-RunMarco4Cube exige -EnableGpuvaLabPromotion: sem o gate ' +
               'GPUVA aberto o submit no node 1 devolve STATUS_NOT_SUPPORTED.')
    }
    if ($DisableWatchdog) {
        throw ('-RunMarco4Cube exige o watchdog: o runner recusa a corrida ' +
               'sem o guardiao SYSTEM em OBSERVING.')
    }
    if ($EnablePhysicalRenderOnly) {
        throw ('-RunMarco4Cube nao vale no perfil physical render-only: ' +
               'esse ciclo e fire-and-collect e termina em reboot.')
    }
    if ($WatchdogTimeoutSeconds -lt 90) {
        throw 'MARCO4_GRAPHICS_CUBE exige WatchdogTimeoutSeconds entre 90 e 600.'
    }
    if (-not (Test-Path -LiteralPath $marco4Runner -PathType Leaf)) {
        throw "Runner ausente: $marco4Runner"
    }
}
if ($RunD3D11Native) {
    if ($D3D11StartDelaySeconds -gt 0 -and ($D3D11StartDelaySeconds + 120) -gt ($WatchdogTimeoutSeconds - 30)) {
        throw ('-D3D11StartDelaySeconds deixa menos de 120 s de harness antes do watchdog.')
    }
    if (-not $EnableGpuvaLabPromotion) {
        throw ('-RunD3D11Native exige -EnableGpuvaLabPromotion: sem o gate ' +
               'GPUVA aberto o submit no node 1 devolve STATUS_NOT_SUPPORTED.')
    }
    if (-not $EnableD3D11LabPromotion) {
        throw ('-RunD3D11Native exige -EnableD3D11LabPromotion para criacao de device nativo.')
    }
    if ($DisableWatchdog) {
        throw ('-RunD3D11Native exige o watchdog: o runner recusa a corrida ' +
               'sem o guardiao SYSTEM em OBSERVING.')
    }
    if ($EnablePhysicalRenderOnly) {
        throw ('-RunD3D11Native nao vale no perfil physical render-only: ' +
               'esse ciclo e fire-and-collect e termina em reboot.')
    }
    if ($WatchdogTimeoutSeconds -lt 90) {
        throw 'D3D11_NATIVE_CONFORMANCE exige WatchdogTimeoutSeconds entre 90 e 600.'
    }
    if (-not (Test-Path -LiteralPath $d3d11Runner -PathType Leaf)) {
        throw "Runner ausente: $d3d11Runner"
    }
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if ($Execute -and
    -not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado (Administrador).'
}

# O rollback para BasicDisplay remove a chave de telemetria, mas nao garante que
# a HQD saiu fisicamente do estado anterior. Ele publica o boot onde ocorreu; o
# binder so pode prosseguir depois que o Windows observar outro boot.
if (Test-Path -LiteralPath $cleanBootMarkerPath) {
    $cleanBootMarker = Get-Content -LiteralPath $cleanBootMarkerPath -Raw |
        ConvertFrom-Json
    $currentBootTicks = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.
        ToUniversalTime().Ticks
    if ([int64]$cleanBootMarker.rollback_boot_ticks -eq [int64]$currentBootTicks) {
        throw (
            'Binding recusado: o rollback anterior exige BOOT FRIO com BasicDisplay ' +
            "antes de um novo ensaio. Marcador: $cleanBootMarkerPath")
    }
    if ($Execute) {
        Remove-Item -LiteralPath $cleanBootMarkerPath -Force
        Write-Host 'Boot posterior ao rollback confirmado; marcador de engine suja liberado.' -ForegroundColor Green
    } else {
        Write-Host 'Boot posterior ao rollback confirmado; marcador sera removido somente no APPLY.' -ForegroundColor Green
    }
}

$umdRegKey = 'HKLM:\SOFTWARE\BC250\Umd'
if (Test-Path -LiteralPath $umdRegKey) {
    $capsProbeValue = (Get-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11CapsProbe' `
        -ErrorAction SilentlyContinue).D3D11CapsProbe
    if ($null -ne $capsProbeValue -and $capsProbeValue -ne 0) {
        throw ('Binding bloqueado: D3D11CapsProbe = ' + $capsProbeValue + '. Desarme antes: ' +
            'powershell -ExecutionPolicy Bypass -File tools\arm-d3d11-caps-probe.ps1 -Disarm')
    }
}

$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like "$hardwareId*" })
if ($devices.Count -ne 1) {
    throw "Dispositivo BC-250 nao encontrado ou multiplos encontrados."
}
$device = $devices[0]
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
# O schema vem do manifesto, nao de literal. Fixar o numero aqui ja custou um
# ciclo: o pacote 3.44/115 era recusado por um gate que ainda dizia 114. O piso
# continua sendo 114, o primeiro schema desta familia de laboratorio.
$expectedSchema = [uint32]$manifest.TelemetrySchemaVersion
if ($manifest.BuildKind -ne 'MCDM_LAB_BRINGUP' -or [bool]$manifest.HardwareValidated -or
    $expectedSchema -lt 114 -or [uint32]$manifest.SkipHwBringup -ne 0) {
    throw 'Contrato do manifesto de laboratorio invalido (kind/validacao/schema/bring-up).'
}
# ==========================================================================
# PREFLIGHT DE REBAIXAMENTO DE PERFIL.
#
# Compara o perfil do candidato com o do ULTIMO bind registrado. Se alguma
# capacidade cair de ligada para desligada, recusa -- a menos que o operador
# declare o rebaixamento. Ver o comentario de -IAcceptProfileDowngrade.
# ==========================================================================
$lastBindingPath = Join-Path $root 'tools\_last_lab_binding.json'
if (Test-Path -LiteralPath $lastBindingPath -PathType Leaf) {
    # DX.36: registro ilegivel NAO pode derrubar o bind nem desligar o preflight.
    # Na Corrida 81 um BugCheck deixou este arquivo com bytes zero; a copia gravada
    # na pasta de evidencia (binding-provenance.json) sobreviveu. Se o registro nao
    # parsear, usa a copia mais recente e AVISA; se nenhuma parsear, recusa com a
    # instrucao de recuperacao. O preflight de rebaixamento continua valendo.
    $lastBinding = $null
    try {
        $lastBinding = Get-Content -LiteralPath $lastBindingPath -Raw | ConvertFrom-Json -ErrorAction Stop
    } catch {
        $lastBinding = $null
    }
    if ($null -eq $lastBinding) {
        $provenanceCopy = Get-ChildItem -LiteralPath (Join-Path $root 'docs\evidence') -Directory |
            Sort-Object LastWriteTime -Descending |
            ForEach-Object { Join-Path $_.FullName 'binding-provenance.json' } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Where-Object {
                try { $null -ne (Get-Content -LiteralPath $_ -Raw | ConvertFrom-Json -ErrorAction Stop).profile } catch { $false }
            } | Select-Object -First 1
        if (-not $provenanceCopy) {
            throw ('Registro do ultimo bind ilegivel (' + $lastBindingPath + ') e nenhuma ' +
                'binding-provenance.json valida em docs\evidence. Provavel BugCheck antes do flush; ' +
                'restaure o arquivo a partir da pasta de evidencia do ultimo bind.')
        }
        Write-Host ('AVISO: ' + $lastBindingPath + ' ilegivel (provavel BugCheck antes do flush). ' +
            'Preflight de rebaixamento usa a copia ' + $provenanceCopy) -ForegroundColor Yellow
        $lastBinding = Get-Content -LiteralPath $provenanceCopy -Raw | ConvertFrom-Json
    }
    if ($null -ne $lastBinding.profile) {
        $downgrades = @()
        foreach ($cap in @('RenderNode','ShaderDispatchEnabled','PersistentSubmissionEnabled')) {
            $before = $lastBinding.profile.$cap
            $now = $manifest.$cap
            if ($null -eq $before) { continue }
            if ([bool]$before -and -not [bool]$now) { $downgrades += $cap }
        }
        if ($lastBinding.profile.ProfileMode -and
            $lastBinding.profile.ProfileMode -ne $manifest.ProfileMode) {
            $downgrades += ('ProfileMode {0} -> {1}' -f `
                $lastBinding.profile.ProfileMode, $manifest.ProfileMode)
        }
        if ($downgrades.Count -gt 0) {
            $msg = ('Perfil do candidato e MAIS FRACO que o do ultimo bind: {0}. ' -f
                    ($downgrades -join ', ')) +
                   'Um ensaio de submissao GFX num perfil rebaixado mede o nada. ' +
                   'Reempacote com os switches certos, ou passe -IAcceptProfileDowngrade ' +
                   'se o rebaixamento for deliberado.'
            if (-not $IAcceptProfileDowngrade) { throw $msg }
            Write-Host ('  [AVISO] REBAIXAMENTO DE PERFIL ACEITO: {0}' -f ($downgrades -join ', '))
        }
    }
}

Assert-ArtifactHash $packageSys $manifest.PackageSysSha256 'SYS'
Assert-ArtifactHash $packagePdb $manifest.PackagePdbSha256 'PDB privado'
Assert-ArtifactHash (Join-Path $packageDir 'bc250umd.dll') $manifest.PackageUmdSha256 'UMD'
Assert-ArtifactHash $packageInf $manifest.PackageInfSha256 'INF'
Assert-ArtifactHash $packageCat $manifest.PackageCatSha256 'CAT'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_sdma.bin') $manifest.SdmaFirmwareSha256 'SDMA firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_rlc.bin') $manifest.RlcFirmwareSha256 'RLC firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_sdma1.bin') $manifest.Sdma1FirmwareSha256 'SDMA1 firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_pfp.bin') $manifest.PfpFirmwareSha256 'PFP firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_me.bin') $manifest.MeFirmwareSha256 'ME firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_ce.bin') $manifest.CeFirmwareSha256 'CE firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_mec.bin') $manifest.MecFirmwareSha256 'MEC firmware'
Assert-ArtifactHash $recoveryPath $manifest.RecoverySha256 'recuperacao'
foreach ($signedArtifact in @($packageSys, (Join-Path $packageDir 'bc250umd.dll'), $packageCat)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $signedArtifact
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
        $signature.SignerCertificate.Thumbprint -ne $manifest.SignerThumbprint) {
        throw "Assinatura invalida ou signer divergente: $signedArtifact"
    }
}

$infText = Get-Content -LiteralPath $packageInf -Raw
$expectedInfValues = [ordered]@{
    'Parameters\Probe,SchemaVersion' = $expectedSchema
    # Vem do manifesto, nao de literal: o pacote e' quem decide o contrato, e
    # fixar 27 aqui recusaria em silencio um pacote empacotado com 20.
    'Parameters,WddmContractVersion' = [uint32]$manifest.WddmContractVersion
    'Parameters\Driver,ActiveMode' = 1
    'Parameters,RenderNode' = $(if ($EnableRenderNode) { 1 } elseif ($EnablePhysicalRenderOnly) { 1 } else { 0 })
    'Parameters,RenderOnlyLab' = $(if ($EnablePhysicalRenderOnly) { 1 } else { 0 })
    'Parameters,SkipHwBringup' = 0
    'Parameters,ShaderDispatch' = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    'Parameters,CpSoftReset' = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    'Parameters,Vmid0PageTable' = 0
    'Parameters,SdmaAutoCtxSw' = $(if ($EnableShaderDispatch) { 0 } else { 1 })
    'Parameters,PersistentSubmission' =
        $(if ($EnablePersistentSubmission) { 1 } else { 0 })
    'Parameters,GpuvaLabPromotion' = 0
    'Parameters,SdmaPlanoAFallback' = 0
}
if ($EnablePhysicalRenderOnly -and
    (-not $EnableShaderDispatch -or -not $EnablePersistentSubmission)) {
    throw ('Physical render-only exige -EnableShaderDispatch e ' +
           '-EnablePersistentSubmission para provar o backend antes de StartDevice retornar sucesso.')
}
if ([bool]$manifest.RenderOnlyLab -ne [bool]$EnablePhysicalRenderOnly) {
    throw 'Perfil solicitado diverge do pacote: reempacote com -EnablePhysicalRenderOnly.'
}
# RenderNode e RenderOnlyLab sao eixos independentes: o par 1/0 e' DISPLAY_RENDER.
# Conferir so' RenderOnlyLab deixava passar um pacote DISPLAY_LAB (RenderNode=0)
# quando o operador pediu -EnableRenderNode; o desvio so' apareceria como
# perfil DISPLAY_ONLY no boot, gastando a corrida.
$expectedManifestRenderNode = ($EnableRenderNode -or $EnablePhysicalRenderOnly)
if ([bool]$manifest.RenderNode -ne $expectedManifestRenderNode) {
    throw ('No de render solicitado diverge do pacote: manifesto RenderNode=' +
        "$([uint32][bool]$manifest.RenderNode), esperado $([uint32]$expectedManifestRenderNode). " +
        'Reempacote com package-bc250mcdm-lab.ps1 -EnableRenderNode.')
}
$expectedProfileMode = $(if ($EnablePhysicalRenderOnly) {
    'PHYSICAL_RENDER_ONLY'
} elseif ($expectedManifestRenderNode) {
    'DISPLAY_RENDER_LAB'
} else {
    'DISPLAY_LAB'
})
if ([string]$manifest.ProfileMode -ne $expectedProfileMode) {
    throw ("ProfileMode do pacote diverge: '$($manifest.ProfileMode)', esperado " +
        "'$expectedProfileMode'.")
}
# 20 -> DXGKDDI_INTERFACE_VERSION_WDDM2_0, 27 -> WDDM2_7
# (bc250_active_miniport.c:7365). Qualquer outro valor faz o DriverEntry
# devolver STATUS_DEVICE_CONFIGURATION_ERROR, e o bind morreria sem diagnostico.
if (@(20, 27) -notcontains [uint32]$manifest.WddmContractVersion) {
    throw ("Manifesto declara WddmContractVersion=$($manifest.WddmContractVersion); " +
        'o DriverEntry so aceita 20 ou 27.')
}
Write-Host ("  Contrato WDDM declarado pelo pacote: {0}" -f $manifest.WddmContractVersion)
# GpuvaLabPromotion e SdmaPlanoAFallback sao declarados no INF com NOCLOBBER (0x00010003):
# sao defaults, nao mandatos, para que a politica pre-bind abaixo possa arma-los sem
# ser apagada pelo AddReg do instalador.
$infValueFlags = @{
    'Parameters,GpuvaLabPromotion' = '0x00010003'
    'Parameters,SdmaPlanoAFallback' = '0x00010003'
}
foreach ($entry in $expectedInfValues.GetEnumerator()) {
    $escapedName = [regex]::Escape($entry.Key)
    $flag = $(if ($infValueFlags.ContainsKey($entry.Key)) {
        $infValueFlags[$entry.Key]
    } else { '0x00010001' })
    if ([regex]::Matches(
            $infText,
            "(?m)^HKR,$escapedName,$flag,$($entry.Value)\s*$").Count -ne 1) {
        throw "INF diverge do contrato solicitado: $($entry.Key)=$($entry.Value) (flag $flag)."
    }
}
if ($EnableShaderDispatch -and -not [bool]$manifest.ShaderDispatchEnabled) {
    throw 'Pacote nao habilita shader: reempacote com package-bc250mcdm-lab.ps1 -EnableShaderDispatch.'
}
if ($EnablePersistentSubmission -and -not [bool]$manifest.PersistentSubmissionEnabled) {
    throw 'Pacote nao habilita a fila persistente: reempacote com -EnablePersistentSubmission.'
}
if ([bool]$manifest.GpuvaLabPromotionEnabled) {
    throw 'Pacote inseguro: GpuvaLabPromotion deve estar desabilitado no manifesto.'
}
if ([bool]$manifest.Node1TripodDefaultArmed -or [bool]$manifest.D3D10_11NativeReady) {
    throw 'Pacote inseguro: acionador Node 1 e gate D3D devem estar fechados no manifesto.'
}
if ($EnableShaderDispatch -and
    (-not [bool]$manifest.CpSoftResetEnabled -or
     [bool]$manifest.Vmid0PageTableEnabled -or
     [bool]$manifest.SdmaAutoCtxSwEnabled)) {
    throw 'Receita F8b invalida no manifesto: exige CpSoftReset=1, Vmid0PageTable=0 e SdmaAutoCtxSw=0.'
}

# --------------------------------------------------------------------------
# Guard-rail 1: o pacote precisa ser do build atual.
#
# O empacotador COPIA o .sys, nao compila. Se driver\wdk\output_lab\bc250mcdm.sys
# for mais novo que a origem registrada no manifesto, este binding instalaria o
# candidato ANTERIOR e o ensaio inteiro seria sobre o binario errado — com
# aparencia de sucesso. Aconteceu em 2026-08-31 12:03.
# --------------------------------------------------------------------------
$builtSys = Join-Path $root 'driver\wdk\output_lab\bc250mcdm.sys'
if (Test-Path -LiteralPath $builtSys) {
    $builtHash = (Get-FileHash -LiteralPath $builtSys -Algorithm SHA256).Hash
    if ($builtHash -ne [string]$manifest.SourceSysSha256) {
        throw (
            "O pacote nao corresponde ao build atual.`n" +
            "  build   : $builtHash`n" +
            "  pacote  : $($manifest.SourceSysSha256)`n" +
            'Reempacote antes de vincular: tools\package-bc250mcdm-lab.ps1 ' +
            '(com as mesmas flags deste binding).')
    }
}
$builtUmd = Join-Path $root 'driver\umd\output\bc250umd.dll'
if (-not (Test-Path -LiteralPath $builtUmd -PathType Leaf) -or
    (Get-FileHash -LiteralPath $builtUmd -Algorithm SHA256).Hash -ne
        [string]$manifest.SourceUmdSha256) {
    throw 'O pacote nao corresponde ao build UMD atual; reempacote antes do bind.'
}

# --------------------------------------------------------------------------
# Guard-rail 2: a engine precisa estar limpa e o fusivel integro.
#
# Estado de fila nao cai com desbind; so o boot frio limpa. Vincular sobre uma
# fila ja ativa faz o motor persistente nao subir, e todos os contadores saem
# zerados — o que se confunde com "quebrado" quando na verdade e' "nao
# exercitado". Aconteceu em 2026-08-31 12:03 com WPTR remanescente 204.
# A fonte de verdade persistente e' C:\BC250_BOOT_FUSE.dat, que sobrevive ao
# rollback e ao reboot.
# --------------------------------------------------------------------------
if (Test-Path -LiteralPath $bootFusePath) {
    try {
        $fuseRaw = [System.IO.File]::ReadAllBytes($bootFusePath)
        if ($fuseRaw.Length -eq 40) {
            $fuseMagic = [BitConverter]::ToUInt32($fuseRaw, 0)
            $fuseState = [BitConverter]::ToUInt32($fuseRaw, 8)
            $fuseLastStatus = [BitConverter]::ToUInt32($fuseRaw, 16)
            if ($fuseMagic -eq 0x45535546 -and ($fuseState -eq 1 -or $fuseState -eq 2)) {
                $fuseStateName = if ($fuseState -eq 1) { 'ARMED (sessao anterior nao fechou limpa)' } else { 'TRIPPED (fusivel anti-boot-loop disparado)' }
                $msg = (
                    "Fusivel persistente acusa engine suja / falha anterior:`n" +
                    "  Arquivo:    $bootFusePath`n" +
                    "  Estado:     $fuseStateName ($fuseState)`n" +
                    "  LastStatus: 0x$($fuseLastStatus.ToString('X8'))`n" +
                    'Restaure BasicDisplay e execute BOOT FRIO antes de novo ensaio.`n' +
                    'Apos o boot frio, use tools\inspect-boot-fuse.ps1 -Reset.')
                if ($IAcceptDirtyEngine) {
                    Write-Warning $msg
                    Write-Warning 'Prosseguindo por -IAcceptDirtyEngine: a telemetria deste ciclo NAO e evidencia.'
                } else {
                    throw $msg
                }
            } else {
                Write-Host '  Fusivel persistente (C:\BC250_BOOT_FUSE.dat): CLEAN (0).' -ForegroundColor Green
            }
        }
    } catch [System.Management.Automation.RuntimeException] {
        throw
    } catch {
        Write-Warning "Falha ao ler ${bootFusePath}: $($_.Exception.Message)"
    }
}
$probeKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
if (-not (Test-Path -LiteralPath $probeKey)) {
    # Silencio nao e' aprovacao.
    #
    # Depois de um rollback a chave some junto com o servico, e este guard
    # passa sem avaliar nada. Ate 2026-09-02 ele nao dizia isso, e a ausencia
    # de reclamacao se lia como "engine verificada limpa" -- que e' o oposto
    # do que aconteceu. Quem realmente protege este caso e o marcador de boot
    # limpo conferido acima; aqui so' se declara que a checagem nao foi
    # avaliavel.
    Write-Host ('  Engine: chave de telemetria ausente (rollback anterior); ' +
        'guard nao avaliavel. A protecao efetiva e o marcador de boot limpo.') `
        -ForegroundColor DarkYellow
}
if (Test-Path -LiteralPath $probeKey) {
    $probeNow = Get-ItemProperty -LiteralPath $probeKey
    # REG_DWORD chega como Int32 e pode vir negativo; normalizar para 32 bits.
    $rawBefore = $probeNow.HqdActiveBefore
    $rawWptr   = $probeNow.HqdActiveBaselineWptrReadback
    $rawEngine = $probeNow.P1EngineState
    $rawP1Ready = $probeNow.P1SelfTestPhysicalReady
    $rawP2Ready = $probeNow.P2PhysicalReady
    $hqdBefore = if ($null -eq $rawBefore) { 0 } else { [int64]$rawBefore -band 0xFFFFFFFFL }
    $wptrLeft  = if ($null -eq $rawWptr)   { 0 } else { [int64]$rawWptr   -band 0xFFFFFFFFL }
    $engineState = if ($null -eq $rawEngine) { 0 } else { [int64]$rawEngine -band 0xFFFFFFFFL }
    $p1Ready = if ($null -eq $rawP1Ready) { 0 } else { [int64]$rawP1Ready -band 0xFFFFFFFFL }
    $p2Ready = if ($null -eq $rawP2Ready) { 0 } else { [int64]$rawP2Ready -band 0xFFFFFFFFL }
    # P1EngineState/P1SelfTestPhysicalReady/P2PhysicalReady registram o ultimo
    # bring-up bem-sucedido. Eles sobrevivem enquanto o pacote/servico antigo
    # ainda esta no DriverStore e, portanto, nao provam que a HQD continua
    # fisicamente ativa depois de um boot frio. Trata-los como "engine suja"
    # cria um impasse: o proprio ensaio que prova P1/P2 impede todo ensaio
    # posterior, mesmo com BasicDisplay, fusivel CLEAN e WPTR zerado.
    #
    # Os dois campos abaixo sao os indicadores de residuo de fila. A protecao
    # entre boots permanece fail-closed no BC250_BOOT_FUSE e no marcador
    # BC250_REQUIRE_CLEAN_BOOT, verificados antes deste bloco.
    if ($hqdBefore -ne 0 -or $wptrLeft -ne 0) {
        $msg = (
            "Engine suja: a fila de computacao ainda carrega estado da sessao anterior.`n" +
            "  HqdActiveBefore              = $hqdBefore`n" +
            "  HqdActiveBaselineWptrReadback = $wptrLeft`n" +
            "  P1EngineState                = $engineState`n" +
            "  P1SelfTestPhysicalReady       = $p1Ready`n" +
            "  P2PhysicalReady               = $p2Ready`n" +
            'Restaure BasicDisplay e so entao faca BOOT FRIO. Reiniciar com ' +
            'bc250mcdm ainda instalado religa a fila persistente.')
        if ($IAcceptDirtyEngine) {
            Write-Warning $msg
            Write-Warning 'Prosseguindo por -IAcceptDirtyEngine: a telemetria deste ciclo NAO e evidencia.'
        } else {
            throw $msg
        }
    }
    if ($engineState -ne 0 -or $p1Ready -ne 0 -or $p2Ready -ne 0) {
        Write-Host (
            '  Probe historico preservado (nao e indicador de fila suja): ' +
            "P1EngineState=$engineState, P1Ready=$p1Ready, P2Ready=$p2Ready.") `
            -ForegroundColor DarkYellow
    }
}

function Get-Bc250DisplayLogDelta([string]$Path, [long]$StartOffset) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $stream = $null
    try {
        $stream = [System.IO.File]::Open(
            $Path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)
        # Truncation/replacement during the bind invalidates the baseline.  Do
        # not search the old part of the file and manufacture a false gate.
        if ($stream.Length -lt $StartOffset) { return '' }
        [void]$stream.Seek($StartOffset, [System.IO.SeekOrigin]::Begin)
        $remaining = [int64]($stream.Length - $StartOffset)
        if ($remaining -le 0) { return '' }
        $buffer = New-Object byte[] $remaining
        $read = $stream.Read($buffer, 0, $buffer.Length)
        return [System.Text.Encoding]::UTF8.GetString($buffer, 0, $read)
    } finally {
        if ($null -ne $stream) { $stream.Dispose() }
    }
}

# O teste Vulkan precisa provar a identidade do ICD antes de APPLY. Em
# 06/09 17:11 o processo elevado ignorou VK_DRIVER_FILES, carregou a copia
# antiga de System32 e gastou uma corrida inteira no mesmo crash. O dry-run do
# runner agora faz LoadLibraryEx pelo caminho absoluto e compara o modulo
# carregado, sem criar instancia Vulkan nem tocar a GPU.
if ($RunRadvComputeTripod) {
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $tripodPreflightOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $radvComputeTripod 2>&1 | ForEach-Object { [string]$_ }) -join "`r`n"
        $tripodPreflightExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousEap
    }
    Write-Host $tripodPreflightOutput
    if ($tripodPreflightExit -ne 0 -or
        $tripodPreflightOutput -notmatch 'DRY-RUN APROVADO') {
        throw ('Binding recusado antes de APPLY: preflight do ' +
            "RADV_WDDM_COMPUTE_TRIPOD falhou (exit=$tripodPreflightExit).")
    }
    Write-Host '  ICD explicito validado antes do binding.' -ForegroundColor Green
}
if ($RunRadvPresentTripod) {
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $tripodPreflightOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $radvPresentTripod 2>&1 | ForEach-Object { [string]$_ }) -join "`r`n"
        $tripodPreflightExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousEap
    }
    Write-Host $tripodPreflightOutput
    if ($tripodPreflightExit -ne 0 -or
        $tripodPreflightOutput -notmatch 'Modo inspect/dry-run: preflights passaram') {
        throw ('Binding recusado antes de APPLY: preflight do ' +
            "RADV_WDDM_PRESENT_TRIPOD falhou (exit=$tripodPreflightExit).")
    }
    Write-Host '  ICD explicito e runner de apresentacao validados antes do binding.' -ForegroundColor Green
}
if ($RunVulkanCube) {
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $cubePreflightOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $vulkanCube 2>&1 | ForEach-Object { [string]$_ }) -join "`r`n"
        $cubePreflightExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousEap
    }
    Write-Host $cubePreflightOutput
    if ($cubePreflightExit -ne 0 -or
        $cubePreflightOutput -notmatch 'Modo inspect/dry-run: preflights passaram') {
        throw ('Binding recusado antes de APPLY: preflight do ' +
            "VULKAN_WSI_CUBE falhou (exit=$cubePreflightExit).")
    }
    Write-Host '  ICD explicito e runner VulkanCube validados antes do binding.' -ForegroundColor Green
}
if ($RunDxvkD3D11) {
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $dxvkPreflightOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $dxvkRunner 2>&1 | ForEach-Object { [string]$_ }) -join "`r`n"
        $dxvkPreflightExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousEap
    }
    Write-Host $dxvkPreflightOutput
    if ($dxvkPreflightExit -ne 0 -or
        $dxvkPreflightOutput -notmatch 'Modo inspect/dry-run: preflights passaram') {
        throw ('Binding recusado antes de APPLY: preflight do ' +
            "DXVK_D3D11 falhou (exit=$dxvkPreflightExit).")
    }
    Write-Host '  ICD explicito e runner DXVK_D3D11 validados antes do binding.' -ForegroundColor Green
}
if ($RunUmdNode1Tripod) {
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $umdTripodPreflightOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $umdNode1Tripod 2>&1 | ForEach-Object { [string]$_ }) -join "`r`n"
        $umdTripodPreflightExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousEap
    }
    Write-Host $umdTripodPreflightOutput
    if ($umdTripodPreflightExit -ne 0 -or
        $umdTripodPreflightOutput -notmatch 'DRY-RUN PASS') {
        throw ('Binding recusado antes de APPLY: preflight do ' +
            "B7_UMD_NODE1_TRIPOD falhou (exit=$umdTripodPreflightExit).")
    }
    Write-Host '  UMD assinada e runner Node 1 validados antes do binding.' -ForegroundColor Green
}
if ($RunMarco3Graphics) {
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $m3PreflightOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $marco3Runner 2>&1 | ForEach-Object { [string]$_ }) -join "`r`n"
        $m3PreflightExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousEap
    }
    Write-Host $m3PreflightOutput
    if ($m3PreflightExit -ne 0 -or
        $m3PreflightOutput -notmatch 'Modo inspect/dry-run: preflights passaram') {
        throw ('Binding recusado antes de APPLY: preflight do ' +
            "MARCO3_GRAPHICS_DRAW falhou (exit=$m3PreflightExit).")
    }
    Write-Host '  ICD explicito e runner Marco 3 Graphics Draw validados antes do binding.' -ForegroundColor Green
}
if ($RunMarco4Cube) {
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $m4PreflightOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $marco4Runner 2>&1 | ForEach-Object { [string]$_ }) -join "`r`n"
        $m4PreflightExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousEap
    }
    Write-Host $m4PreflightOutput
    if ($m4PreflightExit -ne 0 -or
        $m4PreflightOutput -notmatch 'Modo inspect/dry-run: preflights passaram') {
        throw ('Binding recusado antes de APPLY: preflight do ' +
            "MARCO4_GRAPHICS_CUBE falhou (exit=$m4PreflightExit).")
    }
    Write-Host '  ICD explicito e runner Marco 4 Graphics Cube validados antes do binding.' -ForegroundColor Green
}
if ($RunD3D11Native) {
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $d3dPreflightOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $d3d11Runner 2>&1 | ForEach-Object { [string]$_ }) -join "`r`n"
        $d3dPreflightExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousEap
    }
    Write-Host $d3dPreflightOutput
    if ($d3dPreflightExit -ne 0 -or
        $d3dPreflightOutput -notmatch 'MODO SIMULACAO/PREFLIGHT') {
        throw ('Binding recusado antes de APPLY: preflight do ' +
            "D3D11_NATIVE_CONFORMANCE falhou (exit=$d3dPreflightExit).")
    }
    Write-Host '  Runner D3D11 Native Conformance validado antes do binding.' -ForegroundColor Green
}

$guardMode = if ($DisableWatchdog) { 'SEM WATCHDOG' } else { 'COM WATCHDOG' }
Write-Host "=== BINDING DE LABORATORIO (BC-250 MCDM) $guardMode ===" -ForegroundColor Magenta
Write-Host "  Dispositivo: $($device.PNPDeviceID)"
Write-Host "  Servico atual: $($device.Service)"
Write-Host "  Pacote: $packageInf"
if ($DisableWatchdog) {
    Write-Host '  Rollback automatico: DESABILITADO por decisao explicita.' -ForegroundColor Yellow
    Write-Host "  Recuperacao manual: $recoveryPath" -ForegroundColor Yellow
} else {
    Write-Host "  Watchdog Timeout: $WatchdogTimeoutSeconds segundos"
}
Write-Host ""

if (-not $Execute -or -not $IAcceptBlackScreenRisk) {
    Write-Host "Modo Dry-Run. Para executar:" -ForegroundColor Yellow
    # A dica precisa repetir -EnableShaderDispatch: sem ela o contrato do
    # INF exige ShaderDispatch=0 e o binding falha antes deste ponto.
    $shaderFlag = $(if ($EnableShaderDispatch) { ' -EnableShaderDispatch' } else { '' })
    # Mesma razao do -EnableShaderDispatch: cada um destes participa do
    # contrato de INF e de manifesto conferido acima. Omitir qualquer um na
    # dica gera um comando que morre no preflight seguinte.
    $persistentFlag = $(if ($EnablePersistentSubmission) { ' -EnablePersistentSubmission' } else { '' })
    $renderNodeFlag = $(if ($EnableRenderNode) { ' -EnableRenderNode' } else { '' })
    $renderOnlyFlag = $(if ($EnablePhysicalRenderOnly) { ' -EnablePhysicalRenderOnly' } else { '' })
    $gpuvaFlag = $(if ($EnableGpuvaLabPromotion) {
        ' -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion'
    } else { '' })
    $planoAFlag = $(if ($EnableSdmaPlanoAFallback) { ' -EnableSdmaPlanoAFallback' } else { '' })
    $d3d11PromotionFlag = $(if ($EnableD3D11LabPromotion) { ' -EnableD3D11LabPromotion' } else { '' })
    $d3d11SubstFlag = $(if ($EnableD3D11DrawBltSubstitution) { ' -EnableD3D11DrawBltSubstitution' } else { '' })
    # C3.9: sem isto o dry-run sugeria um comando que PERDIA o flag, e a corrida
    # rodaria com o gate global sem ninguem perceber.
    $journalGateFlag = $(if ($EnableNode1JournalGatePerProcess) { ' -EnableNode1JournalGatePerProcess' } else { '' })
    $tripodFlag = $(if ($RunRadvPresentTripod) {
        " -RunRadvPresentTripod -WatchdogTimeoutSeconds $WatchdogTimeoutSeconds"
    } elseif ($RunVulkanCube) {
        " -RunVulkanCube -WatchdogTimeoutSeconds $WatchdogTimeoutSeconds"
    } elseif ($RunDxvkD3D11) {
        " -RunDxvkD3D11 -WatchdogTimeoutSeconds $WatchdogTimeoutSeconds"
    } elseif ($RunRadvComputeTripod) {
        " -RunRadvComputeTripod -WatchdogTimeoutSeconds $WatchdogTimeoutSeconds"
    } elseif ($RunUmdNode1Tripod) {
        " -RunUmdNode1Tripod -WatchdogTimeoutSeconds $WatchdogTimeoutSeconds"
    } elseif ($RunMarco3Graphics) {
        " -RunMarco3Graphics -WatchdogTimeoutSeconds $WatchdogTimeoutSeconds"
    } elseif ($RunMarco4Cube) {
        " -RunMarco4Cube -WatchdogTimeoutSeconds $WatchdogTimeoutSeconds"
    } elseif ($RunD3D11Native) {
        " -RunD3D11Native -WatchdogTimeoutSeconds $WatchdogTimeoutSeconds"
    } else { '' })
    # 14/09: a corrida 11:27 foi GASTA porque a dica perdia este flag. O
    # operador rodou exatamente o comando sugerido, e o log saiu
    # "GfxRingPreflight=0 (present=1 compiled=1)" -- toda a cadeia funcionando,
    # menos o interruptor. E' a MESMA armadilha que o comentario do C3.9,
    # quinze linhas acima, ja descrevia por escrito.
    $gfxRingFlag = $(if ($EnableGfxRingPreflight) { ' -EnableGfxRingPreflight' } else { '' })
    $gfxBringupFlag = $(if ($GfxRingBringupLevel -gt 0) { " -GfxRingBringupLevel $GfxRingBringupLevel" } else { '' })
    $gfxStageFlag = $(if ($GfxRingBringupLevel -ge 4 -and $GfxRingStage -gt 0) { " -GfxRingStage $GfxRingStage" } else { '' })
    # Achado na mesma auditoria: este tambem faltava, desde antes da B.1.
    $shaderTranslatorFlag = $(if ($EnableD3D11ShaderTranslator) { ' -EnableD3D11ShaderTranslator' } else { '' })
    $dwmCaptureFlag = $(if ($EnableDwmCrashCapture) { ' -EnableDwmCrashCapture' } else { '' })
    $restartExplorerFlag = $(if ($RestartExplorer) { ' -RestartExplorer' } else { '' })
    $watchdogFlag = $(if ($DisableWatchdog) {
        ' -DisableWatchdog -IAcceptNoAutomaticRollback'
    } else { ' -IAcceptAutomaticRecoveryReboot' })
    Write-Host ("powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1" +
        " -Execute -IAcceptBlackScreenRisk" + $shaderFlag + $persistentFlag +
        $renderNodeFlag + $renderOnlyFlag + $gpuvaFlag + $planoAFlag + $d3d11PromotionFlag +
        $d3d11SubstFlag + $shaderTranslatorFlag + $gfxRingFlag + $gfxBringupFlag + $gfxStageFlag +
        $journalGateFlag + $tripodFlag + $dwmCaptureFlag + $restartExplorerFlag + $watchdogFlag)
    exit 0
}
if (-not $DisableWatchdog -and -not $IAcceptAutomaticRecoveryReboot) {
    throw 'O watchdog SYSTEM exige -IAcceptAutomaticRecoveryReboot.'
}
# Corrida 269 (doc 710, pedido do Daniel): ensaio curto so' do DCN pode usar
# watchdog de 60 a 119 s com BC250_ALLOW_SHORT_WATCHDOG=1. A excecao vai no
# contrato do guardiao (short_watchdog=1), que so' entao aceita menos de 120 s.
$shortWatchdog = ($env:BC250_ALLOW_SHORT_WATCHDOG -eq '1')
$watchdogFloor = if ($shortWatchdog) { 60 } else { 120 }
if (-not $DisableWatchdog -and $WatchdogTimeoutSeconds -lt $watchdogFloor) {
    throw ("O watchdog SYSTEM exige WatchdogTimeoutSeconds entre {0} e 600." -f $watchdogFloor)
}
if ($shortWatchdog -and $WatchdogTimeoutSeconds -lt 120) {
    Write-Host ("  WATCHDOG CURTO: {0} s (BC250_ALLOW_SHORT_WATCHDOG=1, ensaio so' do DCN); rollback automatico em {0} s." -f $WatchdogTimeoutSeconds) -ForegroundColor Yellow
}
# A janela do binder tem de caber com folga dentro da do guardiao. Se as duas
# expirarem juntas, binder e guardiao disparam a MESMA recovery em paralelo:
# um remove o pacote enquanto o outro ainda le a chave Probe, e a evidencia
# do ensaio sai truncada.
if ($EnablePhysicalRenderOnly -and -not $DisableWatchdog -and
    $PhysicalWindowSeconds -ge ($WatchdogTimeoutSeconds - 30)) {
    throw ("PhysicalWindowSeconds ($PhysicalWindowSeconds) precisa ser pelo menos 30 s menor " +
        "que WatchdogTimeoutSeconds ($WatchdogTimeoutSeconds).")
}

# 1. Limpeza de marcador OEM e pacotes anteriores
if (Test-Path -LiteralPath $markerPath) {
    $oldOem = (Get-Content -LiteralPath $markerPath -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ($oldOem -match '^oem\d+\.inf$') {
        & pnputil.exe /delete-driver $oldOem /uninstall /force 2>&1 | Out-Null
    }
    Remove-Item -LiteralPath $markerPath -Force -ErrorAction SilentlyContinue
}

# 1.1 Pre-copia direta do binario para System32\drivers enquanto o servico esta parado
$preDriverInstalledPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$prePkgSysPath = Join-Path $packageDir 'bc250mcdm.sys'
if (Test-Path -LiteralPath $prePkgSysPath) {
    try {
        Copy-Item -LiteralPath $prePkgSysPath -Destination $preDriverInstalledPath -Force -ErrorAction SilentlyContinue
    } catch {
        # Se falhar (ex: falta de permissao), o PnP / NewDev efetuara a copia
    }
}

# 2. Stage do pacote de laboratorio
Write-Host "[1/4] Estagiando pacote de laboratorio via pnputil..." -ForegroundColor Yellow
$output = (& pnputil.exe /add-driver $packageInf 2>&1) -join "`n"
$exitCode = $LASTEXITCODE
$output | Write-Host
if ($exitCode -ne 0) { throw "pnputil /add-driver falhou: exit=$exitCode" }

$publishedNames = @(
    [regex]::Matches($output, '(?i)\boem\d+\.inf\b') |
        ForEach-Object { $_.Value.ToLowerInvariant() } |
        Select-Object -Unique
)
if ($publishedNames.Count -ne 1) {
    throw 'Nome oemNN.inf nao identificado de forma unica.'
}
$publishedName = $publishedNames[0]
$publishedName | Set-Content -LiteralPath $markerPath -Encoding ascii
Write-Host "  Pacote publicado como: $publishedName" -ForegroundColor Green

# 3. Armar o Watchdog em segundo plano
#
# Dois watchdogs simultaneos brigam pelo mesmo dispositivo: o mais antigo dispara
# o rollback (que remove o pacote OEM) no meio do binding do mais novo, e o
# UpdateDriverForPlugAndPlayDevicesW falha com win32=2. Aconteceu em 29/08 18:14,
# com os PIDs 3092 e 908 armados ao mesmo tempo.
$liveWatchdogs = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*watch-bc250mcdm-lab.ps1*' })
if ($liveWatchdogs.Count -gt 0) {
    Write-Host ''
    Write-Host 'ERRO: ja existe watchdog de laboratorio armado:' -ForegroundColor Red
    foreach ($w in $liveWatchdogs) { Write-Host ("  PID {0}" -f $w.ProcessId) -ForegroundColor Red }
    Write-Host 'Confirme o binding anterior, deixe-o expirar, ou encerre o processo:' -ForegroundColor Yellow
    Write-Host ("  Stop-Process -Id {0} -Force" -f $liveWatchdogs[0].ProcessId) -ForegroundColor Yellow
    throw 'Binding abortado: watchdog concorrente ativo.'
}
$liveSafeGuard = Get-ScheduledTask -TaskName 'BC250-SafeBindGuard' `
    -ErrorAction SilentlyContinue
if ($null -ne $liveSafeGuard -and [string]$liveSafeGuard.State -eq 'Running') {
    throw 'Binding abortado: guardiao SYSTEM de outra corrida ainda esta ativo.'
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-lab-binding"
New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null
# SYS e PDB precisam sobreviver juntos ao proximo build. Na Corrida 43 o PDB
# foi criado na pasta, mas seu hash real divergiu do manifesto e o symchk o
# recusou; alem disso, sem o SYS o debugger nem sequer carregava a imagem.
# Copiar o par antes do bind e conferir o DESTINO, nao apenas a origem.
$evidenceSys = Join-Path $evidenceDir 'bc250mcdm.sys'
$evidencePdb = Join-Path $evidenceDir 'bc250mcdm.pdb'
Copy-Item -LiteralPath $packageSys -Destination $evidenceSys -Force
Copy-Item -LiteralPath $packagePdb -Destination $evidencePdb -Force
$evidenceSysHash = (Get-FileHash -LiteralPath $evidenceSys -Algorithm SHA256).Hash
$evidencePdbHash = (Get-FileHash -LiteralPath $evidencePdb -Algorithm SHA256).Hash
if ($evidenceSysHash -ne [string]$manifest.PackageSysSha256) {
    throw "SYS arquivado divergiu do manifesto: $evidenceSysHash"
}
if ($evidencePdbHash -ne [string]$manifest.PackagePdbSha256) {
    throw "PDB arquivado divergiu do manifesto: $evidencePdbHash"
}
[ordered]@{
    sys_file = 'bc250mcdm.sys'
    sys_sha256 = $evidenceSysHash
    package_sys_manifest_sha256 = [string]$manifest.PackageSysSha256
    pdb_file = 'bc250mcdm.pdb'
    pdb_sha256 = $evidencePdbHash
    package_pdb_manifest_sha256 = [string]$manifest.PackagePdbSha256
    captured_at = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath `
    (Join-Path $evidenceDir 'private-symbols.json') -Encoding UTF8
# B2: logs residuais da UMD em C:\Windows\Temp saem do caminho para isolar este bind.
# DX.50: eram APAGADOS. Depois de um travamento duro o binder morre antes da
# coleta FINAL, e o log do dwm.exe da corrida travada so' existe aqui -- o bind
# seguinte o destruia (Corridas 94 e 95). Agora sao MOVIDOS para a evidencia
# deste bind, com hash, antes de qualquer outra coisa tocar neles.
$residualUmdDir = Join-Path $evidenceDir 'umd-logs-residuais-do-bind-anterior'
$residualUmd = @(Get-ChildItem -LiteralPath 'C:\Windows\Temp' -Filter 'BC250_UMD_LOG_*.txt' -File -ErrorAction SilentlyContinue)
if ($residualUmd.Count -gt 0) {
    New-Item -ItemType Directory -Path $residualUmdDir -Force | Out-Null
    foreach ($residual in $residualUmd) {
        try {
            $residualDest = Join-Path $residualUmdDir $residual.Name
            # COPIA, nao mover: mover levava junto a ACL de C:\Windows\Temp e a
            # sessao de analise (sem elevacao) nao conseguia ler o arquivo
            # (medido na Corrida 96). A copia herda a ACL da pasta de evidencia.
            # A origem so' sai depois de o hash da copia bater.
            Copy-Item -LiteralPath $residual.FullName -Destination $residualDest -Force -ErrorAction Stop
            $residualHash = (Get-FileHash -LiteralPath $residualDest -Algorithm SHA256).Hash
            if ($residualHash -ne (Get-FileHash -LiteralPath $residual.FullName -Algorithm SHA256).Hash) {
                throw 'hash da copia difere da origem; origem mantida'
            }
            '{0}  {1}  {2}  {3:o}' -f $residualHash,
                $residual.Name, $residual.Length, $residual.LastWriteTime |
                Add-Content -LiteralPath (Join-Path $residualUmdDir 'SHA256SUMS.txt') -Encoding UTF8
            Remove-Item -LiteralPath $residual.FullName -Force -ErrorAction Stop
        } catch {
            Write-Host "AVISO: log residual da UMD nao arquivado ($($residual.Name)): $($_.Exception.Message)"
        }
    }
    Write-Host "Logs residuais da UMD arquivados: $($residualUmd.Count) em $residualUmdDir"
}
# Corrida 189: corte de tempo da coleta de logs da UMD (ver Get-Bc250BindUmdLogs).
$bindUmdLogCutoffUtc = [datetime]::UtcNow

# DX.21: DESPEJO DO IB GRAFICO EM STAGING GRAVAVEL PELO dwm.exe.
# LIDO em 22/09 (icacls): a pasta de evidencia fica no perfil do Daniel e so'
# da acesso a SISTEMA, Administradores e Daniel. O dwm.exe roda como DWM-<n>,
# fora dos tres; o fopen do RADV falhava e a unica linha que dizia isso ia para
# stderr, que o dwm.exe nao tem. Por isso gfx-ib-dump saiu VAZIO nas Corridas
# 35, 36 e 66. C:\Windows\Temp ja' e' onde o dwm.exe grava BC250_UMD_LOG_* com
# sucesso. Armado ANTES do bind porque o RADV resolve o diretorio uma vez por
# processo; copiado para a evidencia na coleta FINAL.
$gfxIbDumpStaging = 'C:\Windows\Temp\BC250_GFX_IB_DUMP'
try {
    if (Test-Path -LiteralPath $gfxIbDumpStaging) {
        Get-ChildItem -LiteralPath $gfxIbDumpStaging -File -ErrorAction SilentlyContinue |
            Remove-Item -Force -ErrorAction SilentlyContinue
    } else {
        New-Item -ItemType Directory -Path $gfxIbDumpStaging -Force | Out-Null
    }
    # *S-1-1-0 = Todos; *S-1-5-90-0 = Window Manager Group (DWM-<n>).
    & icacls.exe $gfxIbDumpStaging /grant '*S-1-1-0:(OI)(CI)M' '*S-1-5-90-0:(OI)(CI)M' | Out-Null
    $umdRegistryPath = 'HKLM:\SOFTWARE\BC250\Umd'
    if (-not (Test-Path -LiteralPath $umdRegistryPath)) {
        New-Item -Path $umdRegistryPath -ErrorAction Stop | Out-Null
    }
    Set-ItemProperty -Path $umdRegistryPath -Name 'GfxIbDumpDir' -Value $gfxIbDumpStaging -Type String -ErrorAction Stop
    Write-Host ("  GfxIbDumpDir armado em staging gravavel pelo DWM: {0}" -f $gfxIbDumpStaging) -ForegroundColor Cyan
} catch {
    Write-Host ("  AVISO: staging do despejo de IB falhou: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
}
$displayLogStartOffset = if (Test-Path -LiteralPath $displayLogPath -PathType Leaf) {
    [int64](Get-Item -LiteralPath $displayLogPath).Length
} else { [int64]0 }
[ordered]@{
    path = $displayLogPath
    start_offset = $displayLogStartOffset
    captured_at = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath `
    (Join-Path $evidenceDir 'display-log-baseline.json') -Encoding UTF8

# DX.56: o arquivo binario e cumulativo; este offset separa a corrida atual.
$pagingTraceStartOffset = if (Test-Path -LiteralPath $pagingTracePath -PathType Leaf) {
    [int64](Get-Item -LiteralPath $pagingTracePath).Length
} else { [int64]0 }
[ordered]@{
    path = $pagingTracePath
    start_offset = $pagingTraceStartOffset
    captured_at = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath `
    (Join-Path $evidenceDir 'paging-trace-baseline.json') -Encoding UTF8

# C2: antes de qualquer coisa deste bind, resgata do log cumulativo o trecho que
# corridas anteriores perderam. MEDIDO em 10/09: seis pastas de evidencia do
# mesmo dia estavam truncadas, e a da corrida do marco (11:21) perdeu 438.038
# bytes que continham o FenceId=770 e a prova de visibilidade do HDP -- a
# auditoria daquela corrida foi feita sem a evidencia decisiva. O baseline deste
# bind, ja gravado acima, delimita o fim exato do trecho da corrida anterior.
$tailRecovery = Join-Path $PSScriptRoot 'recover-bc250mcdm-lost-tail.ps1'
if (Test-Path -LiteralPath $tailRecovery -PathType Leaf) {
    try {
        & powershell.exe -ExecutionPolicy Bypass -NoProfile -File $tailRecovery 2>&1 |
            Tee-Object -FilePath (Join-Path $evidenceDir 'tail-recovery-console.txt') |
            ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
    } catch {
        Write-Host "  AVISO: recuperacao de trecho falhou: $($_.Exception.Message)" -ForegroundColor Yellow
    }
} else {
    Write-Host "  AVISO: recover-bc250mcdm-lost-tail.ps1 ausente; trechos perdidos NAO serao resgatados." -ForegroundColor Yellow
}

$watchdogToken = $null
$collectionDeadlineUtc = $null
if ($DisableWatchdog) {
    Write-Host "`n[2/4] Watchdog omitido por aceite explicito; evidencia criada." -ForegroundColor Yellow
} else {
    Write-Host "`n[2/4] Armando Watchdog de Rollback ($WatchdogTimeoutSeconds s)..." -ForegroundColor Yellow
    $watchdogToken = [guid]::NewGuid().ToString('D')
    # C3.16: toda corrida destrutiva usa o guardiao SYSTEM persistente. O
    # PowerShell filho usado antes desapareceu durante as duas travadas de
    # 12/09 e deixou watchdog-state.json eternamente em ARMED.
    $taskName = 'BC250-SafeBindGuard'
    $deadlineTaskName = 'BC250-SafeBindDeadline'
    $guardDir = 'C:\BC250_SAFE_GUARD'
    $guardScript = Join-Path $guardDir 'watch-bc250mcdm-safe-bind.ps1'
    $guardDwmRestore = Join-Path $guardDir 'restore-dwm-crash-capture.ps1'
    $configPath = Join-Path $guardDir 'armed.json'
    New-Item -ItemType Directory -Path $guardDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'watch-bc250mcdm-safe-bind.ps1') `
        -Destination $guardScript -Force
    if ($EnableDwmCrashCapture) {
        Copy-Item -LiteralPath $restoreDwmCapture -Destination $guardDwmRestore -Force
    }
    $armedAtUtc = [datetime]::UtcNow
    $deadlineUtc = $armedAtUtc.AddSeconds($WatchdogTimeoutSeconds)
    # A coleta fecha pelo mesmo relogio absoluto do watchdog. Dez segundos
    # ficam reservados para copiar logs/dumps antes da recovery.
    $collectionDeadlineUtc = $deadlineUtc.AddSeconds(-10)
    $guardConfig = [ordered]@{
        schema_version = 1; task_name = $taskName
        deadline_task_name = $deadlineTaskName; token = $watchdogToken
        instance_id = [string]$device.PNPDeviceID
        timeout_seconds = $WatchdogTimeoutSeconds; evidence_dir = $evidenceDir
        short_watchdog = $(if ($shortWatchdog) { 1 } else { 0 })
        recovery_path = $recoveryPath
        recovery_sha256 = (Get-FileHash $recoveryPath -Algorithm SHA256).Hash
        expected_sys_sha256 = [string]$manifest.PackageSysSha256
        expected_inf_sha256 = [string]$manifest.PackageInfSha256
        expected_driver_version = [string]$manifest.DriverVer
        armed_boot_ticks = [long](Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
        armed_at_utc_ticks = [long]$armedAtUtc.Ticks
        deadline_utc_ticks = [long]$deadlineUtc.Ticks
        dwm_capture_enabled = [bool]$EnableDwmCrashCapture
        dwm_restore_script = $(if ($EnableDwmCrashCapture) { $guardDwmRestore } else { $null })
        dwm_restore_sha256 = $(if ($EnableDwmCrashCapture) {
            (Get-FileHash -LiteralPath $guardDwmRestore -Algorithm SHA256).Hash
        } else { $null })
        automatic_recovery_reboot = $true; hardware_validated = $false
    }
    # DX.45b host (Corrida 88): travamento duro ~40 s depois do bind deixou
    # armed.json com 1178 bytes TODOS ZERO e o XML das duas tarefas ausente de
    # System32\Tasks -- o guardiao nao tinha config nem tarefa no boot seguinte
    # e o rollback nao aconteceu. Mesma classe do _last_lab_binding.json da 81.
    # Config em WRITE-THROUGH; depois do registro das tarefas, flush do hive
    # HKLM e do cache do volume antes de o bind prosseguir.
    $armedBytes = [System.Text.UTF8Encoding]::new($true).GetPreamble() +
        [System.Text.UTF8Encoding]::new($false).GetBytes(($guardConfig | ConvertTo-Json -Depth 5))
    $armedStream = [System.IO.FileStream]::new($configPath,
        [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None, 4096, [System.IO.FileOptions]::WriteThrough)
    try {
        $armedStream.Write($armedBytes, 0, $armedBytes.Length)
        $armedStream.Flush($true)
    } finally {
        $armedStream.Dispose()
    }
    $guardConfig | ConvertTo-Json -Depth 5 | Set-Content `
        (Join-Path $evidenceDir 'safe-bind-watchdog-config.json') -Encoding UTF8
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $deadlineTaskName -Confirm:$false -ErrorAction SilentlyContinue
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument `
        "-NoProfile -ExecutionPolicy Bypass -File `"$guardScript`" -ConfigPath `"$configPath`" -Execute"
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $taskPrincipal = New-ScheduledTaskPrincipal -UserId SYSTEM -LogonType ServiceAccount -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 15) `
        -MultipleInstances IgnoreNew -StartWhenAvailable
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
        -Principal $taskPrincipal -Settings $settings -Force | Out-Null
    $deadlineAction = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument `
        "-NoProfile -ExecutionPolicy Bypass -File `"$guardScript`" -ConfigPath `"$configPath`" -Execute -DeadlineEnforcer"
    $deadlineTrigger = New-ScheduledTaskTrigger -Once -At $deadlineUtc.ToLocalTime()
    Register-ScheduledTask -TaskName $deadlineTaskName -Action $deadlineAction `
        -Trigger $deadlineTrigger -Principal $taskPrincipal -Settings $settings -Force | Out-Null
    # DX.45b host: as tarefas vivem no hive SOFTWARE (TaskCache) e em
    # System32\Tasks; os dois sao gravados preguicosamente pelo Windows.
    if (-not ('Bc250Durable.Reg' -as [type])) {
        Add-Type -Namespace Bc250Durable -Name Reg -MemberDefinition `
            '[System.Runtime.InteropServices.DllImport("advapi32.dll")] public static extern int RegFlushKey(System.IntPtr hKey);'
    }
    $regFlush = [Bc250Durable.Reg]::RegFlushKey([IntPtr][long]-2147483646)  # HKEY_LOCAL_MACHINE
    $volFlushOk = $true
    try { Write-VolumeCache -DriveLetter C -ErrorAction Stop } catch { $volFlushOk = $false }
    if ($regFlush -ne 0 -or -not $volFlushOk -or
        -not (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) -or
        ((Get-Content -LiteralPath $configPath -Raw) -notmatch '"task_name"')) {
        throw "Guardiao nao ficou duravel (RegFlushKey=$regFlush VolumeCache=$volFlushOk); bind cancelado antes de tocar o driver."
    }
    Write-Host "[OK] Guardiao duravel: armed.json write-through, HKLM e volume C: descarregados."
    Start-ScheduledTask -TaskName $taskName
    $guardState = Join-Path $evidenceDir 'safe-bind-watchdog-state.json'
    $guardDeadline = (Get-Date).AddSeconds(10)
    $guardObserving = $false
    do {
        Start-Sleep -Milliseconds 250
        if (Test-Path $guardState) {
            try {
                $guardObserving =
                    ((Get-Content $guardState -Raw | ConvertFrom-Json).state -eq 'OBSERVING')
            } catch { $guardObserving = $false }
        }
    } until ($guardObserving -or (Get-Date) -ge $guardDeadline)
    if (-not $guardObserving) {
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $deadlineTaskName -Confirm:$false -ErrorAction SilentlyContinue
        throw 'Watchdog SYSTEM nao entrou em OBSERVING.'
    }
    Write-Host "  Watchdog SYSTEM ativo; reboot automatico autorizado." -ForegroundColor Green

    if ($EnableDwmCrashCapture) {
        Write-Warning ('DWM LOCALDUMPS ARMADO PARA ESTA CORRIDA: um fail-fast do dwm.exe ' +
            'vai gerar dump completo; a configuracao WER sera restaurada transacionalmente.')
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $armDwmCapture `
            -EvidenceDir $evidenceDir -Token $watchdogToken -Execute
        if ($LASTEXITCODE -ne 0) {
            throw 'Binding bloqueado: LocalDumps do dwm.exe nao armou.'
        }
    } else {
        Write-Host '  DWM LocalDumps: DESARMADO (default fail-closed).' -ForegroundColor DarkYellow
    }
}

# DriverEntry e StartDevice leem estes valores durante o proprio binding. A
# politica precisa estar aplicada e relida ANTES de carregar o novo SYS; gravar
# depois criava uma corrida e podia exercitar um modo diferente do solicitado.
$paramKey = "HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters"
if (-not (Test-Path -LiteralPath $paramKey)) {
    New-Item -Path $paramKey -Force | Out-Null
}
$requestedPolicy = [ordered]@{
    SkipHwBringup = 0
    ShaderDispatch = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    CpSoftReset = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    Vmid0PageTable = 0
    SdmaAutoCtxSw = $(if ($EnableShaderDispatch) { 0 } else { 1 })
    PersistentSubmission = $(if ($EnablePersistentSubmission) { 1 } else { 0 })
    GpuvaLabPromotion = $(if ($EnableGpuvaLabPromotion) { 1 } else { 0 })
    SdmaPlanoAFallback = $(if ($EnableSdmaPlanoAFallback) { 1 } else { 0 })
    # B.1: entra na politica pre-bind pelo mesmo motivo do C3.9 -- o KMD le o
    # valor UMA VEZ no StartDevice, e o INF declara o campo com NOCLOBBER
    # justamente para nao apagar o que esta politica escreveu.
    GfxRingPreflight = $(if ($EnableGfxRingPreflight) { 1 } else { 0 })
    GfxRingBringup = $GfxRingBringupLevel
    GfxRingStage = $GfxRingStage
    # C3.9: entra na politica pre-bind porque a chave de servico so' nasce aqui,
    # e o KMD le o valor UMA VEZ no StartDevice. O INF declara o campo com
    # NOCLOBBER justamente para nao apagar o que esta politica escreveu.
    Node1JournalGatePerProcess = $(if ($EnableNode1JournalGatePerProcess) { 1 } else { 0 })
    RenderNode = $(if ($EnableRenderNode) { 1 } elseif ($EnablePhysicalRenderOnly) { 1 } else { 0 })
    RenderOnlyLab = $(if ($EnablePhysicalRenderOnly) { 1 } else { 0 })
}
$policyBefore = [ordered]@{}
$parametersBefore = Get-ItemProperty -LiteralPath $paramKey -ErrorAction Stop
foreach ($entry in $requestedPolicy.GetEnumerator()) {
    $oldProperty = $parametersBefore.PSObject.Properties[$entry.Key]
    $policyBefore[$entry.Key] = if ($null -eq $oldProperty) {
        $null
    } else {
        $oldProperty.Value
    }
    Set-ItemProperty -LiteralPath $paramKey -Name $entry.Key `
        -Value ([uint32]$entry.Value) -Type DWord -Force
    $readback = Get-ItemPropertyValue -LiteralPath $paramKey -Name $entry.Key `
        -ErrorAction Stop
    if ([uint32]$readback -ne [uint32]$entry.Value) {
        throw "Readback pre-bind falhou para $($entry.Key)."
    }
}
[ordered]@{
    captured_at = (Get-Date).ToString('o')
    phase = 'PRE_BIND_POLICY_APPLIED_READBACK_OK'
    before = $policyBefore
    requested = $requestedPolicy
    hardware_validated = $false
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
    (Join-Path $evidenceDir 'prebind-policy.json') -Encoding UTF8
Write-Host "  Politica pre-bind aplicada e verificada." -ForegroundColor Green
Write-Host ("  RenderNode={0}; PersistentSubmission={1}; GpuvaLabPromotion={2}; SdmaPlanoAFallback={3}; Node1JournalGatePerProcess={4}." -f
    $requestedPolicy.RenderNode, $requestedPolicy.PersistentSubmission,
    $requestedPolicy.GpuvaLabPromotion, $requestedPolicy.SdmaPlanoAFallback,
    $requestedPolicy.Node1JournalGatePerProcess)
if ($EnableNode1JournalGatePerProcess) {
    Write-Host "  AVISO: Node1JournalGatePerProcess=1 -- relaxamento do gate do Node 1, NAO VALIDADO EM HARDWARE." -ForegroundColor Yellow
}
if ($GfxRingBringupLevel -gt 0) {
    Write-Host ("  GfxRingBringup={0} -- ENSAIO DO ANEL GRAFICO: o driver vai ESCREVER em CP_RB0." -f $GfxRingBringupLevel) -ForegroundColor Cyan
    if ($GfxRingBringupLevel -ge 5) {
        Write-Host "    Level 5: pipeline 3D completo com Draw Initiator (DRAW_INDEX_AUTO) e Fence. Risco maximo." -ForegroundColor Yellow
    } elseif ($GfxRingBringupLevel -ge 4) {
        $stageDesc = switch ($GfxRingStage) {
            1 { "Sub-etapa 4.1: Shaders (Bloco 1) + ColorTarget 0 (Bloco 2) + TargetMask (Bloco 3) + Fence (Bloco 7)" }
            2 { "Sub-etapa 4.2: + Viewport & Scissor (Bloco 4) + Fence (Bloco 7)" }
            3 { "Sub-etapa 4.3: + Primitive Assembler (Bloco 5) + Fence (Bloco 7) (SkipDraw=1)" }
            4 { "Sub-etapa 4.4: + Draw Initiator (Bloco 6: DRAW_INDEX_AUTO) + Fence (Bloco 7)" }
            default { "Sub-etapa 4.1: Shaders + ColorTarget 0 + TargetMask + Fence" }
        }
        Write-Host ("    Level 4: isolamento progressivo PM4 3D -- Stage {0} ({1}). Risco alto." -f $GfxRingStage, $stageDesc) -ForegroundColor Yellow
    } elseif ($GfxRingBringupLevel -ge 3) {
        Write-Host "    Level 3: submissao de SH_REG (shaders VS+PS) no anel grafico. Risco alto." -ForegroundColor Yellow
    } elseif ($GfxRingBringupLevel -ge 2) {
        Write-Host "    Level 2: desalta CE/PFP/ME e LIGA o anel. Risco maior que o level 1." -ForegroundColor Yellow
    } else {
        Write-Host "    Level 1: apenas configura e confere; as micro-engines seguem HALTED." -ForegroundColor Cyan
    }
    Write-Host "    O ROLLBACK e' sempre executado e conferido por readback."
    Write-Host "    Procure: [GfxRing] BRINGUP veredito / readback / partida / rollback"
    Write-Host "    NAO VALIDADO EM HARDWARE." -ForegroundColor Yellow
}
if ($EnableGfxRingPreflight) {
    Write-Host "  GfxRingPreflight=1 -- o anel grafico CP_RB0 sera FOTOGRAFADO (somente leitura)." -ForegroundColor Cyan
    Write-Host "    Procure no log, nesta ordem:" -ForegroundColor Cyan
    Write-Host "      [StartDevice] GfxRingPreflight=1 (present=1 compiled=1)"
    Write-Host "      [GfxRing] PREFLIGHT OK: Reads=7 Writes=0 (transporte Writes=0)"
    Write-Host "      [GfxRing] CP_RB0 COMO ENCONTRADO: ..."
    Write-Host "      [GfxRing] CONTRA O CATALOGO: ... -> CONFERE|DIVERGE"
    Write-Host "    Writes diferente de 0 em qualquer dos dois contadores e DEFEITO NOSSO, nao medicao." -ForegroundColor Yellow
    Write-Host "    compiled=0 significa imagem de producao: o probe fisico nao existe nela." -ForegroundColor Yellow
    Write-Host "    NAO VALIDADO EM HARDWARE." -ForegroundColor Yellow
}

# Armar ou limpar D3D11LabPromotion da UMD
if ($EnableD3D11LabPromotion) {
    if (-not (Test-Path -LiteralPath $umdRegKey)) {
        New-Item -Path $umdRegKey -Force | Out-Null
    }
    Set-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11LabPromotion' -Value 1 -Type DWord -Force
    $readbackD3D11 = Get-ItemPropertyValue -LiteralPath $umdRegKey -Name 'D3D11LabPromotion' -ErrorAction Stop
    if ([uint32]$readbackD3D11 -ne 1) {
        throw 'Readback pre-bind falhou para D3D11LabPromotion.'
    }
    Write-Host '  D3D11LabPromotion=1 armado em HKLM:\SOFTWARE\BC250\Umd' -ForegroundColor Green
} else {
    if (Test-Path -LiteralPath $umdRegKey) {
        Remove-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11LabPromotion' -ErrorAction SilentlyContinue
    }
}

# C1: armar ou limpar D3D11DrawBltSubstitution da UMD (fail-closed por default)
if ($EnableD3D11DrawBltSubstitution) {
    if (-not (Test-Path -LiteralPath $umdRegKey)) {
        New-Item -Path $umdRegKey -Force | Out-Null
    }
    Set-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11DrawBltSubstitution' -Value 1 -Type DWord -Force
    $readbackSubst = Get-ItemPropertyValue -LiteralPath $umdRegKey -Name 'D3D11DrawBltSubstitution' -ErrorAction Stop
    if ([uint32]$readbackSubst -ne 1) {
        throw 'Readback pre-bind falhou para D3D11DrawBltSubstitution.'
    }
    Write-Host '  D3D11DrawBltSubstitution=1 armado -- Draw sera SUBSTITUIDO por copia 1:1 no SDMA.' -ForegroundColor Yellow
    Write-Host '  ATENCAO: substituicao NAO e rasterizacao; pixel na tela nao prova pipeline 3D.' -ForegroundColor Yellow
} else {
    if (Test-Path -LiteralPath $umdRegKey) {
        Remove-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11DrawBltSubstitution' -ErrorAction SilentlyContinue
    }
}

# C3: armar ou limpar D3D11ShaderTranslator da UMD
if ($EnableD3D11ShaderTranslator) {
    if (-not (Test-Path -LiteralPath $umdRegKey)) {
        New-Item -Path $umdRegKey -Force | Out-Null
    }
    $resolvedTranslatorDll = $D3D11ShaderTranslatorDllPath
    if ([string]::IsNullOrWhiteSpace($resolvedTranslatorDll)) {
        if (Test-Path -LiteralPath 'C:\BC250_LIB\libvkd3d-shader-1.dll' -PathType Leaf) {
            $resolvedTranslatorDll = 'C:\BC250_LIB\libvkd3d-shader-1.dll'
        } else {
            $foundDll = Get-ChildItem -Path $env:TEMP -Filter "libvkd3d-shader-1.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
            if ($foundDll) { $resolvedTranslatorDll = $foundDll }
        }
    }
    if (-not $resolvedTranslatorDll -or -not (Test-Path -LiteralPath $resolvedTranslatorDll -PathType Leaf)) {
        throw "libvkd3d-shader-1.dll nao encontrada para D3D11ShaderTranslator. Especifique -D3D11ShaderTranslatorDllPath."
    }
    Set-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11ShaderTranslator' -Value 1 -Type DWord -Force
    Set-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11ShaderTranslatorPath' -Value $resolvedTranslatorDll -Type String -Force
    Write-Host "  D3D11ShaderTranslator=1 armado ($resolvedTranslatorDll)" -ForegroundColor Yellow
} else {
    if (Test-Path -LiteralPath $umdRegKey) {
        Remove-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11ShaderTranslator' -ErrorAction SilentlyContinue
        Remove-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11ShaderTranslatorPath' -ErrorAction SilentlyContinue
    }
}

# 4. Executar o binding via NewDev ou pnputil
Write-Host "`n[3/4] Vinculando driver ao hardware..." -ForegroundColor Yellow
if (-not ('Bc250LabNewDev.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Bc250LabNewDev
{
    public static class NativeMethods
    {
        [DllImport("newdev.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool UpdateDriverForPlugAndPlayDevicesW(
            IntPtr hwndParent,
            string hardwareId,
            string fullInfPath,
            UInt32 installFlags,
            [MarshalAs(UnmanagedType.Bool)] ref bool rebootRequired);
    }
}
'@
}

$installFlagForce = [uint32]0x00000001
$rebootRequired = $false
$nativeOk = [Bc250LabNewDev.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW(
    [IntPtr]::Zero,
    $hardwareId,
    $packageInf,
    $installFlagForce,
    [ref]$rebootRequired)

$win32Error = if ($nativeOk) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
Write-Host "  UpdateDriverForPlugAndPlayDevicesW: success=$nativeOk win32=$win32Error reboot=$rebootRequired"

if (-not $nativeOk) {
    # 'pnputil /install /driver <oemN.inf>' nao existe: so' imprime a tela de
    # uso. Parecia fallback e nao era.
    Write-Warning "NewDev falhou (win32=$win32Error), tentando fallback valido..."
    $fallback = (& pnputil.exe /add-driver $packageInf /install 2>&1) -join "`n"
    $fallback | Write-Host
    if ($LASTEXITCODE -ne 0) { Write-Warning "Fallback pnputil tambem falhou: exit=$LASTEXITCODE" }
}

# 5. Confirmar que nenhum instalador sobrescreveu a politica pre-bind.
Write-Host "`n[4/4] Verificando parametros efetivos pos-binding..." -ForegroundColor Yellow
foreach ($entry in $requestedPolicy.GetEnumerator()) {
    $readback = Get-ItemPropertyValue -LiteralPath $paramKey -Name $entry.Key `
        -ErrorAction Stop
    if ([uint32]$readback -ne [uint32]$entry.Value) {
        throw "Politica pos-binding divergiu em $($entry.Key)."
    }
}
Write-Host "  Politica pos-binding coincide com a politica pre-bind." -ForegroundColor Green
Write-Host ("  RenderNode={0}; ShaderDispatch={1}; PersistentSubmission={2}; GPUVA={3}." -f
    $requestedPolicy.RenderNode, $requestedPolicy.ShaderDispatch,
    $requestedPolicy.PersistentSubmission, $requestedPolicy.GpuvaLabPromotion)
if ($requestedPolicy.GpuvaLabPromotion -eq 1) {
    # O readback acima e' a prova de que o NOCLOBBER do INF funcionou: a
    # promocao armada antes do install sobreviveu ao AddReg do instalador.
    Write-Host '  GpuvaLabPromotion sobreviveu ao instalador (NOCLOBBER confirmado).' -ForegroundColor Green
}

# Verificar estado do dispositivo
Start-Sleep -Seconds 3
$devAfter = Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like "$hardwareId*" }
Write-Host "`nEstado do dispositivo:" -ForegroundColor Cyan
Write-Host "  Service: $($devAfter.Service)"
Write-Host "  Status: $($devAfter.Status)"
Write-Host "  ConfigManagerErrorCode: $($devAfter.ConfigManagerErrorCode)"

$driverInstalledPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$pkgSysPath = Join-Path $root 'driver\wdk\package\output_lab\bc250mcdm.sys'
if (Test-Path $driverInstalledPath) {
    $instHash = (Get-FileHash -LiteralPath $driverInstalledPath -Algorithm SHA256).Hash
    $pkgHash = (Get-FileHash -LiteralPath $pkgSysPath -Algorithm SHA256).Hash
    Write-Host "  Hash instalado: $($instHash.Substring(0,16))"
    Write-Host "  Hash do pacote: $($pkgHash.Substring(0,16))"
    if ($instHash -eq $pkgHash) {
        Write-Host "  Binario em System32\drivers ATUALIZADO com sucesso!" -ForegroundColor Green
    } else {
        Write-Host "  AVISO: Binario no System32 nao atualizou automaticamente; copiando..." -ForegroundColor Yellow
        try {
            Copy-Item -LiteralPath $pkgSysPath -Destination $driverInstalledPath -Force -ErrorAction Stop
            Write-Host "  Binario copiado com sucesso para System32\drivers!" -ForegroundColor Green
        } catch {
            Write-Warning "Falha ao copiar para System32 (servico em execucao pelo kernel): $_"
        }
    }
}

# Vincular NAO e' ter vinculado: o script ja' anunciou "INSTALADO!" com o
# dispositivo em BasicDisplay e, depois, sem servico nenhum. Conferir aqui, e
# desarmar o proprio watchdog se falhou - deixa-lo armado so' produz mais um
# rollback em cima de um estado que ja' esta quebrado.
if ($devAfter.Service -ne 'bc250mcdm') {
    Write-Host ''
    Write-Host "BINDING NAO PEGOU: dispositivo em '$($devAfter.Service)', esperado bc250mcdm." -ForegroundColor Red
    if ($win32Error -eq 1072) {
        Write-Host 'win32=1072 = ERROR_SERVICE_MARKED_FOR_DELETE.' -ForegroundColor Red
        Write-Host 'O servico bc250mcdm esta pendente de exclusao e nao pode ser recriado' -ForegroundColor Yellow
        Write-Host 'ate' + [char]39 + ' o sistema soltar a referencia. Isso exige REINICIAR.' -ForegroundColor Yellow
    }
    if (-not $DisableWatchdog) {
        Write-Host 'Desarmando o watchdog desta tentativa para nao empilhar rollbacks.' -ForegroundColor Yellow
    }
    # C3.16: todas as corridas usam a tarefa SYSTEM. Um bind que falhou antes
    # de tocar no dispositivo nao pode deixar uma recovery orfa no boot.
    if (-not $DisableWatchdog) {
        $disarmOk = Disarm-Bc250SafeGuard 'BC250-SafeBindGuard' $evidenceDir
        Write-Host ("  Guardiao SYSTEM desarmado (limpeza_ok={0})." -f $disarmOk) -ForegroundColor Yellow
    }
    if (Test-Path -LiteralPath $umdRegKey) {
        Remove-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11LabPromotion' -ErrorAction SilentlyContinue
        Remove-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11DrawBltSubstitution' -ErrorAction SilentlyContinue
        Remove-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11ShaderTranslator' -ErrorAction SilentlyContinue
        Remove-ItemProperty -LiteralPath $umdRegKey -Name 'D3D11ShaderTranslatorPath' -ErrorAction SilentlyContinue
    }
    throw "Binding de laboratorio falhou (win32=$win32Error)."
}

# Captura imediata da telemetria, ANTES de o watchdog poder disparar.
#
# No ciclo 3.30 o watchdog expirou e fez rollback automatico antes de o
# confirm rodar: o servico bc250mcdm sumiu levando a chave Probe junto e a
# evidencia canonica do ensaio se perdeu. O ensaio e caro e so cabe uma
# submissao por boot; a captura nao pode depender de uma acao humana dentro
# da janela do watchdog.
$probeKeyPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
try {
    if (Test-Path -LiteralPath $probeKeyPath) {
        $probeSnapshot = Get-ItemProperty -LiteralPath $probeKeyPath -ErrorAction Stop
        $probeOrdered = [ordered]@{}
        foreach ($probeProperty in ($probeSnapshot.PSObject.Properties |
                Where-Object { $_.Name -notlike 'PS*' } |
                Sort-Object Name)) {
            $probeOrdered[$probeProperty.Name] = $probeProperty.Value
        }
        $probeOrdered | ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath (Join-Path $evidenceDir 'probe-registry-at-bind.json') -Encoding UTF8
        $probeRegPath = 'HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
        & reg.exe export $probeRegPath (Join-Path $evidenceDir 'probe-registry-at-bind.txt') /y | Out-Null
        Write-Host ("  Telemetria capturada no bind: {0} valores." -f $probeOrdered.Count) -ForegroundColor Cyan
    } else {
        Write-Host '  AVISO: chave de telemetria ausente logo apos o binding.' -ForegroundColor Yellow
    }
} catch {
    # Best-effort: a captura nunca pode derrubar o ensaio nem o watchdog.
    Write-Host ("  AVISO: falha ao capturar telemetria no bind: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
}

# RADV_WDDM_COMPUTE_TRIPOD: uma unica corrida, aqui, com a telemetria do bind ja
# arquivada e o watchdog ainda armado. Depois deste ponto a tela pode estar
# apagada e nao existe operador para lancar nada.
if ($RunRadvComputeTripod -or $RunRadvPresentTripod -or $RunVulkanCube -or $RunDxvkD3D11 -or $RunUmdNode1Tripod -or $RunMarco3Graphics -or $RunMarco4Cube -or $RunD3D11Native) {
    # Nao misturar duas experiencias caras. O RADV/UMD so pode comecar depois que
    # ESTE bind (nao linhas antigas do log global) tiver chegado aos marcos necessarios.
    # Para o ensaio D3D11 nativo, a aplicacao cria sua propria janela e SwapChain via DXGI,
    # necessitando que o VidPn esteja comitado e o SourceAddress definido para o scanout do GOP.
    # O timeout de 45 s garante convergencia com folga mesmo sob carga do DWM.
    $displayGateTimeout = if ($RunD3D11Native -or $RunMarco3Graphics -or $RunMarco4Cube) { 45 } else { 30 }
    $displayGateDeadline = (Get-Date).AddSeconds($displayGateTimeout)
    $displayDelta = ''
    $displayPrimary = $false
    $displayCommit = $false
    $displayAddress = $false
    do {
        $displayDelta = Get-Bc250DisplayLogDelta $displayLogPath $displayLogStartOffset
        $displayPrimary = $displayDelta -match '\[GetStdAllocData\] SharedPrimary:'
        $displayCommit = $displayDelta -match '=== CommitVidPn Entered:'
        $displayAddress = $displayDelta -match '=== SetVidPnSourceAddress '
        $gateSatisfied = if ($RunD3D11Native) {
            ($displayCommit -and $displayAddress)
        } else {
            ($displayPrimary -and $displayCommit -and $displayAddress)
        }
        if (-not $gateSatisfied) {
            Start-Sleep -Milliseconds 250
        }
    } until ($gateSatisfied -or (Get-Date) -ge $displayGateDeadline)
    $displayDelta | Set-Content -LiteralPath `
        (Join-Path $evidenceDir 'display-log-this-bind.txt') -Encoding UTF8
    $displayGatePassed = if ($RunD3D11Native) {
        ($displayCommit -and $displayAddress)
    } else {
        ($displayPrimary -and $displayCommit -and $displayAddress)
    }
    [ordered]@{
        state = $(if ($displayGatePassed) { 'PASSED' } elseif ($RunUmdNode1Tripod) {
            'UMD_NODE1_SKIPPED'
        } elseif ($RunMarco3Graphics) {
            'MARCO3_GRAPHICS_SKIPPED'
        } elseif ($RunMarco4Cube) {
            'MARCO4_CUBE_SKIPPED'
        } elseif ($RunD3D11Native) {
            'D3D11_NATIVE_SKIPPED'
        } elseif ($RunRadvPresentTripod) {
            'RADV_PRESENT_SKIPPED'
        } elseif ($RunVulkanCube) {
            'VULKAN_CUBE_SKIPPED'
        } elseif ($RunDxvkD3D11) {
            'DXVK_D3D11_SKIPPED'
        } else { 'RADV_SKIPPED' })
        shared_primary = $displayPrimary
        commit_vidpn = $displayCommit
        set_vidpn_source_address = $displayAddress
        start_offset = $displayLogStartOffset
        checked_at = (Get-Date).ToString('o')
        hardware_validated = $false
    } | ConvertTo-Json | Set-Content -LiteralPath `
        (Join-Path $evidenceDir 'compute-display-prerequisite-gate.json') -Encoding UTF8

    if ($env:BC250_CAPTURE_MONITOR_EDID -eq '1') {
        try {
            & (Join-Path $PSScriptRoot 'capture-bc250-monitor-edid.ps1') `
                -Phase Bound -EvidenceDir $evidenceDir -SkipWmi
        } catch {
            Write-Warning ("EDID Bound nao capturado: {0}" -f $_.Exception.Message)
        }
    }

    if (-not $displayGatePassed) {
        $experimentName = $(if ($RunUmdNode1Tripod) {
            'B7_UMD_NODE1_TRIPOD'
        } elseif ($RunMarco3Graphics) {
            'MARCO3_GRAPHICS_DRAW'
        } elseif ($RunMarco4Cube) {
            'MARCO4_GRAPHICS_CUBE'
        } elseif ($RunD3D11Native) {
            'D3D11_NATIVE_CONFORMANCE'
        } elseif ($RunRadvPresentTripod) {
            'RADV_WDDM_PRESENT_TRIPOD'
        } elseif ($RunVulkanCube) {
            'VULKAN_WSI_CUBE'
        } elseif ($RunDxvkD3D11) {
            'DXVK_D3D11'
        } else { 'RADV_WDDM_COMPUTE_TRIPOD' })
        Write-Warning ($experimentName + ' NAO EXECUTADO: o display deste bind ' +
            "nao chegou aos requisitos (SharedPrimary=$displayPrimary, CommitVidPn=$displayCommit, " +
            "SetVidPnSourceAddress=$displayAddress) em $displayGateTimeout s. O watchdog permanece armado.")
    } else {
        Write-Host ''
        if ($RunRadvPresentTripod) {
            Write-Host 'Display convergiu; executando UMA corrida RADV_WDDM_PRESENT_TRIPOD sob watchdog...' -ForegroundColor Magenta
            $previousEap = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                $tripodOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
                    -File $radvPresentTripod -EvidenceDir $evidenceDir -Execute 2>&1 |
                    Out-String)
                $tripodExit = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $previousEap
            }
            $tripodOutput | Set-Content -LiteralPath `
                (Join-Path $evidenceDir 'radv-present-tripod-console.txt') -Encoding UTF8
            Write-Host $tripodOutput
            if ($tripodExit -ne 0) {
                Write-Warning ('RADV_WDDM_PRESENT_TRIPOD falhou (exit=' + $tripodExit +
                    '). Nao repita este build; o watchdog permanece armado e a evidencia esta gravada.')
            }
        } elseif ($RunVulkanCube) {
            Write-Host 'Display convergiu; executando UMA corrida VULKAN_WSI_CUBE sob watchdog...' -ForegroundColor Magenta
            $previousEap = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                $cubeTimeout = [math]::Min(180, [math]::Max(30, $WatchdogTimeoutSeconds - 90))
                $cubeOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
                    -File $vulkanCube -EvidenceDir $evidenceDir -Execute `
                    -Frames $VulkanCubeFrames -TimeoutSeconds $cubeTimeout 2>&1 |
                    Out-String)
                $cubeExit = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $previousEap
            }
            $cubeOutput | Set-Content -LiteralPath `
                (Join-Path $evidenceDir 'vulkan-cube-console.txt') -Encoding UTF8
            Write-Host $cubeOutput
            if ($cubeExit -ne 0) {
                Write-Warning ('VULKAN_WSI_CUBE falhou (exit=' + $cubeExit +
                    '). Nao repita este build; o watchdog permanece armado e a evidencia esta gravada.')
            }
        } elseif ($RunDxvkD3D11) {
            Write-Host 'Display convergiu; executando UMA corrida DXVK_D3D11 sob watchdog...' -ForegroundColor Magenta
            $previousEap = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                $dxvkTimeout = [math]::Min(180, [math]::Max(30, $WatchdogTimeoutSeconds - 90))
                $dxvkOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
                    -File $dxvkRunner -EvidenceDir $evidenceDir -Execute `
                    -Frames $DxvkFrames -TimeoutSeconds $dxvkTimeout 2>&1 |
                    Out-String)
                $dxvkExit = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $previousEap
            }
            $dxvkOutput | Set-Content -LiteralPath `
                (Join-Path $evidenceDir 'dxvk-d3d11-console.txt') -Encoding UTF8
            Write-Host $dxvkOutput
            if ($dxvkExit -ne 0) {
                Write-Warning ('DXVK_D3D11 falhou (exit=' + $dxvkExit +
                    '). Nao repita este build; o watchdog permanece armado e a evidencia esta gravada.')
            }
        } elseif ($RunUmdNode1Tripod) {
            Write-Host 'Display convergiu; executando UMA corrida B7_UMD_NODE1_TRIPOD sob watchdog...' -ForegroundColor Magenta
            $previousEap = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                $tripodOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
                    -File $umdNode1Tripod -EvidenceDir $evidenceDir -Execute `
                    -IAcceptOnePhysicalNode1Submission 2>&1 | Out-String)
                $tripodExit = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $previousEap
            }
            $tripodOutput | Set-Content -LiteralPath `
                (Join-Path $evidenceDir 'umd-node1-tripod-console.txt') -Encoding UTF8
            Write-Host $tripodOutput
            if ($tripodExit -ne 0) {
                Write-Warning ('B7_UMD_NODE1_TRIPOD falhou (exit=' + $tripodExit +
                    '). Nao repita este build; o watchdog permanece armado e a evidencia esta gravada.')
            }
        } elseif ($RunMarco3Graphics) {
            Write-Host 'Display convergiu; executando UMA corrida MARCO3_GRAPHICS_DRAW sob watchdog...' -ForegroundColor Magenta
            $previousEap = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                $runnerTimeout = [math]::Min(180, [math]::Max(30, $WatchdogTimeoutSeconds - 90))
                $m3Output = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
                    -File $marco3Runner -EvidenceDir $evidenceDir -Execute `
                    -Frames $Marco3Frames -TimeoutSeconds $runnerTimeout 2>&1 |
                    Out-String)
                $m3Exit = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $previousEap
            }
            $m3Output | Set-Content -LiteralPath `
                (Join-Path $evidenceDir 'marco3-graphics-console.txt') -Encoding UTF8
            Write-Host $m3Output
            if ($m3Exit -ne 0) {
                Write-Warning ('MARCO3_GRAPHICS_DRAW falhou (exit=' + $m3Exit +
                    '). Nao repita este build; o watchdog permanece armado e a evidencia esta gravada.')
            }
        } elseif ($RunMarco4Cube) {
            Write-Host 'Display convergiu; executando UMA corrida MARCO4_GRAPHICS_CUBE sob watchdog...' -ForegroundColor Magenta
            $previousEap = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                $runnerTimeout = [math]::Min(180, [math]::Max(30, $WatchdogTimeoutSeconds - 90))
                $m4Output = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
                    -File $marco4Runner -EvidenceDir $evidenceDir -Execute `
                    -Frames $Marco4Frames -TimeoutSeconds $runnerTimeout 2>&1 |
                    Out-String)
                $m4Exit = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $previousEap
            }
            $m4Output | Set-Content -LiteralPath `
                (Join-Path $evidenceDir 'marco4-graphics-cube-console.txt') -Encoding UTF8
            Write-Host $m4Output
            if ($m4Exit -ne 0) {
                Write-Warning ('MARCO4_GRAPHICS_CUBE falhou (exit=' + $m4Exit +
                    '). Nao repita este build; o watchdog permanece armado e a evidencia esta gravada.')
            }
        } elseif ($RunD3D11Native) {
            Write-Host 'Display convergiu; executando UMA corrida D3D11_NATIVE_CONFORMANCE sob watchdog...' -ForegroundColor Magenta
            $previousEap = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                if ($D3D11StartDelaySeconds -gt 0) {
                    Write-Host ("  Atraso do harness D3D11: {0} s (Corrida 143)." -f $D3D11StartDelaySeconds) -ForegroundColor Yellow
                    Start-Sleep -Seconds $D3D11StartDelaySeconds
                }
                $runnerTimeout = [math]::Min(180, [math]::Max(60, $WatchdogTimeoutSeconds - 60 - $D3D11StartDelaySeconds))
                $d3dOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
                    -File $d3d11Runner -EvidenceDir $evidenceDir -Execute `
                    -Frames $D3D11Frames -TimeoutSeconds $runnerTimeout 2>&1 |
                    Out-String)
                $d3dExit = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $previousEap
            }
            $d3dOutput | Set-Content -LiteralPath `
                (Join-Path $evidenceDir 'd3d11-native-conformance-console.txt') -Encoding UTF8
            Write-Host $d3dOutput
            if ($d3dExit -ne 0) {
                Write-Warning ('D3D11_NATIVE_CONFORMANCE falhou (exit=' + $d3dExit +
                    '). Nao repita este build; o watchdog permanece armado e a evidencia esta gravada.')
            }
        } else {
        Write-Host 'Display convergiu; executando UMA corrida RADV_WDDM_COMPUTE_TRIPOD sob watchdog...' -ForegroundColor Magenta
    # Mesma armadilha descrita em Invoke-Bc250NativeCmd, e que me pegou na
    # corrida 06/09 11:53: no PowerShell 5.1, "& nativo ... 2>&1" com
    # $ErrorActionPreference = 'Stop' transforma CADA linha de stderr em
    # NativeCommandError TERMINANTE. O runner recusou a corrida (Codigo 43) e
    # escreveu isso no stderr; o binder morreu aqui mesmo, antes de gravar o
    # console do tripe, antes da telemetria final e antes da confirmacao
    # visual. A recusa do runner e' um resultado esperado do gate -- nunca
    # pode derrubar quem o chamou.
        $previousEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $tripodOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
                -File $radvComputeTripod -EvidenceDir $evidenceDir -Execute 2>&1 |
                Out-String)
            $tripodExit = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousEap
        }
        $tripodOutput | Set-Content -LiteralPath `
            (Join-Path $evidenceDir 'radv-compute-tripod-console.txt') -Encoding UTF8
        Write-Host $tripodOutput
        if ($tripodExit -ne 0) {
            Write-Warning ('RADV_WDDM_COMPUTE_TRIPOD falhou (exit=' + $tripodExit +
                '). Nao repita este build; o watchdog permanece armado e a evidencia esta gravada.')
        }
        }
    }
}

# C3.15 (12/09): o log e' cumulativo e passou de 1 GB na corrida 18:21.
# Copia-lo integralmente a cada bind acrescentava I/O sem preservar informacao
# nova; o delta deste bind ja foi materializado acima. O watchdog agora coleta
# a cauda pos-rollback separadamente, limitada a 64 MB.
$deltaLogDest = Join-Path $evidenceDir 'display-log-this-bind.txt'
if (Test-Path -LiteralPath $deltaLogDest -PathType Leaf) {
    [ordered]@{
        source = $displayLogPath
        archived_as = 'display-log-this-bind.txt'
        bytes = [int64](Get-Item -LiteralPath $deltaLogDest).Length
        sha256 = (Get-FileHash -LiteralPath $deltaLogDest -Algorithm SHA256).Hash
        bind_start_offset = $displayLogStartOffset
        note = 'delta delimitado deste bind; log cumulativo integral nao e copiado'
        archived_at = (Get-Date).ToString('o')
        hardware_validated = $false
    } | ConvertTo-Json | Set-Content -LiteralPath `
        (Join-Path $evidenceDir 'display-log-archive.json') -Encoding UTF8
} else {
    'display-log-this-bind.txt ausente no fim do bind.' |
        Set-Content -LiteralPath (Join-Path $evidenceDir 'display-log-archive.json') -Encoding ASCII
}

if ($GfxRingBringupLevel -gt 0 -and (Test-Path -LiteralPath $deltaLogDest -PathType Leaf)) {
    Write-Host ''
    Write-Host '=== TELEMETRIA DO ANEL GRAFICO FISICO (GfxRing CP_RB0) ===' -ForegroundColor Magenta
    $gfxLines = Select-String -Path $deltaLogDest -Pattern '\[GfxRing\]' -SimpleMatch
    if ($gfxLines) {
        foreach ($m in $gfxLines) {
            $t = $m.Line.Trim()
            if ($t -match 'veredito|TRIPE|PASS|SUCESSO|Telemetria|Submit3DDraw') {
                Write-Host "  $t" -ForegroundColor Green
            } elseif ($t -match 'FALHOU|RECUSADO|AVISO|MISMATCH|TIMEOUT') {
                Write-Host "  $t" -ForegroundColor Red
            } else {
                Write-Host "  $t" -ForegroundColor Cyan
            }
        }
    } else {
        Write-Host '  Nenhuma linha [GfxRing] encontrada no delta deste bind.' -ForegroundColor Yellow
    }
}

$umdLogCollected = $false
$umdAggregate = Join-Path $evidenceDir 'bc250_umd_log.txt'
$umdSnapshotEntries = @()
$umdSources = @('C:\Windows\Temp')
if ($env:TEMP -and (Test-Path -LiteralPath $env:TEMP)) { $umdSources += $env:TEMP }
$userTemps = @(Get-ChildItem -Path 'C:\Users\*\AppData\Local\Temp' -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
foreach ($ut in $userTemps) { if ($umdSources -notcontains $ut) { $umdSources += $ut } }
$umdLogs = Get-Bc250BindUmdLogs $umdSources $bindUmdLogCutoffUtc
foreach ($umdLog in $umdLogs) {
    $snapshotPath = Join-Path $evidenceDir $umdLog.Name.ToLowerInvariant()
    if (-not (Copy-Bc250SharedLog $umdLog.FullName $snapshotPath)) { continue }
    $umdSnapshotEntries += [ordered]@{
        name = $umdLog.Name
        source_last_write_utc = $umdLog.LastWriteTimeUtc.ToString('o')
        copied_bytes = (Get-Item -LiteralPath $snapshotPath).Length
        sha256 = (Get-FileHash -LiteralPath $snapshotPath -Algorithm SHA256).Hash
    }
    Add-Bc250LogToAggregate $snapshotPath $umdAggregate
    $umdLogCollected = $true
}
if (-not $umdLogCollected) {
    'bc250umd.dll nunca escreveu: a DLL nao foi carregada por nenhum processo.' |
        Set-Content -LiteralPath $umdAggregate -Encoding UTF8
}
[ordered]@{
    kind = 'INTERMEDIATE_BIND_SNAPSHOT'
    captured_at = (Get-Date).ToString('o')
    note = 'Copiado pelo binder com a janela fisica ainda aberta; nao e a cauda do travamento.'
    files = @($umdSnapshotEntries)
} | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'umd-snapshot-manifest.json') -Encoding UTF8

# Registro duravel do que foi realmente instalado, gravado ANTES de qualquer
# ciclo que possa reiniciar a maquina. A quarentena depende disto para
# reconhecer o residuo mesmo depois de o pacote ter sido reconstruido para um
# candidato posterior; se so' fosse escrito no fim, o caminho
# fire-and-collect (que termina em reboot) nunca o produziria.
[ordered]@{
    evidence_dir = $evidenceDir
    watchdog_token = $watchdogToken
    watchdog_disabled = [bool]$DisableWatchdog
    instance_id = $device.PNPDeviceID
    published_inf = $publishedName
    package_sys_sha256 = (Get-FileHash -LiteralPath $pkgSysPath -Algorithm SHA256).Hash
    # PERFIL DO CANDIDATO LIGADO. Sem este registro o bind seguinte nao tem com
    # o que comparar, e o rebaixamento de perfil passa calado -- foi o que
    # quase gastou a Corrida 45 num pacote READ-ONLY.
    profile = [ordered]@{
        ProfileMode = [string]$manifest.ProfileMode
        RenderNode = [uint32]$manifest.RenderNode
        ShaderDispatchEnabled = [bool]$manifest.ShaderDispatchEnabled
        PersistentSubmissionEnabled = [bool]$manifest.PersistentSubmissionEnabled
        GfxWaveProbe = [uint32]$manifest.GfxWaveProbe
        DriverVer = [string]$manifest.DriverVer
    }
    # ======================================================================
    # PROVENIENCIA DOS COMPONENTES FORA DO PACOTE ASSINADO.
    #
    # A vulkan_radeon.dll (RADV) e as DLLs de apoio vivem em C:\BC250_LIB, nao
    # no pacote assinado -- logo package_sys_sha256 NAO as identifica. Na
    # corrida 2026-09-18 10:20 o unico componente que mudou entre candidatos foi
    # exatamente a RADV, e a pasta de evidencia nao registrava qual DLL havia
    # sido carregada: R4 ("o binario testado e o binario novo?") ficava sem
    # resposta para a peca que estava em teste.
    #
    # Ausencia e' declarada, nao omitida: DLL que nao existe entra como null com
    # motivo, para que o log nunca sugira que a medicao foi feita.
    # ======================================================================
    external_libs = @(
        foreach ($libName in @('vulkan_radeon.dll', 'libvkd3d-shader-1.dll')) {
            $libPath = Join-Path 'C:\BC250_LIB' $libName
            if (Test-Path -LiteralPath $libPath) {
                $fi = Get-Item -LiteralPath $libPath
                [ordered]@{
                    name = $libName
                    path = $libPath
                    bytes = $fi.Length
                    modified_utc = $fi.LastWriteTimeUtc.ToString('o')
                    sha256 = (Get-FileHash -LiteralPath $libPath -Algorithm SHA256).Hash
                }
            } else {
                [ordered]@{
                    name = $libName
                    path = $libPath
                    bytes = $null
                    modified_utc = $null
                    sha256 = $null
                    absent_reason = 'arquivo ausente no momento do bind'
                }
            }
        }
    )
} | ConvertTo-Json -Depth 5 | ForEach-Object {
    # DX.36: escrita WRITE-THROUGH. Na Corrida 81 o 0x116 veio 20 s depois do
    # Set-Content e o arquivo ficou com 1762 bytes TODOS ZERO (tamanho gravado,
    # dados nao): o bind seguinte quebrou no ConvertFrom-Json. FileOptions.WriteThrough
    # + Flush($true) forca os dados ao disco antes do bind prosseguir.
    $lastBindingBytes = [System.Text.UTF8Encoding]::new($true).GetPreamble() +
        [System.Text.UTF8Encoding]::new($false).GetBytes([string]$_)
    $lastBindingStream = [System.IO.FileStream]::new(
        (Join-Path $root 'tools\_last_lab_binding.json'),
        [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None, 4096, [System.IO.FileOptions]::WriteThrough)
    try {
        $lastBindingStream.Write($lastBindingBytes, 0, $lastBindingBytes.Length)
        $lastBindingStream.Flush($true)
    } finally {
        $lastBindingStream.Dispose()
    }
}

# A pasta de evidencia e' o que sobrevive e e' arquivado com SHA-256; tools/ e'
# estado de trabalho e e' sobrescrito pelo bind seguinte. Sem esta copia, a
# proveniencia da RADV se perderia exatamente na corrida que a testa.
Copy-Item -LiteralPath (Join-Path $root 'tools\_last_lab_binding.json') `
          -Destination (Join-Path $evidenceDir 'binding-provenance.json') -Force -ErrorAction SilentlyContinue

# ==========================================================================
# A3 — ciclo FIRE-AND-COLLECT do perfil physical render-only.
#
# Este perfil nao tem, e nao pode ter, confirmacao humana: ele substitui o
# BasicDisplay na unica funcao PCI com saida de video, entao a tela apaga por
# construcao e o operador nao consegue rodar o confirmador. Ate o 3.79 o
# roteiro mantinha a ficcao da confirmacao; na pratica todo ensaio terminava
# em timeout do guardiao seguido de rollback de emergencia, e o operador
# ficava no escuro sem saber se aquilo era falha ou o comportamento previsto.
#
# Aqui o desfecho vira contrato explicito: janela fixa, retrato, rollback e
# reboot -- todos incondicionais. Nao existe caminho de sucesso que deixe o
# driver instalado, e por isso nao existe mais ambiguidade de leitura.
#
# O que este bloco NAO faz, deliberadamente: escrever
# safe-bind-confirmation.json. Aquele arquivo afirma display_visible=$true e,
# com a tela apagada, seria uma mentira gravada em evidencia -- exatamente o
# tipo de falso sucesso que o projeto proibe. Em vez de forjar a confirmacao,
# o binder assume o controle desregistrando a tarefa.
# ==========================================================================
if ($EnablePhysicalRenderOnly) {
    Write-Host ''
    Write-Host '=== CICLO FIRE-AND-COLLECT (physical render-only) ===' -ForegroundColor Magenta
    Write-Host ("  Janela de bring-up: $PhysicalWindowSeconds s. Rollback e reboot sao automaticos.")

    # (a) Serie temporal do PnP durante a janela.
    $physicalSamples = @()
    $windowDeadline = (Get-Date).AddSeconds($PhysicalWindowSeconds)
    $sampleIndex = 0
    do {
        $sampleIndex++
        $sampleDevices = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
            Where-Object { $_.PNPDeviceID -eq [string]$device.PNPDeviceID })
        $physicalSamples += [ordered]@{
            index = $sampleIndex
            at = (Get-Date).ToString('o')
            device_count = $sampleDevices.Count
            service = $(if ($sampleDevices.Count -eq 1) { [string]$sampleDevices[0].Service } else { $null })
            code = $(if ($sampleDevices.Count -eq 1) { [uint32]$sampleDevices[0].ConfigManagerErrorCode } else { $null })
        }
        Start-Sleep -Seconds 2
    } until ((Get-Date) -ge $windowDeadline)
    $physicalSamples | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $evidenceDir 'physical-pnp-samples.json') -Encoding UTF8
    Write-Host ("  {0} amostras de PnP gravadas." -f $physicalSamples.Count) -ForegroundColor Cyan

    # (b) Segundo retrato da telemetria: o do bind pegou o driver recem-carregado,
    #     este pega o estado depois de o dxgkrnl ter decidido o que fazer com ele.
    try {
        if (Test-Path -LiteralPath $probeKeyPath) {
            $finalSnapshot = Get-ItemProperty -LiteralPath $probeKeyPath -ErrorAction Stop
            $finalOrdered = [ordered]@{}
            foreach ($finalProperty in ($finalSnapshot.PSObject.Properties |
                    Where-Object { $_.Name -notlike 'PS*' } | Sort-Object Name)) {
                $finalOrdered[$finalProperty.Name] = $finalProperty.Value
            }
            $finalOrdered | ConvertTo-Json -Depth 4 |
                Set-Content -LiteralPath (Join-Path $evidenceDir 'probe-registry-final.json') -Encoding UTF8
            & reg.exe export 'HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe' `
                (Join-Path $evidenceDir 'probe-registry-final.txt') /y | Out-Null
            Write-Host ("  Telemetria final: {0} valores." -f $finalOrdered.Count) -ForegroundColor Cyan
        } else {
            Write-Host '  AVISO: chave de telemetria ausente no fim da janela.' -ForegroundColor Yellow
        }
    } catch {
        Write-Host ("  AVISO: falha ao capturar telemetria final: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
    }

    # (b2) COLETA FINAL DOS LOGS DA UMD.
    #
    # A coleta da linha ~1300 acontece LOGO APOS o bind, e por isso arquiva um
    # SNAPSHOT de poucos segundos. Nas corridas de 13/09 08:12, 11:25 e 11:32 o
    # log do dwm.exe terminou antes do momento que importava -- na 11:32 ele
    # parou exatamente no "BltViaKmd ENTER", e a linha "LEAVE" com o hr do
    # escape, que responderia a corrida inteira, foi descartada porque o DWM
    # seguiu vivo por mais 30 s. Tres criterios ficaram sem resposta por isso.
    #
    # Aqui a janela fisica ja terminou, entao este e' o estado final real. Os
    # arquivos vao com sufixo -FINAL para NAO sobrescrever o snapshot: os dois
    # juntos mostram o que mudou durante a janela.
    try {
        $umdFinalAggregate = Join-Path $evidenceDir 'bc250_umd_log-FINAL.txt'
        $umdFinalSources = @('C:\Windows\Temp')
        if ($env:TEMP -and (Test-Path -LiteralPath $env:TEMP)) { $umdFinalSources += $env:TEMP }
        $userTemps = @(Get-ChildItem -Path 'C:\Users\*\AppData\Local\Temp' -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
        foreach ($ut in $userTemps) { if ($umdFinalSources -notcontains $ut) { $umdFinalSources += $ut } }
        $umdFinal = Get-Bc250BindUmdLogs $umdFinalSources $bindUmdLogCutoffUtc
        foreach ($log in $umdFinal) {
            $dest = Join-Path $evidenceDir ($log.BaseName.ToLowerInvariant() + '-FINAL.txt')
            if (-not (Copy-Bc250SharedLog $log.FullName $dest)) { continue }
            Add-Bc250LogToAggregate $dest $umdFinalAggregate
        }
        Write-Host ("  Coleta FINAL da UMD: {0} arquivo(s)." -f $umdFinal.Count) -ForegroundColor Yellow
        if ($umdFinal.Count -eq 0) {
            'Nenhum BC250_UMD_LOG_*.txt em C:\Windows\Temp no fim da janela fisica.' |
                Set-Content -LiteralPath $umdFinalAggregate -Encoding UTF8
        }
    } catch {
        Write-Host ("  AVISO: coleta FINAL da UMD falhou: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
    }

    # (c) Assumir o controle antes de mexer no pacote, para nao disputar a
    #     mesma recovery com o guardiao.
    if (-not $DisableWatchdog) {
        $disarmOk = Disarm-Bc250SafeGuard 'BC250-SafeBindGuard' $evidenceDir
        Write-Host ("  Guardiao SYSTEM assumido pelo binder (limpeza_ok={0})." -f $disarmOk) -ForegroundColor Yellow
    }

    # (d) Rollback incondicional.
    Write-Host '  Executando rollback...' -ForegroundColor Yellow
    $rollback = Invoke-Bc250NativeCmd $recoveryPath
    [ordered]@{
        executed_at = (Get-Date).ToString('o')
        recovery_path = $recoveryPath
        exit_code = $rollback.exit_code
        output = $rollback.output
        hardware_validated = $false
    } | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $evidenceDir 'physical-rollback.json') -Encoding UTF8
    Write-Host ("  Recovery exit={0}." -f $rollback.exit_code)

    # (e) Marcador de boot limpo, no mesmo formato de rollback-bc250mcdm-lab.ps1.
    #     Estado de fila nao cai com desbind; so' o boot frio limpa.
    [ordered]@{
        state = 'CLEAN_BOOT_REQUIRED'
        rollback_boot_ticks = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.
            ToUniversalTime().Ticks
        rollback_completed_at = (Get-Date).ToString('o')
        restored_service = 'BasicDisplay'
        restored_code = 0
        hardware_validated = $false
    } | ConvertTo-Json | Set-Content -LiteralPath $cleanBootMarkerPath -Encoding UTF8

    # (f) Reboot incondicional, com escalada se o shutdown recusar.
    Write-Host '  Reiniciando em 15 s. A evidencia ja esta gravada.' -ForegroundColor Yellow
    & shutdown.exe /r /t 15 /f /d p:2:4 /c "BC250 fire-and-collect"
    $shutdownExit = $LASTEXITCODE
    if ($shutdownExit -ne 0) {
        Write-Host ("  shutdown.exe falhou (exit=$shutdownExit); escalando para Restart-Computer.") -ForegroundColor Red
        Restart-Computer -Force
    }
    Write-Host ''
    Write-Host 'Evidencia deste ensaio:' -ForegroundColor Cyan
    Write-Host "  $evidenceDir"
    Write-Host 'NAO VALIDADO EM HARDWARE ate haver fence fisica e conferencia byte a byte.' -ForegroundColor Yellow
    exit 0
}

# ==========================================================================
# COLETA FINAL NO CAMINHO COMUM.
#
# O binder sai LOGO APOS o bind, e por isso a coleta da linha ~1300 arquiva um
# SNAPSHOT de poucos segundos -- tudo o que importa acontece depois. Nas
# corridas de 13/09 08:12, 11:25, 11:32 e 11:55 o log do dwm.exe terminou antes
# do momento decisivo; na 11:32 e na 11:55 ele parou exatamente no
# "BltViaKmd ENTER", perdendo o "LEAVE" com o hr do pfnEscapeCb -- o unico dado
# que nomeia por que o dxgkrnl recusa o escape. QUATRO criterios perdidos pela
# mesma causa.
#
# A primeira tentativa de corrigir isto foi inserida dentro do ramo
# `if ($EnablePhysicalRenderOnly)`, que este perfil NAO executa. Por isso agora
# fica no caminho comum, e a janela termina com folga ANTES do watchdog.
# ==========================================================================
if (-not $DisableWatchdog) {
    # Corrida 40: o harness morreu em 16 s e a coleta FINAL fechou 22 s apos o
    # bind, mas o watchdog continuou por mais 104 s. Tres crashes do dwm.exe e
    # 29 submissoes GFX ficaram depois da coleta. A janela termina pelo RELOGIO
    # ABSOLUTO do watchdog, nunca pela saida (cedo ou tarde) do harness.
    if ($null -eq $collectionDeadlineUtc) {
        throw 'Coleta FINAL recusada: prazo absoluto da janela nao foi definido.'
    }
    Write-Host ''
    Write-Host ("=== JANELA DE OBSERVACAO ATE {0:o} UTC ===" -f $collectionDeadlineUtc) -ForegroundColor Magenta
    Write-Host '  (a saida do harness nao encerra a observacao; o watchdog SYSTEM segue como rede de seguranca)'

    if ($RestartExplorer) {
        Write-Host '  Reiniciando explorer.exe para forcar enumeracao e criacao de recursos DXGI...' -ForegroundColor Yellow
        Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    }

    while ([datetime]::UtcNow -lt $collectionDeadlineUtc) {
        Start-Sleep -Milliseconds 500
    }

    try {
        $umdFinalAggregate = Join-Path $evidenceDir 'bc250_umd_log-FINAL.txt'
        $umdFinal = Get-Bc250BindUmdLogs @('C:\Windows\Temp') $bindUmdLogCutoffUtc
        foreach ($log in $umdFinal) {
            $dest = Join-Path $evidenceDir ($log.BaseName.ToLowerInvariant() + '-FINAL.txt')
            if (-not (Copy-Bc250SharedLog $log.FullName $dest)) { continue }
            Add-Bc250LogToAggregate $dest $umdFinalAggregate
        }
        if ($umdFinal.Count -eq 0) {
            'Nenhum BC250_UMD_LOG_*.txt em C:\Windows\Temp no fim da janela.' |
                Set-Content -LiteralPath $umdFinalAggregate -Encoding UTF8
        }
        Write-Host ("  Coleta FINAL da UMD: {0} arquivo(s)." -f $umdFinal.Count) -ForegroundColor Yellow
    } catch {
        Write-Host ("  AVISO: coleta FINAL da UMD falhou: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
    }

    # Doc 516 (Corrida 160): apps isolados (AppContainer: menu Iniciar, Pesquisa,
    # Central de Acoes) gravam BC250_UMD_LOG_* e BC250_SHADER_FAIL_*.bin na
    # pasta Temp do proprio pacote, e o %TEMP% do usuario guarda os de Explorer
    # e harness. 772 logs de AppContainer nunca tinham sido coletados. Copia os
    # escritos durante esta janela para umd-logs-appcontainer\ com o pacote no
    # nome, sem apagar a origem.
    try {
        $windowStartLocal = $collectionDeadlineUtc.ToLocalTime().AddSeconds(-($WatchdogTimeoutSeconds + 180))
        $acDir = Join-Path $evidenceDir 'umd-logs-appcontainer'
        $acRoots = @()
        $acRoots += @(Get-ChildItem -Path 'C:\Users\*\AppData\Local\Packages\*\AC\Temp' -Directory -ErrorAction SilentlyContinue)
        $acRoots += @(Get-ChildItem -Path 'C:\Users\*\AppData\Local\Temp' -Directory -ErrorAction SilentlyContinue)
        $acCount = 0
        foreach ($root in $acRoots) {
            $files = @(Get-ChildItem -LiteralPath $root.FullName -File -ErrorAction SilentlyContinue |
                Where-Object { ($_.Name -like 'BC250_UMD_LOG_*.txt' -or $_.Name -like 'BC250_SHADER_FAIL_*.bin') -and
                               $_.LastWriteTime -ge $windowStartLocal })
            foreach ($file in $files) {
                if ($acCount -eq 0) { New-Item -ItemType Directory -Force -Path $acDir | Out-Null }
                $pkg = Split-Path -Leaf (Split-Path -Parent (Split-Path -Parent $root.FullName))
                if ($root.FullName -like '*\AppData\Local\Temp') { $pkg = 'UserTemp' }
                if (-not (Copy-Bc250SharedLog $file.FullName (Join-Path $acDir ($pkg + '__' + $file.Name)))) { continue }
                $acCount++
            }
        }
        Write-Host ("  Coleta de AppContainer/Temp do usuario: {0} arquivo(s) em {1}." -f $acCount, $acDir) -ForegroundColor Yellow
    } catch {
        Write-Host ("  AVISO: coleta de AppContainer falhou: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
    }

    # DX.21: despejo de IB do staging gravavel pelo DWM -> evidencia, com
    # contagem declarada. Zero arquivos agora e' conclusivo sobre o RADV (o
    # staging e' gravavel), nao mais sobre a ACL.
    try {
        $gfxIbDumpEvidence = Join-Path $evidenceDir 'gfx-ib-dump'
        if (-not (Test-Path -LiteralPath $gfxIbDumpEvidence)) {
            New-Item -ItemType Directory -Path $gfxIbDumpEvidence -Force | Out-Null
        }
        $ibDumps = @(Get-ChildItem -LiteralPath 'C:\Windows\Temp\BC250_GFX_IB_DUMP' -File -ErrorAction SilentlyContinue)
        foreach ($dump in $ibDumps) {
            Copy-Item -LiteralPath $dump.FullName -Destination (Join-Path $gfxIbDumpEvidence $dump.Name) -Force
        }
        ("{0} arquivo(s) copiados de C:\Windows\Temp\BC250_GFX_IB_DUMP em {1:o}" -f $ibDumps.Count, (Get-Date)) |
            Set-Content -LiteralPath (Join-Path $gfxIbDumpEvidence '_COLETA.txt') -Encoding UTF8
        Write-Host ("  Coleta FINAL do despejo de IB: {0} arquivo(s)." -f $ibDumps.Count) -ForegroundColor Yellow
    } catch {
        Write-Host ("  AVISO: coleta FINAL do despejo de IB falhou: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
    }

    # Doc 634 (Corrida 241): menu Iniciar e Pesquisa sao AppContainer e a RADV
    # grava o despejo de IB em <pacote>\AC\Temp\BC250_GFX_IB_DUMP. So a coleta
    # pos-trava pegava esses arquivos; numa corrida sem trava a pre-condicao do
    # doc 633 (controle do preambulo do menu) ficou sem prova automatica.
    try {
        $acIbStartLocal = $collectionDeadlineUtc.ToLocalTime().AddSeconds(-($WatchdogTimeoutSeconds + 180))
        $acIbEvidence = Join-Path $evidenceDir 'gfx-ib-dump-appcontainer'
        $acIbCount = 0
        $acIbRoots = @(Get-ChildItem -Path 'C:\Users\*\AppData\Local\Packages\*\AC\Temp\BC250_GFX_IB_DUMP' -Directory -ErrorAction SilentlyContinue)
        foreach ($acIbRoot in $acIbRoots) {
            $acIbPkg = Split-Path -Leaf (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $acIbRoot.FullName)))
            $acIbFiles = @(Get-ChildItem -LiteralPath $acIbRoot.FullName -File -ErrorAction SilentlyContinue |
                Where-Object { $_.LastWriteTime -ge $acIbStartLocal })
            if ($acIbFiles.Count -eq 0) { continue }
            $acIbDest = Join-Path $acIbEvidence $acIbPkg
            New-Item -ItemType Directory -Force -Path $acIbDest | Out-Null
            foreach ($acIbFile in $acIbFiles) {
                Copy-Item -LiteralPath $acIbFile.FullName -Destination (Join-Path $acIbDest $acIbFile.Name) -Force
                $acIbCount++
            }
        }
        Write-Host ("  Coleta FINAL dos IBs da RADV em AppContainer: {0} arquivo(s) em {1}." -f $acIbCount, $acIbEvidence) -ForegroundColor Yellow
    } catch {
        Write-Host ("  AVISO: coleta FINAL dos IBs da RADV em AppContainer falhou: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
    }

    if ($EnableDwmCrashCapture) {
        try {
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $restoreDwmCapture `
                -EvidenceDir $evidenceDir -Execute
            if ($LASTEXITCODE -ne 0) {
                Write-Warning 'LocalDumps: restauracao terminou com codigo nao-zero; o guardiao tentara novamente.'
            }
        } catch {
            Write-Warning ("LocalDumps: restauracao no binder falhou; o guardiao tentara novamente: {0}" -f $_.Exception.Message)
        }
    }

    # Cauda do log do KMD no fim da janela, pelo offset do baseline. Sem isto o
    # arquivamento depende do watchdog, que morre junto em caso de BugCheck.
    #
    # -----------------------------------------------------------------------
    # DUAS PASSAGENS, E O TRUNCAMENTO E DECLARADO. Corrigido em 19/09.
    #
    # MEDIDO na Corrida 36 (12:11:58): esta copia arquivou 210.851 bytes de uma
    # cauda que tinha 219.325. Os 8.474 bytes que faltaram continham o BLOCO
    # INTEIRO DO TDR -- [TDR_GFX_QUIESCE], [TDR_GFX_WAVES], [TDR_GFX_WAVEPC] --
    # isto e, exatamente a medicao que a corrida existia para produzir. A pasta
    # de evidencia nao dizia que estava incompleta; parecia inteira.
    #
    # O codigo nunca esteve errado: ele copia do baseline ate o EOF do momento.
    # O defeito e TEMPORAL -- o KMD continua escrevendo depois da copia. Por
    # isso agora sao duas passagens com um respiro entre elas, e o json registra
    # os dois tamanhos. Se a segunda ainda crescer, o arquivo diz que cresceu,
    # em vez de o leitor descobrir dias depois que analisou meia corrida.
    # -----------------------------------------------------------------------
    # T3 (doc 691): log do gatilho de processo novo e CSV do monitor (cumulativos;
    # o log da T3 traz data, hora e sessao por linha). So' copia; falha so' avisa.
    foreach ($extra in @('C:\BC250_PROCESSO_NOVO.log', 'C:\BC250_MONITOR.csv')) {
        try {
            if (Test-Path -LiteralPath $extra) {
                Copy-Item -LiteralPath $extra -Destination (Join-Path $evidenceDir ([IO.Path]::GetFileName($extra))) -Force
            }
        } catch {
            Write-Warning "Nao copiei $extra para a evidencia: $($_.Exception.Message)"
        }
    }
    $finalTailPath = Join-Path $evidenceDir 'display-log-this-bind-FINAL.txt'
    $tailPass1 = -1
    $tailPass2 = -1
    try {
        if (Test-Path -LiteralPath $displayLogPath -PathType Leaf) {
            $fs = [System.IO.File]::Open($displayLogPath, 'Open', 'Read', 'ReadWrite')
            try {
                $null = $fs.Seek($displayLogStartOffset, 'Begin')
                $out = [System.IO.File]::Create($finalTailPath)
                try { $fs.CopyTo($out) } finally { $out.Dispose() }
            } finally { $fs.Dispose() }
            $tailPass1 = (Get-Item -LiteralPath $finalTailPath).Length

            # Respiro curto e SEGUNDA passagem. O KMD escreve o bloco do TDR
            # depois do timeout de 2 s do dxgkrnl, que costuma cair justamente
            # na borda desta coleta.
            Start-Sleep -Seconds 8
            $fs2 = [System.IO.File]::Open($displayLogPath, 'Open', 'Read', 'ReadWrite')
            try {
                $null = $fs2.Seek($displayLogStartOffset, 'Begin')
                $out2 = [System.IO.File]::Create($finalTailPath)
                try { $fs2.CopyTo($out2) } finally { $out2.Dispose() }
            } finally { $fs2.Dispose() }
            $tailPass2 = (Get-Item -LiteralPath $finalTailPath).Length

            # ================================================================
            # CONTEUDO, NAO TAMANHO (21/09, apos a Corrida 50).
            #
            # O criterio de completude era $tailPass2 -ge $expected -- puro
            # TAMANHO. Na Corrida 50 isso deu "completo" com 2.751.230 bytes que
            # eram 100% ESPACO: o KMD ainda nao havia descarregado o trecho, e a
            # copia pegou o preenchimento. O arquivo tinha tamanho plausivel e
            # ZERO conteudo, e enganou a leitura da corrida -- a conclusao foi
            # "perdemos a coleta" quando o dado estava intacto no arquivo do
            # arquivamento inicial.
            #
            # Medir o proxy (tamanho) em vez do efeito (conteudo) e a classe de
            # defeito mais cara deste projeto. Aqui a captura passa a OLHAR os
            # bytes, e retenta enquanto forem so' preenchimento.
            # ================================================================
            $paddingRetries = 0
            while ($paddingRetries -lt 4) {
                $probe = [System.IO.File]::ReadAllBytes($finalTailPath)
                $nonPad = 0
                foreach ($byte in $probe) {
                    if ($byte -ne 32 -and $byte -ne 0 -and $byte -ne 13 -and $byte -ne 10) { $nonPad++ }
                    if ($nonPad -gt 64) { break }
                }
                if ($nonPad -gt 64) { break }
                $paddingRetries++
                Write-Host ("  [AVISO] cauda FINAL veio SO' com preenchimento ({0} bytes, tentativa {1}/4) -- o KMD ainda nao descarregou; aguardando" -f $probe.Length, $paddingRetries) -ForegroundColor Yellow
                Start-Sleep -Seconds 6
                $fsR = [System.IO.File]::Open($displayLogPath, 'Open', 'Read', 'ReadWrite')
                try {
                    $null = $fsR.Seek($displayLogStartOffset, 'Begin')
                    $outR = [System.IO.File]::Create($finalTailPath)
                    try { $fsR.CopyTo($outR) } finally { $outR.Dispose() }
                } finally { $fsR.Dispose() }
                $tailPass2 = (Get-Item -LiteralPath $finalTailPath).Length
            }
            $finalBytes = [System.IO.File]::ReadAllBytes($finalTailPath)
            $finalNonPad = 0
            foreach ($byte in $finalBytes) {
                if ($byte -ne 32 -and $byte -ne 0 -and $byte -ne 13 -and $byte -ne 10) { $finalNonPad++ }
                if ($finalNonPad -gt 64) { break }
            }
            $contentIsPadding = ($finalNonPad -le 64)
            if ($contentIsPadding) {
                Write-Host ("  [ALERTA] display-log-this-bind-FINAL.txt tem {0} bytes e NENHUM conteudo -- e' preenchimento. NAO leia esse arquivo como se fosse a corrida; use display-log-this-bind.txt e recover-bc250mcdm-lost-tail.ps1" -f $finalBytes.Length) -ForegroundColor Red
            }

            $liveLen = (Get-Item -LiteralPath $displayLogPath).Length
            $expected = $liveLen - $displayLogStartOffset
            # Completude exige AS DUAS COISAS: tamanho suficiente E conteudo real.
            $complete = ($tailPass2 -ge $expected) -and (-not $contentIsPadding)
            [pscustomobject]@{
                source              = $displayLogPath
                bind_start_offset   = $displayLogStartOffset
                bytes_pass1         = $tailPass1
                bytes_pass2         = $tailPass2
                grew_between_passes = ($tailPass2 - $tailPass1)
                live_log_length     = $liveLen
                expected_tail_bytes = $expected
                content_is_padding  = $contentIsPadding
                padding_retries     = $paddingRetries
                complete_at_capture = $complete
                note                = 'duas passagens; se complete_at_capture for false a pasta esta INCOMPLETA e o resgate pelo offset do baseline e obrigatorio'
                hardware_validated  = $false
            } | ConvertTo-Json | Set-Content -LiteralPath `
                (Join-Path $evidenceDir 'display-log-final-archive.json') -Encoding UTF8

            if ($tailPass2 -gt $tailPass1) {
                Write-Host ("  Cauda FINAL do KMD: 1a passagem {0} bytes, 2a passagem {1} (+{2}). O log seguiu crescendo." -f `
                    $tailPass1, $tailPass2, ($tailPass2 - $tailPass1)) -ForegroundColor Yellow
            } else {
                Write-Host ("  Cauda FINAL do log do KMD arquivada ({0} bytes)." -f $tailPass2) -ForegroundColor Yellow
            }
            if (-not $complete) {
                Write-Host ("  AVISO: cauda arquivada ({0}) menor que o esperado ({1}). PASTA INCOMPLETA -- use tools\recover-bc250mcdm-lost-tail.ps1." -f `
                    $tailPass2, $expected) -ForegroundColor Red
            }
        }
    } catch {
        Write-Host ("  AVISO: cauda FINAL do KMD falhou: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
    }
}

# DX.39: indice humano dos logs JA ARQUIVADOS. Falha do indexador nao muda o
# rollback nem converte uma corrida incompleta em sucesso. O bruto prevalece.
try {
    $telemetryIndexer = Join-Path $PSScriptRoot 'index-bc250-telemetry.py'
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if ((Test-Path -LiteralPath $telemetryIndexer) -and $null -ne $pythonCommand -and
        (Test-Path -LiteralPath (Join-Path $evidenceDir 'display-log-this-bind-FINAL.txt'))) {
        & $pythonCommand.Source $telemetryIndexer $evidenceDir
        if ($LASTEXITCODE -ne 0) {
            Write-Warning ("DX.39: indexador saiu com {0}; logs brutos preservados." -f $LASTEXITCODE)
        }
    } else {
        Write-Warning 'DX.39: indice nao gerado (Python, script ou log FINAL ausente); logs brutos preservados.'
    }
} catch {
    Write-Warning ("DX.39: indice falhou: {0}; logs brutos preservados." -f $_.Exception.Message)
}

Write-Host "`n==========================================================================" -ForegroundColor Green
Write-Host "DRIVER DE LABORATORIO INSTALADO!" -ForegroundColor Green
if ($DisableWatchdog) {
    Write-Host 'Watchdog DESABILITADO; nao ha confirmacao temporizada a executar.' -ForegroundColor Yellow
    Write-Host "Em caso de falha use: $recoveryPath" -ForegroundColor Yellow
} else {
    Write-Host "O Watchdog SYSTEM continua armado por $WatchdogTimeoutSeconds segundos." -ForegroundColor Yellow
    Write-Host "Se o video estiver visivel, confirme para desarmar o rollback:" -ForegroundColor Yellow
    Write-Host "powershell -NoProfile -ExecutionPolicy Bypass -File `"$safeConfirmScript`" -ConfigPath `"C:\BC250_SAFE_GUARD\armed.json`" -Token `"$watchdogToken`" -IConfirmDisplayIsVisible"
}
Write-Host "==========================================================================" -ForegroundColor Green
