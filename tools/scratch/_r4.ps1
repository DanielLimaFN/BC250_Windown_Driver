$root = 'C:\Users\Daniel\Desktop\NamixWDDM'
$sys = "$root\driver\wdk\package\output_lab\bc250mcdm.sys"
$umd = "$root\driver\wdk\package\output_lab\bc250umd.dll"

function Check([string]$path, [string[]]$pats) {
    $fi = Get-Item $path
    Write-Output ("--- {0}  {1:N0} bytes  {2}" -f $fi.Name, $fi.Length, $fi.LastWriteTime)
    Write-Output ("    SHA256 {0}" -f (Get-FileHash $path -Algorithm SHA256).Hash)
    $a = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($path))
    $miss = @()
    foreach ($p in $pats) {
        $ok = $a.Contains($p)
        if (-not $ok) { $miss += $p }
        Write-Output ("    {0,-40} {1}" -f $p, $(if ($ok) {'PRESENTE'} else {'AUSENTE'}))
    }
    if ($miss.Count -gt 0) { Write-Output ("    >>> AUSENTES: {0}" -f ($miss -join ', ')) }
    else { Write-Output '    >>> todas presentes' }
}

Write-Output '=== R4 NO SYS ASSINADO (Fases 1, 2 e 3) ==='
Check $sys @(
  'WPTR_READBACK PASS',
  'WPTR_READBACK_DIVERGENTE',
  'WPTR_ESCRITA_RECUSADA',
  'WPTR_NAO_PUBLICADO',
  'INDETERMINADO_SLOT_ZERADO_SEM_POISON',
  'TRANSPORTE_MMIO_NAO_MAPEADO',
  'READ32_RECUSOU_OFFSET_OU_LIMITE',
  'STALL_BASELINE',
  'TDR_GFX_STALL',
  'ME_HAS_ACTIVE_CE_BUFFER_FLAG',
  'GFX_CNTX_NOT_AVAIL_TO_ME',
  'ChamadasIB2',
  'UTCL1 FORCE_INVALIDATION nao aplicado',
  'GFX_TRIPE_VEREDITO',
  'GfxIbInspect'
)

Write-Output ''
Write-Output '=== R4 NA UMD ASSINADA ==='
Check $umd @(
  'NAO MAPEAVEL PELA CPU',
  'Perna 3 do tripe fica sem medicao',
  'DRAW EXECUTADO NATIVO EM VULKAN'
)

Write-Output ''
Write-Output '=== ESTADO DA MAQUINA PARA A CORRIDA ==='
Get-PnpDevice -Class Display | ForEach-Object { Write-Output ("  {0} | Problem={1} | {2}" -f $_.Status, $_.Problem, $_.FriendlyName) }
foreach ($p in @('C:\BC250_BOOT_FUSE.json','C:\BC250_REQUIRE_CLEAN_BOOT.json')) {
    if (Test-Path $p) { Write-Output ("  {0}: {1}" -f (Split-Path $p -Leaf), ((Get-Content $p -Raw) -replace "`r`n",' ')) }
    else { Write-Output ("  {0}: INEXISTENTE" -f (Split-Path $p -Leaf)) }
}
Write-Output ("  vulkan_radeon.dll: {0}" -f (Get-FileHash 'C:\BC250_LIB\vulkan_radeon.dll' -Algorithm SHA256).Hash)
$cl = (Get-Process claude -ErrorAction SilentlyContinue | Measure-Object).Count
Write-Output ("  processos claude ativos: {0}" -f $cl)
