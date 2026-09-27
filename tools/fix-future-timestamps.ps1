$now = Get-Date
Get-ChildItem -Path (Join-Path $PSScriptRoot '..') -Recurse -Force -ErrorAction SilentlyContinue | Where-Object {
    $_.LastWriteTime -gt $now
} | ForEach-Object {
    Write-Host "Ajustando timestamp futuro: $($_.FullName) ($($_.LastWriteTime) -> $now)"
    $_.LastWriteTime = $now
}
