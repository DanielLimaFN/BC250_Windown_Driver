# Central de Testes CLI - AMD BC-250 (BC250-V3)
# Uso:
#   powershell -ExecutionPolicy Bypass -File cli\Runner_CLI.ps1 [opcoes]
#
# Parametros:
#   -All            : Executa a bateria completa de testes (T1..T12 e Fases)
#   -Test <ID>      : Executa um teste especifico (ex: T1, T2, T5, T11, T12)
#   -Phase <N>      : Executa a auditoria de uma fase especifica (0, 1, 2, 3 ou 4)
#   -Json           : Emite a saida consolidada em formato JSON estruturado
#   -Summary        : Exibe uma tabela resumo compacta
#   -Report         : Salva um relatorio detalhado em reports/

param (
    [switch]$All,
    [string]$Test = "",
    [int]$Phase = -1,
    [switch]$Json,
    [switch]$Summary,
    [switch]$Report,
    [switch]$Help
)

$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BinDir = Join-Path $Root "bin"
$ReportsDir = Join-Path $Root "reports"

if (-not (Test-Path $ReportsDir)) {
    New-Item -ItemType Directory -Path $ReportsDir -Force | Out-Null
}

if ($Help -or (-not $All -and $Test -eq "" -and $Phase -eq -1 -and -not $Json -and -not $Summary)) {
    Write-Host "`n===============================================================================" -ForegroundColor Cyan
    Write-Host " CENTRAL DE TESTES CLI - AMD BC-250 (BC250-V3)" -ForegroundColor Cyan
    Write-Host "===============================================================================" -ForegroundColor Cyan
    Write-Host "`nComandos disponiveis:"
    Write-Host "  -All             : Executa todos os testes de silicio (T1..T12) e auditoria de fases"
    Write-Host "  -Test <ID>       : Executa um teste especifico (ex: -Test T1, -Test T2, -Test T5, -Test T11, -Test T12)"
    Write-Host "  -Phase <0..4>    : Executa a auditoria de uma fase (0=Purga, 1=Seguranca, 2=DPM, 3=FW, 4=MQD)"
    Write-Host "  -Json            : Emite a saida em formato JSON para processamento automatizado"
    Write-Host "  -Summary         : Exibe a tabela consolidada com status PROVADO / NAO VALIDADO / BLOQUEADO"
    Write-Host "  -Report          : Exporta relatorio Markdown em reports/`n"
    Write-Host "Exemplos:"
    Write-Host "  powershell -File cli\Runner_CLI.ps1 -All"
    Write-Host "  powershell -File cli\Runner_CLI.ps1 -Test T11"
    Write-Host "  powershell -File cli\Runner_CLI.ps1 -Test T12"
    Write-Host "  powershell -File cli\Runner_CLI.ps1 -All -Json"
    Write-Host "  powershell -File cli\Runner_CLI.ps1 -All -Report`n"
    exit 0
}

# Garante que os binarios estao compilados
if (-not (Test-Path (Join-Path $BinDir "t1_scratch_mmio.exe"))) {
    Write-Host "[*] Compilando binarios da Central de Testes..." -ForegroundColor Yellow
    & (Join-Path $Root "build_all.bat")
}

$TestDefinitions = @(
    @{ Id = "T1";  Exe = "t1_scratch_mmio.exe";           Title = "Scratch Round-Trip Host MMIO (Walking 1s/0s)" },
    @{ Id = "T2";  Exe = "t2_pm4_scratch.exe";            Title = "Scratch via PM4 Ring (Validacao em 4 Pontos)" },
    @{ Id = "T3";  Exe = "t3_write_stick.exe";            Title = "Write-Stick Sweep & Multibank Array Probe" },
    @{ Id = "T4";  Exe = "t4_fill_pattern.exe";           Title = "Detector de Offsets Invalidos / Fill Patterns" },
    @{ Id = "T5";  Exe = "t5_sdma_fence_poison.exe";      Title = "SDMA0 DMA Copy & Hardware Fence (Poisoning)" },
    @{ Id = "T6";  Exe = "t6_telemetry_correlation.exe";  Title = "Correlacao Dinamica de Carga e Telemetria SMU" },
    @{ Id = "T7";  Exe = "t7_psp_protocol_validate.exe";  Title = "Validacao Offline do Protocolo PSP & GPCOM" },
    @{ Id = "T8";  Exe = "t8_fallback_audit.exe";         Title = "Auditoria de Fallbacks e Integridade de Codigo" },
    @{ Id = "T9";  Exe = "t9_bringup_bisect.exe";         Title = "Hardware Bring-up & Diagnostico no Registro" },
    @{ Id = "T10"; Exe = "t10_baseline_plausibility.exe"; Title = "Baseline de Plausibilidade & Limites Fisicos RDNA1" },
    @{ Id = "T11"; Exe = "t11_vram_memtest.exe";          Title = "MemTest VRAM GDDR6 Completo (256-bit Stress)" },
    @{ Id = "T12"; Exe = "t12_smu_mailbox_probe.exe";     Title = "Sonda Direta de Mailbox SMU & Handshake SMN" }
)

$SelectedTests = @()
if ($All -or $Summary) {
    $SelectedTests = $TestDefinitions
} elseif ($Test -ne "") {
    $match = $TestDefinitions | Where-Object { $_.Id -ieq $Test }
    if ($match) {
        $SelectedTests = @($match)
    } else {
        Write-Host "[-] Teste '$Test' nao encontrado. Disponiveis: T1 a T12" -ForegroundColor Red
        exit 1
    }
}

