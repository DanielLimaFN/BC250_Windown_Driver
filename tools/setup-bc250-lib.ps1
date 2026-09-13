# =============================================================================
# Setup BC250 Lib: copia as DLLs do vkd3d-shader para um diretorio publico (C:\BC250_LIB)
# com permissoes RX concedidas a Todos e Pacotes de Aplicativos, para que
# processos de sistema (como dwm.exe sob DWM-1) possam carrega-las sem Access Denied.
# =============================================================================
$ErrorActionPreference = 'Stop'

$targetDir = 'C:\BC250_LIB'
if (-not (Test-Path $targetDir)) {
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
}

$srcDir = 'C:\Users\Daniel\AppData\Local\Temp\bc250-vkd3d-fedora-1.17-2\root\usr\x86_64-w64-mingw32\sys-root\mingw\bin'
if (-not (Test-Path $srcDir)) {
    throw "Diretorio fonte ausente: $srcDir"
}

Copy-Item (Join-Path $srcDir 'libvkd3d-shader-1.dll') $targetDir -Force
Copy-Item (Join-Path $srcDir 'libgcc_s_seh-1.dll') $targetDir -Force
Copy-Item (Join-Path $srcDir 'libwinpthread-1.dll') $targetDir -Force

# Concede RX a Todos (*S-1-1-0) e Todos os Pacotes de Aplicativos (*S-1-15-2-1)
& icacls.exe $targetDir /grant '*S-1-1-0:(OI)(CI)RX' /grant '*S-1-15-2-1:(OI)(CI)RX' /T | Out-Null

Write-Host "DLLs do vkd3d-shader preparadas com sucesso em ${targetDir}:" -ForegroundColor Green
Get-ChildItem $targetDir | Format-Table Name, Length, LastWriteTime
