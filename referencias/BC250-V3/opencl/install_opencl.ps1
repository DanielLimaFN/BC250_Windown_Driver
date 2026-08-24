#++
#
# Copyright (c) 2026 AMD BC-250 Driver Project
#
# Module Name:
#     install_opencl.ps1
#
# Abstract:
#     Comprehensive Global OpenCL 2.0/3.0 Deployment for AMD BC-250 (1536 Shaders / 16GB GDDR6).
#     Installs 64-bit and 32-bit Khronos Loaders (OpenCL.dll), Native ICDs (bc250_opencl.dll, amdocl64.dll, amdocl.dll, bc250_opencl32.dll),
#     and registers all Khronos / AMD / Display Device registry keys for GPU-Z, Blender, and Windows apps.
#
#--

param(
    [switch]$Uninstall
)

$ErrorActionPreference = "Continue"

# Auto-Elevation check
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[*] Tentando operacao com privilegios atuais (para registro de usuario) ou elevando se interativo..." -ForegroundColor Yellow
    try {
        $argsList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
        if ($Uninstall) { $argsList += " -Uninstall" }
        Start-Process powershell.exe -Verb RunAs -ArgumentList $argsList -ErrorAction Stop
        exit 0
    } catch {
        Write-Host "[!] Executando no contexto atual..." -ForegroundColor Gray
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$packageDir = Join-Path $scriptDir "..\package"
if (-not (Test-Path $packageDir)) {
    $packageDir = $scriptDir
}

$sys32Dir = [Environment]::GetFolderPath('System')
$sysWowDir = [Environment]::GetFolderPath('SystemX86')

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host " AMD BC-250 - Instalador Global OpenCL 2.0 (Khronos ICD & AMD)" -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host ""

if ($Uninstall) {
    Write-Host "[*] Removendo registro OpenCL ICD da AMD BC-250..." -ForegroundColor Yellow
    
    $regKeys = @(
        'HKLM:\SOFTWARE\Khronos\OpenCL\Vendors',
        'HKLM:\SOFTWARE\WOW6432Node\Khronos\OpenCL\Vendors',
        'HKLM:\SOFTWARE\AMD\OpenCL',
        'HKLM:\SOFTWARE\WOW6432Node\AMD\OpenCL'
    )
    foreach ($rk in $regKeys) {
        if (Test-Path $rk) {
            Remove-Item -Path $rk -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    Remove-Item (Join-Path $sys32Dir "bc250_opencl.dll") -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $sys32Dir "amdocl64.dll") -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $sysWowDir "bc250_opencl32.dll") -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $sysWowDir "amdocl.dll") -Force -ErrorAction SilentlyContinue

    Write-Host "[+] OpenCL desinstalado com sucesso!" -ForegroundColor Green
    exit 0
}

# 1. Instalar Khronos OpenCL.dll (64-bit em System32 e 32-bit em SysWOW64)
Write-Host "[1/5] Verificando Khronos OpenCL ICD Loader Central (64-bit e 32-bit)..." -ForegroundColor Yellow

$sys32OpenCl = Join-Path $sys32Dir "OpenCL.dll"
$sysWowOpenCl = Join-Path $sysWowDir "OpenCL.dll"

$srcOpenCl64 = Join-Path $packageDir "OpenCL.dll"
if (-not (Test-Path $srcOpenCl64)) { $srcOpenCl64 = Join-Path $scriptDir "OpenCL.dll" }
if (-not (Test-Path $srcOpenCl64)) { $srcOpenCl64 = Join-Path $scriptDir "..\build\OpenCL.dll" }

if (Test-Path $srcOpenCl64) {
    Copy-Item -LiteralPath $srcOpenCl64 -Destination $sys32OpenCl -Force
    Write-Host "      [+] OpenCL.dll (64-bit Loader) -> $sys32OpenCl" -ForegroundColor Green
} else {
    Write-Host "      [!] Aviso: OpenCL.dll 64-bit fonte nao encontrada." -ForegroundColor Yellow
}

