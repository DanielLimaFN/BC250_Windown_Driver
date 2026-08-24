<#
    Sonda dos Registradores do CP Ring Buffer 0 (Segmento 0 vs Segmento 1)
#>
param(
    [switch]$Execute
)

$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'

if (-not $Execute) {
    throw 'Use -Execute.'
}

function Run([string[]]$Arguments) {
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $cli
    $psi.Arguments = (($Arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' ')
    $psi.WorkingDirectory = $root; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = New-Object Diagnostics.Process; $p.StartInfo = $psi
    [void]$p.Start()
    $out = $p.StandardOutput.ReadToEnd() + $p.StandardError.ReadToEnd()
    [void]$p.WaitForExit()
    return $out.Trim()
}

Write-Host "=== SONDAGEM DOS REGISTRADORES DO CP RING BUFFER ===" -ForegroundColor Cyan

# Arm write
Run @('arm','write') | Out-Null

$offsets = [ordered]@{
    # Segmento 0 (offset tradicional)
    'Seg0_CP_RB0_BASE'         = 0x0C100
    'Seg0_CP_RB0_CNTL'         = 0x0C104
    'Seg0_CP_RB0_RPTR_ADDR'    = 0x0C10C
    'Seg0_CP_RB0_RPTR_ADDR_HI' = 0x0C110
    'Seg0_CP_RB0_WPTR'         = 0x0C150
    'Seg0_CP_RB0_BASE_HI'      = 0x0C2C4
    'Seg0_CP_RB0_RPTR'         = 0x08700

    # Segmento 1 (Base 0x28000 + dw * 4)
    # Hipótese A: dw = 0x3040 -> 0x34100
    'Seg1_A_CP_RB0_BASE'       = 0x34100
    'Seg1_A_CP_RB0_CNTL'       = 0x34104
    'Seg1_A_CP_RB0_RPTR_ADDR'  = 0x3410C
    'Seg1_A_CP_RB0_WPTR'       = 0x34150
    'Seg1_A_CP_RB0_BASE_HI'    = 0x342C4

    # Hipótese B: dw = 0x1040 -> 0x2C100
    'Seg1_B_CP_RB0_BASE'       = 0x2C100
    'Seg1_B_CP_RB0_BASE_HI'    = 0x2C104
    'Seg1_B_CP_RB0_CNTL'       = 0x2C108
    'Seg1_B_CP_RB0_RPTR_ADDR'  = 0x2C10C
    'Seg1_B_CP_RB0_WPTR'       = 0x2C140

    # Segmento 0 - offsets wddm_hw.c (0x0DA60)
    'WDDM_CP_RB0_BASE'         = 0x0DA60
    'WDDM_CP_RB0_BASE_HI'      = 0x0DA64
    'WDDM_CP_RB0_CNTL'         = 0x0DA68
    'WDDM_CP_RB0_RPTR'         = 0x0DA6C
    'WDDM_CP_RB0_WPTR_HI'      = 0x0DA74
    'WDDM_CP_RB0_WPTR'         = 0x0DA78
    'WDDM_CP_RB_ACTIVE'        = 0x0DA7C
}

foreach ($k in $offsets.Keys) {
    $offHex = ('0x{0:X5}' -f $offsets[$k])
    $res = Run @('reg','r',$offHex)
    Write-Host ("  {0,-28} ({1}) = {2}" -f $k, $offHex, $res)
}

Run @('arm','readonly') | Out-Null
