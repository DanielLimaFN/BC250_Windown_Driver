[CmdletBinding()]
param(
    [string]$PackageDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($PackageDir)) {
    $PackageDir = Join-Path $root 'driver\wdk\package\output_active'
}
$inf = Join-Path $PackageDir 'bc250mcdm.inf'
$sys = Join-Path $PackageDir 'bc250mcdm.sys'
$cat = Join-Path $PackageDir 'bc250mcdm.cat'
$activeSys = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$marker = 'C:\BC250_TEST_OEMINF.txt'
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'
$hardwareId = 'PCI\VEN_1002&DEV_13FE'

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell como Administrador.'
}
foreach ($path in @($inf,$sys,$cat)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Pacote incompleto: $path" }
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$logDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-mcdm-replace"
New-Item -ItemType Directory -Path $logDir | Out-Null

function Save-Native([string]$Name, [scriptblock]$Command) {
    $text = (& $Command 2>&1) -join "`n"
    $code = $LASTEXITCODE
    $text | Set-Content -LiteralPath (Join-Path $logDir "$Name.txt") -Encoding UTF8
    $text | Write-Host
    return $code
}

$deviceBefore = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$instancePrefix*"
})
$deviceBefore | Select-Object Name,PNPDeviceID,Service,Status,ConfigManagerErrorCode |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $logDir 'device-before.json') -Encoding UTF8

$oldInfs = @(Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' |
    Where-Object {
        $text = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        $null -ne $text -and $text -match '(?im)^Class=ComputeAccelerator\s*$' -and
        $text -match '(?i)PCI\\VEN_1002&DEV_13FE' -and
        $text -match '(?im)^AddService=bc250mcdm,' -and
        $text -match '(?im)^ServiceBinary=%12%\\bc250mcdm\.sys\s*$'
    })
foreach ($oldInf in $oldInfs) {
    $code = Save-Native "delete-$($oldInf.BaseName)" { pnputil.exe /delete-driver $oldInf.Name /uninstall /force }
    if ($code -ne 0) { throw "Falha removendo $($oldInf.Name): exit=$code" }
}

& sc.exe stop bc250mcdm 2>&1 | Out-Null
& sc.exe delete bc250mcdm 2>&1 | Out-Null
if (Test-Path -LiteralPath $activeSys) {
    & takeown.exe /F $activeSys /A 2>&1 | Out-Null
    & icacls.exe $activeSys /grant '*S-1-5-32-544:F' 2>&1 | Out-Null
    & attrib.exe -R $activeSys 2>&1 | Out-Null
    Remove-Item -LiteralPath $activeSys -Force
}
if (Test-Path -LiteralPath $marker) { Remove-Item -LiteralPath $marker -Force }

$addCode = Save-Native 'add-driver' { pnputil.exe /add-driver $inf }
if ($addCode -ne 0) { throw "pnputil /add-driver falhou: exit=$addCode" }

$packageInfHash = (Get-FileHash -LiteralPath $inf -Algorithm SHA256).Hash
$published = @(Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' |
    Where-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -eq $packageInfHash })
if ($published.Count -ne 1) { throw "INF publicado nao foi identificado unicamente: $($published.Count)" }
$publishedName = $published[0].Name.ToLowerInvariant()
$publishedName | Set-Content -LiteralPath $marker -Encoding ASCII

if (-not ('Bc250Replace.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Bc250Replace
{
    public static class NativeMethods
    {
        [DllImport("newdev.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool UpdateDriverForPlugAndPlayDevicesW(
            IntPtr hwndParent,
            string hardwareId,
            string fullInfPath,
            UInt32 installFlags,
            [MarshalAs(UnmanagedType.Bool)] ref bool rebootRequired);
    }
}
'@
}

$rebootRequired = $false
$ok = [Bc250Replace.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW(
    [IntPtr]::Zero, $hardwareId, $inf, [uint32]1, [ref]$rebootRequired)
$win32 = if ($ok) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
@{
    success = $ok
    win32_error = $win32
    reboot_required = $rebootRequired
    published_inf = $publishedName
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDir 'binding-result.json') -Encoding UTF8
if (-not $ok) { throw "Force binding falhou: Win32=$win32" }

Start-Sleep -Seconds 12
$deviceAfter = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$instancePrefix*"
})
$deviceAfter | Select-Object Name,PNPDeviceID,Service,Status,ConfigManagerErrorCode |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $logDir 'device-after.json') -Encoding UTF8
if (Test-Path -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe') {
    Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe' |
        Select-Object * -ExcludeProperty PSPath,PSParentPath,PSChildName,PSDrive,PSProvider |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $logDir 'probe-registry.json') -Encoding UTF8
}
Get-Content -LiteralPath (Join-Path $env:SystemRoot 'INF\setupapi.dev.log') -Tail 1800 |
    Set-Content -LiteralPath (Join-Path $logDir 'setupapi-tail.txt') -Encoding UTF8

$auditScript = Join-Path $root 'tools\audit-bc250mcdm-failclosed-binding.ps1'
$auditCode = Save-Native 'post-binding-audit' {
    powershell -NoProfile -ExecutionPolicy Bypass -File $auditScript
}
if ($auditCode -ne 0) {
    throw "Replace concluido, mas o gate fisico fail-closed foi bloqueado. Logs: $logDir"
}

Write-Host "REPLACE CONCLUIDO: $publishedName"
if ($deviceAfter.Count -eq 1) {
    Write-Host "DISPOSITIVO: service=$($deviceAfter[0].Service) code=$($deviceAfter[0].ConfigManagerErrorCode)"
}
Write-Host "LOGS: $logDir"
Write-Host 'NAO VALIDADO PARA ACELERACAO'
exit 0