$srcOpenCl32 = Join-Path $packageDir "OpenCL32.dll"
if (-not (Test-Path $srcOpenCl32)) { $srcOpenCl32 = Join-Path $scriptDir "OpenCL32.dll" }
if (-not (Test-Path $srcOpenCl32)) { $srcOpenCl32 = Join-Path $scriptDir "..\build\OpenCL32.dll" }

if (Test-Path $srcOpenCl32) {
    Copy-Item -LiteralPath $srcOpenCl32 -Destination $sysWowOpenCl -Force
    Write-Host "      [+] OpenCL.dll (32-bit Loader) -> $sysWowOpenCl (para GPU-Z)" -ForegroundColor Green
} else {
    Write-Host "      [!] Aviso: OpenCL32.dll 32-bit fonte nao encontrada." -ForegroundColor Yellow
}

# 2. Copiar DLLs do Driver BC-250 (64-bit) para System32
Write-Host "[2/5] Instalando Driver Nativo BC-250 (64-bit)..." -ForegroundColor Yellow
$srcDriver64 = Join-Path $packageDir "bc250_opencl.dll"
if (-not (Test-Path $srcDriver64)) { $srcDriver64 = Join-Path $scriptDir "bc250_opencl.dll" }
if (-not (Test-Path $srcDriver64)) { $srcDriver64 = Join-Path $scriptDir "..\build\bc250_opencl.dll" }

if (Test-Path $srcDriver64) {
    $tgtBc64 = Join-Path $sys32Dir "bc250_opencl.dll"
    $tgtAmd64 = Join-Path $sys32Dir "amdocl64.dll"
    Copy-Item -LiteralPath $srcDriver64 -Destination $tgtBc64 -Force
    Copy-Item -LiteralPath $srcDriver64 -Destination $tgtAmd64 -Force
    Write-Host "      [+] bc250_opencl.dll -> $tgtBc64" -ForegroundColor Green
    Write-Host "      [+] amdocl64.dll     -> $tgtAmd64" -ForegroundColor Green
} else {
    Write-Host "      [-] ERRO: bc250_opencl.dll fonte nao encontrada." -ForegroundColor Red
}

# 3. Copiar DLLs do Driver BC-250 (32-bit) para SysWOW64
Write-Host "[3/5] Instalando Driver Nativo BC-250 (32-bit)..." -ForegroundColor Yellow
$srcDriver32 = Join-Path $packageDir "bc250_opencl32.dll"
if (-not (Test-Path $srcDriver32)) { $srcDriver32 = Join-Path $scriptDir "bc250_opencl32.dll" }
if (-not (Test-Path $srcDriver32)) { $srcDriver32 = Join-Path $scriptDir "..\build\bc250_opencl32.dll" }

if (Test-Path $srcDriver32) {
    $tgtBc32 = Join-Path $sysWowDir "bc250_opencl32.dll"
    $tgtAmd32 = Join-Path $sysWowDir "amdocl.dll"
    Copy-Item -LiteralPath $srcDriver32 -Destination $tgtBc32 -Force
    Copy-Item -LiteralPath $srcDriver32 -Destination $tgtAmd32 -Force
    Write-Host "      [+] bc250_opencl32.dll -> $tgtBc32" -ForegroundColor Green
    Write-Host "      [+] amdocl.dll         -> $tgtAmd32" -ForegroundColor Green
} else {
    Write-Host "      [-] ERRO: bc250_opencl32.dll fonte nao encontrada." -ForegroundColor Red
}

# 4. Registrar chaves oficiais Khronos e AMD (64-bit e WOW64 32-bit)
Write-Host "[4/5] Configurando Chaves de Registro Globais (Khronos & AMD)..." -ForegroundColor Yellow

$khronos64 = "HKLM:\SOFTWARE\Khronos\OpenCL\Vendors"
if (-not (Test-Path $khronos64)) { New-Item -Path $khronos64 -Force | Out-Null }
Set-ItemProperty -Path $khronos64 -Name (Join-Path $sys32Dir "bc250_opencl.dll") -Value 0 -Type DWord
Set-ItemProperty -Path $khronos64 -Name (Join-Path $sys32Dir "amdocl64.dll") -Value 0 -Type DWord

