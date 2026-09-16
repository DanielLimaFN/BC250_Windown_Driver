<#
.SYNOPSIS
    Teste de Conformidade Direct3D 11 Nativo (bc250umd.dll -> Vulkan Bridge -> BC-250).

.DESCRIPTION
    Executa o pipeline Direct3D 11 canonico via Windows Runtime padrao (sem DXVK na pasta),
    roteando atraves da UMD nativa do projeto (bc250umd.dll), Vulkan Graphics Bridge,
    Mesa RADV e silicio fisico da AMD BC-250 (1002:13FE).

    Valida:
    - Negociacao de interface D3D10_1/D3D11 com o runtime do Windows.
    - Criacao de recursos (Textures, Buffers, RTV, DSV) e compilacao de shaders via D3DCompile.
    - Pipeline Cache e execucao de draws via vkCmdDraw e apresentacao WDDM.
    - Sustentacao de 600 quadros a 60 FPS no silicio real.
#>
[CmdletBinding()]
param(
    [string]$EvidenceDir,
    [string]$Harness,
    [ValidateRange(10,300)][int]$TimeoutSeconds = 120,
    [ValidateRange(1,3600)][int]$Frames = 600,
    [switch]$Headless,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$probePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$kmdAudit = Join-Path $root 'tools\audit-bc250mcdm-safety.py'
$safeGuardAuditScript = Join-Path $root 'tools\assert-bc250-safe-bind-guard.ps1'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'

if ([string]::IsNullOrWhiteSpace($Harness)) {
    $Harness = Join-Path $root 'driver\tests\output_d3d11_native\bc250-d3d11-conformance.exe'
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $root ("docs\evidence\" + $stamp + "_d3d11_native_conformance")
}

Write-Host "===================================================================" -ForegroundColor Cyan
Write-Host " BC-250 DIRECT3D 11 NATIVE CONFORMANCE GATE (bc250umd.dll)" -ForegroundColor Cyan
Write-Host " Target: AMD BC-250 APU (1002:13FE) | Windows 10 22H2 x64" -ForegroundColor Cyan
Write-Host "===================================================================" -ForegroundColor Cyan
Write-Host "  Harness:      $Harness"
Write-Host "  Frames:       $Frames"
Write-Host "  Headless:     $Headless"
Write-Host "  Evidence Dir: $EvidenceDir"

if (-not (Test-Path -LiteralPath $Harness)) {
    throw "Harness D3D11 nativo nao encontrado em: $Harness. Execute tools\build-d3d11-native-conformance.bat primeiro."
}

if (-not $Execute) {
    Write-Host "`n[MODO SIMULACAO/PREFLIGHT] Para executar no silicio, execute com -Execute." -ForegroundColor Yellow
    exit 0
}

if (-not (Test-Path -LiteralPath $EvidenceDir)) {
    New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
}

$evidenceLog = Join-Path $EvidenceDir 'd3d11_native_conformance.log'

Write-Host "`n[PREFLIGHT 1/3] Auditoria de seguranca KMD..." -ForegroundColor Cyan
if (Test-Path -LiteralPath $kmdAudit) {
    & python $kmdAudit
    if ($LASTEXITCODE -ne 0) {
        throw "Auditoria de seguranca do KMD reprovou com exit $LASTEXITCODE."
    }
    Write-Host "  -> KMD Safety Audit: PASS" -ForegroundColor Green
}

Write-Host "`n[PREFLIGHT 2/3] Verificando integridade do boot fuse..." -ForegroundColor Cyan
$inspectScript = Join-Path $root 'tools\inspect-boot-fuse.ps1'
if (Test-Path -LiteralPath $inspectScript) {
    & $inspectScript
}

Write-Host "`n[PREFLIGHT 3/3] Checando biblioteca auxiliar C:\BC250_LIB..." -ForegroundColor Cyan
if (-not (Test-Path -LiteralPath 'C:\BC250_LIB\vulkan_radeon.dll')) {
    Write-Host "  [AVISO] vulkan_radeon.dll nao encontrada em C:\BC250_LIB. Verifique variaveis de ambiente." -ForegroundColor Yellow
} else {
    Write-Host "  -> C:\BC250_LIB: OK" -ForegroundColor Green
}

Write-Host "`n[EXECUCAO NO SILICIO] Disparando bc250-d3d11-conformance.exe..." -ForegroundColor Cyan

$argsList = @("--frames", "$Frames")
if ($Headless) {
    $argsList += "--headless"
}

$pinfo = New-Object System.Diagnostics.ProcessStartInfo
$pinfo.FileName = $Harness
$pinfo.Arguments = ($argsList -join ' ')
$pinfo.RedirectStandardOutput = $true
$pinfo.RedirectStandardError = $true
$pinfo.UseShellExecute = $false
$pinfo.CreateNoWindow = $false

$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $pinfo

$stdoutBuilder = New-Object System.Text.StringBuilder
$stderrBuilder = New-Object System.Text.StringBuilder

$stdoutHandler = {
    if (-not [string]::IsNullOrEmpty($EventArgs.Data)) {
        Write-Host $EventArgs.Data
        [void]$stdoutBuilder.AppendLine($EventArgs.Data)
    }
}
$stderrHandler = {
    if (-not [string]::IsNullOrEmpty($EventArgs.Data)) {
        Write-Host $EventArgs.Data -ForegroundColor Red
        [void]$stderrBuilder.AppendLine($EventArgs.Data)
    }
}

Register-ObjectEvent -InputObject $proc -EventName 'OutputDataReceived' -Action $stdoutHandler | Out-Null
Register-ObjectEvent -InputObject $proc -EventName 'ErrorDataReceived' -Action $stderrHandler | Out-Null

[void]$proc.Start()
$proc.BeginOutputReadLine()
$proc.BeginErrorReadLine()

$exited = $proc.WaitForExit($TimeoutSeconds * 1000)

if (-not $exited) {
    $proc.Kill()
    Write-Host "`n[TIMEOUT] Teste excedeu $TimeoutSeconds segundos e foi cancelado." -ForegroundColor Red
    $exitCode = -1
} else {
    $exitCode = $proc.ExitCode
}

$fullLog = "STDOUT:`r`n" + $stdoutBuilder.ToString() + "`r`nSTDERR:`r`n" + $stderrBuilder.ToString()
Set-Content -LiteralPath $evidenceLog -Value $fullLog -Encoding UTF8

Write-Host "`n===================================================================" -ForegroundColor Cyan
Write-Host " RESULTADO DO TESTE D3D11 NATIVO (EXIT CODE: $exitCode)" -ForegroundColor Cyan
Write-Host "===================================================================" -ForegroundColor Cyan
if ($exitCode -eq 0) {
    Write-Host " VEREDITO: SUCESSO - D3D11 Nativo validado com sucesso!" -ForegroundColor Green
} else {
    Write-Host " VEREDITO: FALHA - Exit code $exitCode. Consulte log em $evidenceLog" -ForegroundColor Red
}

exit $exitCode
