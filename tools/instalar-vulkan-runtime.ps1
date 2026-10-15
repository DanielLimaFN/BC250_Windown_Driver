# ICD.1 (doc 757): deixa o Windows pronto para qualquer app Vulkan achar a BC-250,
# como o instalador do driver da AMD faz.
#
#   1. Runtime do Vulkan (loader da Khronos, vulkan-1.dll em System32): se faltar,
#      instala em silencio o VulkanRT.exe do Vulkan SDK que ja esta na maquina.
#      Sem ele nenhum app Vulkan abre (o vkcube baixado da internet nao acha
#      vulkan-1.dll). E' o mesmo pacote que os drivers da AMD/NVIDIA instalam.
#   2. Remove a entrada antiga HKLM\SOFTWARE\Khronos\Vulkan\Drivers que aponta para
#      driver\umd\output\bc250_icd.json (DLL bc250_vulkan.dll inexistente, de 31/08).
#      O driver passa a ser achado pelo adaptador (VulkanDriverName no INF).
#      Backup em C:\BC250_VK_ICD_ANTIGO.reg antes de apagar.
#
# Precisa de administrador. Nao mexe no driver nem na placa.
[CmdletBinding()]
param(
    [string]$VulkanRt = 'C:\VulkanSDK\1.4.357.0\Helpers\VulkanRT.exe'
)

$ErrorActionPreference = 'Stop'
$loader = Join-Path $env:SystemRoot 'System32\vulkan-1.dll'

if (Test-Path -LiteralPath $loader -PathType Leaf) {
    Write-Host ("Runtime do Vulkan presente: {0} versao {1}" -f $loader, (Get-Item -LiteralPath $loader).VersionInfo.FileVersion)
} else {
    if (-not (Test-Path -LiteralPath $VulkanRt -PathType Leaf)) {
        throw "Runtime do Vulkan ausente e instalador nao encontrado: $VulkanRt"
    }
    Write-Host "Instalando o runtime do Vulkan (Khronos) de $VulkanRt ..."
    $p = Start-Process -FilePath $VulkanRt -ArgumentList '/S' -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "VulkanRT.exe /S saiu com $($p.ExitCode)" }
    if (-not (Test-Path -LiteralPath $loader -PathType Leaf)) {
        throw "VulkanRT.exe terminou mas $loader nao apareceu"
    }
    Write-Host ("Runtime do Vulkan instalado: versao {0}" -f (Get-Item -LiteralPath $loader).VersionInfo.FileVersion)
}

$key = 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers'
if (Test-Path -LiteralPath $key) {
    $props = (Get-Item -LiteralPath $key).Property
    $stale = @($props | Where-Object { $_ -match '(?i)\\driver\\umd\\output\\bc250_icd\.json$' })
    if ($stale.Count -gt 0) {
        & reg.exe export 'HKLM\SOFTWARE\Khronos\Vulkan\Drivers' 'C:\BC250_VK_ICD_ANTIGO.reg' /y | Out-Null
        foreach ($name in $stale) {
            Remove-ItemProperty -LiteralPath $key -Name $name
            Write-Host "Entrada antiga do ICD removida: $name (backup em C:\BC250_VK_ICD_ANTIGO.reg)"
        }
    }
    $rest = @((Get-Item -LiteralPath $key).Property)
    if ($rest.Count -gt 0) {
        Write-Host ("Outros ICDs registrados no sistema (mantidos): {0}" -f ($rest -join '; '))
    }
}

foreach ($var in @('VK_ICD_FILENAMES', 'VK_DRIVER_FILES', 'VK_ADD_DRIVER_FILES', 'VK_LOADER_DRIVERS_SELECT', 'VK_LOADER_DRIVERS_DISABLE')) {
    foreach ($scope in @('Machine', 'User')) {
        $v = [Environment]::GetEnvironmentVariable($var, $scope)
        if ($v) { Write-Warning "$var definido em $scope ($v): o app nao usara o caminho de driver oficial" }
    }
}
Write-Host 'ICD.1: Windows pronto para apps Vulkan (o driver aparece quando a BC-250 estiver com o bc250mcdm).'
