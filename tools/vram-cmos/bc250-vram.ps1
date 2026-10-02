[CmdletBinding()]
param(
    [switch]$Ler,
    [int]$SetUma = 0,
    [switch]$Confirmo,
    # doc 553: -Ler tambem le e LIMPA o motivo do ultimo reset (FCH), como o Linux.
    [switch]$SemMotivoReset,
    [switch]$NaoLimparReset
)

# bc250-vram.ps1 -- equivalente Windows de "bc250-linux-referencia.sh --ler-vram / --set-vram".
#   .\bc250-vram.ps1 -Ler
#   .\bc250-vram.ps1 -SetUma 4096            (simulacao: mostra o plano, nada grava)
#   .\bc250-vram.ps1 -SetUma 4096 -Confirmo  (grava; aplica no proximo boot frio)
# Carrega bc250cmos.sys como servico temporario, roda bc250-vram.exe, arquiva a saida
# em docs\evidence\vram-cmos\ e SEMPRE descarrega o driver no fim. Doc 550.

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = Split-Path -Parent (Split-Path -Parent $here)
$sys = Join-Path $here 'out\bc250cmos.sys'
$exe = Join-Path $here 'out\bc250-vram.exe'
$svc = 'bc250cmos'

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Rode em PowerShell como Administrador.'
}
foreach ($f in $sys, $exe) {
    if (-not (Test-Path -LiteralPath $f)) { throw "Ausente: $f -- rode tools\vram-cmos\build.bat antes." }
}
if (-not $Ler -and $SetUma -le 0) { $Ler = $true }
if ($Confirmo -and $SetUma -le 0) { throw '-Confirmo so vale com -SetUma <MB>.' }

$bcd = (bcdedit /enum '{current}') -join "`n"
if ($bcd -notmatch '(?im)^testsigning\s+(Yes|Sim|On)') {
    throw 'testsigning desligado: o bc250cmos.sys (assinatura de teste) nao carrega.'
}

$evDir = Join-Path $root 'docs\evidence\vram-cmos'
New-Item -ItemType Directory -Force $evDir | Out-Null
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$mode = if ($SetUma -gt 0) { if ($Confirmo) { "set-$SetUma" } else { "simula-$SetUma" } } else { 'ler' }
$log = Join-Path $evDir "$stamp-$env:COMPUTERNAME-$mode.txt"

$cliArgs = @()
if ($SetUma -gt 0) { $cliArgs += @('--set-uma', "$SetUma"); if ($Confirmo) { $cliArgs += '--confirmo' } } else {
    $cliArgs += '--ler'
    if (-not $SemMotivoReset) { $cliArgs += '--motivo-reset'; if (-not $NaoLimparReset) { $cliArgs += '--limpar-reset' } }
}

$created = $false
try {
    if (Get-Service -Name $svc -ErrorAction SilentlyContinue) {
        sc.exe stop $svc | Out-Null
        sc.exe delete $svc | Out-Null
        Start-Sleep -Milliseconds 500
    }
    sc.exe create $svc type= kernel start= demand binPath= "$sys" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "sc create falhou ($LASTEXITCODE)" }
    $created = $true
    sc.exe start $svc | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "sc start falhou ($LASTEXITCODE) -- certificado BC250-WIN Test confiavel? testsigning?" }

    $header = @(
        "bc250-vram.ps1 $stamp maquina=$env:COMPUTERNAME modo=$mode",
        "bc250cmos.sys sha256=$((Get-FileHash $sys -Algorithm SHA256).Hash)",
        "bc250-vram.exe sha256=$((Get-FileHash $exe -Algorithm SHA256).Hash)",
        "RAM vista pelo Windows (bytes)=$((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory)",
        ''
    )
    $out = & $exe @cliArgs 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    ($header + $out + '', "exit=$code") | Set-Content -LiteralPath $log -Encoding UTF8
    $out | ForEach-Object { Write-Host $_ }
    Write-Host ''
    Write-Host "Evidencia: $log"
    if ($code -ne 0) { exit $code }
}
finally {
    if ($created) {
        sc.exe stop $svc | Out-Null
        sc.exe delete $svc | Out-Null
    }
}
