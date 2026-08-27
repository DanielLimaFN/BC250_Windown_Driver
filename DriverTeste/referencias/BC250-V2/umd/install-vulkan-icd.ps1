# =============================================================================
#  install-vulkan-icd.ps1 - Registra o Driver Vulkan da AMD BC-250 no Windows
#
#  Padrao: Registra o Mesa RADV Vulkan 1.3 (vulkan_radeon.dll) com compilador
#  ACO nativo para execucao de shaders, jogos e IA / LLMs (LM Studio / llama.cpp).
#
#  Uso:
#    powershell -ExecutionPolicy Bypass -File install-vulkan-icd.ps1
#    powershell -ExecutionPolicy Bypass -File install-vulkan-icd.ps1 -Driver "RADV"
#    powershell -ExecutionPolicy Bypass -File install-vulkan-icd.ps1 -Driver "Lightweight"
#    powershell -ExecutionPolicy Bypass -File install-vulkan-icd.ps1 -Uninstall
#
#  Requer elevacao de Administrador (escreve em HKLM).
# =============================================================================
[CmdletBinding()]
param(
    [ValidateSet('Lightweight', 'RADV')]
    [string]$Driver = 'Lightweight',
    [string]$DllPath = '',
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $pr = [Security.Principal.WindowsPrincipal]::new($id)
    if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host "[!] Verificando privilegios de Administrador..." -ForegroundColor Yellow
        try {
            $argsList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
            if ($Uninstall) { $argsList += " -Uninstall" }
            elseif ($Driver -ne 'RADV') { $argsList += " -Driver `"$Driver`"" }
            Start-Process powershell -Verb RunAs -ArgumentList $argsList -ErrorAction Stop
            exit
        } catch {
            Write-Host "  [!] Aviso: Executando no contexto atual. Para salvar em HKLM, execute como Administrador." -ForegroundColor Yellow
        }
    }
}

Assert-Admin

$regKeys = @(
    'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers',
    'HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers'
)

# ---------------------------------------------------------------------------
# DESINSTALAÇÃO / LIMPEZA COMPLETA
# ---------------------------------------------------------------------------
if ($Uninstall) {
    foreach ($k in $regKeys) {
        if (-not (Test-Path $k)) { continue }
        foreach ($name in (Get-Item $k).Property) {
            if ($name -like '*bc250*' -or $name -like '*vulkan_radeon*') {
                Remove-ItemProperty -Path $k -Name $name -Force
                Write-Host "Removido do Registro: $name" -ForegroundColor Yellow
            }
        }
    }
    Write-Host 'Driver Vulkan da BC-250 desregistrado com sucesso.' -ForegroundColor Green
    exit 0
}

# ---------------------------------------------------------------------------
# LOCALIZAÇÃO DO DRIVER ESCOLHIDO
# ---------------------------------------------------------------------------
$root = Split-Path $PSScriptRoot -Parent
$Sys32 = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::System)

if ($Driver -eq 'RADV') {
    $driverDllName = "vulkan_radeon.dll"
    $driverJsonName = "vulkan_radeon.json"
    $apiVersion = "1.3.296"
    $candidates = @(
        (Join-Path $Sys32 $driverDllName),
        (Join-Path $root "package\$driverDllName"),
        (Join-Path $root "build\$driverDllName")
    )
} else {
    $driverDllName = "amdbc250vulkan.dll"
    $driverJsonName = "amdbc250vulkan.json"
    $apiVersion = "1.2.0"
    $candidates = @(
        (Join-Path $Sys32 $driverDllName),
        (Join-Path $root "package\$driverDllName"),
        (Join-Path $root "build\$driverDllName")
    )
}

if ($DllPath -eq '') {
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { $DllPath = $c; break }
    }
}

if ($DllPath -eq '' -or -not (Test-Path -LiteralPath $DllPath)) {
    throw "$driverDllName nao encontrada. Verifique os arquivos em package\ ou execute o build."
}
$DllPath = (Resolve-Path -LiteralPath $DllPath).Path

# Validacao de binario PE
$head = [System.IO.File]::ReadAllBytes($DllPath)[0..1]
if ($head[0] -ne 0x4D -or $head[1] -ne 0x5A) {
    throw "$DllPath nao e um binario PE valido."
}

Write-Host "==========================================================================" -ForegroundColor Cyan
Write-Host "   AMD BC-250: CONFIGURACAO E REGISTRO VULKAN OFICIAL ($Driver)          " -ForegroundColor Cyan
Write-Host "==========================================================================" -ForegroundColor Cyan
Write-Host "DLL Selecionada : $DllPath ($([math]::Round((Get-Item $DllPath).Length / 1MB, 2)) MB)" -ForegroundColor Green

# ---------------------------------------------------------------------------
# GERAÇÃO DO MANIFESTO JSON
# ---------------------------------------------------------------------------
$jsonDir = Split-Path $DllPath -Parent
$jsonPath = Join-Path $jsonDir $driverJsonName

$manifest = [ordered]@{
    file_format_version = '1.0.0'
    ICD = [ordered]@{
        library_path = $DllPath
        api_version  = $apiVersion
    }
}
$jsonContent = $manifest | ConvertTo-Json -Depth 4

try {
    [System.IO.File]::WriteAllText($jsonPath, $jsonContent, (New-Object System.Text.UTF8Encoding($false)))
} catch {
    # Fallback para pasta package caso System32 esteja protegido
    $jsonPath = Join-Path $root "package\$driverJsonName"
    $manifest.ICD.library_path = $DllPath
    $jsonContent = $manifest | ConvertTo-Json -Depth 4
    [System.IO.File]::WriteAllText($jsonPath, $jsonContent, (New-Object System.Text.UTF8Encoding($false)))
}

Write-Host "Manifesto JSON  : $jsonPath (Vulkan $apiVersion)" -ForegroundColor Green

# ---------------------------------------------------------------------------
# REGISTRO NO WINDOWS (64-bit e 32-bit)
# ---------------------------------------------------------------------------
foreach ($k in $regKeys) {
    try {
        if (-not (Test-Path $k)) { New-Item -Path $k -Force | Out-Null }
        
        # Remove entradas antigas conflitantes (apenas 1 driver Vulkan BC-250 ativo por vez)
        foreach ($prop in (Get-Item $k).Property) {
            if (($prop -like '*amdbc250vulkan*' -or $prop -like '*vulkan_radeon*') -and $prop -ne $jsonPath) {
                Remove-ItemProperty -Path $k -Name $prop -Force -ErrorAction SilentlyContinue
                Write-Host "  [-] Entrada antiga/conflitante removida de $($k): $prop" -ForegroundColor Yellow
            }
        }
        
        # Registra o manifesto oficial ativo
        New-ItemProperty -Path $k -Name $jsonPath -Value 0 -PropertyType DWord -Force | Out-Null
        Write-Host "  [+] Registrado com sucesso em: $k" -ForegroundColor Green
    } catch {
        Write-Host "  [!] Aviso ao gravar em $($k): $($_.Exception.Message)" -ForegroundColor DarkYellow
    }
}

# ---------------------------------------------------------------------------
# ATUALIZAÇÃO DO tools_ai E AMBIENTE LOCAL
# ---------------------------------------------------------------------------
$aiDir = Join-Path (Split-Path $root -Parent) "tools_ai\llama-cpp"
if (Test-Path $aiDir) {
    Copy-Item $DllPath (Join-Path $aiDir $driverDllName) -Force -ErrorAction SilentlyContinue
    Copy-Item $jsonPath (Join-Path $aiDir $driverJsonName) -Force -ErrorAction SilentlyContinue
    Write-Host "  [+] Driver sincronizado com a pasta de IA: $aiDir" -ForegroundColor Green
}

Write-Host "`n[SUCESSO] O Vulkan da AMD BC-250 esta configurado e pronto para uso!" -ForegroundColor Green
Write-Host "Verifique o status executando:  vulkaninfo --summary`n" -ForegroundColor Cyan
