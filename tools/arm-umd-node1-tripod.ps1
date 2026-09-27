<#
.SYNOPSIS
    Inspeciona, arma ou desarma a autorizacao single-shot do B7 UMD Node 1.

.DESCRIPTION
    Este script NAO instala driver, NAO inicia o harness e NAO toca a GPU.
    Armar apenas grava o token que a UMD compara com o ambiente do executavel
    bc250-umd-node1-tripod.exe. O DWM nunca satisfaz essa politica.
#>
[CmdletBinding(DefaultParameterSetName='Inspect')]
param(
    [Parameter(ParameterSetName='Arm',Mandatory=$true)][switch]$Arm,
    [Parameter(ParameterSetName='Disarm',Mandatory=$true)][switch]$Disarm,
    [Parameter(ParameterSetName='Arm',Mandatory=$true)]
    [ValidatePattern('^[0-9A-Fa-f]{32}$')][string]$Token,
    [Parameter(ParameterSetName='Arm',Mandatory=$true)]
    [switch]$IAcceptOnePhysicalNode1Submission
)

$ErrorActionPreference = 'Stop'
$key = 'HKLM:\SOFTWARE\BC250\Umd'
$armedName = 'Node1TripodArmed'
$tokenName = 'Node1TripodToken'

function Show-State {
    $armed = $null
    $tokenPresent = $false
    if (Test-Path -LiteralPath $key) {
        $item = Get-ItemProperty -LiteralPath $key -ErrorAction Stop
        $armed = $item.$armedName
        $tokenPresent = $null -ne $item.$tokenName
    }
    [pscustomobject]@{
        Key = $key
        Armed = if ($null -eq $armed) { 0 } else { [int]$armed }
        TokenPresent = $tokenPresent
        HardwareTouched = $false
        Verdict = 'NAO VALIDADO EM HARDWARE'
    }
}

if ($Arm) {
    if (-not $IAcceptOnePhysicalNode1Submission) {
        throw 'Arm recusado: aceite explicito ausente.'
    }
    $capsProbe = 0
    if (Test-Path -LiteralPath $key) {
        $raw = (Get-ItemProperty -LiteralPath $key -Name D3D11CapsProbe `
            -ErrorAction SilentlyContinue).D3D11CapsProbe
        if ($null -ne $raw) { $capsProbe = [int]$raw }
    }
    if ($capsProbe -ne 0) {
        throw 'Arm recusado: D3D11CapsProbe deve estar desarmado.'
    }
    if (-not (Test-Path -LiteralPath $key)) {
        New-Item -Path $key -Force | Out-Null
    }
    New-ItemProperty -LiteralPath $key -Name $tokenName -PropertyType String `
        -Value $Token.ToUpperInvariant() -Force | Out-Null
    New-ItemProperty -LiteralPath $key -Name $armedName -PropertyType DWord `
        -Value 1 -Force | Out-Null
    Write-Host 'Autorizacao gravada. Nenhum harness foi iniciado e a GPU nao foi tocada.'
} elseif ($Disarm) {
    if (Test-Path -LiteralPath $key) {
        New-ItemProperty -LiteralPath $key -Name $armedName -PropertyType DWord `
            -Value 0 -Force | Out-Null
        Remove-ItemProperty -LiteralPath $key -Name $tokenName `
            -ErrorAction SilentlyContinue
    }
    Write-Host 'B7 UMD Node 1 desarmado.'
}

Show-State
