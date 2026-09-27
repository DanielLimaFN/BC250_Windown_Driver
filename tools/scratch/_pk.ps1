$root = 'C:\Users\Daniel\Desktop\NamixWDDM'
$log = "$root\tools\scratch\_pk.log"
Set-Location $root

Write-Output '=== PACKAGE LAB ASSINADO (perfil DISPLAY_RENDER_LAB, contrato 27) ==='
& powershell -NoProfile -ExecutionPolicy Bypass -File "$root\tools\package-bc250mcdm-lab.ps1" `
    -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode *> $log
Write-Output ("PACKAGE EXIT = {0}" -f $LASTEXITCODE)
$bad = Select-String -LiteralPath $log -Pattern 'FALHOU|FAILED|Exception|throw|RECUSADO|error C' | Select-Object -First 10
if ($bad) { $bad | ForEach-Object { Write-Output ("  {0}" -f $_.Line.Trim()) } } else { Write-Output '  (sem falhas)' }
Write-Output '--- cauda ---'
Get-Content $log -Tail 14 | ForEach-Object { Write-Output ("  {0}" -f $_) }

Write-Output ''
Write-Output '=== AUDITORIA DE PACOTE ==='
$pa = Get-ChildItem "$root\tools" -Filter 'audit-*package*.py' -File | Select-Object -First 1
if ($pa) {
    $o = & python $pa.FullName 2>&1
    Write-Output ("  {0} EXIT = {1}" -f $pa.Name, $LASTEXITCODE)
    $o | Select-Object -Last 8 | ForEach-Object { Write-Output ("    {0}" -f $_) }
}
Write-Output ''
Write-Output '=== AUDITORIA DE SEGURANCA ==='
$o2 = & python "$root\tools\audit-bc250mcdm-safety.py" 2>&1
Write-Output ("  EXIT = {0}" -f $LASTEXITCODE)
$o2 | Select-Object -First 1 | ForEach-Object { Write-Output ("    {0}" -f $_) }

Write-Output ''
Write-Output '=== MANIFESTO NOVO ==='
$j = Get-Content "$root\driver\wdk\package\output_lab.manifest.json" -Raw | ConvertFrom-Json
foreach ($k in @('DriverVer','BuildKind','ProfileMode','WddmContractVersion','RenderNode','RenderOnlyLab',
                 'ShaderDispatchEnabled','PersistentSubmissionEnabled','TelemetrySchemaVersion',
                 'SourceSysSha256','PackageSysSha256','SourceUmdSha256','PackageUmdSha256',
                 'PackageInfSha256','PackageCatSha256','Verdict')) {
    Write-Output ("  {0,-28} {1}" -f $k, $j.$k)
}
