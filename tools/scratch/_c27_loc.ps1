$root = 'C:\Users\Daniel\Desktop\NamixWDDM'
Write-Output '=== PASTAS DE EVIDENCIA MAIS RECENTES ==='
Get-ChildItem "$root\docs\evidence" -Directory | Sort-Object Name -Descending | Select-Object -First 4 | ForEach-Object {
    $f = Get-ChildItem $_.FullName -File -ErrorAction SilentlyContinue
    Write-Output ("  {0}  ({1} arq, {2:N0} bytes)" -f $_.Name, $f.Count, ($f | Measure-Object Length -Sum).Sum)
}
$last = Get-ChildItem "$root\docs\evidence" -Directory | Sort-Object Name -Descending | Select-Object -First 1
Write-Output ''
Write-Output ("=== {0} ===" -f $last.Name)
Get-ChildItem $last.FullName -File | Sort-Object Length -Descending | ForEach-Object {
    Write-Output ("  {0,-46} {1,13:N0}  {2}" -f $_.Name, $_.Length, $_.LastWriteTime.ToString('HH:mm:ss'))
}
Write-Output ''
Write-Output '=== PROVENIENCIA (R4) ==='
$bp = Join-Path $last.FullName 'binding-provenance.json'
if (Test-Path $bp) { Get-Content $bp -Raw } else { Write-Output '  binding-provenance.json AUSENTE' }
Write-Output ''
Write-Output '=== CANDIDATO ESPERADO (empacotado por mim as 14:11) ==='
Write-Output '  DriverVer 3.184.1.0 | SYS E7E7A3B2D11C9B67... | UMD 473EE3E8A311CB5E... | RADV 19D62EC2...'
Write-Output ''
Write-Output '=== ESTADO FINAL DA MAQUINA ==='
Get-PnpDevice -Class Display | ForEach-Object { Write-Output ("  {0} | Problem={1} | {2}" -f $_.Status, $_.Problem, $_.FriendlyName) }
foreach ($p in @('C:\BC250_BOOT_FUSE.json','C:\BC250_REQUIRE_CLEAN_BOOT.json')) {
    if (Test-Path $p) { Write-Output ("  {0}: {1}" -f (Split-Path $p -Leaf), ((Get-Content $p -Raw) -replace "`r`n",' ')) }
    else { Write-Output ("  {0}: INEXISTENTE" -f (Split-Path $p -Leaf)) }
}
Write-Output '--- BugCheck / dump apos 14:00 ---'
Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='Microsoft-Windows-WER-SystemErrorReporting'} -MaxEvents 5 -ErrorAction SilentlyContinue |
  ForEach-Object { $m=($_.Message -replace "`r`n",' '); Write-Output ("  {0} | {1}" -f $_.TimeCreated, $m.Substring(0,[Math]::Min(120,$m.Length))) }
Get-Item 'C:\Windows\MEMORY.DMP' -ErrorAction SilentlyContinue | ForEach-Object { Write-Output ("  MEMORY.DMP {0:N0} bytes {1}" -f $_.Length, $_.LastWriteTime) }
