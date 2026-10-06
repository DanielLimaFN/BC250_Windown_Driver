[CmdletBinding()]
param(
    # Remove a tarefa em vez de registrar.
    [switch]$Remover
)

# registrar-coleta-automatica.ps1 (doc 594) -- rodar UMA VEZ, em PowerShell ELEVADO.
# Registra a tarefa agendada "BC250 coleta pos-trava no boot": roda como SYSTEM a cada
# inicializacao o tools\coleta-automatica-no-boot.ps1, que so' coleta quando o boot
# anterior terminou sujo. -Remover desfaz.

$ErrorActionPreference = 'Stop'
$taskName = 'BC250 coleta pos-trava no boot'
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Rode em PowerShell como Administrador.'
}

if ($Remover) {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "Tarefa removida: $taskName" -ForegroundColor Yellow
    return
}

$script = Join-Path $PSScriptRoot 'coleta-automatica-no-boot.ps1'
if (-not (Test-Path -LiteralPath $script)) { throw "Script ausente: $script" }

# O SYSTEM nao tem o PATH do usuario: grava onde esta o python (o coletor decodifica o rastro com ele).
$py = Get-Command python -ErrorAction SilentlyContinue
$pyDir = if ($py) { Split-Path -Parent $py.Source } else { '' }
if (-not $pyDir) { Write-Host 'AVISO: python nao encontrado; o rastro nao sera decodificado na coleta automatica.' -ForegroundColor Yellow }
[ordered]@{ PythonDir = $pyDir; RegisteredAt = (Get-Date).ToString('o') } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'coleta-automatica-config.json') -Encoding UTF8

$action = New-ScheduledTaskAction -Execute 'powershell.exe' `
    -Argument ('-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "{0}"' -f $script)
$trigger = New-ScheduledTaskTrigger -AtStartup
$taskPrincipal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Minutes 30) -StartWhenAvailable
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $taskPrincipal `
    -Settings $settings -Description 'BC-250: coleta pos-trava automatica no boot (doc 594). So le e copia.' -Force | Out-Null

$t = Get-ScheduledTask -TaskName $taskName
Write-Host "Tarefa registrada: $($t.TaskName) (estado: $($t.State))" -ForegroundColor Green
Write-Host "Script: $script"
Write-Host "Python: $pyDir"
Write-Host 'Transcricao de cada boot: C:\BC250_COLETA_AUTOMATICA.log'
