[CmdletBinding()]
param(
    [ValidateRange(3000, 4200)][int]$FrequencyMhz = 3500,
    [switch]$Get,
    [switch]$Reset
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$hklmPath = "HKLM:\SOFTWARE\BC250\Cpu"
$hkcuPath = "HKCU:\SOFTWARE\BC250\Cpu"

if ($Reset) {
    $FrequencyMhz = 3500
}

function Get-TargetKey {
    if (Test-Path $hklmPath) { return $hklmPath }
    if (Test-Path $hkcuPath) { return $hkcuPath }
    return $null
}

try {
    if ($Get) {
        $val = $null
        try {
            if (Test-Path $hklmPath) {
                $val = (Get-ItemProperty -Path $hklmPath -Name "MaxBoostClkMhz" -ErrorAction SilentlyContinue).MaxBoostClkMhz
            }
        } catch {}
        if (-not $val) {
            try {
                if (Test-Path $hkcuPath) {
                    $val = (Get-ItemProperty -Path $hkcuPath -Name "MaxBoostClkMhz" -ErrorAction SilentlyContinue).MaxBoostClkMhz
                }
            } catch {}
        }
        if (-not $val) { $val = 3500 }

        $result = @{
            Success = $true
            FrequencyMhz = [int]$val
            MinFrequencyMhz = 3000
            MaxFrequencyMhz = 4200
            DefaultFrequencyMhz = 3500
            Message = "Frequencia atual configurada: $val MHz"
        }
        $result | ConvertTo-Json -Compress
        exit 0
    }

    # 1. Salvar no Registro (Persistência)
    $savedPath = $hklmPath
    try {
        if (-not (Test-Path $hklmPath)) {
            New-Item -Path $hklmPath -Force -ErrorAction Stop | Out-Null
        }
        Set-ItemProperty -Path $hklmPath -Name "MaxBoostClkMhz" -Value $FrequencyMhz -Type DWord -Force -ErrorAction Stop
    }
    catch {
        $savedPath = $hkcuPath
        if (-not (Test-Path $hkcuPath)) {
            New-Item -Path $hkcuPath -Force | Out-Null
        }
        Set-ItemProperty -Path $hkcuPath -Name "MaxBoostClkMhz" -Value $FrequencyMhz -Type DWord -Force
    }

    # 2. Executar despacho de hardware SMU Q3 0x8F
    $smuExe = Join-Path $PSScriptRoot "smu-cpu-msg-test.exe"
    $hardwareAttempted = $false
    $hardwareSuccess = $false
    $cpuResponse = "Ferramenta de despacho SMU não encontrada."
    $cpuRaw = ""

    if (Test-Path $smuExe) {
        $hardwareAttempted = $true
        try {
            $pinfo = New-Object System.Diagnostics.ProcessStartInfo
            $pinfo.FileName = $smuExe
            $pinfo.Arguments = "3 0x8F $FrequencyMhz"
            $pinfo.RedirectStandardOutput = $true
            $pinfo.RedirectStandardError = $true
            $pinfo.UseShellExecute = $false
            $pinfo.CreateNoWindow = $true

            $p = New-Object System.Diagnostics.Process
            $p.StartInfo = $pinfo
            [void]$p.Start()
            $cpuRaw = $p.StandardOutput.ReadToEnd()
            [void]$p.WaitForExit(4000)

            if ($cpuRaw -match "ResponseStatus:\s*0x01") {
                $hardwareSuccess = $true
                $cpuResponse = "OK (0x01) - A CPU aceitou o comando de boost e aplicou $FrequencyMhz MHz."
            }
            elseif ($cpuRaw -match "FAIL:\s*CreateFile\s*gle=(\d+)") {
                $gle = $matches[1]
                if ($gle -eq "2") {
                    $cpuResponse = "KMD Offline (GLE 2: Driver não encontrado). A CPU não respondeu no silício porque o adaptador de vídeo está em 'BasicDisplay'. O valor foi salvo no registro para o próximo bind."
                } else {
                    $cpuResponse = "Falha ao abrir dispositivo KMD (GLE $gle). A CPU não respondeu."
                }
            }
            elseif ($cpuRaw -match "ResponseStatus:\s*0xFD") {
                $cpuResponse = "REJEITADO (0xFD) - A CPU recusou o argumento de frequência ($FrequencyMhz MHz)."
            }
            elseif ($cpuRaw -match "ResponseStatus:\s*0xFE") {
                $cpuResponse = "COMANDO_DESCONHECIDO (0xFE) - O firmware da CPU não reconheceu a mensagem 0x8F."
            }
            elseif ($cpuRaw -match "TIMEOUT") {
                $cpuResponse = "TIMEOUT (0x00) - A CPU não respondeu dentro do limite de tempo."
            }
            else {
                $cpuResponse = $cpuRaw.Trim()
            }
        }
        catch {
            $cpuResponse = "Erro ao executar despacho SMU: $($_.Exception.Message)"
        }
    }

    $result = @{
        Success = $true
        FrequencyMhz = $FrequencyMhz
        HardwareAttempted = $hardwareAttempted
        HardwareSuccess = $hardwareSuccess
        CpuResponse = $cpuResponse
        RawOutput = $cpuRaw.Trim()
        KeyPath = $savedPath
        Message = "Frequência de $FrequencyMhz MHz ($([math]::Round($FrequencyMhz/1000, 2)) GHz) processada. $cpuResponse"
    }
    $result | ConvertTo-Json -Compress
}
catch {
    $err = @{
        Success = $false
        FrequencyMhz = $FrequencyMhz
        Message = "Falha ao configurar frequência da CPU: $($_.Exception.Message)"
    }
    $err | ConvertTo-Json -Compress
    exit 1
}
