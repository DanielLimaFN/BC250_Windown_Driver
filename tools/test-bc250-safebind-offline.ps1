[CmdletBinding()]
param()

# ==========================================================================
# Ensaio a SECO do harness de safe-bind. Zero instalacao, zero hardware.
#
# Existe por causa do 3.79: a correcao do guardiao e da recuperacao nao pode
# ser exercitada pela primeira vez durante um ensaio fisico, porque o ensaio
# e' caro, cabe uma submissao por boot frio, e a tela fica apagada justamente
# quando o guardiao precisa funcionar. Este script prova offline tudo o que
# nao depende de silicio.
#
# Cobre:
#   1. Parse de todos os scripts do harness.
#   2. DRY-RUN do guardiao com configuracao valida.
#   3. Fail-closed do guardiao contra sete configuracoes invalidas.
#   4. Get-Bc250PnpVerdict lido contra o dispositivo real (leitura pura).
#   5. Laco de polling da recuperacao: caminho feliz e caminho de timeout.
#
# NAO cobre, e nao pretende cobrir: qualquer coisa que exija a GPU.
# O veredito continua NAO VALIDADO EM HARDWARE.
# ==========================================================================

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $env:TEMP ('bc250-safebind-offline-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
$fail = 0
function Ok([string]$m) { Write-Host "  OK   $m" -ForegroundColor Green }
function Bad([string]$m) { Write-Host "  FAIL $m" -ForegroundColor Red; $script:fail++ }

# Invocacao de nativo com a preferencia rebaixada: sem isso o proprio ensaio
# morre no NativeCommandError do stderr, que e' a mesma armadilha do PS 5.1
# corrigida dentro do guardiao.
function Invoke-Guarded([scriptblock]$Block) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Block } finally { $ErrorActionPreference = $previous }
}

Write-Host ''
Write-Host '=== 1. Parse dos scripts do harness ===' -ForegroundColor Cyan
foreach ($relative in @(
        'tools\watch-bc250mcdm-safe-bind.ps1',
        'tools\bind-bc250mcdm-lab.ps1',
        'tools\bind-bc250mcdm-renderonly-safe.ps1',
        'tools\confirm-bc250mcdm-safe-bind.ps1',
        'tools\inspect-bc250mcdm-rendernode.ps1',
        'tools\inspect-bc250mcdm-renderonly-safe.ps1',
        'tools\audit-bc250mcdm-lab-package.ps1',
        'tools\package-bc250mcdm-lab.ps1',
        'tools\package-bc250mcdm-renderonly-safe.ps1')) {
    $path = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $path)) { Bad "$relative ausente"; continue }
    $errors = $null
    $null = [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$null, [ref]$errors)
    if ($errors -and $errors.Count -gt 0) { Bad "$relative -> $($errors[0].Message)" }
    else { Ok $relative }
}

$guard = Join-Path $root 'tools\watch-bc250mcdm-safe-bind.ps1'
$recovery = Join-Path $root 'tools\BC250_RECOVERY.cmd'
$instanceId = 'PCI\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00\4&19CAF403&0&0041'
$evidence = Join-Path $work 'evidence'
New-Item -ItemType Directory -Path $evidence -Force | Out-Null

function New-GuardConfig([hashtable]$Override) {
    $armedAtUtc = [datetime]::UtcNow
    $deadlineUtc = $armedAtUtc.AddSeconds(240)
    $c = [ordered]@{
        schema_version = 1
        task_name = 'BC250-SafeBindGuard-OFFLINE-TEST'
        deadline_task_name = 'BC250-SafeBindDeadline-OFFLINE-TEST'
        token = [guid]::NewGuid().ToString('D')
        instance_id = $instanceId
        timeout_seconds = 240
        evidence_dir = $evidence
        recovery_path = $recovery
        recovery_sha256 = (Get-FileHash $recovery -Algorithm SHA256).Hash
        armed_boot_ticks = 1
        armed_at_utc_ticks = [long]$armedAtUtc.Ticks
        deadline_utc_ticks = [long]$deadlineUtc.Ticks
        automatic_recovery_reboot = $true
        hardware_validated = $false
    }
    foreach ($k in $Override.Keys) {
        if ($null -eq $Override[$k]) { $c.Remove($k) } else { $c[$k] = $Override[$k] }
    }
    $p = Join-Path $work ('cfg-' + [guid]::NewGuid().ToString('N') + '.json')
    $c | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $p -Encoding UTF8
    return $p
}

