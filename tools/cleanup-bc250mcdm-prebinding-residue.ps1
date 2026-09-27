<#
.SYNOPSIS
    Quarentena recuperavel de residuo orfao do pacote experimental bc250mcdm.

.DESCRIPTION
    Trata dois residuos que sobrevivem a remocao do pacote OEM:
      - o marcador C:\BC250_TEST_OEMINF.txt apontando para um INF inexistente;
      - o binario orfao %SystemRoot%\System32\drivers\bc250mcdm.sys.

    O SYS so e' aceito como orfao quando o Windows ja teve a chance de concluir
    a propria transacao de remocao:
      - servico bc250mcdm ausente e chave de servico ausente;
      - nenhum pacote OEM casando com o dispositivo e o servico;
      - BC-250 exclusivamente em BasicDisplay, Codigo 0;
      - PendingFileRenameOperations sem qualquer referencia ao binario;
      - ultimo boot POSTERIOR a ultima escrita do binario.

    Se houver operacao de rename pendente, ou se o boot for anterior a escrita
    do SYS, o cleanup recusa e pede reboot: a transacao e' do Windows, nao
    nossa.

    Nada e' apagado. Marcador e binario vao para C:\BC250_DRIVER_QUARANTINE com
    nome datado e evidencia arquivada; qualquer falha reverte as movimentacoes
    ja feitas.
