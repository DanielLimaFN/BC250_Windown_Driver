<#
    Auditoria estatica, sem abrir o driver, da quarentena de SETUP_TMR.

    Garante defesa em profundidade no kernel, biblioteca e CLI. O caminho so
    pode voltar a existir quando houver reserva dinamica exclusiva que produza
    os enderecos PA/MC/GPUVA e o tamanho a partir da mesma alocacao.
#>
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$root = Split-Path -Parent $PSScriptRoot
$kernelPath = Join-Path $root 'dbg\psp.c'
$libPath = Join-Path $root 'studio\lib\bc250lib.c'
$cliPath = Join-Path $root 'studio\cli\bc250ctl.c'
$failures = [System.Collections.Generic.List[string]]::new()

foreach ($path in @($kernelPath, $libPath, $cliPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}

$kernel = Get-Content -LiteralPath $kernelPath -Raw
$lib = Get-Content -LiteralPath $libPath -Raw
$cli = Get-Content -LiteralPath $cliPath -Raw

function Function-Body([string]$text, [string]$signature, [string]$nextSignature) {
    $pattern = [regex]::Escape($signature) + '(?<body>.*?)' + [regex]::Escape($nextSignature)
    $match = [regex]::Match($text, $pattern, [Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $match.Success) { throw "Funcao nao encontrada: $signature" }
    return $match.Groups['body'].Value
}

$kernelBody = Function-Body $kernel 'NTSTATUS BcPspSetupTmr(' 'NTSTATUS BcPspFree('
if ($kernelBody -notmatch 'STATUS_NOT_SUPPORTED') { $failures.Add('Kernel nao retorna STATUS_NOT_SUPPORTED.') }
if ($kernelBody -notmatch 'BC250DBG_OPS_BADOP') { $failures.Add('Kernel nao marca RespStatus como BADOP.') }
foreach ($forbidden in @('BcPspSubmit', 'BcRegReadRaw', 'BcRegWriteRaw', '0x0F800000', '0x400000', 'GCMC_VM_FB_OFFSET')) {
    if ($kernelBody -match [regex]::Escape($forbidden)) { $failures.Add("Kernel ainda contem operacao/constante proibida: $forbidden.") }
}

$libBody = Function-Body $lib 'int Bc250PspSetupTmr(' 'int Bc250GfxInit('
if ($libBody -notmatch 'ERROR_NOT_SUPPORTED') { $failures.Add('Biblioteca nao retorna ERROR_NOT_SUPPORTED.') }
if ($libBody -notmatch 'BC250DBG_OPS_BADOP') { $failures.Add('Biblioteca nao marca RespStatus como BADOP.') }
if ($libBody -match 'Ioctl\s*\(') { $failures.Add('Biblioteca ainda envia IOCTL de SETUP_TMR.') }

$kernelLoad = Function-Body $kernel 'NTSTATUS BcPspLoadIpFw(' 'NTSTATUS BcPspSetupTmr('
$kernelCpGuard = $kernelLoad.IndexOf('FwType >= BC250_PSP_FW_CP_ME', [StringComparison]::Ordinal)
$kernelAlloc = $kernelLoad.IndexOf('MmAllocateContiguousMemorySpecifyCache', [StringComparison]::Ordinal)
if ($kernelCpGuard -lt 0 -or $kernelAlloc -lt 0 -or $kernelCpGuard -gt $kernelAlloc) {
    $failures.Add('Kernel nao recusa firmware CP antes de alocar/submeter.')
}
if ($kernelLoad -notmatch 'STATUS_NOT_SUPPORTED') { $failures.Add('Kernel LOAD_IP_FW CP nao retorna STATUS_NOT_SUPPORTED.') }

$libLoad = Function-Body $lib 'int Bc250PspLoadIpFw(' 'int Bc250SdmaF32Reset('
$libCpGuard = $libLoad.IndexOf('fwType >= BC250_PSP_FW_CP_ME', [StringComparison]::Ordinal)
$libIoctl = $libLoad.IndexOf('Ioctl(', [StringComparison]::Ordinal)
if ($libCpGuard -lt 0 -or $libIoctl -lt 0 -or $libCpGuard -gt $libIoctl) {
    $failures.Add('Biblioteca nao recusa firmware CP antes do IOCTL.')
}

$cliLoadBranch = $cli.IndexOf('if (_stricmp(argv[2], "loadfw") == 0', [StringComparison]::Ordinal)
$cliCpGuard = $cli.IndexOf('fwType >= BC250_PSP_FW_CP_ME', $cliLoadBranch, [StringComparison]::Ordinal)
$cliFileOpen = $cli.IndexOf('fopen(path, "rb")', $cliLoadBranch, [StringComparison]::Ordinal)
if ($cliLoadBranch -lt 0 -or $cliCpGuard -lt 0 -or $cliFileOpen -lt 0 -or $cliCpGuard -gt $cliFileOpen) {
    $failures.Add('CLI nao recusa firmware CP antes de abrir arquivo/IOCTL.')
}

$tmrBranch = [regex]::Match(
    $cli,
    'if \(_stricmp\(argv\[2\], "tmr"\) == 0\) \{(?<body>.*?)\n    \}',
    [Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $tmrBranch.Success) { throw 'Branch CLI psp tmr nao encontrado.' }
if ($tmrBranch.Groups['body'].Value -notmatch 'QUARENTENA') { $failures.Add('CLI psp tmr nao anuncia quarentena.') }
foreach ($forbidden in @('Bc250PspSetupTmr', 'Bc250Arm')) {
    if ($tmrBranch.Groups['body'].Value -match [regex]::Escape($forbidden)) { $failures.Add("CLI psp tmr ainda chama $forbidden.") }
}
if ($tmrBranch.Groups['body'].Value -match '\bIoctl\s*\(') { $failures.Add('CLI psp tmr ainda chama Ioctl.') }

$gfxQuarantine = $cli.IndexOf('GFX bringup legado em QUARENTENA', [StringComparison]::Ordinal)
$gfxHistoricalArm = $cli.IndexOf('Bc250Arm(&g_Ctx, BC250DBG_ARM_WRITE);', $gfxQuarantine, [StringComparison]::Ordinal)
if ($gfxQuarantine -lt 0) { $failures.Add('CLI gfx bringup nao anuncia quarentena.') }
elseif ($gfxHistoricalArm -lt 0) { $failures.Add('Bloco historico do gfx bringup nao foi localizado para auditoria.') }
else {
    $between = $cli.Substring($gfxQuarantine, $gfxHistoricalArm - $gfxQuarantine)
    if ($between -notmatch 'return 1;') { $failures.Add('GFX bringup nao retorna antes do primeiro arm historico.') }
}

if ($failures.Count -gt 0) {
    Write-Host 'AUDITORIA PSP TMR: FALHA' -ForegroundColor Red
    foreach ($failure in $failures) { Write-Host "  - $failure" -ForegroundColor Red }
    exit 1
}

Write-Host 'AUDITORIA PSP TMR: OK' -ForegroundColor Green
Write-Host '  Kernel: STATUS_NOT_SUPPORTED, sem submit/MMIO/endereco fixo.'
Write-Host '  Biblioteca: ERROR_NOT_SUPPORTED, sem IOCTL.'
Write-Host '  CLI: psp tmr e gfx bringup recusados antes de arm/escrita.'
Write-Host '  LOAD_IP_FW CP: CLI/kernel/biblioteca recusam antes de arquivo/staging/IOCTL.'
Write-Host '  Veredito: NAO VALIDADO EM HARDWARE.'