Write-Host '=== 2. DRY-RUN do guardiao com configuracao valida ===' -ForegroundColor Cyan
$cfg = New-GuardConfig @{}
$out = Invoke-Guarded { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $guard -ConfigPath $cfg 2>&1 }
if ($LASTEXITCODE -eq 0 -and ($out -join ' ') -match 'DRY-RUN') { Ok "aceito: $out" }
else { Bad "recusou configuracao valida (exit=$LASTEXITCODE): $out" }

Write-Host '=== 3. Fail-closed contra configuracao invalida ===' -ForegroundColor Cyan
foreach ($case in @(
        @{ nome = 'campo automatic_recovery_reboot ausente'; ov = @{ automatic_recovery_reboot = $null } },
        @{ nome = 'campo recovery_sha256 ausente';           ov = @{ recovery_sha256 = $null } },
        @{ nome = 'campo armed_boot_ticks ausente';          ov = @{ armed_boot_ticks = $null } },
        @{ nome = 'campo deadline_task_name ausente';        ov = @{ deadline_task_name = $null } },
        @{ nome = 'campo deadline_utc_ticks ausente';        ov = @{ deadline_utc_ticks = $null } },
        @{ nome = 'prazo absoluto incoerente';               ov = @{ deadline_utc_ticks = 1 } },
        @{ nome = 'schema_version diferente de 1';           ov = @{ schema_version = 2 } },
        @{ nome = 'instance_id de outro dispositivo';        ov = @{ instance_id = 'PCI\VEN_10DE&DEV_1234\1' } },
        @{ nome = 'timeout abaixo do minimo';                ov = @{ timeout_seconds = 30 } },
        @{ nome = 'timeout acima do maximo';                 ov = @{ timeout_seconds = 900 } },
        @{ nome = 'token que nao e GUID';                    ov = @{ token = 'nao-e-um-guid' } })) {
    $p = New-GuardConfig $case.ov
    $null = Invoke-Guarded { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $guard -ConfigPath $p 2>&1 }
    if ($LASTEXITCODE -ne 0) { Ok "recusou: $($case.nome)" }
    else { Bad "ACEITOU configuracao invalida: $($case.nome)" }
}

Write-Host '=== 4. Veredito de PnP por amostragem (leitura pura) ===' -ForegroundColor Cyan
# Reaproveita a funcao real do guardiao sem executar o corpo do script.
$text = Get-Content -LiteralPath $guard -Raw
$start = $text.IndexOf('function Get-Bc250PnpVerdict')
$stop = $text.IndexOf('# A4 ', $start)
if ($start -lt 0 -or $stop -lt 0) {
    Bad 'nao foi possivel isolar Get-Bc250PnpVerdict no guardiao'
} else {
    $body = $text.Substring($start, $stop - $start)
    $body = $body.Substring(0, $body.LastIndexOf('}') + 1)
    $config = [pscustomobject]@{ instance_id = $instanceId }
    $samplesPath = Join-Path $evidence 'offline-samples.json'
    function Write-AtomicJson([string]$Path, [object]$Value) {
        $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding UTF8
    }
    Invoke-Expression $body
    $verdict = Get-Bc250PnpVerdict
    if ($verdict.samples.Count -eq 5) { Ok 'coletou exatamente 5 amostras' }
    else { Bad "coletou $($verdict.samples.Count) amostras" }
    Write-Host ("       healthy={0}; servico={1}; codigo={2}" -f
        $verdict.healthy, $verdict.samples[0].service, $verdict.samples[0].code)
    if ($verdict.healthy) { Ok 'dispositivo em BasicDisplay/Codigo 0 nas cinco amostras' }
    else { Bad 'dispositivo NAO esta em BasicDisplay/Codigo 0; nao vincule nada' }
}

