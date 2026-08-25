[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^oem[0-9]+\.inf$')]
    [string]$PublishedName
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$publishedNameLower = $PublishedName.ToLowerInvariant()
$installedInf = Join-Path $env:SystemRoot "INF\$publishedNameLower"
$marker = 'C:\BC250_TEST_OEMINF.txt'
$recoveryRoot = 'C:\BC250_RECOVERY.cmd'
$recoverySource = Join-Path $root 'tools\BC250_RECOVERY.cmd'

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado.'
}
if (-not (Test-Path -LiteralPath $recoveryRoot) -or
    (Get-FileHash $recoveryRoot).Hash -ne (Get-FileHash $recoverySource).Hash) {
    throw 'Recuperacao C:\BC250_RECOVERY.cmd ausente ou divergente.'
}
if (-not (Test-Path -LiteralPath $installedInf)) {
    throw "INF publicado nao existe: $installedInf"
}

$infText = Get-Content -LiteralPath $installedInf -Raw
$requiredPatterns = @(
    '(?im)^Class=ComputeAccelerator\s*$',
    '(?im)^ClassGuid=\{F01A9D53-3FF6-48D2-9F97-C8A7004BE10C\}\s*$',
    '(?im)PCI\\VEN_1002&DEV_13FE',
    '(?im)^AddService=bc250readonly,',
    '(?im)^ServiceBinary=%12%\\bc250readonly\.sys\s*$'
)
foreach ($pattern in $requiredPatterns) {
    if ($infText -notmatch $pattern) {
        throw "INF $publishedNameLower nao pertence ao pacote BC250 readonly esperado."
    }
}

$publishedNameLower | Set-Content -LiteralPath $marker -Encoding Ascii
$writtenLines = @(Get-Content -LiteralPath $marker)
if ($writtenLines.Count -ne 1 -or $writtenLines[0] -cne $publishedNameLower) {
    throw "Marcador divergiu apos escrita: '$($writtenLines -join '|')'"
}
$written = $writtenLines[0]

Write-Host "Marcador reparado: $marker = $written"
Write-Host "INF validado: $installedInf"
Write-Host 'Nenhum dispositivo foi ativado ou reiniciado.'
Write-Host 'Veredito: NAO VALIDADO EM HARDWARE'
