<#
    Ensaio de Matriz de Firmwares VCN:
    Verifica se o PSP SOS (psp_v11_0_8) aceita algum dos 28 binarios VCN do upstream.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,60)][int]$CommandTimeoutSeconds = 25,
    [int[]]$TypesToTest = @(13, 29) # 13=VCN/UVD, 29=VCN0
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$abiHeader = Join-Path $root 'inc\bc250dbg_abi.h'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

$vcnDir = Join-Path $root 'firmware\vcn'
if (-not (Test-Path $vcnDir)) {
    throw "Diretorio de firmwares VCN ausente: $vcnDir"
}

$firmwareFiles = Get-ChildItem -Path $vcnDir -Filter '*.bin' | Sort-Object Name
if ($firmwareFiles.Count -eq 0) {
    throw "Nenhum arquivo .bin encontrado em $vcnDir. Execute tools\download-vcn-firmwares.ps1 primeiro."
}

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute; ele envia comandos LOAD_IP_FW para o PSP.'
}

$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }

foreach ($path in @($cli,$abiHeader,$workspaceDriver,$installedDriver)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}

if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }

$driverHash = (Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if ($driverHash -ne (Get-FileHash $installedDriver -Algorithm SHA256).Hash) { throw 'Build instalado diverge do workspace.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-vcn-firmware-matrix"
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

$failure = $null
$armed = $false; $pspCreated = $false
$resultados = @()

try {
    $infoPre = Run 'info_pre' @('info')
    if (($infoPre.output -join ' ') -notmatch 'bc250dbg v\d+\.\d+\s+ABI \d+') {
        throw 'Preflight recusado: info nao reportou ABI do bc250dbg.'
    }

    Run 'arm_write' @('arm','write') | Out-Null; $armed = $true
    Run 'psp_init' @('psp','init') | Out-Null; $pspCreated = $true

    Add-Gate 'A_psp_ring_inicializado' $true 'Anel GPCOM do PSP criado com sucesso.' $null

    foreach ($fileItem in $firmwareFiles) {
        $relPath = "firmware\vcn\$($fileItem.Name)"
        
        foreach ($fwType in $TypesToTest) {
            $testName = "loadfw_type${fwType}_$($fileItem.BaseName)"
            $r = Run $testName @('psp','loadfw',"$fwType",$relPath) -AllowFailure
            
            $texto = $r.output -join "`n"
            $resp = if ($texto -match 'RespStatus\s+:\s+0x([0-9A-Fa-f]{8})') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
            $fence = ($texto -match 'fence\s+:\s+alcancado')
            $versao = if ($texto -match 'versao ucode\s*:\s*0x([0-9A-Fa-f]+)') { '0x' + $Matches[1] } else { $null }
            $dwords = if ($texto -match 'payload\s*:\s*(\d+)\s+dwords') { [int]$Matches[1] } else { $null }
            $aceito = ($fence -and $resp -eq 0)

            $resObj = [pscustomobject]@{
                arquivo = $fileItem.Name
                caminho = $relPath
                tamanho_bytes = $fileItem.Length
                tipo_psp = $fwType
                versao_ucode = $versao
                payload_dwords = $dwords
                fence_alcancada = $fence
                resp_status = if ($null -ne $resp) { '0x{0:X8}' -f $resp } else { 'SEM_RESPOSTA' }
                aceito = $aceito
                exit_code = $r.exit_code
                saida = $r.output
            }
            $resultados += $resObj

            $statusStr = if ($aceito) { 'ACEITO' } else { "Recusado ($($resObj.resp_status))" }
            Write-Host ("  [{0,-4}] {1,-28} Type {2,2} -> {3}" -f $(if ($fence) { 'OK' } else { 'TO' }), $fileItem.Name, $fwType, $statusStr)
        }
    }

    $semResposta = @($resultados | Where-Object { -not $_.fence_alcancada } | ForEach-Object { "$($_.arquivo) (tipo $($_.tipo_psp))" })
    Add-Gate 'B_psp_respondeu_todas_submissoes' ($semResposta.Count -eq 0) `
        'O PSP consumiu o frame e retornou fence/status para todas as tentativas.' `
        ([pscustomobject]@{ sem_resposta = $semResposta; total = $resultados.Count })

    $aceitosList = @($resultados | Where-Object { $_.aceito })
    Add-Gate 'C_registro_resultados_vcn' $true `
        ("Total de {0} tentativas executadas. {1} aceitas pelo PSP." -f $resultados.Count, $aceitosList.Count) `
        ([pscustomobject]@{ aceitos_count = $aceitosList.Count; aceitos = $aceitosList })

} catch {
    $failure = $_.Exception.Message
} finally {
    if ($armed) {
        if ($pspCreated) { Run 'psp_rollback' @('psp','rollback') -AllowFailure | Out-Null }
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
}

$bugchecks = @()
try {
    $bugchecks = @(Get-WinEvent -FilterHashtable @{ LogName='System'; Id=1001; StartTime=$startedAt } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -like '*WER-SystemErrorReporting*' } |
        ForEach-Object { [pscustomobject]@{ time = $_.TimeCreated.ToString('o'); message = ($_.Message -split "`r?`n")[0] } })
} catch { }
Add-Gate 'D_sem_bugcheck' ($bugchecks.Count -eq 0) 'Nenhum bugcheck na janela do ensaio.' $bugchecks

$reprovados = @($gates | Where-Object { -not $_.passed } | ForEach-Object { $_.gate })
if ($reprovados.Count -and -not $failure) { $failure = 'Gates reprovados: ' + ($reprovados -join ', ') }

$aceitos = @($resultados | Where-Object { $_.aceito } | ForEach-Object { "$($_.arquivo) (tipo $($_.tipo_psp))" })
$recusados = @($resultados | Where-Object { -not $_.aceito } | ForEach-Object { "$($_.arquivo) (tipo $($_.tipo_psp))" })

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'VCN Firmware Matrix: test which VCN firmwares from Linux upstream are accepted by the BC-250 PSP SOS'
    verdict = $verdict
    passed = (-not $failure)
    failure = $failure
    total_tentativas = $resultados.Count
    firmware_accepted = $aceitos
    firmware_refused = $recusados
    reboot_required = $armed
    environment = [pscustomobject]@{
        driver_sha256 = $driverHash.ToLowerInvariant()
        types_tested = $TypesToTest
    }
    firmwares = $resultados
    gates = $gates
    failed_gates = $reprovados
    bugchecks = $bugchecks
    commands = $events
}

$record | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $dir 'result.json') -Encoding UTF8
$events | ForEach-Object {
    "=== $($_.name) ==="; "`$ $($_.command)"; $_.output; "exit=$($_.exit_code)"; ''
} | Set-Content (Join-Path $dir 'result.txt') -Encoding UTF8

Write-Host ""
Write-Host "================================================================="
Write-Host "Evidencia gravada em: $dir"
Write-Host "================================================================="
foreach ($g in $gates) {
    Write-Host ("  [{0}] {1}: {2}" -f $(if ($g.passed) { 'OK  ' } else { 'FALHA' }), $g.gate, $g.detail)
}
Write-Host ""
Write-Host ("Total de testes : {0}" -f $resultados.Count)
Write-Host ("Aceitos pelo PSP: {0}" -f $(if ($aceitos.Count) { ($aceitos -join ', ') } else { 'NENHUM (todos recusados)' }))
Write-Host ("Veredito global : $verdict")
if ($armed) { Write-Host 'Nota: firmware carregado no PSP permanece ate o reboot.' }
if ($failure) { Write-Error $failure; exit 1 }
exit 0