Write-Host '=== 5. Laco de polling da recuperacao ===' -ForegroundColor Cyan
# Extrai o comando real de :poll_basic_display e o executa isolado, para que o
# ensaio prove o codigo do arquivo e nao uma copia que pode divergir.
# @() obrigatorio: com um unico resultado, Where-Object devolve a String e
# $cmdLines[0] passaria a indexar CARACTERES em vez de linhas.
$cmdLines = @((Get-Content -LiteralPath $recovery) | Where-Object { $_ -match 'amostra \{0\}' })
if ($cmdLines.Count -ne 1) {
    Bad "esperava uma linha de polling em BC250_RECOVERY.cmd, achei $($cmdLines.Count)"
} else {
    $line = [string]$cmdLines[0]
    $body = $line.Substring(
        $line.IndexOf('-Command "') + 10,
        $line.LastIndexOf('" >>') - ($line.IndexOf('-Command "') + 10))
    $body = $body.Replace('%INSTANCE_ID%', $instanceId).
        Replace('%RESCAN_EVERY%', '20').Replace('%POLL_INTERVAL%', '2')

    $happy = Join-Path $work 'poll-happy.ps1'
    $body.Replace('%~1', '10') | Set-Content -LiteralPath $happy -Encoding ASCII
    $happyOut = Invoke-Guarded { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $happy 2>&1 }
    $happyLine = @($happyOut | Where-Object { $_ -match 'amostra \d+' })
    if ($LASTEXITCODE -eq 0) { Ok "caminho feliz sai 0 na primeira amostra:$($happyLine -join '')" }
    else { Bad "caminho feliz devolveu $LASTEXITCODE" }

    # Dispositivo inexistente: prova o timeout, a cadencia e o exit 1.
    $sad = Join-Path $work 'poll-sad.ps1'
    $body.Replace('%~1', '5').Replace('DEV_13FE', 'DEV_DEAD') |
        Set-Content -LiteralPath $sad -Encoding ASCII
    $sadOut = Invoke-Guarded { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $sad 2>&1 }
    $sadExit = $LASTEXITCODE
    $sampleCount = @($sadOut | Where-Object { $_ -match 'amostra \d+' }).Count
    if ($sadExit -eq 1) { Ok "timeout devolve exit 1 apos $sampleCount amostras registradas" }
    else { Bad "timeout devolveu $sadExit (esperado 1)" }
    if ($sampleCount -ge 3) { Ok 'cada amostra foi registrada individualmente' }
    else { Bad "apenas $sampleCount amostras registradas em 5 s" }
}

Write-Host '=== 6. Prazo absoluto e ordem fail-closed ===' -ForegroundColor Cyan
$watchText = Get-Content -LiteralPath $guard -Raw
$rollbackIndex = $watchText.IndexOf("Write-State 'ROLLBACK_STARTED'")
$recoveryIndex = $watchText.IndexOf('& cmd.exe /d /c', $rollbackIndex)
if ($rollbackIndex -ge 0 -and $recoveryIndex -gt $rollbackIndex) {
    Ok 'ROLLBACK_STARTED e publicado antes da recovery'
    $criticalWindow = $watchText.Substring(
        $rollbackIndex, $recoveryIndex - $rollbackIndex)
    if ($criticalWindow -notmatch 'Get-CimInstance|Get-WinEvent') {
        Ok 'nenhuma consulta WMI/evento bloqueia a recovery depois do prazo'
    } else {
        Bad 'consulta WMI/evento encontrada antes da recovery depois do prazo'
    }
} else {
    Bad 'nao localizou a ordem ROLLBACK_STARTED -> recovery'
}
$labBindText = Get-Content -LiteralPath `
    (Join-Path $root 'tools\bind-bc250mcdm-lab.ps1') -Raw
foreach ($needle in @('BC250-SafeBindDeadline','-DeadlineEnforcer',
        'deadline_utc_ticks','Global\BC250SafeBindRecovery')) {
    $haystack = if ($needle -eq 'Global\BC250SafeBindRecovery') {
        $watchText
    } else { $labBindText }
    if ($haystack.Contains($needle)) { Ok "contrato presente: $needle" }
    else { Bad "contrato ausente: $needle" }
}

Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ''
if ($fail -eq 0) {
    Write-Host 'ENSAIO OFFLINE DO SAFE-BIND: PASS' -ForegroundColor Green
    Write-Host 'Zero instalacao, zero hardware. NAO VALIDADO EM HARDWARE.' -ForegroundColor Yellow
    exit 0
}
Write-Host "ENSAIO OFFLINE DO SAFE-BIND: $fail FALHA(S)" -ForegroundColor Red
exit 1