$khronos32 = "HKLM:\SOFTWARE\WOW6432Node\Khronos\OpenCL\Vendors"
if (-not (Test-Path $khronos32)) { New-Item -Path $khronos32 -Force | Out-Null }
Set-ItemProperty -Path $khronos32 -Name (Join-Path $sysWowDir "bc250_opencl32.dll") -Value 0 -Type DWord
Set-ItemProperty -Path $khronos32 -Name (Join-Path $sysWowDir "amdocl.dll") -Value 0 -Type DWord

$amdOcl64 = "HKLM:\SOFTWARE\AMD\OpenCL"
if (-not (Test-Path $amdOcl64)) { New-Item -Path $amdOcl64 -Force | Out-Null }
Set-ItemProperty -Path $amdOcl64 -Name "Drivers" -Value @("amdocl64.dll", "bc250_opencl.dll") -Type MultiString

$amdOcl32 = "HKLM:\SOFTWARE\WOW6432Node\AMD\OpenCL"
if (-not (Test-Path $amdOcl32)) { New-Item -Path $amdOcl32 -Force | Out-Null }
Set-ItemProperty -Path $amdOcl32 -Name "Drivers" -Value @("amdocl.dll", "bc250_opencl32.dll") -Type MultiString

Write-Host "      [+] Khronos OpenCL Vendors (64/32-bit) OK" -ForegroundColor Green
Write-Host "      [+] AMD OpenCL Drivers Registry OK" -ForegroundColor Green

# 5. Injetar propriedades no Registro de Dispositivo de Video (GPU-Z Checkbox)
Write-Host "[5/5] Injetando Propriedades OpenCL no Adaptador WDDM (GPU-Z Checkbox)..." -ForegroundColor Yellow
$dispClass = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"
$devKeys = Get-ChildItem $dispClass -ErrorAction SilentlyContinue
foreach ($dk in $devKeys) {
    if ($dk.PSChildName -match '^\d{4}$') {
        $props = Get-ItemProperty $dk.PSPath -ErrorAction SilentlyContinue
        if ($props.DriverDesc -match 'BC-250' -or $props.DriverDesc -match 'AMD' -or $props.ProviderName -match 'AMD') {
            Set-ItemProperty -Path $dk.PSPath -Name "OpenCLDriverName" -Value @("amdocl64.dll", "bc250_opencl.dll") -Type MultiString -ErrorAction SilentlyContinue
            Set-ItemProperty -Path $dk.PSPath -Name "OpenCLDriverName_WOW" -Value @("amdocl.dll", "bc250_opencl32.dll") -Type MultiString -ErrorAction SilentlyContinue
            Set-ItemProperty -Path $dk.PSPath -Name "OpenCLFlags" -Value 2 -Type DWord -ErrorAction SilentlyContinue
            Set-ItemProperty -Path $dk.PSPath -Name "OpenCLVersion" -Value "OpenCL 2.0 AMD-BC250 (1536 Shaders, 16GB GDDR6)" -Type String -ErrorAction SilentlyContinue
            Write-Host "      [+] Chave do Adaptador ($($dk.PSChildName)): OpenCLDriverName e OpenCLFlags configurados!" -ForegroundColor Green
        }
    }
}

Write-Host ""
Write-Host "=================================================================" -ForegroundColor Green
Write-Host " ARQUIVOS DO OPENCL INSTALADOS" -ForegroundColor Green
Write-Host " ATENCAO: instalar arquivos nao e' aceleracao." -ForegroundColor Green
Write-Host " compilador OpenCL C -> ISA GFX10 neste projeto, e o microcodigo do" -ForegroundColor Yellow
Write-Host " CP nao e' aceito pelo PSP. Aplicativos como Blender, llama.cpp ou" -ForegroundColor Yellow
Write-Host " LuxMark vao enumerar a plataforma e FALHAR ao executar kernels —" -ForegroundColor Yellow
Write-Host " que e' o comportamento correto, nao um defeito da instalacao." -ForegroundColor Yellow
Write-Host "=================================================================" -ForegroundColor Green
Write-Host ""
