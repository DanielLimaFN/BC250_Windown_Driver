# Corrida 297 (doc 757): depois do bind, confere se um app Vulkan comum acha a BC-250
# sozinho, sem variavel de ambiente, como acharia um driver da AMD.
#
#   1. espera a BC-250 (PCI 1002:13FE) com o servico bc250mcdm e Status OK (teto 240 s);
#   2. espera mais 20 s (desktop no nosso driver);
#   3. registra o VulkanDriverName do adaptador e roda "vulkaninfo --summary" com as
#      variaveis VK_* limpas neste processo (vulkaninfo nao cria VkDevice);
#   4. escreve VULKAN_VERIFICADO e a linha do dispositivo, ou VULKAN_SEM_DISPOSITIVO.
# Log: C:\BC250_VULKAN_APOS_BIND.log (o EXECUTAR copia para a pasta de evidencia).
[CmdletBinding()]
param(
    [int]$TetoBindSegundos = 240,
    [int]$EsperaDesktopSegundos = 20
)

$ErrorActionPreference = 'Continue'
$log = 'C:\BC250_VULKAN_APOS_BIND.log'
function Log([string]$m) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $m
    Write-Host $line
    Add-Content -LiteralPath $log -Value $line -Encoding utf8
}
Set-Content -LiteralPath $log -Value '' -Encoding utf8
Log "VULKAN_APOS_BIND: inicio (teto do bind $TetoBindSegundos s)"

function Get-Bc250Bound {
    $dev = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -match 'VEN_1002&DEV_13FE' } | Select-Object -First 1
    if (-not $dev) { return $null }
    $svc = (Get-PnpDeviceProperty -InstanceId $dev.InstanceId -KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue).Data
    $drvKey = (Get-PnpDeviceProperty -InstanceId $dev.InstanceId -KeyName 'DEVPKEY_Device_Driver' -ErrorAction SilentlyContinue).Data
    [pscustomobject]@{ Status = $dev.Status; Service = $svc; InstanceId = $dev.InstanceId; DriverKey = $drvKey }
}

$t0 = Get-Date
$bound = $null
while (((Get-Date) - $t0).TotalSeconds -lt $TetoBindSegundos) {
    $b = Get-Bc250Bound
    if ($b -and $b.Service -eq 'bc250mcdm' -and $b.Status -eq 'OK') { $bound = $b; break }
    Start-Sleep -Seconds 2
}
if (-not $bound) {
    Log 'VULKAN_APOS_BIND: DESISTIU -- a BC-250 nao ficou em bc250mcdm/OK no prazo'
    exit 2
}
Log ("VULKAN_APOS_BIND: BC-250 em bc250mcdm/OK apos {0:N0} s; esperando {1} s" -f ((Get-Date) - $t0).TotalSeconds, $EsperaDesktopSegundos)
Start-Sleep -Seconds $EsperaDesktopSegundos

if ($bound.DriverKey) {
    $regPath = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\$($bound.DriverKey)"
    $name = (Get-ItemProperty -LiteralPath $regPath -Name 'VulkanDriverName' -ErrorAction SilentlyContinue).VulkanDriverName
    Log ("VULKAN_APOS_BIND: VulkanDriverName do adaptador = {0}" -f $(if ($name) { ($name -join '; ') } else { 'AUSENTE' }))
    if ($name) {
        foreach ($json in @($name)) {
            Log ("VULKAN_APOS_BIND: manifesto existe = {0} ({1})" -f (Test-Path -LiteralPath $json), $json)
        }
    }
}

foreach ($var in @('VK_ICD_FILENAMES', 'VK_DRIVER_FILES', 'VK_ADD_DRIVER_FILES', 'VK_LOADER_DRIVERS_SELECT', 'VK_LOADER_DRIVERS_DISABLE', 'VK_INSTANCE_LAYERS')) {
    Remove-Item -LiteralPath "Env:$var" -ErrorAction SilentlyContinue
}
$vi = Join-Path $env:SystemRoot 'System32\vulkaninfo.exe'
if (-not (Test-Path -LiteralPath $vi)) { $vi = 'C:\VulkanSDK\1.4.357.0\Bin\vulkaninfoSDK.exe' }
if (-not (Test-Path -LiteralPath $vi)) {
    Log 'VULKAN_APOS_BIND: vulkaninfo nao encontrado'
    exit 3
}
$out = & $vi --summary 2>&1 | Out-String
Add-Content -LiteralPath $log -Value $out -Encoding utf8
$devLine = ($out -split "`r?`n" | Where-Object { $_ -match 'deviceName' } | Select-Object -First 3) -join ' | '
if ($out -match '(?i)BC-250|RADV') {
    Log "VULKAN_VERIFICADO: $devLine"
    Log 'VULKAN_APOS_BIND: agora abra o vkcube baixado (e outros apps Vulkan), mexa e redimensione a janela.'
    exit 0
}
Log "VULKAN_SEM_DISPOSITIVO: o loader nao listou a BC-250 ($devLine)"
exit 1
