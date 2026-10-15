[CmdletBinding()]
param(
    # Espera antes de coletar: o driver despeja as migalhas (DX66/EVT1) no log
    # durante o PnP do boot; a coleta precisa vir depois.
    [int]$WaitSeconds = 90,
    # Coleta mesmo sem desligamento sujo (teste manual).
    [switch]$Force
)

# coleta-automatica-no-boot.ps1 (doc 594) -- roda como SYSTEM a cada boot, pela tarefa
# "BC250 coleta pos-trava no boot" (tools\registrar-coleta-automatica.ps1).
# Se o boot anterior terminou SUJO (Kernel-Power 41, EventLog 6008 ou BugCheck 1001
# depois do inicio deste boot) e a ultima pasta *-lab-binding ainda nao foi coletada,
# roda tools\coletar-pos-trava.ps1 nela e grava coleta-automatica.json. Assim o motivo
# do reset, os logs da UMD e as caudas do log/rastro nao se perdem se o Daniel esquecer
# a coleta manual ou rodar a recuperacao antes.
# So' le e copia (o mesmo que a coleta manual). Nao mexe em driver, registro nem dispositivo.

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$logPath = 'C:\BC250_COLETA_AUTOMATICA.log'
try { Start-Transcript -LiteralPath $logPath -Append | Out-Null } catch { }

function Note([string]$m) { Write-Host ("[{0}] {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $m) }

try {
    $configPath = Join-Path $PSScriptRoot 'coleta-automatica-config.json'
    if (Test-Path -LiteralPath $configPath) {
        $cfg = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
        if ($cfg.PythonDir -and (Test-Path -LiteralPath $cfg.PythonDir)) {
            $env:PATH = $cfg.PythonDir + ';' + $env:PATH
        }
    }

    $bootTime = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
    Note "Boot atual: $bootTime. Esperando $WaitSeconds s para o driver despejar as migalhas no log."
    Start-Sleep -Seconds $WaitSeconds

    $dirty = @()
    foreach ($q in @(
            @{ LogName = 'System'; ProviderName = 'Microsoft-Windows-Kernel-Power'; Id = 41 },
            @{ LogName = 'System'; Id = 6008 },
            @{ LogName = 'System'; Id = 1001; ProviderName = 'Microsoft-Windows-WER-SystemErrorReporting' })) {
        $q.StartTime = $bootTime.AddMinutes(-1)
        try { $dirty += @(Get-WinEvent -FilterHashtable $q -ErrorAction Stop) } catch { }
    }
    if ($dirty.Count -gt 0) {
        Note ("Desligamento SUJO no boot anterior: " + (($dirty | ForEach-Object { "Id=$($_.Id)" }) -join ', '))
    } else {
        Note 'Boot anterior terminou limpo (sem eventos 41/6008/1001).'
    }
    if ($dirty.Count -eq 0 -and -not $Force) { Note 'Nada a coletar.'; return }

    $last = Get-ChildItem -LiteralPath (Join-Path $root 'docs\evidence') -Directory -ErrorAction Stop |
        Where-Object { $_.Name -like '*-lab-binding' } | Sort-Object CreationTime | Select-Object -Last 1
    if (-not $last) { Note 'Nenhuma pasta *-lab-binding.'; return }
    $marker = Join-Path $last.FullName 'coleta-automatica.json'
    if ((Test-Path -LiteralPath $marker) -and -not $Force) { Note "Ja coletada: $($last.Name)"; return }
    if ((Test-Path -LiteralPath (Join-Path $last.FullName 'reset-reason-pos-trava.txt')) -and -not $Force) { Note "Coleta manual ja feita: $($last.Name)"; return }
    if ($last.CreationTime -ge $bootTime) { Note "A pasta mais recente e' deste boot ($($last.Name)); nada a coletar."; return }
    if ($last.CreationTime -lt $bootTime.AddHours(-12) -and -not $Force) {
        Note "A pasta mais recente e' de mais de 12 h antes deste boot ($($last.Name)); nao e' a corrida que caiu."; return
    }

    Note "Coletando: $($last.FullName)"
    $collector = Join-Path $root 'tools\coletar-pos-trava.ps1'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $collector -EvidenceDir $last.FullName
    $code = $LASTEXITCODE

    # Doc 595: teste de RAM logo depois da coleta (a GDDR6 e' RAM e VRAM ao mesmo tempo).
    $ramExe = Join-Path $root 'tools\bc250-ramtest\out\bc250_ramtest.exe'
    $ramOut = Join-Path $last.FullName 'ramtest-pos-trava.txt'
    $ramCode = $null
    if (Test-Path -LiteralPath $ramExe) {
        Note 'Rodando o teste de RAM (70% da RAM livre, 2 passadas)...'
        & $ramExe --passes 2 --out $ramOut
        $ramCode = $LASTEXITCODE
        Note "Teste de RAM: exit=$ramCode (0 = sem erros, 1 = ERROS de memoria, 2 = nao rodou). Relatorio: $ramOut"
    } else {
        Note "Teste de RAM ausente ($ramExe); rode tools\bc250-ramtest\build.bat."
    }
    [ordered]@{
        collected_at   = (Get-Date).ToString('o')
        boot_time      = $bootTime.ToString('o')
        dirty_events   = @($dirty | ForEach-Object { [ordered]@{ id = $_.Id; time = $_.TimeCreated.ToString('o') } })
        collector_exit = $code
        ramtest_exit   = $ramCode
        transcript     = $logPath
        note           = 'Coleta automatica no boot (doc 594). So leitura e copia.'
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $marker -Encoding UTF8
    Note "Coleta concluida (exit=$code). Marcador: $marker"
} catch {
    Note "FALHA na coleta automatica: $($_.Exception.Message)"
} finally {
    try { Stop-Transcript | Out-Null } catch { }
}
