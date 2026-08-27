# Diagnostico do lado do SO para o bc250wddm.
#
# A telemetria do Registro conta o que o DRIVER viu. Quando ela sai limpa e o
# dispositivo mesmo assim nao inicia, a razao esta do lado do dxgkrnl -- e ele
# so fala pelo codigo de problema do PnP e pelo log de eventos.
#
#   powershell -ExecutionPolicy Bypass -File wddm\diag.ps1

$ErrorActionPreference = 'Continue'

Write-Host "`n=== 1. Telemetria do driver ===" -ForegroundColor Cyan
$params = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250wddm\Parameters'
$telemetry = $null
if (Test-Path $params) {
    $telemetry = Get-ItemProperty $params
    $telemetry |
        Select-Object -Property * -ExcludeProperty PS* |
        Format-List
} else {
    Write-Host "  (chave ausente -- o driver nunca chegou a rodar)" -ForegroundColor Yellow
}

Write-Host "`n=== 2. Estado PnP dos adaptadores de video ===" -ForegroundColor Cyan
# Problem = codigo CM_PROB_*. 0 = OK. 43 = FAILED_POST_START.
$displayDevices = @(Get-PnpDevice -Class Display)
$displayDevices |
    Select-Object FriendlyName, Status, Problem, ProblemDescription, InstanceId |
    Format-List

Write-Host "`n=== 3. Driver realmente selecionado para o dispositivo ===" -ForegroundColor Cyan
$displayDevices | ForEach-Object {
    $inf = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_DriverInfPath' -ErrorAction SilentlyContinue).Data
    $ver = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_DriverVersion'  -ErrorAction SilentlyContinue).Data
    [PSCustomObject]@{ Nome = $_.FriendlyName; Inf = $inf; Versao = $ver }
} | Format-List

Write-Host "`n=== 4. Eventos relevantes dos ultimos 15 minutos ===" -ForegroundColor Cyan
$since = (Get-Date).AddMinutes(-15)
$logs = @('System', 'Microsoft-Windows-Kernel-PnP/Configuration')

foreach ($log in $logs) {
    try {
        $events = Get-WinEvent -FilterHashtable @{ LogName = $log; StartTime = $since } -ErrorAction Stop |
                  Where-Object { $_.Message -match 'bc250|dxgkrnl|display|v.deo|graphics' }
        if ($events) {
            Write-Host "--- $log ---" -ForegroundColor Green
            $events | Select-Object TimeCreated, Id, LevelDisplayName, ProviderName, Message | Format-List
        }
    } catch {
        Write-Host "--- $log : sem eventos no periodo ---" -ForegroundColor DarkGray
    }
}

Write-Host "`n=== 5. Pacotes bc250wddm na driver store ===" -ForegroundColor Cyan
# Varios oemNN.inf acumulados sao normais depois de varias iteracoes; o que
# importa e qual deles o item 3 aponta como selecionado.
pnputil /enum-drivers | Select-String -Pattern 'wddm.inf' -Context 1, 5

Write-Host "`n=== 6. Veredito consolidado ===" -ForegroundColor Cyan
$bc250 = $displayDevices | Where-Object {
    $_.InstanceId -like 'PCI\VEN_1002&DEV_13FE*'
} | Select-Object -First 1

$pnpOk = $null -ne $bc250 -and
         $bc250.Status -eq 'OK' -and
         $bc250.Problem -eq 'CM_PROB_NONE'
$startOk = $null -ne $telemetry -and $telemetry.Start_Status -eq 0
$vidPnOk = $null -ne $telemetry -and $telemetry.Commit_Status -eq 0
$presentOk = $null -ne $telemetry -and
             $telemetry.Present_Entered -eq 1 -and
             $telemetry.Present_Exited -eq 1 -and
             $telemetry.Present_Count -gt 0
$vsyncOk = $null -ne $telemetry -and $telemetry.VsyncCount -gt 0

if ($pnpOk -and $startOk -and $vidPnOk -and $presentOk -and $vsyncOk) {
    Write-Host 'PASSOU: dispositivo OK, VidPN ativo, quadros apresentados e VSync progredindo.' -ForegroundColor Green
    Write-Host 'Eventos 411 anteriores podem registrar uma tentativa transitoria durante a troca do pacote; nao representam o estado atual.' -ForegroundColor DarkGray
} else {
    Write-Host 'FALHOU ou INCONCLUSIVO: um ou mais criterios operacionais nao foram atendidos.' -ForegroundColor Red
    [PSCustomObject]@{
        PnpOk = $pnpOk
        StartOk = $startOk
        VidPnOk = $vidPnOk
        PresentOk = $presentOk
        VsyncOk = $vsyncOk
    } | Format-List
}
