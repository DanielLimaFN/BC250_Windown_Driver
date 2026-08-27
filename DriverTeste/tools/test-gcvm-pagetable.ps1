<#
    Passo 1 do gate de memoria: construir e conferir uma page table na VRAM.

    Este ensaio escreve VRAM, mas NENHUM registrador de traducao. Se a tabela
    sair errada, ela e apenas bytes numa area de VRAM que ninguem usa, porque
    nenhuma unidade de traducao foi apontada para ela. A placa termina no mesmo
    estado em que comecou.

    Por que este passo existe separado: a medicao de 2026-08-23 mostrou que a
    placa nao tem page table nenhuma (CONTEXT0 desabilitado, PT_BASE zerado) e
    que todo o acesso a memoria da SDMA depende do bypass que o POST deixou.
    Ligar traducao substitui esse bypass. Antes de chegar perto disso, a
    estrutura precisa estar comprovadamente correta em memoria.

    O que este ensaio prova, se passar:
      - o driver constroi PTEs no formato do upstream, com PA derivado do
        registrador GCMC_VM_FB_OFFSET lido do silicio;
      - cada entrada volta identica na leitura da CPU;
      - a regiao usada e devolvida ao conteudo original, dword a dword.

    O que ele NAO prova: que a GPU aceita essa tabela. Isso e passo 5 do plano e
    exige ligar traducao, o que nao acontece aqui.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,60)][int]$CommandTimeoutSeconds = 20,
    [uint32]$TableOffset = 0x0FF10000,
    # 512 paginas de 4 KB a partir de 0x0FD00000 cobrem 2 MB e terminam
    # exatamente onde comeca o anel da SDMA, em 0x0FF00000. O valor anterior
    # (0x0FF20000) estourava a abertura de 256 MB.
    [uint32]$TargetOffset = 0x0FD00000,
    [ValidateRange(1,512)][int]$Pages = 512
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute; ele escreve VRAM (com snapshot e rollback).'
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
foreach ($path in @($cli,$workspaceDriver,$installedDriver)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }
$driverHash = (Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if ($driverHash -ne (Get-FileHash $installedDriver -Algorithm SHA256).Hash) { throw 'Build instalado diverge do workspace.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-gcvm-pagetable"
New-Item -ItemType Directory -Path $dir -Force | Out-Null

$events = [System.Collections.Generic.List[object]]::new()
$gates = [System.Collections.Generic.List[object]]::new()

function Run([string]$Name, [string[]]$Arguments, [switch]$AllowFailure) {
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $cli
    $psi.Arguments = (($Arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' ')
    $psi.WorkingDirectory = $root; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = New-Object Diagnostics.Process; $p.StartInfo = $psi
    if (-not $p.Start()) { throw "Falha ao iniciar $Name" }
    $ot = $p.StandardOutput.ReadToEndAsync(); $et = $p.StandardError.ReadToEndAsync()
    $to = -not $p.WaitForExit($CommandTimeoutSeconds * 1000)
    if ($to) { $p.Kill(); $p.WaitForExit() }
    $ec = if ($to) { 1460 } else { $p.ExitCode }
    $r = [pscustomobject]@{
        name = $Name
        command = 'bc250ctl.exe ' + ($Arguments -join ' ')
        exit_code = $ec
        timed_out = $to
        output = @(($ot.Result + $et.Result) -split "`r?`n" | Where-Object { $_ })
    }
    $events.Add($r)
    if (($to -or $ec -ne 0) -and -not $AllowFailure) { throw "$Name falhou: exit=$ec timeout=$to" }
    return $r
}
function Add-Gate([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $gates.Add([pscustomobject]@{ gate = $Name; passed = $Passed; detail = $Detail; data = $Data })
}
function Get-GcvmField([object]$Resultado, [string]$Campo) {
    foreach ($linha in $Resultado.output) {
        if ($linha -match ("\s{2}" + [regex]::Escape($Campo) + "\s*:\s*(.+?)\s*$")) { return $Matches[1] }
    }
    return $null
}

$failure = $null; $armed = $false; $built = $false
try {
    $infoPre = Run 'info_pre' @('info')
    # A compatibilidade de ABI ja e imposta pela biblioteca: ela recusa o driver
    # quando AbiVersion diverge do header com que o CLI foi compilado. Fixar a versao
    # aqui tambem so criava manutencao — cada ABI nova reprovava ensaios antigos por
    # motivo cosmetico. O que importa checar e o armamento e que 'info' respondeu.
    if (($infoPre.output -join ' ') -notmatch 'bc250dbg v\d+\.\d+\s+ABI \d+') {
        throw 'Preflight recusado: info nao reportou versao/ABI do driver.'
    }

    # O estado de traducao tem de estar como o POST deixou: contexto desabilitado
    # e sem page table. Se alguem ja mexeu, este ensaio nao mede o que promete.
    $dumpPre = Run 'gcvm_dump_pre' @('gcvm','dump')
    $ctx0 = Get-GcvmField $dumpPre 'CONTEXT0_CNTL'
    $ptBase = Get-GcvmField $dumpPre 'CTX0_PT_BASE'
    $ctx0Valor = [Convert]::ToUInt32($ctx0.Substring(2),16)
    $ctx0Habilitado = (($ctx0Valor -band 1) -ne 0)
    Add-Gate 'A_traducao_desligada_no_inicio' ((-not $ctx0Habilitado) -and $ptBase -match '^0x0+$') `
        'CONTEXT0 desabilitado e PT_BASE zerado: o ensaio comeca do estado do POST.' `
        ([pscustomobject]@{ context0_cntl = $ctx0; pt_base = $ptBase })
    if ($ctx0Habilitado) { throw 'CONTEXT0 ja esta habilitado: estado inesperado, reinicie.' }

    Run 'arm_write' @('arm','write') | Out-Null; $armed = $true

    $build = Run 'gcvm_ptbuild' @('gcvm','ptbuild',
        ('0x{0:X8}' -f $TableOffset), ('0x{0:X8}' -f $TargetOffset), "$Pages") -AllowFailure
    $built = $true
    $buildText = $build.output -join "`n"
    $divergencias = if ($buildText -match 'conferencia\s+:\s+(\d+) divergencias em (\d+) entradas') {
        [pscustomobject]@{ divergencias = [int]$Matches[1]; entradas = [int]$Matches[2] }
    } else { $null }
    $fbOffset = Get-GcvmField $build 'GCMC_VM_FB_OFFSET'
    $paLinha = Get-GcvmField $build 'PA coberto'
    $flags = Get-GcvmField $build 'flags do PTE'

    Add-Gate 'B_tabela_construida_e_conferida' `
        ($build.exit_code -eq 0 -and $divergencias -and $divergencias.divergencias -eq 0 -and $divergencias.entradas -eq $Pages) `
        'Cada PTE volta identico na leitura da CPU.' `
        ([pscustomobject]@{
            exit_code = $build.exit_code; conferencia = $divergencias
            fb_offset = $fbOffset; pa = $paLinha; flags = $flags; saida = $build.output
        })

    # A tabela nao pode ter mexido em registrador de traducao. Se mexeu, o
    # ensaio inteiro perde sentido, porque deixaria de ser o passo seguro.
    $dumpPos = Run 'gcvm_dump_pos_build' @('gcvm','dump')
    $iguais = $true
    $camposMudados = @()
    foreach ($linha in $dumpPre.output) {
        if ($linha -match '^\s{2}([A-Z0-9_]+)\s*:\s*(.+?)\s*$') {
            $campo = $Matches[1]; $antes = $Matches[2]
            $depois = Get-GcvmField $dumpPos $campo
            if ($null -ne $depois -and $depois -ne $antes) {
                $iguais = $false
                $camposMudados += "$campo : $antes -> $depois"
            }
        }
    }
    Add-Gate 'C_nenhum_registrador_de_traducao_mudou' $iguais `
        'O dump GCVM antes e depois da construcao tem de ser identico.' `
        ([pscustomobject]@{ campos_mudados = $camposMudados })
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($built) {
        $rb = Run 'gcvm_ptrollback' @('gcvm','ptrollback') -AllowFailure
        $rbText = $rb.output -join "`n"
        $rbOk = ($rb.exit_code -eq 0 -and $rbText -match 'divergencias\s+:\s+0')
        Add-Gate 'D_regiao_restaurada' $rbOk `
            'A regiao da tabela volta ao conteudo original, dword a dword.' `
            ([pscustomobject]@{ exit_code = $rb.exit_code; saida = $rb.output })
    }
    if ($armed) { Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null }
}

try {
    $dumpFinal = Run 'gcvm_dump_final' @('gcvm','dump') -AllowFailure
    $ctxFinal = Get-GcvmField $dumpFinal 'CONTEXT0_CNTL'
    Add-Gate 'E_estado_final_igual_ao_post' ($ctxFinal -eq $ctx0) `
        'CONTEXT0_CNTL no fim igual ao do inicio.' `
        ([pscustomobject]@{ inicio = $ctx0; fim = $ctxFinal })
} catch { if (-not $failure) { $failure = $_.Exception.Message } }

$reprovados = @($gates | Where-Object { -not $_.passed } | ForEach-Object { $_.gate })
if ($reprovados.Count -and -not $failure) { $failure = 'Gates reprovados: ' + ($reprovados -join ', ') }

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Step 1 of the memory gate: build a page table in VRAM and verify it by CPU readback'
    verdict = $verdict
    passed = (-not $failure)
    failure = $failure
    touched_translation_registers = $false
    not_covered = @(
        'Whether the GPU accepts this table: that is step 5 and requires enabling translation',
        'Page table depth, aperture placement and invalidation are not exercised here'
    )
    environment = [pscustomobject]@{
        driver_sha256 = $driverHash.ToLowerInvariant()
        table_offset = ('0x{0:X8}' -f $TableOffset)
        target_offset = ('0x{0:X8}' -f $TargetOffset)
        pages = $Pages
    }
    gates = $gates
    failed_gates = $reprovados
    commands = $events
}
$record | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $dir 'result.json') -Encoding UTF8
$events | ForEach-Object {
    "=== $($_.name) ==="; "`$ $($_.command)"; $_.output; "exit=$($_.exit_code)"; ''
} | Set-Content (Join-Path $dir 'result.txt') -Encoding UTF8

Write-Host "Evidencia: $dir"
foreach ($g in $gates) {
    Write-Host ("  [{0}] {1}" -f $(if ($g.passed) { 'ok  ' } else { 'FALHA' }), $g.gate)
}
if ($failure) { Write-Error $failure; exit 1 }
Write-Host 'Passo 1 do gate de memoria: page table construida e conferida.'
Write-Host 'Nenhum registrador de traducao foi tocado; a placa esta como estava.'
Write-Host "Veredito global permanece: $verdict"
exit 0
