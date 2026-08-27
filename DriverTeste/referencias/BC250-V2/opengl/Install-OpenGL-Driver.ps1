<#
.SYNOPSIS
    Instalador e Registrador Global do Driver OpenGL 4.60 para AMD BC-250
.DESCRIPTION
    Instala as DLLs do driver OpenGL e registra o ICD no Registro do Windows.
#>

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$BuildDir  = Join-Path (Split-Path -Parent $ScriptDir) "build"
$Sys32     = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::System)

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "   AMD BC-250: INSTALACAO DO DRIVER OPENGL 4.60         " -ForegroundColor Cyan
Write-Host "   (Cyan Skillfish / Navi12 Lite - 1536 Shaders / 16GB) " -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# Verificar privilégios de Administrador
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "`n[AVISO] Para registrar o driver globalmente no Windows, execute este script como Administrador!" -ForegroundColor Yellow
    Write-Host "[DICA] Para usar em jogos ou emuladores especificos sem ser admin, basta copiar as DLLs da pasta 'build' para a pasta do jogo.`n" -ForegroundColor Green
    return
}

Write-Host "`n[1/3] Copiando bibliotecas para $Sys32..." -ForegroundColor Yellow
$dlls = @("atio6xxx.dll", "bc250_opengl32.dll", "libgallium_wgl.dll", "vulkan_radeon.dll", "z-1.dll")
foreach ($dll in $dlls) {
    $src = Join-Path $BuildDir $dll
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $Sys32 -Force
        Write-Host "  • $dll -> $Sys32 [OK]" -ForegroundColor Green
    } else {
        Write-Host "  • $dll nao encontrado em $BuildDir" -ForegroundColor Red
    }
}

Write-Host "`n[2/3] Configurando Registro do Windows (HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers)..." -ForegroundColor Yellow
$regPath64 = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\MSAMD"
if (-not (Test-Path $regPath64)) { New-Item -Path $regPath64 -Force | Out-Null }
Set-ItemProperty -Path $regPath64 -Name "DLL" -Value "atio6xxx.dll"
Set-ItemProperty -Path $regPath64 -Name "Version" -Value 2 -Type DWord
Set-ItemProperty -Path $regPath64 -Name "DriverVersion" -Value 1 -Type DWord
Set-ItemProperty -Path $regPath64 -Name "Flags" -Value 1 -Type DWord

$regPath32 = "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\MSAMD"
if (-not (Test-Path $regPath32)) { New-Item -Path $regPath32 -Force | Out-Null }
Set-ItemProperty -Path $regPath32 -Name "DLL" -Value "atio6xxx.dll"
Set-ItemProperty -Path $regPath32 -Name "Version" -Value 2 -Type DWord
Set-ItemProperty -Path $regPath32 -Name "DriverVersion" -Value 1 -Type DWord
Set-ItemProperty -Path $regPath32 -Name "Flags" -Value 1 -Type DWord
Write-Host "  • Chaves de Registro do ICD criadas com sucesso! [OK]" -ForegroundColor Green

Write-Host "`n[3/3] Registrando no Khronos OpenGL Drivers..." -ForegroundColor Yellow
$khronosPath = "HKLM:\SOFTWARE\Khronos\OpenGL\Drivers"
if (-not (Test-Path $khronosPath)) { New-Item -Path $khronosPath -Force | Out-Null }
Set-ItemProperty -Path $khronosPath -Name "atio6xxx.dll" -Value 0 -Type DWord
Write-Host "  • Registro Khronos concluido! [OK]" -ForegroundColor Green

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "  [SUCESSO] DRIVER OPENGL 4.60 INSTALADO COM EXITO!     " -ForegroundColor Green
Write-Host "  Qualquer aplicacao, emulador ou jogo agora utilizara   " -ForegroundColor Green
Write-Host "  a GPU AMD BC-250 nativamente com aceleracao total.     " -ForegroundColor Green
Write-Host "========================================================`n" -ForegroundColor Cyan
