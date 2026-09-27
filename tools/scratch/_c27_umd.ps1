$dir = 'C:\Users\Daniel\Desktop\NamixWDDM\docs\evidence\2026-09-18_14-20-07-DESKTOP-HQAG6T4-lab-binding'
$kmd = Join-Path $dir 'display-log-this-bind-FINAL.txt'
$files = Get-ChildItem $dir -Filter '*.txt' -File | ForEach-Object { $_.FullName }

function A([string]$t, [string]$p, [int]$max = 6) {
    Write-Output ''
    Write-Output ("--- {0}   [{1}] ---" -f $t, $p)
    $tot = 0; $shown = 0
    foreach ($f in $files) {
        $h = Select-String -LiteralPath $f -Pattern $p -ErrorAction SilentlyContinue
        if ($h) {
            $tot += $h.Count
            if ($shown -lt $max) {
                $h | Select-Object -First ($max - $shown) | ForEach-Object {
                    Write-Output ("    [{0}] {1}" -f (Split-Path $_.Path -Leaf), $_.Line.Trim())
                    $shown++
                }
            }
        }
    }
    Write-Output ("    TOTAL: {0}" -f $tot)
}

Write-Output '############ UMD: A CORRECAO DO memType ############'
A 'LinearPresentBuf criado com nivel' 'LinearPresentBuf LAZY criado' 4
A 'AVISO nao host-visible' 'NAO MAPEAVEL PELA CPU|Perna 3 do tripe fica sem medicao' 4
A 'INSPECAO LINEAR_BUF' 'INSPECAO LINEAR_BUF' 5

Write-Output ''
Write-Output '############ UMD/RADV ############'
A 'BC-250 UNIFICADO' 'BC-250 UNIFICADO' 4
A 'Chain [' 'Chain \[' 4
A 'SubmitCommand #' 'BC-250 SubmitCommand #' 5
A 'NTSTATUS de falha no submit' 'falhou com NTSTATUS' 5
A 'DRAW EXECUTADO' 'DRAW EXECUTADO NATIVO' 3
A 'DEVICE_REMOVED / 0xC00002B6' 'C00002B6' 5
A 'PRESENT RECUSADO' 'PRESENT RECUSADO' 4

Write-Output ''
Write-Output '############ CONTRASTE SDMA (mesma tecnica, anel que funciona) ############'
$s = Select-String -LiteralPath $kmd -Pattern 'IB_TRIPE_VEREDITO'
Write-Output ("  IB_TRIPE_VEREDITO (SDMA): {0}" -f $s.Count)
$s | Select-Object -Last 1 | ForEach-Object { Write-Output ("  {0}" -f $_.Line.Trim()) }
Write-Output ("  IB EXECUTADO: {0}" -f (Select-String -LiteralPath $kmd -Pattern 'IB EXECUTADO').Count)

Write-Output ''
Write-Output '############ CONFORMANCE ############'
$c = Join-Path $dir 'conformance-stdout.tmp'
if (Test-Path $c) { Get-Content $c | ForEach-Object { Write-Output ("  {0}" -f $_) } }
A 'RESULTADO / VEREDITO' 'RESULTADO DO TESTE|VEREDITO:' 4

Write-Output ''
Write-Output '############ ESTADO FINAL ############'
foreach ($n in @('safe-bind-final.json','compute-display-prerequisite-gate.json')) {
    $p = Join-Path $dir $n
    if (Test-Path $p) {
        Write-Output ("--- {0} ---" -f $n)
        $j = Get-Content $p -Raw | ConvertFrom-Json
        if ($n -like 'safe-bind*') {
            Write-Output ("  final_state={0} detail={1} recovery_exit={2}" -f $j.final_state, $j.detail, $j.recovery_exit_code)
        } else { Write-Output ("  state={0} shared_primary={1} commit_vidpn={2} set_addr={3}" -f $j.state, $j.shared_primary, $j.commit_vidpn, $j.set_vidpn_source_address) }
    }
}
