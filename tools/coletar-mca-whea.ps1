[CmdletBinding()]
param(
    [string]$EvidenceDir = '',
    [string]$OutFile = ''
)

$ErrorActionPreference = 'SilentlyContinue'

if (-not $OutFile) {
    if ($EvidenceDir -and (Test-Path -LiteralPath $EvidenceDir)) {
        $OutFile = Join-Path $EvidenceDir 'mca-whea-pos-trava.txt'
    } else {
        $OutFile = 'mca-whea-pos-trava.txt'
    }
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("================================================================================")
[void]$sb.AppendLine("   AMD BC-250 -- DIAGNOSTICO DE HARDWARE ERRORS, MCA BANKS E WHEA (POS-TRAVA)   ")
[void]$sb.AppendLine("================================================================================")
[void]$sb.AppendLine("Gerado em: " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
[void]$sb.AppendLine("")

# 1. Kernel-Power Event ID 41
[void]$sb.AppendLine("--- [1] EVENTOS KERNEL-POWER 41 (ULTIMOS REINICIOS ABRUPTOS) ---")
$kpEvents = @(Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='Microsoft-Windows-Kernel-Power'; Id=41} -MaxEvents 5)
if ($kpEvents.Count -gt 0) {
    foreach ($evt in $kpEvents) {
        [xml]$xml = $evt.ToXml()
        $bcCode = ($xml.Event.EventData.Data | Where-Object { $_.Name -eq 'BugcheckCode' }).'#text'
        $p1 = ($xml.Event.EventData.Data | Where-Object { $_.Name -eq 'BugcheckParameter1' }).'#text'
        $p2 = ($xml.Event.EventData.Data | Where-Object { $_.Name -eq 'BugcheckParameter2' }).'#text'
        $p3 = ($xml.Event.EventData.Data | Where-Object { $_.Name -eq 'BugcheckParameter3' }).'#text'
        $p4 = ($xml.Event.EventData.Data | Where-Object { $_.Name -eq 'BugcheckParameter4' }).'#text'
        $pbTime = ($xml.Event.EventData.Data | Where-Object { $_.Name -eq 'PowerButtonTimestamp' }).'#text'
        [void]$sb.AppendLine("  Hora: " + $evt.TimeCreated + " | BugcheckCode: " + $bcCode + " | P1=" + $p1 + " P2=" + $p2 + " P3=" + $p3 + " P4=" + $p4)
    }
} else {
    [void]$sb.AppendLine("  Nenhum evento Kernel-Power 41 encontrado.")
}
[void]$sb.AppendLine("")

# 2. Microsoft-Windows-WHEA-Logger (System Log)
[void]$sb.AppendLine("--- [2] EVENTOS MICROSOFT-WINDOWS-WHEA-LOGGER (SYSTEM) ---")
$wheaLoggerEvents = @(Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='Microsoft-Windows-WHEA-Logger'} -MaxEvents 10)
if ($wheaLoggerEvents.Count -gt 0) {
    foreach ($evt in $wheaLoggerEvents) {
        [void]$sb.AppendLine("  [ID " + $evt.Id + "] Hora: " + $evt.TimeCreated + " Nivel: " + $evt.LevelDisplayName)
        [void]$sb.AppendLine("  Mensagem: " + $evt.Message)
        [xml]$xml = $evt.ToXml()
        $rawHex = ($xml.Event.EventData.Data | Where-Object { $_.Name -eq 'RawData' }).'#text'
        if ($rawHex) {
            $sampleLen = [Math]::Min(256, $rawHex.Length)
            [void]$sb.AppendLine("  RawData: " + $rawHex.Substring(0, $sampleLen))
        }
    }
} else {
    [void]$sb.AppendLine("  Nenhum evento em Microsoft-Windows-WHEA-Logger.")
}
[void]$sb.AppendLine("")

# 3. Microsoft-Windows-Kernel-WHEA
[void]$sb.AppendLine("--- [3] REGISTROS BRUTOS WHEA (KERNEL-WHEA OPERATIONAL E ERRORS) ---")
$kernelWhea = @(Get-WinEvent -LogName 'Microsoft-Windows-Kernel-WHEA/Operational', 'Microsoft-Windows-Kernel-WHEA/Errors' -MaxEvents 15)
if ($kernelWhea.Count -gt 0) {
    foreach ($evt in $kernelWhea) {
        [xml]$xml = $evt.ToXml()
        $dataList = $xml.Event.EventData.Data
        $rawHex = ($dataList | Where-Object { $_.Name -eq 'RawData' }).'#text'
        $type = ($dataList | Where-Object { $_.Name -eq 'Type' }).'#text'
        $len = ($dataList | Where-Object { $_.Name -eq 'Length' }).'#text'
        
        [void]$sb.AppendLine("  [ID " + $evt.Id + "] Hora: " + $evt.TimeCreated + " Log: " + $evt.LogName + " Tipo: " + $type + " Len: " + $len)
        if ($rawHex -and $rawHex.Length -ge 32) {
            $bytes = [byte[]](0..($rawHex.Length/2 - 1) | ForEach-Object { [Convert]::ToByte($rawHex.Substring($_ * 2, 2), 16) })
            $sig = [System.Text.Encoding]::ASCII.GetString($bytes[0..[Math]::Min(3, $bytes.Length - 1)])
            $creator = if ($bytes.Length -ge 20) { [System.Text.Encoding]::ASCII.GetString($bytes[16..19]) } else { "" }
            [void]$sb.AppendLine("    Assinatura: " + $sig + " | Criador: " + $creator + " | Bytes totais: " + $bytes.Length)
            
            if ($bytes.Length -ge 64) {
                $secCount = [BitConverter]::ToUInt16($bytes, 6)
                $severity = [BitConverter]::ToUInt32($bytes, 8)
                [void]$sb.AppendLine("    Secoes CPER: " + $secCount + " | Severidade: 0x" + $severity.ToString('X8'))
            }
            $hexDump = [BitConverter]::ToString($bytes, 0, [Math]::Min(64, $bytes.Length))
            [void]$sb.AppendLine("    Hex inicial: " + $hexDump)
        }
    }
} else {
    [void]$sb.AppendLine("  Nenhum evento registrado em Kernel-WHEA.")
}
[void]$sb.AppendLine("")

$text = $sb.ToString()
[System.IO.File]::WriteAllText($OutFile, $text, [System.Text.Encoding]::UTF8)
Write-Host "Diagnostico WHEA/MCA gravado em: $OutFile" -ForegroundColor Cyan
Write-Host $text
