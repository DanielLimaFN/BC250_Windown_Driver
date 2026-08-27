<#
.SYNOPSIS
    Instalador e Registrador Global do OpenAL Soft para AMD BC-250 (Cyan Skillfish)
.DESCRIPTION
    Instala as bibliotecas de áudio OpenAL Soft (64-bit e 32-bit) no System32 e SysWOW64,
    registra as chaves de Registro do Windows e configura o fallback para modo headless (alsoft.ini),
    eliminando erros como 'MMDevApiOpenPlayback 0x80070490' no Minecraft e outros jogos.
#>

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$Sys32     = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::System)
$SysWOW64  = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::SystemX86)
$ProgramData = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::CommonApplicationData)
$AppData   = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::ApplicationData)

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "   AMD BC-250: INSTALADOR DO SUBSISTEMA OPENAL SOFT     " -ForegroundColor Cyan
Write-Host "   (Cyan Skillfish / Navi12 Lite - Oberon PS5 APU)      " -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# 1. Configurar fallback de usuário imediatamente
$iniSrc = "$ScriptDir\alsoft.ini"
if (Test-Path $iniSrc) {
    Copy-Item -Path $iniSrc -Destination "$AppData\alsoft.ini" -Force
    [Environment]::SetEnvironmentVariable("ALSOFT_CONF", "$AppData\alsoft.ini", "User")
    [Environment]::SetEnvironmentVariable("ALSOFT_DRIVERS", "-wasapi,null", "User")
    Write-Host "  • [User] alsoft.ini configurado em $AppData\alsoft.ini [OK]" -ForegroundColor Green
}

# 2. Verificar Privilégios de Administrador
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "`n[AVISO] Privilégios de Administrador não detectados." -ForegroundColor Yellow
    Write-Host "  • Configuração de usuário aplicada no AppData." -ForegroundColor Green
    Write-Host "  • Para instalação global no System32/SysWOW64, execute 'Register-OpenAL.bat'.`n" -ForegroundColor Cyan
    return
}

# 3. Instalar DLLs 64-bit em System32
Write-Host "`n[1/4] Instalando bibliotecas 64-bit em $Sys32..." -ForegroundColor Yellow
$dlls64 = @(
    @{ Src = "$ScriptDir\x64\OpenAL32.dll"; Dst = "$Sys32\OpenAL32.dll" },
    @{ Src = "$ScriptDir\x64\soft_oal.dll"; Dst = "$Sys32\soft_oal.dll" }
)

foreach ($d in $dlls64) {
    if (Test-Path $d.Src) {
        Copy-Item -Path $d.Src -Destination $d.Dst -Force
        Write-Host "  • $(Split-Path $d.Dst -Leaf) -> $Sys32 [OK]" -ForegroundColor Green
    } else {
        Write-Host "  • Aviso: $($d.Src) não encontrado!" -ForegroundColor Red
    }
}

# 4. Instalar DLLs 32-bit em SysWOW64
if (Test-Path $SysWOW64) {
    Write-Host "`n[2/4] Instalando bibliotecas 32-bit em $SysWOW64..." -ForegroundColor Yellow
    $dlls32 = @(
        @{ Src = "$ScriptDir\x86\OpenAL32.dll"; Dst = "$SysWOW64\OpenAL32.dll" },
        @{ Src = "$ScriptDir\x86\soft_oal.dll"; Dst = "$SysWOW64\soft_oal.dll" }
    )

    foreach ($d in $dlls32) {
        if (Test-Path $d.Src) {
            Copy-Item -Path $d.Src -Destination $d.Dst -Force
            Write-Host "  • $(Split-Path $d.Dst -Leaf) -> $SysWOW64 [OK]" -ForegroundColor Green
        } else {
            Write-Host "  • Aviso: $($d.Src) não encontrado!" -ForegroundColor Red
        }
    }
}

# 5. Configurar Arquivos de Configuração alsoft.ini no Sistema
Write-Host "`n[3/4] Configurando alsoft.ini no Sistema..." -ForegroundColor Yellow
if (Test-Path $iniSrc) {
    $iniDestinations = @(
        "$ProgramData\alsoft.ini",
        "$AppData\alsoft.ini",
        "$env:WINDIR\alsoft.ini"
    )
    foreach ($dest in $iniDestinations) {
        Copy-Item -Path $iniSrc -Destination $dest -Force
        Write-Host "  • alsoft.ini -> $dest [OK]" -ForegroundColor Green
    }
}

# 6. Configurar Registro do Windows e Variáveis de Ambiente
Write-Host "`n[4/4] Configurando Registro do Windows e Variáveis Globais..." -ForegroundColor Yellow

$regKeys = @(
    "HKLM:\SOFTWARE\OpenAL",
    "HKLM:\SOFTWARE\WOW6432Node\OpenAL",
    "HKLM:\SOFTWARE\Creative Labs\OpenAL",
    "HKLM:\SOFTWARE\WOW6432Node\Creative Labs\OpenAL"
)

foreach ($rk in $regKeys) {
    if (-not (Test-Path $rk)) { New-Item -Path $rk -Force | Out-Null }
    Set-ItemProperty -Path $rk -Name "Installed" -Value 1 -Type DWord
    Set-ItemProperty -Path $rk -Name "Version" -Value "1.23.1" -Type String
}
Write-Host "  • Chaves de Registro do OpenAL configuradas com sucesso! [OK]" -ForegroundColor Green

# Definir variáveis de ambiente do sistema
[Environment]::SetEnvironmentVariable("ALSOFT_CONF", "$ProgramData\alsoft.ini", "Machine")
[Environment]::SetEnvironmentVariable("ALSOFT_DRIVERS", "-wasapi,null", "Machine")
[Environment]::SetEnvironmentVariable("ALSOFT_LOGLEVEL", "0", "Machine")
Write-Host "  • Variáveis de ambiente ALSOFT_CONF e ALSOFT_DRIVERS definidas! [OK]" -ForegroundColor Green

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "  [SUCESSO] SUBSISTEMA OPENAL INSTALADO COM EXITO!      " -ForegroundColor Green
Write-Host "  Minecraft Java Edition e jogos com áudio 3D prontos.  " -ForegroundColor Green
Write-Host "========================================================`n" -ForegroundColor Cyan
