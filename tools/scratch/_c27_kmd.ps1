$dir = 'C:\Users\Daniel\Desktop\NamixWDDM\docs\evidence\2026-09-18_14-20-07-DESKTOP-HQAG6T4-lab-binding'
$kmd = Join-Path $dir 'display-log-this-bind-FINAL.txt'
Write-Output ("=== {0:N0} bytes, {1} linhas ===" -f (Get-Item $kmd).Length, (Get-Content $kmd).Count)

function S([string]$t, [string]$p, [int]$max = 10) {
    Write-Output ''
    Write-Output ("--- {0}   [{1}] ---" -f $t, $p)
    $h = Select-String -LiteralPath $kmd -Pattern $p -ErrorAction SilentlyContinue
    Write-Output ("    TOTAL: {0}" -f $h.Count)
    if ($h.Count -gt 0) {
        $h | Select-Object -First $max | ForEach-Object { Write-Output ("    L{0}: {1}" -f $_.LineNumber, $_.Line.Trim()) }
        if ($h.Count -gt $max) { Write-Output '    ...'; $h | Select-Object -Last 2 | ForEach-Object { Write-Output ("    L{0}: {1}" -f $_.LineNumber, $_.Line.Trim()) } }
    }
}

Write-Output ''
Write-Output '############ CRITERIO 1: A ESCRITA DO WPTR CHEGA AO REGISTRADOR? ############'
S 'WPTR_READBACK PASS' 'WPTR_READBACK PASS' 8
S 'WPTR_READBACK_DIVERGENTE' 'WPTR_READBACK_DIVERGENTE' 8
S 'WPTR_READBACK INDETERMINADO' 'WPTR_READBACK INDETERMINADO' 6
S 'WPTR_ESCRITA_RECUSADA' 'WPTR_ESCRITA_RECUSADA' 6
S 'WPTR_NAO_PUBLICADO' 'WPTR_NAO_PUBLICADO' 6

Write-Output ''
Write-Output '############ CRITERIO 2: STALL_BASELINE vs TDR ############'
S 'STALL_BASELINE' 'STALL_BASELINE' 6
S 'TDR_GFX_STALL' 'TDR_GFX_STALL' 12
S 'TDR_GFX_QUIESCE Snapshot' 'TDR_GFX_QUIESCE\] Snapshot' 6

Write-Output ''
Write-Output '############ CRITERIO 3: CLASSE DO IB (CHAIN vs IB2) ############'
S 'GfxIbInspect dados' '\[GfxIbInspect\] VA=' 8
S 'GfxIbInspect VEREDITO' '\[GfxIbInspect\] VEREDITO' 8
S 'IB_SIZE_MUDOU' 'IB_SIZE_MUDOU' 8

Write-Output ''
Write-Output '############ TRIPE E SUBMISSOES ############'
S 'GFX_TRIPE_VEREDITO' 'GFX_TRIPE_VEREDITO' 6
S 'GFX_TRIPE AVISO' 'GFX_TRIPE AVISO' 4
S 'SubmitVirtual Node1 GFX ENTER' 'SubmitVirtual Node1 GFX\] ENTER' 8
S 'GfxEngine SUBMIT SUCESSO' 'GfxEngine\] SUBMIT SUCESSO' 8
S 'GfxEngine SUBMIT RECUSA' 'GfxEngine\] SUBMIT RECUSA' 8
S 'FENCE COMMIT' 'FENCE COMMIT' 6
S 'GfxEngine ONLINE' 'GfxEngine\] ONLINE' 4

Write-Output ''
Write-Output '############ FALHAS ############'
S 'TDR' 'ResetFromTimeout|RestartFromTimeout|TDR_QUIESCE|TDR_RESTART' 8
S 'BugCheck/DEVICE_REMOVED/StopDevice' 'BugCheck|DEVICE_REMOVED|StopDevice' 6
S 'GFXHUB FAULT' 'GFXHUB_FAULT|FaultStatus=0x[1-9A-F]' 6
S 'RECUSA/RECUSADO' 'RECUSA|RECUSADO' 12
S 'UTCL1' 'UTCL1' 6

Write-Output ''
Write-Output '############ PIXEL ############'
S 'DdiPresent ENTER' 'DdiPresent\] ENTER' 6
S 'BYTE_PROOF' 'BYTE_PROOF' 6
S 'NonZero' 'NonZero=' 8

Write-Output ''
Write-Output '############ CAUDA (onde parou) ############'
Get-Content $kmd -Tail 30 | ForEach-Object { Write-Output ("    {0}" -f $_) }
