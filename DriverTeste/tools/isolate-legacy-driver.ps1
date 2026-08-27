param(
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$expectedHardware = 'PCI\VEN_1002&DEV_13FE'
$expectedService = 'bc250v4'
$expectedInf = 'oem48.inf'

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute como Administrador.'
}

$device = Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$expectedHardware*"
} | Select-Object -First 1
$driver = Get-CimInstance Win32_PnPSignedDriver | Where-Object {
    $_.DeviceID -like "$expectedHardware*"
} | Select-Object -First 1

if (-not $device -or -not $driver) {
    throw 'BC-250 ou pacote de driver ligado a ela nao encontrado.'
}
if ($device.Service -ne $expectedService) {
    throw "Alvo recusado: servico atual e '$($device.Service)', esperado '$expectedService'."
}
if ($driver.InfName -ne $expectedInf) {
    throw "Alvo recusado: INF atual e '$($driver.InfName)', esperado '$expectedInf'."
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$backup = Join-Path $projectRoot "recovery\bc250v4-$stamp"
$package = Join-Path $backup 'driver-package'
New-Item -ItemType Directory -Path $package -Force | Out-Null

Write-Host "[*] exportando $expectedInf para $package"
& pnputil.exe /export-driver $expectedInf $package | Tee-Object -FilePath (Join-Path $backup 'pnputil-export.txt')
if ($LASTEXITCODE -ne 0) {
    throw "Exportacao do driver falhou (exit $LASTEXITCODE). Nenhuma remocao foi feita."
}

$exportedInf = Get-ChildItem -LiteralPath $package -Recurse -File -Filter '*.inf' |
    Where-Object {
        $body = Get-Content -LiteralPath $_.FullName -Raw
        $body -match 'DEV_13FE' -and $body -match 'bc250v4'
    } | Select-Object -First 1
if (-not $exportedInf) {
    throw 'Backup recusado: INF exportado nao contem DEV_13FE e bc250v4. Nenhuma remocao foi feita.'
}

$installedSys = Join-Path $env:SystemRoot 'System32\drivers\bc250wddm.sys'
if (Test-Path -LiteralPath $installedSys) {
    Copy-Item -LiteralPath $installedSys -Destination (Join-Path $backup 'bc250wddm.sys') -Force
}
& reg.exe export 'HKLM\SYSTEM\CurrentControlSet\Services\bc250v4' (Join-Path $backup 'bc250v4-service.reg') /y | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'restore-legacy-driver.ps1') `
    -Destination (Join-Path $backup 'restore-legacy-driver.ps1') -Force

$manifest = [pscustomobject]@{
    schema_version = 1
    created_at = (Get-Date).ToString('o')
    hardware_id = $expectedHardware
    instance_id = $device.PNPDeviceID
    service = $device.Service
    inf_name = $driver.InfName
    driver_provider = $driver.DriverProviderName
    driver_version = $driver.DriverVersion
    driver_date = $driver.DriverDate
    installed_sys = $installedSys
    installed_sys_sha256 = if (Test-Path -LiteralPath $installedSys) {
        (Get-FileHash -LiteralPath $installedSys -Algorithm SHA256).Hash.ToLowerInvariant()
    } else { $null }
    exported_inf = $exportedInf.FullName
    restore_command = 'powershell -ExecutionPolicy Bypass -File restore-legacy-driver.ps1 -BackupDirectory <backup> -Reboot'
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding UTF8

Write-Host "Backup validado: $backup" -ForegroundColor Green
Write-Host "Restaure com: powershell -ExecutionPolicy Bypass -File tools\restore-legacy-driver.ps1 -BackupDirectory `"$backup`" -Reboot"

if (-not $Apply) {
    Write-Host 'PREPARACAO CONCLUIDA. Nenhuma alteracao PnP foi feita.' -ForegroundColor Yellow
    Write-Host 'A remocao ao vivo esta bloqueada: bc250wddm.sys causa bugcheck 0xC4/0x62 ao descarregar.'
    exit 0
}

throw @'
OPERACAO BLOQUEADA: a remocao ao vivo de bc250v4 reproduziu bugcheck 0xC4/0x62.
O Driver Verifier detectou alocacoes de pool pendentes em bc250wddm.sys durante
o descarregamento. Nao e seguro repetir pnputil /delete-driver /uninstall ate o
dump ser analisado e o caminho de teardown ser corrigido, ou ate existir um
procedimento offline validado.
'@

Write-Warning 'A unica GPU ativa sera retirada do bc250v4. A tela pode apagar ate o reboot/fallback BasicDisplay.'
$confirmation = Read-Host 'Digite exatamente ISOLAR BC250V4 para continuar'
if ($confirmation -cne 'ISOLAR BC250V4') {
    throw 'Confirmacao incorreta. Nenhuma remocao foi feita.'
}

$labService = Get-Service -Name 'bc250dbg' -ErrorAction SilentlyContinue
if ($labService -and $labService.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Stopped) {
    Write-Host '[*] parando bc250dbg antes da troca PnP'
    Stop-Service -Name 'bc250dbg' -Force
    $labService.WaitForStatus([System.ServiceProcess.ServiceControllerStatus]::Stopped,
                              [TimeSpan]::FromSeconds(10))
    $labService.Refresh()
    if ($labService.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Stopped) {
        throw 'bc250dbg nao parou; remocao do driver de video foi abortada.'
    }
}

Write-Host "[*] removendo binding e pacote $expectedInf"
& pnputil.exe /delete-driver $expectedInf /uninstall /force | Tee-Object -FilePath (Join-Path $backup 'pnputil-delete.txt')
if ($LASTEXITCODE -ne 0) {
    throw "Remocao falhou (exit $LASTEXITCODE). Use o script de restauracao se necessario."
}

& pnputil.exe /scan-devices | Out-Host
Set-Content -LiteralPath (Join-Path $backup 'ISOLATION_APPLIED.txt') `
    -Value ((Get-Date).ToString('o')) -Encoding ASCII

Write-Host 'Isolamento aplicado. O Windows sera reiniciado em 15 segundos.' -ForegroundColor Green
Write-Host 'Para cancelar o reboot: shutdown /a'
shutdown.exe /r /t 15 /c 'Isolamento do driver legado BC-250 concluido'