#>
[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptOrphanCleanup
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$driverLeaf = 'bc250mcdm.sys'
$serviceRegistryPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm'
$serviceRegistryNative = 'HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm'
$sessionManagerPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager'

function Get-PendingRenameEntries {
    $property = Get-ItemProperty -LiteralPath $sessionManagerPath `
        -Name PendingFileRenameOperations -ErrorAction SilentlyContinue
    if ($null -eq $property) { return @() }
    return @($property.PendingFileRenameOperations |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Get-DriverPendingRenameEntries {
    return @(Get-PendingRenameEntries |
        Where-Object { $_ -match [regex]::Escape($driverLeaf) })
}

# Abrir o arquivo pedindo apenas o direito DELETE nao apaga nada: e' a unica
# forma barata de saber, ANTES de mexer em qualquer coisa, se a movimentacao
# para quarentena e' sequer possivel.
#
# Em 06/09/2026 a primeira execucao real descobriu isso no meio da transacao:
# o marcador ja havia sido movido quando o Move-Item do SYS bateu em "acesso
# negado" e o rollback teve de desfazer. A ACL de
# System32\drivers\bc250mcdm.sys nao tem NENHUMA entrada para Administradores
# -- so' SYSTEM, TrustedInstaller (FullControl) e Usuarios (ReadAndExecute) --
# entao nem elevado da para move-lo. Uma transacao que nao pode terminar nao
# deve comecar.
function Get-DriverDeleteAccess {
    try {
        $stream = New-Object System.IO.FileStream(
            $driverPath,
            [System.IO.FileMode]::Open,
            [System.Security.AccessControl.FileSystemRights]::Delete,
            [System.IO.FileShare]::ReadWrite,
            4096,
            [System.IO.FileOptions]::None)
        $stream.Dispose()
        return 'GRANTED'
    } catch [System.UnauthorizedAccessException] {
        return 'DENIED_BY_ACL'
    } catch [System.IO.IOException] {
        return 'IN_USE'
    } catch {
        return 'UNKNOWN'
    }
}

$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -like "$instancePrefix*" })
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'BasicDisplay' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'Cleanup recusado: BC-250 nao esta exclusivamente em BasicDisplay Codigo 0.'
}
$service = Get-Service -Name bc250mcdm -ErrorAction SilentlyContinue
$servicePresent = $null -ne $service
if ($servicePresent -and $service.Status -ne 'Stopped') {
    throw 'Cleanup recusado: servico bc250mcdm existe e nao esta parado.'
}
$serviceRegistryPresent = Test-Path -LiteralPath $serviceRegistryPath
if ($servicePresent) {
    if (-not $serviceRegistryPresent) {
        throw 'Cleanup recusado: servico existe, mas sua chave exata esta ausente.'
    }
    $serviceConfig = Get-ItemProperty -LiteralPath $serviceRegistryPath
    if ([string]$serviceConfig.ImagePath -ne '\SystemRoot\System32\drivers\bc250mcdm.sys') {
        throw 'Cleanup recusado: ImagePath do servico diverge do binario experimental exato.'
    }
} elseif ($serviceRegistryPresent) {
    throw 'Cleanup recusado: chave do servico existe sem servico enumeravel; reinicie e audite novamente.'
}

$driverPresent = $false
$driverHash = ''
$driverLength = 0L
$driverWriteUtc = [datetime]::MinValue
$lastBootUtc = ([datetime](Get-CimInstance Win32_OperatingSystem -ErrorAction Stop).LastBootUpTime).ToUniversalTime()
if (Test-Path -LiteralPath $driverPath) {
    # O SYS so e' orfao quando nada mais o referencia e o Windows ja fechou a
    # propria transacao de remocao. Sem isso, quem conclui e' o reboot.
    if ($servicePresent -or $serviceRegistryPresent) {
        throw 'Cleanup recusado: bc250mcdm.sys existe e ainda ha servico/chave referenciando o binario.'
    }
    $pendingDriverRenames = Get-DriverPendingRenameEntries
    if ($pendingDriverRenames.Count -ne 0) {
        throw ('Cleanup recusado: existe PendingFileRenameOperations para o binario ' +
            "($($pendingDriverRenames -join ' | ')). Reinicie e audite novamente; nao apague o SYS manualmente.")
    }
    $driverItem = Get-Item -LiteralPath $driverPath -ErrorAction Stop
    $driverWriteUtc = $driverItem.LastWriteTimeUtc
    $driverLength = $driverItem.Length
    if ($lastBootUtc -le $driverWriteUtc) {
        throw ('Cleanup recusado: o ultimo boot (' + $lastBootUtc.ToString('o') +
            ') nao e posterior a escrita do SYS (' + $driverWriteUtc.ToString('o') +
            '). Reinicie para o Windows concluir a propria transacao.')
    }
    $driverDeleteAccess = Get-DriverDeleteAccess
    if ($driverDeleteAccess -ne 'GRANTED') {
        throw ("Cleanup recusado: o SYS orfao nao pode ser movido ($driverDeleteAccess). " +
            'A ACL de System32\drivers so concede FullControl a SYSTEM e ' +
            'TrustedInstaller; nem elevado o Administrador tem DELETE. Nao altere ' +
            'a ACL nem tome posse do arquivo por causa disto: para a trilha de ' +
            'laboratorio o binario orfao nao bloqueia nada (o proprio install o ' +
            'sobrescreve), e para a trilha fail-closed a remocao deve ser ' +
            'agendada ao Windows, que a executa como SYSTEM no boot.')
    }
    $driverHash = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
    $driverPresent = $true
}

$matchingPackages = @(Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' |
    Where-Object {
        $text = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        $null -ne $text -and $text -match '(?i)PCI\\VEN_1002&DEV_13FE' -and
        $text -match '(?im)^AddService=bc250mcdm,' -and
        $text -match '(?im)^ServiceBinary=%12%\\bc250mcdm\.sys\s*$'
    })
if ($matchingPackages.Count -ne 0) {
    throw "Cleanup recusado: ainda existem pacotes MCDM: $($matchingPackages.Name -join ', ')."
}
$markerPresent = Test-Path -LiteralPath $markerPath
$markerBytes = @()
$markerText = ''
$markerIsValid = $false
if ($markerPresent) {
    $markerBytes = [System.IO.File]::ReadAllBytes($markerPath)
    $markerText = [System.Text.Encoding]::ASCII.GetString($markerBytes).Trim()
    $markerIsValid = $markerText -match '^oem\d+\.inf$'
    if ($markerIsValid -and
        (Test-Path -LiteralPath (Join-Path $env:SystemRoot "INF\$markerText"))) {
        throw 'Cleanup recusado: marcador aponta para um INF ainda existente.'
    }
}
if (-not $markerPresent -and -not $driverPresent) {
    throw 'Cleanup recusado: nenhum residuo orfao presente; estado nao corresponde ao caso auditado.'
}

Write-Host 'BC250 PREBINDING ORPHAN CLEANUP PREFLIGHT: PASS'
Write-Host "  device: $($devices[0].Service) / $($devices[0].ConfigManagerErrorCode)"
$serviceState = if ($servicePresent) { [string]$service.Status } else { 'Absent' }
Write-Host "  service: $serviceState; service key present: $serviceRegistryPresent"
Write-Host "  matching MCDM packages: $($matchingPackages.Count)"
if ($markerPresent) {
    Write-Host "  marker: present; bytes: $($markerBytes.Length); valid OEM name: $markerIsValid"
} else {
    Write-Host '  marker: absent'
}
if ($driverPresent) {
    Write-Host "  orphan SYS: present; bytes: $driverLength; sha256: $driverHash"
    Write-Host "  orphan SYS last write (UTC): $($driverWriteUtc.ToString('o'))"
    Write-Host "  last boot (UTC):             $($lastBootUtc.ToString('o'))"
    Write-Host '  pending rename for SYS: none'
} else {
    Write-Host '  orphan SYS: absent'
}
if (-not $Execute) {
    if ($IAcceptOrphanCleanup) {
        throw '-IAcceptOrphanCleanup nao e valido sem -Execute.'
    }
    Write-Host 'DRY-RUN: zero escrita.'
    exit 0
}
if (-not $IAcceptOrphanCleanup) {
    throw 'Cleanup recusado: falta -IAcceptOrphanCleanup.'
}
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Cleanup exige PowerShell elevado.'
}

# Second gate immediately before the exact mutations.
$secondDevice = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -like "$instancePrefix*" })
$secondService = Get-Service -Name bc250mcdm -ErrorAction SilentlyContinue
$secondServicePresent = $null -ne $secondService
if ($secondDevice.Count -ne 1 -or $secondDevice[0].Service -ne 'BasicDisplay' -or
    $secondDevice[0].ConfigManagerErrorCode -ne 0 -or
    $secondServicePresent -ne $servicePresent -or
    ($secondServicePresent -and $secondService.Status -ne 'Stopped') -or
    (Test-Path -LiteralPath $serviceRegistryPath) -ne $serviceRegistryPresent -or
    (Test-Path -LiteralPath $driverPath) -ne $driverPresent -or
    (Test-Path -LiteralPath $markerPath) -ne $markerPresent) {
    throw 'Cleanup abortado: estado mudou depois do preflight.'
}
if ($driverPresent) {
    if ((Get-DriverPendingRenameEntries).Count -ne 0) {
        throw 'Cleanup abortado: PendingFileRenameOperations para o binario apareceu depois do preflight.'
    }
    if ((Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash -ne $driverHash) {
        throw 'Cleanup abortado: o SYS orfao mudou depois do preflight.'
    }
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-mcdm-orphan-cleanup"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null
$registryBackup = Join-Path $evidenceDir 'bc250mcdm-service.reg'
if ($servicePresent) {
    & reg.exe export $serviceRegistryNative $registryBackup /y | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $registryBackup)) {
        throw 'Cleanup abortado: export recuperavel da chave do servico falhou.'
    }
}
$quarantineDir = 'C:\BC250_DRIVER_QUARANTINE'
if (-not (Test-Path -LiteralPath $quarantineDir)) {
    New-Item -ItemType Directory -Path $quarantineDir | Out-Null
}

$quarantinedMarker = $null
if ($markerPresent) {
    $quarantinedMarker = Join-Path $quarantineDir "BC250_TEST_OEMINF-$stamp.txt.orphaned"
    Move-Item -LiteralPath $markerPath -Destination $quarantinedMarker
    if (Test-Path -LiteralPath $markerPath) {
        throw 'Cleanup incompleto: marcador nao foi movido para quarentena.'
    }
}

$quarantinedDriver = $null
if ($driverPresent) {
    $quarantinedDriver = Join-Path $quarantineDir "bc250mcdm-orphan-$stamp.sys.orphaned"
    try {
        Move-Item -LiteralPath $driverPath -Destination $quarantinedDriver
    } catch {
        if ($null -ne $quarantinedMarker -and (Test-Path -LiteralPath $quarantinedMarker)) {
            Move-Item -LiteralPath $quarantinedMarker -Destination $markerPath
        }
        throw "Cleanup falhou movendo o SYS orfao para quarentena: $($_.Exception.Message)"
    }
    if (Test-Path -LiteralPath $driverPath) {
        if ($null -ne $quarantinedMarker -and (Test-Path -LiteralPath $quarantinedMarker)) {
            Move-Item -LiteralPath $quarantinedMarker -Destination $markerPath
        }
        throw 'Cleanup incompleto: o SYS orfao nao foi movido para quarentena.'
    }
    $quarantinedHash = (Get-FileHash -LiteralPath $quarantinedDriver -Algorithm SHA256).Hash
    if ($quarantinedHash -ne $driverHash) {
        throw "Cleanup incompleto: hash do SYS em quarentena diverge ($quarantinedHash != $driverHash)."
    }
}

$deleteOutput = if ($servicePresent) {
    (& sc.exe delete bc250mcdm 2>&1) -join "`n"
} else {
    'service already absent; no sc.exe mutation required'
}
$deleteExit = if ($servicePresent) { $LASTEXITCODE } else { 0 }
$deleteOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'sc-delete.txt') -Encoding UTF8
if ($deleteExit -ne 0) {
    if ($null -ne $quarantinedDriver -and (Test-Path -LiteralPath $quarantinedDriver)) {
        Move-Item -LiteralPath $quarantinedDriver -Destination $driverPath
    }
    if ($null -ne $quarantinedMarker -and (Test-Path -LiteralPath $quarantinedMarker)) {
        Move-Item -LiteralPath $quarantinedMarker -Destination $markerPath
    }
    throw "Cleanup falhou removendo servico; residuos restaurados (exit=$deleteExit)."
}
if ($servicePresent) {
    Start-Sleep -Seconds 2
    if (Get-Service -Name bc250mcdm -ErrorAction SilentlyContinue) {
        throw 'Cleanup parcial: servico ficou marcado para remocao; reinicie antes do stage.'
    }
}

