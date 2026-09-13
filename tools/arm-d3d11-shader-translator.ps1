# =============================================================================
# D3D11ShaderTranslator — armar / desarmar / inspecionar
#
# Controla as duas chaves de registro exigidas por bc250umd.dll para carregar
# dinamicamente o vkd3d-shader x64 e compilar shaders para SPIR-V:
#   D3D11ShaderTranslator     (REG_DWORD = 1)
#   D3D11ShaderTranslatorPath (REG_SZ = caminho absoluto da DLL)
#
# Default de fabrica: DESARMADO.
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Arm,
    [switch]$Disarm,
    [string]$DllPath,
    [switch]$IAcceptBlackScreenRisk
)

$ErrorActionPreference = 'Stop'

$key      = 'HKLM:\SOFTWARE\BC250\Umd'
$flagName = 'D3D11ShaderTranslator'
$pathName = 'D3D11ShaderTranslatorPath'

function Show-State {
    if (Test-Path -LiteralPath $key) {
        $flag = (Get-ItemProperty -LiteralPath $key -Name $flagName -ErrorAction SilentlyContinue).$flagName
        $path = (Get-ItemProperty -LiteralPath $key -Name $pathName -ErrorAction SilentlyContinue).$pathName
        if ($null -eq $flag -or $flag -eq 0) {
            Write-Host "D3D11_SHADER_TRANSLATOR: DESARMADO ($flagName = 0 ou ausente)"
        } else {
            Write-Host "D3D11_SHADER_TRANSLATOR: ARMADO ($flagName = $flag)" -ForegroundColor Yellow
        }
        if ($null -ne $path) {
            Write-Host "  $pathName = $path" -ForegroundColor Cyan
        } else {
            Write-Host "  ${pathName}: ausente"
        }
    } else {
        Write-Host "D3D11_SHADER_TRANSLATOR: DESARMADO (chave $key ausente)"
    }
    Write-Host "  chave: $key"
    Write-Host "NAO VALIDADO EM HARDWARE"
}

if (-not $Arm -and -not $Disarm) {
    Show-State
    Write-Host ''
    Write-Host 'Uso: -Arm -DllPath <caminho_absoluto> -IAcceptBlackScreenRisk   |   -Disarm'
    exit 0
}

$identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado: a chave fica em HKLM.'
}

if ($Disarm) {
    if (Test-Path -LiteralPath $key) {
        Remove-ItemProperty -LiteralPath $key -Name $flagName -ErrorAction SilentlyContinue
        Remove-ItemProperty -LiteralPath $key -Name $pathName -ErrorAction SilentlyContinue
        Write-Host "D3D11_SHADER_TRANSLATOR: DESARMADO com sucesso." -ForegroundColor Green
    } else {
        Write-Host "D3D11_SHADER_TRANSLATOR: ja desarmado (chave ausente)."
    }
    Show-State
    exit 0
}

if ($Arm) {
    if (-not $IAcceptBlackScreenRisk) {
        throw 'Armar o tradutor exige -IAcceptBlackScreenRisk explicito.'
    }
    if ([string]::IsNullOrWhiteSpace($DllPath)) {
        throw 'Armar o tradutor exige -DllPath com o caminho absoluto da libvkd3d-shader-1.dll.'
    }
    $resolved = (Resolve-Path -LiteralPath $DllPath).Path
    if (-not [IO.Path]::IsPathRooted($resolved) -or -not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Arquivo nao encontrado ou caminho nao absoluto: $DllPath"
    }

    if (-not (Test-Path -LiteralPath $key)) {
        New-Item -ItemType Directory -Path $key -Force | Out-Null
    }
    Set-ItemProperty -LiteralPath $key -Name $flagName -Value 1 -Type DWord
    Set-ItemProperty -LiteralPath $key -Name $pathName -Value $resolved -Type String

    Write-Host "D3D11_SHADER_TRANSLATOR: ARMADO com sucesso." -ForegroundColor Yellow
    Write-Host "  DLL: $resolved" -ForegroundColor Yellow
    Show-State
    exit 0
}
