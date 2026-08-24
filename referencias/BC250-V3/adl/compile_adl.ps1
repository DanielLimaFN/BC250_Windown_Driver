$vsPath = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build"
$srcDir = "C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V2\adl"
$binDir = "C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V2\build"
$pkgDir = "C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V2\package"

if (-not (Test-Path $binDir)) { New-Item -ItemType Directory -Path $binDir -Force }
if (-not (Test-Path $pkgDir)) { New-Item -ItemType Directory -Path $pkgDir -Force }

Write-Host "=== [1/2] Compilando atiadlxx.dll (64-bit x64)..." -ForegroundColor Cyan
$cmdX64 = "call `"$vsPath\vcvars64.bat`" >nul 2>&1 && cl /nologo /O2 /MD /W3 /I`"$srcDir`" `"$srcDir\atiadlxx.c`" /link /DLL /MACHINE:X64 /DEF:`"$srcDir\atiadlxx.def`" /OUT:`"$binDir\atiadlxx.dll`" gdi32.lib user32.lib"
cmd.exe /c $cmdX64
if ($LASTEXITCODE -ne 0) {
    Write-Error "Falha ao compilar atiadlxx.dll (x64)"
    exit 1
}
Copy-Item "$binDir\atiadlxx.dll" "$pkgDir\atiadlxx.dll" -Force

Write-Host "=== [2/2] Compilando atiadlxy.dll (32-bit x86 para GPU-Z 32-bit)..." -ForegroundColor Cyan
$cmdX86 = "call `"$vsPath\vcvars32.bat`" >nul 2>&1 && cl /nologo /O2 /MD /W3 /I`"$srcDir`" `"$srcDir\atiadlxx.c`" /link /DLL /MACHINE:X86 /DEF:`"$srcDir\atiadlxx.def`" /OUT:`"$binDir\atiadlxy.dll`" gdi32.lib user32.lib"
cmd.exe /c $cmdX86
if ($LASTEXITCODE -ne 0) {
    Write-Error "Falha ao compilar atiadlxy.dll (x86)"
    exit 1
}
Copy-Item "$binDir\atiadlxy.dll" "$pkgDir\atiadlxy.dll" -Force

# Tambem compila atiadlxx 32-bit para os casos onde o executavel 32-bit procura atiadlxx.dll
$cmdX86_xx = "call `"$vsPath\vcvars32.bat`" >nul 2>&1 && cl /nologo /O2 /MD /W3 /I`"$srcDir`" `"$srcDir\atiadlxx.c`" /link /DLL /MACHINE:X86 /DEF:`"$srcDir\atiadlxx.def`" /OUT:`"$binDir\atiadlxx_32.dll`" gdi32.lib user32.lib"
cmd.exe /c $cmdX86_xx

Write-Host "`n========================================================" -ForegroundColor Green
Write-Host "  COMPILACAO CONCLUIDA COM SUCESSO!" -ForegroundColor Green
Write-Host "  64-bit DLL: $pkgDir\atiadlxx.dll" -ForegroundColor Green
Write-Host "  32-bit DLL: $pkgDir\atiadlxy.dll (USADA PELO GPU-Z!)" -ForegroundColor Green
Write-Host "========================================================" -ForegroundColor Green
