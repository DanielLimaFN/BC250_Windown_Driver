[CmdletBinding()]
param([switch]$Execute)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$marker = 'C:\BC250_TEST_OEMINF.txt'
$packageInf = Join-Path $root 'driver\wdk\package\output\bc250readonly.inf'
$stageScript = Join-Path $root 'tools\stage-bc250readonly-package.ps1'
$expectedInstancePrefix = 'PCI\VEN_1002&DEV_13FE'

function Test-Bc250ReadonlyInf([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $text = Get-Content -LiteralPath $Path -Raw
    return ($text -match '(?im)^Class=ComputeAccelerator\s*$' -and
        $text -match '(?im)^ClassGuid=\{F01A9D53-3FF6-48D2-9F97-C8A7004BE10C\}\s*$' -and
        $text -match '(?im)PCI\\VEN_1002&DEV_13FE' -and
        $text -match '(?im)^AddService=bc250readonly,' -and
        $text -match '(?im)^ServiceBinary=%12%\\bc250readonly\.sys\s*$')
}

if (-not (Test-Path -LiteralPath $marker)) { throw 'Marcador OEM ausente.' }
$markerLines = @(Get-Content -LiteralPath $marker)
if ($markerLines.Count -ne 1 -or $markerLines[0] -notmatch '^oem[0-9]+\.inf$') {
    throw 'Marcador OEM invalido.'
}
$oldPublishedName = $markerLines[0].ToLowerInvariant()
$oldInf = Join-Path $env:SystemRoot "INF\$oldPublishedName"
if (-not (Test-Bc250ReadonlyInf $oldInf)) {
    throw "$oldPublishedName nao pertence ao pacote BC250 readonly esperado."
}
if (-not (Test-Bc250ReadonlyInf $packageInf)) {
    throw 'Novo INF do workspace nao passou na validacao de identidade.'
}
$newInfText = Get-Content -LiteralPath $packageInf -Raw
if ($newInfText -notmatch '(?im)^DriverVer=08/25/2026,0\.5\.0\.0\s*$') {
    throw 'Pacote novo nao e a versao readonly preflight 0.5.0.0.'
}

$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$expectedInstancePrefix*"
})
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'BasicDisplay' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'BC-250 nao esta exclusivamente em BasicDisplay saudavel.'
}
if (Get-Service bc250readonly -ErrorAction SilentlyContinue) {
    throw 'Servico bc250readonly ja existe; refresh recusado.'
}

Write-Host "Preflight aprovado: $oldPublishedName esta apenas pre-estagiado."
Write-Host 'A BC-250 permanece em BasicDisplay, ConfigManagerErrorCode=0.'
if (-not $Execute) {
    Write-Host 'Zero escrita: repita com -Execute para substituir o pacote staged.'
    Write-Host 'Veredito: NAO VALIDADO EM HARDWARE'
    exit 0
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado.'
}

$deleteOutput = (& pnputil.exe /delete-driver $oldPublishedName /force 2>&1) -join "`n"
$deleteExit = $LASTEXITCODE
$deleteOutput | Write-Host
if ($deleteExit -ne 0) {
    throw "Falha ao remover pacote staged ${oldPublishedName}: exit=$deleteExit"
}
Remove-Item -LiteralPath $marker -Force

& $stageScript -Execute
if ($LASTEXITCODE -ne 0) { throw 'Pre-estagio da versao nova falhou.' }

$newMarkerLines = @(Get-Content -LiteralPath $marker)
if ($newMarkerLines.Count -ne 1 -or
    $newMarkerLines[0] -notmatch '^oem[0-9]+\.inf$') {
    throw 'Novo marcador OEM ausente ou invalido. Nao ative o driver.'
}
$newPublishedName = $newMarkerLines[0].ToLowerInvariant()
$matchingPackages = @(
    Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' |
        Where-Object { Test-Bc250ReadonlyInf $_.FullName }
)
if ($matchingPackages.Count -ne 1 -or
    $matchingPackages[0].Name.ToLowerInvariant() -ne $newPublishedName) {
    throw 'Driver Store contem cardinalidade inesperada de pacotes BC250 readonly. Nao ative o driver.'
}

Write-Host "Refresh concluido: $oldPublishedName -> $newPublishedName"
Write-Host 'Permanece sem /install; binding continua BasicDisplay.'
Write-Host 'Veredito: NAO VALIDADO EM HARDWARE'