$quarantineRecord = [ordered]@{
    stamp = $stamp
    computer = $env:COMPUTERNAME
    device_instance = $devices[0].PNPDeviceID
    device_service = $devices[0].Service
    device_problem_code = $devices[0].ConfigManagerErrorCode
    service_present_before = $servicePresent
    service_key_present_before = $serviceRegistryPresent
    matching_oem_packages = $matchingPackages.Count
    marker_quarantined = $quarantinedMarker
    marker_text = $markerText
    sys_quarantined = $quarantinedDriver
    sys_sha256 = $driverHash
    sys_bytes = $driverLength
    sys_last_write_utc = $(if ($driverPresent) { $driverWriteUtc.ToString('o') } else { '' })
    last_boot_utc = $lastBootUtc.ToString('o')
    pending_rename_entries_for_sys = 0
    deletion_performed = $false
    hardware_validated = $false
}
$quarantineRecord | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'orphan-quarantine.json') -Encoding UTF8

$auditScript = Join-Path $root 'tools\audit-bc250mcdm-prebinding.ps1'
$auditOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $auditScript 2>&1) -join "`n"
$auditExit = $LASTEXITCODE
$auditOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'prebinding-after.txt') -Encoding UTF8
if ($auditExit -ne 0) {
    throw 'Cleanup executado, mas o prebinding final bloqueou; consulte a evidencia.'
}
Write-Host 'BC250 PREBINDING ORPHAN CLEANUP: PASS'
if ($servicePresent) { Write-Host "  registry backup: $registryBackup" }
if ($null -ne $quarantinedMarker) { Write-Host "  marker quarantine: $quarantinedMarker" }
if ($null -ne $quarantinedDriver) { Write-Host "  SYS quarantine:    $quarantinedDriver" }
Write-Host "  evidence: $evidenceDir"
Write-Host 'Nada foi apagado; a quarentena e recuperavel.'
exit 0
