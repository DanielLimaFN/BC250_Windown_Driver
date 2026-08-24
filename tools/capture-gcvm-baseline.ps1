<#
    Captura SOMENTE LEITURA do estado de traducao do GC (GCVM/GCMC).

    Primeiro passo do gate de memoria. Toda a execucao de SDMA demonstrada neste
    projeto acontece sem page table nossa; antes de montar VMID 0 e preciso
    saber, campo a campo, o que o POST deixou configurado.

    Nada aqui arma escrita, inicializa engine, carrega firmware ou toca em
    registrador. Por isso este ensaio NAO exige boot limpo e NAO condena a
    sessao: pode rodar a qualquer momento, inclusive numa sessao ja usada, e o
    resultado continua valido como observacao daquele instante.

    A comparacao com o que o upstream exige e feita por tools/decode-gcvm.py e
    gravada junto com a evidencia.
#>
param(
    [string]$OutputRoot,
    [ValidateRange(2,60)][int]$CommandTimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)

if (-not (Test-Path -LiteralPath $cli)) { throw "CLI ausente: $cli" }
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-gcvm-baseline"
New-Item -ItemType Directory -Path $dir -Force | Out-Null

$events = [System.Collections.Generic.List[object]]::new()
function Run([string]$Name, [string[]]$Arguments) {
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
    $r = [pscustomobject]@{
        name = $Name
        command = 'bc250ctl.exe ' + ($Arguments -join ' ')
        exit_code = if ($to) { 1460 } else { $p.ExitCode }
        timed_out = $to
        output = @(($ot.Result + $et.Result) -split "`r?`n" | Where-Object { $_ })
    }
    $events.Add($r)
    return $r
}

$info = Run 'info' @('info')
$abiOk = (($info.output -join ' ') -match 'bc250dbg v\d+\.\d+\s+ABI \d+')
$armamento = if (($info.output -join ' ') -match 'armamento=(\w+)') { $Matches[1] } else { 'desconhecido' }

$dump = Run 'gcvm_dump' @('gcvm','dump')
$status = Run 'gcvm_status' @('gcvm','status')
$fault = Run 'sdma_fault' @('sdma','fault')

$dumpFile = Join-Path $dir 'gcvm-dump.txt'
$dump.output | Set-Content $dumpFile -Encoding UTF8

$decoded = $null
$decoder = Join-Path $root 'tools\decode-gcvm.py'
if ((Get-Command python -ErrorAction SilentlyContinue) -and (Test-Path $decoder)) {
    try {
        $decoded = & python $decoder $dumpFile 2>&1
        $decoded | Set-Content (Join-Path $dir 'gcvm-decoded.txt') -Encoding UTF8
    } catch { }
}
$divergencias = $null
if ($decoded) {
    $linha = @($decoded | Where-Object { $_ -match 'Divergencias contra o upstream: (\d+)' })
    if ($linha.Count) {
        if ($linha[0] -match 'Divergencias contra o upstream: (\d+)') { $divergencias = [int]$Matches[1] }
    }
}

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Read-only capture of the GC translation state, as left by POST'
    verdict = $verdict
    read_only = $true
    abi_esperada_ok = $abiOk
    armamento = $armamento
    divergencias_contra_upstream = $divergencias
    nota = @(
        'Divergencia contra o upstream nao e defeito: o POST configurou a placa para o proprio uso.',
        'Este ensaio nao responde se existem page tables nem onde elas estao.',
        'Nenhum registrador foi escrito; a sessao nao foi contaminada.',
        'GCVM_INVALIDATE_ENG0_SEM nao e lido: ler esse registrador ADQUIRE o semaforo (medido em 2026-08-23).'
    )
    commands = $events
}
$record | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $dir 'result.json') -Encoding UTF8
$events | ForEach-Object {
    "=== $($_.name) ==="; "`$ $($_.command)"; $_.output; "exit=$($_.exit_code)"; ''
} | Set-Content (Join-Path $dir 'result.txt') -Encoding UTF8

Write-Host "Evidencia: $dir"
if (-not $abiOk) { Write-Host '  ATENCAO: info nao reportou versao/ABI do driver.' }
Write-Host ("  armamento: {0} (nenhuma escrita foi feita)" -f $armamento)
if ($null -ne $divergencias) {
    Write-Host ("  divergencias contra gfxhub_v2_0.c do upstream: {0}" -f $divergencias)
    Write-Host '  detalhe campo a campo em gcvm-decoded.txt'
} else {
    Write-Host '  decodificador nao rodou; gcvm-dump.txt tem os valores crus.'
}
Write-Host "Veredito global permanece: $verdict"
exit 0