$Results = @()

# Executa Testes Selecionados
foreach ($t in $SelectedTests) {
    $exePath = Join-Path $BinDir $t.Exe
    if (-not (Test-Path $exePath)) {
        Write-Host "[-] Binario $exePath nao encontrado." -ForegroundColor Red
        continue
    }

    if ($Json) {
        $out = & $exePath --json
        try {
            $parsed = $out | ConvertFrom-Json
            $Results += $parsed
        } catch {
            $Results += [PSCustomObject]@{
                id = $t.Id
                title = $t.Title
                verdict = "ERRO"
                description = ""
                criterion = "Falha ao processar JSON"
                measured = "$out"
                details = ""
            }
        }
    } else {
        & $exePath
        $rawJson = & $exePath --json
        try { $Results += ($rawJson | ConvertFrom-Json) } catch {}
    }
}

# Executa Auditoria de Fases se solicitado
if ($All -or $Phase -ge 0) {
    $phaseExe = Join-Path $BinDir "phase_audit.exe"
    if (Test-Path $phaseExe) {
        if ($Json) {
            $arg = if ($Phase -ge 0) { @("--phase", "$Phase", "--json") } else { @("--json") }
            $out = & $phaseExe $arg
            try {
                $parsed = $out | ConvertFrom-Json
                $Results += $parsed
            } catch {}
        } else {
            $arg = if ($Phase -ge 0) { @("--phase", "$Phase") } else { @() }
            & $phaseExe $arg
            $rawJson = if ($Phase -ge 0) { & $phaseExe --phase $Phase --json } else { & $phaseExe --json }
            try { $Results += ($rawJson | ConvertFrom-Json) } catch {}
        }
    }
}

# Emissao JSON
if ($Json) {
    $Results | ConvertTo-Json -Depth 4
    exit 0
}

# Exibição de Resumo Consolidado
if ($All -or $Summary -or $SelectedTests.Count -gt 1) {
    Write-Host "===============================================================================" -ForegroundColor Cyan
    Write-Host " MATRIZ CONSOLIDADA DE RESULTADOS - CENTRAL DE TESTES BC250-V3" -ForegroundColor Cyan
    Write-Host "===============================================================================" -ForegroundColor Cyan
    
    $proved = ($Results | Where-Object { $_.verdict -eq "PROVADO" }).Count
    $notVal = ($Results | Where-Object { $_.verdict -eq "NAO VALIDADO EM HARDWARE" }).Count
    $blocked = ($Results | Where-Object { $_.verdict -eq "BLOQUEADO" }).Count
    $errors = ($Results | Where-Object { $_.verdict -eq "ERRO" }).Count

    foreach ($r in $Results) {
        $color = switch ($r.verdict) {
            "PROVADO"                   { "Green" }
            "NAO VALIDADO EM HARDWARE"  { "Yellow" }
            "BLOQUEADO"                 { "Cyan" }
            default                     { "Red" }
        }
        $statusStr = switch ($r.verdict) {
            "PROVADO"                   { "[ PROVADO ]               " }
            "NAO VALIDADO EM HARDWARE"  { "[ NAO VALIDADO EM HW ]    " }
            "BLOQUEADO"                 { "[ BLOQUEADO ]             " }
            default                     { "[ ERRO ]                  " }
        }
        Write-Host " $($r.id.PadRight(8)) : $statusStr - $($r.title)" -ForegroundColor $color
    }

    Write-Host "`nTotais: $proved PROVADOS | $notVal NAO VALIDADOS | $blocked BLOQUEADOS | $errors ERROS`n" -ForegroundColor White
}

# Exportacao de Relatorio Markdown
if ($Report) {
    $timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
    $repFile = Join-Path $ReportsDir "Relatorio_Testes_$timestamp.md"

    $lines = @()
    $lines += "# Relatorio da Central de Testes - AMD BC-250 (BC250-V3)"
    $lines += ""
    $lines += "**Data da Execucao:** $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    $lines += "**Adaptador Alvo:** AMD BC-250 (Cyan Skillfish / Oberon, PCI\VEN_1002&DEV_13FE)"
    $lines += ""
    $lines += "---"
    $lines += ""
    $lines += "## Matriz de Resultados Consolidados"
    $lines += ""
    $lines += "| ID | Teste | Veredito | Descricao | Criterio | Medicao |"
    $lines += "|---|---|---|---|---|---|"

    foreach ($r in $Results) {
        $badge = switch ($r.verdict) {
            "PROVADO"                   { "PROVADO" }
            "NAO VALIDADO EM HARDWARE"  { "NAO VALIDADO" }
            "BLOQUEADO"                 { "BLOQUEADO" }
            default                     { "ERRO" }
        }
        $desc = ($r.description -replace "`r`n", " " -replace '\|', '/')
        $crit = ($r.criterion -replace '\|', '/')
        $meas = ($r.measured -replace '\|', '/')
        $lines += "| $($r.id) | $($r.title) | $badge | $desc | $crit | $meas |"
    }

    $lines += ""
    $lines += "---"
    $lines += "*Relatorio gerado automaticamente pela Central de Testes BC250-V3.*"

    Set-Content -Path $repFile -Value ($lines -join "`r`n") -Encoding UTF8
    Write-Host "[+] Relatorio salvo com sucesso em: $repFile" -ForegroundColor Green
}
