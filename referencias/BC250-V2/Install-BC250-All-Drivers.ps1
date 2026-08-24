<#
.SYNOPSIS
    Instalador Completo e Automatizado do Driver AMD BC-250 (Vulkan 1.2 + OpenGL 4.6 + OpenAL Soft)
.DESCRIPTION
    Instala as DLLs de Vulkan, OpenGL e OpenAL no System32 / SysWOW64 e configura o Registro do Windows
    e variáveis de ambiente para compatibilidade total com Minecraft Java Edition, FurMark, RPCS3, Ryujinx, etc.
#>

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$BuildDir  = Join-Path $ScriptDir "build"
$PackageDir = Join-Path $ScriptDir "package"
$OpenalDir = Join-Path $ScriptDir "openal"
$Sys32     = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::System)
$SysWOW64  = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::SystemX86)
$ProgramData = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::CommonApplicationData)
$AppData   = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::ApplicationData)

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "   AMD BC-250: INSTALADOR COMPLETO VULKAN, OPENGL & OPENAL" -ForegroundColor Cyan
Write-Host "   (Cyan Skillfish / Navi12 Lite - 1536 SPs / 16GB GDDR6)" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# 1. Configurar fallback de usuário do OpenAL imediatamente (garante funcionamento mesmo sem admin)
$alsoftIniSrc = "$OpenalDir\alsoft.ini"
if (-not (Test-Path $alsoftIniSrc)) { $alsoftIniSrc = "$PackageDir\alsoft.ini" }
if (Test-Path $alsoftIniSrc) {
    Copy-Item -Path $alsoftIniSrc -Destination "$AppData\alsoft.ini" -Force
    [Environment]::SetEnvironmentVariable("ALSOFT_CONF", "$AppData\alsoft.ini", "User")
    [Environment]::SetEnvironmentVariable("ALSOFT_DRIVERS", "-wasapi,null", "User")
    Write-Host "  • [User] alsoft.ini configurado em $AppData\alsoft.ini [OK]" -ForegroundColor Green
}

# 2. Verificar Privilégios de Administrador para Instalação no Sistema
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "`n[AVISO] Privilégios de Administrador não detectados." -ForegroundColor Yellow
    Write-Host "  • A configuração de usuário do OpenAL foi aplicada no AppData." -ForegroundColor Green
    Write-Host "  • Para instalar as DLLs no System32/SysWOW64 e registrar globalmente no Windows:" -ForegroundColor Yellow
    Write-Host "    Execute 'Install-BC250-All-Drivers.bat' (ele solicitará elevação automaticamente).`n" -ForegroundColor Cyan
    return
}

# 3. Copiar DLLs do Driver Vulkan & OpenGL & OpenAL (64-bit -> System32)
Write-Host "`n[1/5] Instalando bibliotecas 64-bit em $Sys32..." -ForegroundColor Yellow

$files64 = @(
    @{ Name = "vulkan_radeon.dll"; Path = "$PackageDir\vulkan_radeon.dll" },
    @{ Name = "vulkan_radeon.json"; Path = "$PackageDir\vulkan_radeon.json" },
    @{ Name = "amdbc250vulkan.dll"; Path = "$PackageDir\amdbc250vulkan.dll" },
    @{ Name = "amdbc250vulkan.json"; Path = "$PackageDir\amdbc250vulkan.json" },
    @{ Name = "atio6xxx.dll"; Path = "$PackageDir\atio6xxx.dll" },
    @{ Name = "libgallium_wgl.dll"; Path = "$PackageDir\libgallium_wgl.dll" },
    @{ Name = "z-1.dll"; Path = "$PackageDir\z-1.dll" },
    @{ Name = "OpenAL32.dll"; Path = "$OpenalDir\x64\OpenAL32.dll" },
    @{ Name = "soft_oal.dll"; Path = "$OpenalDir\x64\soft_oal.dll" }
)

foreach ($f in $files64) {
    if (Test-Path $f.Path) {
        Copy-Item -Path $f.Path -Destination (Join-Path $Sys32 $f.Name) -Force
        Write-Host "  • $($f.Name) -> $Sys32 [OK]" -ForegroundColor Green
    } else {
        Write-Host "  • Aviso: $($f.Path) nao encontrado" -ForegroundColor Yellow
    }
}

# 4. Copiar DLLs 32-bit em SysWOW64
if (Test-Path $SysWOW64) {
    Write-Host "`n[2/5] Instalando bibliotecas 32-bit em $SysWOW64..." -ForegroundColor Yellow
    $files32 = @(
        @{ Name = "OpenAL32.dll"; Path = "$OpenalDir\x86\OpenAL32.dll" },
        @{ Name = "soft_oal.dll"; Path = "$OpenalDir\x86\soft_oal.dll" }
    )
    foreach ($f in $files32) {
        if (Test-Path $f.Path) {
            Copy-Item -Path $f.Path -Destination (Join-Path $SysWOW64 $f.Name) -Force
            Write-Host "  • $($f.Name) -> $SysWOW64 [OK]" -ForegroundColor Green
        }
    }
}

