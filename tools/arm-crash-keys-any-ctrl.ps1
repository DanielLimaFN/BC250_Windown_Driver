# Corrida 145 -- EXECUTAR ELEVADO pelo Daniel. Arma o dump pelo teclado com
# QUALQUER Ctrl (esquerdo ou direito) + Scroll Lock DUAS vezes (o Windows exige
# duas; nao ha' opcao de uma). Fonte: Microsoft Learn, "Forcing a System Crash
# from the Keyboard": Dump1Keys 0x02 (Ctrl direito) | 0x20 (Ctrl esquerdo) = 0x22;
# Dump2Key = indice na keyToScanTbl; Scroll Lock (scan 0x46) e' o indice 125 = 0x7D.
# A doc exige CrashOnCtrlScroll = 0 quando o atalho alternativo esta definido.
# Vale so' depois de REINICIAR. -Undo volta ao atalho padrao (Ctrl direito).
param([switch]$Undo)
$ErrorActionPreference = 'Stop'
foreach ($name in @('i8042prt', 'kbdhid')) {
    $params = "HKLM:\SYSTEM\CurrentControlSet\Services\$name\Parameters"
    $dump = "HKLM:\SYSTEM\CurrentControlSet\Services\$name\crashdump"
    if ($Undo) {
        if (Test-Path -LiteralPath $dump) { Remove-Item -LiteralPath $dump -Recurse -Force }
        New-ItemProperty -LiteralPath $params -Name CrashOnCtrlScroll -PropertyType DWord -Value 1 -Force | Out-Null
        Write-Host "${name}: atalho padrao restaurado (Ctrl direito + Scroll Lock 2x)."
        continue
    }
    if (-not (Test-Path -LiteralPath $dump)) { New-Item -Path $dump -Force | Out-Null }
    New-ItemProperty -LiteralPath $dump -Name Dump1Keys -PropertyType DWord -Value 0x22 -Force | Out-Null
    New-ItemProperty -LiteralPath $dump -Name Dump2Key -PropertyType DWord -Value 0x7D -Force | Out-Null
    New-ItemProperty -LiteralPath $params -Name CrashOnCtrlScroll -PropertyType DWord -Value 0 -Force | Out-Null
    Write-Host "${name}: Dump1Keys=0x22 Dump2Key=0x7D CrashOnCtrlScroll=0"
}
Write-Host 'Pronto. REINICIE (boot frio) para valer.'
