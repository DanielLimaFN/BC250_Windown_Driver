<#
.SYNOPSIS
    Arma ou desarma o opt-out do modelo Flip do DXGI (F2.2).

.DESCRIPTION
    Escreve HKLM:\SOFTWARE\BC250\Umd\D3D11NoScanoutOptOut.

    Armado, Bc250Umd10CreateResource declara
    DXGI_DDI_PRIMARY_DRIVER_FLAG_NO_SCANOUT (dxgiddi.h:218) nos back buffers
    marcados com DXGI_DDI_PRIMARY_OPTIONAL. E' o mecanismo DOCUMENTADO para a
    UMD dizer ao runtime que apresenta por COPIA, e nao por flip.

    POR QUE ISSO IMPORTA NESTE DRIVER: o scanout e' o framebuffer fixo herdado
    do UEFI GOP e escrita em registrador de DCN esta proibida no escopo atual.
    Nao podemos fazer flip. Aceitar a cadeia de flip calados e' declarar um cap
    sem backend -- o que a Lei 4 proibe.

    POR QUE NAO VEM ARMADO: armado, o DXGI muda o caminho inteiro para Blt, e a
    corrida deixa de medir se as correcoes do caminho de Flip (DescribeAllocation
    em F2.1, SetDisplayMode em F2.3) resolveram alguma coisa. A ordem e':
    primeiro uma corrida com o Flip corrigido; se o E_FAIL persistir, arma-se
    esta chave e a segunda corrida mede o opt-out -- SEM recompilar nem
    reinstalar nada.

    A UMD le' a chave UMA vez por processo, na primeira consulta. Processos ja
    em execucao (dwm.exe) so' enxergam a mudanca depois de reiniciarem.

.PARAMETER Arm
    Grava 1.

.PARAMETER Disarm
    Grava 0.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\arm-d3d11-no-scanout-optout.ps1 -Arm
#>
[CmdletBinding()]
param(
    [switch]$Arm,
    [switch]$Disarm
)

$ErrorActionPreference = 'Stop'

$key  = 'HKLM:\SOFTWARE\BC250\Umd'
$name = 'D3D11NoScanoutOptOut'

if ($Arm -and $Disarm) {
    throw 'Escolha -Arm OU -Disarm, nao os dois.'
}

if (-not $Arm -and -not $Disarm) {
    if (Test-Path $key) {
        $current = (Get-ItemProperty -Path $key -Name $name -ErrorAction SilentlyContinue).$name
    } else {
        $current = $null
    }
    if ($null -eq $current) {
        Write-Host "$name : AUSENTE (a UMD trata como desarmado)"
    } else {
        Write-Host "$name : $current ($(if ($current -ne 0) { 'ARMADO' } else { 'desarmado' }))"
    }
    Write-Host ''
    Write-Host 'Use -Arm para armar o opt-out do Flip, ou -Disarm para desarmar.'
    exit 0
}

if (-not (Test-Path $key)) {
    New-Item -Path $key -Force | Out-Null
}

$value = if ($Arm) { 1 } else { 0 }
New-ItemProperty -Path $key -Name $name -Value $value -PropertyType DWord -Force | Out-Null

$readback = (Get-ItemProperty -Path $key -Name $name).$name
if ($readback -ne $value) {
    throw "Readback divergente: escrito $value, lido $readback"
}

Write-Host "$name = $readback ($(if ($value -ne 0) { 'ARMADO' } else { 'desarmado' })), readback conferido."
if ($Arm) {
    Write-Host ''
    Write-Host 'ARMADO: o DXGI deve passar a pedir Blt (Flags=0x1) em vez de Flip (0x2).'
    Write-Host 'Confira no log da UMD: "D3D11_NO_SCANOUT_OPTOUT: ARMADO" e'
    Write-Host '"PRIMARY_DESC: NO_SCANOUT declarado no back buffer".'
    Write-Host 'Processos ja em execucao so enxergam a mudanca ao reiniciar.'
}
Write-Host 'NAO VALIDADO EM HARDWARE.'