# 5. Configurar Registro do Windows para Vulkan ICD
Write-Host "`n[3/5] Configurando Registro Vulkan ICD (RADV Mesa + BC-250 Native)..." -ForegroundColor Yellow
$vulkanReg64 = "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers"
$vulkanReg32 = "HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers"
$jsonPath = "$Sys32\vulkan_radeon.json"

foreach ($reg in @($vulkanReg64, $vulkanReg32)) {
    if (-not (Test-Path $reg)) { New-Item -Path $reg -Force | Out-Null }
    foreach ($prop in (Get-Item $reg).Property) {
        if (($prop -like '*amdbc250vulkan*' -or $prop -like '*vulkan_radeon*') -and $prop -ne $jsonPath) {
            Remove-ItemProperty -Path $reg -Name $prop -Force -ErrorAction SilentlyContinue
            Write-Host "  [-] Entrada antiga/conflitante removida de $($reg): $prop" -ForegroundColor DarkYellow
        }
    }
    Set-ItemProperty -Path $reg -Name $jsonPath -Value 0 -Type DWord
}
Write-Host "  • Vulkan ICD 1.3 RADV registrado em 64-bit e 32-bit: $jsonPath [OK]" -ForegroundColor Green

# 6. Configurar Registro do Windows para OpenGL ICD
Write-Host "`n[4/5] Configurando Registro OpenGL 4.6 ICD..." -ForegroundColor Yellow
$oglKeys = @(
    "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\MSAMD",
    "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\amdbc250vulkan",
    "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\atio6xxx",
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\MSAMD",
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\amdbc250vulkan",
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\atio6xxx"
)

foreach ($key in $oglKeys) {
    if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
    Set-ItemProperty -Path $key -Name "DLL" -Value "atio6xxx.dll"
    Set-ItemProperty -Path $key -Name "Version" -Value 2 -Type DWord
    Set-ItemProperty -Path $key -Name "DriverVersion" -Value 1 -Type DWord
    Set-ItemProperty -Path $key -Name "Flags" -Value 1 -Type DWord
}
Write-Host "  • Chaves do Registro OpenGL configuradas com sucesso! [OK]" -ForegroundColor Green

# 7. Configurar OpenAL Soft & Fallback Headless no Sistema
Write-Host "`n[5/5] Configurando Subsistema OpenAL Soft & Fallback Headless..." -ForegroundColor Yellow

if (Test-Path $alsoftIniSrc) {
    $iniDests = @(
        "$ProgramData\alsoft.ini",
        "$AppData\alsoft.ini",
        "$env:WINDIR\alsoft.ini"
    )
    foreach ($d in $iniDests) {
        Copy-Item -Path $alsoftIniSrc -Destination $d -Force
        Write-Host "  • alsoft.ini -> $d [OK]" -ForegroundColor Green
    }
}

$alRegKeys = @(
    "HKLM:\SOFTWARE\OpenAL",
    "HKLM:\SOFTWARE\WOW6432Node\OpenAL",
    "HKLM:\SOFTWARE\Creative Labs\OpenAL",
    "HKLM:\SOFTWARE\WOW6432Node\Creative Labs\OpenAL"
)
foreach ($ark in $alRegKeys) {
    if (-not (Test-Path $ark)) { New-Item -Path $ark -Force | Out-Null }
    Set-ItemProperty -Path $ark -Name "Installed" -Value 1 -Type DWord
    Set-ItemProperty -Path $ark -Name "Version" -Value "1.23.1" -Type String
}

# 8. Configurar Variaveis de Ambiente Globais
[Environment]::SetEnvironmentVariable("MESA_GL_VERSION_OVERRIDE", "4.6COMPAT", "Machine")
[Environment]::SetEnvironmentVariable("MESA_GLSL_VERSION_OVERRIDE", "460", "Machine")
[Environment]::SetEnvironmentVariable("ALSOFT_CONF", "$ProgramData\alsoft.ini", "Machine")
[Environment]::SetEnvironmentVariable("ALSOFT_DRIVERS", "-wasapi,null", "Machine")
[Environment]::SetEnvironmentVariable("ALSOFT_LOGLEVEL", "0", "Machine")
Write-Host "  • Variaveis de ambiente MESA (OpenGL 4.6) e ALSOFT (OpenAL) configuradas! [OK]" -ForegroundColor Green

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "  [SUCESSO] DRIVERS VULKAN, OPENGL E OPENAL INSTALADOS! " -ForegroundColor Green
Write-Host "  Minecraft Java Edition e FurMark prontos para rodar.  " -ForegroundColor Green
Write-Host "========================================================`n" -ForegroundColor Cyan
