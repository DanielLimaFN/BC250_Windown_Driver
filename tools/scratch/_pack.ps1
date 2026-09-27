$root = 'C:\Users\Daniel\Desktop\NamixWDDM'
$log = "$root\tools\scratch\_pack.log"
Set-Location $root

Write-Output '=== EMPACOTAMENTO LAB (assinado) ==='
& powershell -NoProfile -ExecutionPolicy Bypass -File "$root\tools\build-bc250mcdm-lab-bringup.ps1" `
    -Execute -IUnderstandThisEnablesDestructiveBringup *> $log
Write-Output ("PACK EXIT = {0}" -f $LASTEXITCODE)
$bad = Select-String -LiteralPath $log -Pattern 'FALHOU|FAILED|ERRO|error|Exception|RECUSADO' | Select-Object -First 12
if ($bad) { $bad | ForEach-Object { Write-Output ("  {0}" -f $_.Line.Trim()) } } else { Write-Output '  (sem falhas)' }
Write-Output '--- cauda do empacotamento ---'
Get-Content $log -Tail 16 | ForEach-Object { Write-Output ("  {0}" -f $_) }

Write-Output ''
Write-Output '=== MANIFESTO DO PACOTE ==='
$man = "$root\driver\wdk\package\output_lab.manifest.json"
if (Test-Path $man) {
    $j = Get-Content $man -Raw | ConvertFrom-Json
    foreach ($k in @('DriverVer','BuildKind','ProfileMode','SourceSysSha256','PackageSysSha256','SourceUmdSha256','PackageUmdSha256','PackageInfSha256','PackageCatSha256','Verdict')) {
        Write-Output ("  {0,-20} {1}" -f $k, $j.$k)
    }
}
