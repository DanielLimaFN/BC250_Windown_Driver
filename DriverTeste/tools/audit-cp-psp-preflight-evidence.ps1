param(
    [string]$EvidenceDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'docs\evidence\2026-08-24_18-27-33-DESKTOP-HQAG6T4-cp-psp-preflight'),
    [string]$LinuxSweep = (Join-Path (Split-Path -Parent $PSScriptRoot) 'referencias\linuxDump\parsed_linux_sweep_named.json')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 1.0
$resultPath = Join-Path $EvidenceDir 'result.json'
foreach ($path in @($resultPath, $LinuxSweep)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}

$result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
$linux = Get-Content -LiteralPath $LinuxSweep -Raw | ConvertFrom-Json
$failures = [System.Collections.Generic.List[string]]::new()

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { $failures.Add($Message) }
}

function Parse-Hex([string]$Value) {
    if ($Value -notmatch '^0x[0-9A-Fa-f]{8}$') { throw "Hex invalido: $Value" }
    return [Convert]::ToUInt32($Value.Substring(2), 16)
}

function Linux-Reg([string]$Name) {
    $matches = @($linux | Where-Object { $_.names -contains $Name })
    if ($matches.Count -ne 1) { throw "Registro Linux '$Name': esperava 1 entrada, achei $($matches.Count)." }
    return $matches[0]
}

Require ($result.capture_completed -eq $true) 'captura nao terminou'
Require ($result.writes_authorized -eq $false) 'captura autorizou escrita'
Require ([int]$result.writes_reported -eq 0) 'captura reportou escrita'
Require ($result.verdict -match '^N.O VALIDADO EM HARDWARE$') 'veredito divergente'
Require ($result.checks.abi_29 -eq $true) 'ABI 29 nao confirmada'
Require ($result.checks.readonly_before -eq $true -and $result.checks.readonly_after -eq $true) 'readonly nao preservado'
Require ($result.hashes_match -eq $false) 'auditoria esperava registrar a divergencia conhecida de hashes'

$expectedWorkspace = '0095e4e09a5b2bd9f6c5661f406651e95c63578ca6da18928b9fa8b86c2aa730'
$expectedInstalled = '3f070a48a03ad2111e6a4ee96b3940f07f128dd11021497c6f480bbc66aa39ab'
Require ($result.workspace_driver_sha256 -eq $expectedWorkspace) 'hash do workspace mudou'
Require ($result.installed_driver_sha256 -eq $expectedInstalled) 'hash instalado mudou'

$boot = $result.registers.RLC_RLCS_BOOTLOAD_STATUS
$utcl2 = $result.registers.RLC_RLCS_UTCL2_CNTL
$gpm = $result.registers.RLC_GPM_STAT
Require ($boot.offset -eq '0x3BA34' -and $boot.first -eq '0x00000000' -and $boot.second -eq '0x00000000') 'BOOTLOAD_STATUS inesperado'
Require ($utcl2.offset -eq '0x3BB98' -and $utcl2.first -eq '0x00000018' -and $utcl2.second -eq '0x00000018') 'UTCL2 inesperado'
Require ($gpm.offset -eq '0x3B9B8' -and $gpm.first -eq '0x00F40016' -and $gpm.second -eq '0x00F40016') 'RLC_GPM_STAT Windows inesperado'

$linuxUtcl2 = Linux-Reg 'RLC_RLCS_UTCL2_CNTL'
$linuxGpm = Linux-Reg 'RLC_GPM_STAT'
Require ($linuxUtcl2.offset -eq '0x3BB98' -and $linuxUtcl2.value -eq '0x00000018') 'UTCL2 Linux inesperado'
Require ($linuxGpm.offset -eq '0x3B9B8' -and $linuxGpm.value -eq '0x00B40016') 'RLC_GPM_STAT Linux inesperado'

$windowsGpm = Parse-Hex $gpm.first
$healthyGpm = Parse-Hex $linuxGpm.value
$gpmDiff = $windowsGpm -bxor $healthyGpm
Require ($gpmDiff -eq 0x00400000) 'diferenca GPM nao e somente RLC_EXEC_ROM_CODE'
Require (($windowsGpm -band 0x00400000) -ne 0) 'RLC_EXEC_ROM_CODE nao esta ativo no Windows'
Require (($healthyGpm -band 0x00400000) -eq 0) 'RLC_EXEC_ROM_CODE nao esta apagado no Linux'
Require (((Parse-Hex $boot.first) -band 0x80000001) -eq 0) 'BOOTLOAD_COMPLETE ou IRAM_LOADED inesperadamente ativo'

foreach ($name in @('CP_PFP_INSTR_PNTR','CP_ME_INSTR_PNTR','CP_CE_INSTR_PNTR','CP_MEC1_INSTR_PNTR')) {
    $reg = $result.registers.$name
    Require ($reg.first -eq '0x00000000' -and $reg.second -eq '0x00000000') "$name nao esta zerado"
}
Require ($result.registers.CP_ME_CNTL.first -eq '0x15000000') 'CP_ME_CNTL nao esta haltado'
Require ($result.registers.CP_MEC_CNTL.first -eq '0x50000000') 'CP_MEC_CNTL nao esta haltado'

$pspText = (($result.psp_status | ForEach-Object { $_.output }) -join "`n")
Require ($pspText -match 'C2PMSG_64\s*:\s*0x80000000') 'TOS ready nao registrado'
Require ($pspText -match 'criado por nos:\s*nao') 'ring PSP inesperadamente criado pelo laboratorio'

if ($failures.Count) {
    Write-Host 'AUDITORIA PSP PREFLIGHT: FALHA' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'AUDITORIA PSP PREFLIGHT: OK' -ForegroundColor Green
Write-Host '  20 leituras, zero escrita, ABI 29 permaneceu readonly.'
Write-Host '  TOS pronto; ring do laboratorio ausente; hashes divergentes registrados.'
Write-Host '  BOOTLOAD_STATUS=0: IRAM_LOADED=0 e BOOTLOAD_COMPLETE=0.'
Write-Host '  UTCL2 Windows/Linux: 0x00000018.'
Write-Host '  GPM Windows 0x00F40016 vs Linux 0x00B40016: somente RLC_EXEC_ROM_CODE.'
Write-Host '  RLC na ROM no Windows; CP e MEC haltados, quatro PCs zerados.'
Write-Host '  Veredito: NAO VALIDADO EM HARDWARE.'
