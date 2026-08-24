<#
    Diagnóstico Detalhado do Ring 0 e PM4
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

Write-Host "=== DIAGNOSTICO DO COMMAND PROCESSOR RING 0 ===" -ForegroundColor Cyan

Run @('arm','write') | Out-Null

# 1. Snapshot
Write-Host "1. Gravando ucodes e unhalt..."
Run @('cp','directload','pfp','0x0E000000','firmware\cyan_skillfish2_pfp.bin') | Out-Null
Run @('cp','directload','me','0x0E080000','firmware\cyan_skillfish2_me.bin') | Out-Null
Run @('cp','directload','ce','0x0E100000','firmware\cyan_skillfish2_ce.bin') | Out-Null
Run @('cp','jt','pfp','0x0E000000','firmware\cyan_skillfish2_pfp.bin') | Out-Null
Run @('cp','jt','me','0x0E080000','firmware\cyan_skillfish2_me.bin') | Out-Null
Run @('cp','jt','ce','0x0E100000','firmware\cyan_skillfish2_ce.bin') | Out-Null

# 2. Ring Init
Write-Host "2. Inicializando Ring 0..."
$initRes = Run @('gfx','init','0x0E200000','4096')
Write-Host "   $initRes"

# 3. Status antes do envio
Write-Host "`n3. Estado dos Registradores ANTES do envio:"
$regs = @(
    0x086D8, 0x08694, 0x08698, 0x0869C, 0x08680, 0x08010, 0x08014,
    0x0C100, 0x0C104, 0x0C150, 0x08700,
    0x0A600, 0x0A604, 0x0A614, 0x0A618, 0x0A61C, 0x0A200, 0x0A100, 0x0A104,
    0x0263C, 0x09800
)
foreach ($r in $regs) {
    $h = ('0x{0:X5}' -f $r)
    $val = Run @('reg','r',$h)
    Write-Host "   $h : $val"
}

# 4. Escreve valor inicial na fence
Write-Host "`n4. Gravando 0x11111111 na fence VRAM 0x0E201200..."
Run @('vram','w','0x0E201200','0x11111111') | Out-Null
$fencePre = Run @('vram','r','0x0E201200')
Write-Host "   Fence antes: $fencePre"

# 5. Envia WRITE_DATA
Write-Host "`n5. Submetendo PACKET3_WRITE_DATA (0xCAFEBABE)..."
$submitRes = Run @('gfx','writedata','0x0E201200','0xCAFEBABE')
Write-Host "   $submitRes"

# 6. Status apos o envio
Write-Host "`n6. Estado dos Registradores APOS o envio:"
foreach ($r in $regs) {
    $h = ('0x{0:X5}' -f $r)
    $val = Run @('reg','r',$h)
    Write-Host "   $h : $val"
}

# 7. Fence apos
$fencePost = Run @('vram','r','0x0E201200')
Write-Host "`n7. Valor da Fence apos submissao: $fencePost"

# 8. Limpeza / Rollback
Write-Host "`n8. Restaurando HALT e rollback..."
Run @('cp','halt') | Out-Null
Run @('cp','rollback','all') | Out-Null
Run @('arm','readonly') | Out-Null
Write-Host "=== FIM DO DIAGNOSTICO ===" -ForegroundColor Green
