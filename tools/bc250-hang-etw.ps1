[CmdletBinding(DefaultParameterSetName='Status')]
param(
    [Parameter(ParameterSetName='Start',Mandatory=$true)][switch]$Start,
    [Parameter(ParameterSetName='Start')][string]$EvidenceDir,
    [Parameter(ParameterSetName='Stop',Mandatory=$true)][switch]$Stop,
    [string]$ManifestPath,
    [Parameter(ParameterSetName='Recover',Mandatory=$true)][switch]$Recover,
    [Parameter(ParameterSetName='Status')][switch]$Status
)

# DX.83: duas sessoes ETW em modo arquivo, com cauda recuperavel apos reset:
# xperf para escalonamento/threads; logman para DxgKrnl e DWM. Nenhum bind aqui.
$ErrorActionPreference = 'Stop'
$registryPath = 'HKLM:\SOFTWARE\BC250\Umd'
$registryName = 'HangTrace'
$latestPointer = 'C:\BC250_HANG_ETW_LATEST.txt'

function Assert-Admin {
    $principal = New-Object Security.Principal.WindowsPrincipal(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'ETW e HangTrace exigem PowerShell elevado.'
    }
}

function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    $output = & $Exe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe falhou (exit=$LASTEXITCODE): $($Arguments -join ' ')`n$($output -join "`n")"
    }
    return ($output -join "`n")
}

function Save-Manifest($data, [string]$path) {
    $data | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
}

function Add-FileResults($data) {
    $files = @()
    foreach ($path in @($data.kernel_etl, $data.graphics_etl)) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $file = Get-Item -LiteralPath $path
            $entry = [ordered]@{
                path = $file.FullName
                bytes = $file.Length
                last_write = $file.LastWriteTime.ToString('o')
            }
            try { $entry.sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
            catch { $entry.hash_error = $_.Exception.Message }
            $files += $entry
        } else {
            $files += [ordered]@{ path = $path; missing = $true }
        }
    }
    $data.files = $files
}

function Restore-HangTrace($data) {
    if ($data.hangtrace_previous_present) {
        New-Item -Path $registryPath -Force | Out-Null
        New-ItemProperty -LiteralPath $registryPath -Name $registryName -PropertyType DWord `
            -Value ([int]$data.hangtrace_previous_value) -Force | Out-Null
    } else {
        Remove-ItemProperty -LiteralPath $registryPath -Name $registryName `
            -ErrorAction SilentlyContinue
    }
}

if ($Start) {
    Assert-Admin
    if (Test-Path -LiteralPath $latestPointer -PathType Leaf) {
        $priorPath = (Get-Content -LiteralPath $latestPointer -Raw).Trim()
        if (Test-Path -LiteralPath $priorPath -PathType Leaf) {
            $prior = Get-Content -LiteralPath $priorPath -Raw | ConvertFrom-Json
            if ($prior.state -eq 'RUNNING' -or $prior.state -eq 'STOP_INCOMPLETE') {
                throw "Captura anterior pendente ($($prior.state)): $priorPath. Execute -Stop ou -Recover."
            }
        }
    }
    $xperf = (Get-Command xperf.exe -ErrorAction Stop).Source
    $logman = (Get-Command logman.exe -ErrorAction Stop).Source
    if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
        $EvidenceDir = Join-Path (Split-Path -Parent $PSScriptRoot) `
            ('docs\evidence\' + (Get-Date -Format 'yyyy-MM-dd_HH-mm-ss') + '-DX83-HANG-ETW')
    }
    $dir = [System.IO.Path]::GetFullPath($EvidenceDir)
    if (Test-Path -LiteralPath $dir) {
        if (Get-ChildItem -LiteralPath $dir -Force | Select-Object -First 1) {
            throw "EvidenceDir deve estar vazia: $dir"
        }
    } else { New-Item -Path $dir -ItemType Directory -Force | Out-Null }
    $boot = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
    $id = (Get-Date).ToString('yyyyMMddHHmmss')
    $session = "BC250HangGraphics$id"
    $providers = Join-Path $dir 'providers.txt'
    @(
        # DX.88: +Resource (0x40) e +DriverEvents (0x400). O Explorer pede uma
        # textura compartilhada 1024x832 e o runtime recusa com E_INVALIDARG
        # DEPOIS de o KMD aceitar (KmdStatus=0), 461 vezes na Corrida 136; sem
        # estes eventos o ETW nao mostra em que etapa o dxgkrnl recusa.
        'Microsoft-Windows-DxgKrnl 0x00000000017084C1 5',
        'Microsoft-Windows-Dwm-Core 0x0000000000100021 5',
        'Microsoft-Windows-Kernel-Process 0x0000000000000070 4'
    ) | Set-Content -LiteralPath $providers -Encoding Ascii
    foreach ($name in @('Microsoft-Windows-DxgKrnl','Microsoft-Windows-Dwm-Core',
            'Microsoft-Windows-Kernel-Process')) {
        Invoke-Native $logman @('query','providers',$name) | Out-Null
    }
    $previous = Get-ItemProperty -LiteralPath $registryPath -Name $registryName `
        -ErrorAction SilentlyContinue
    $oldPresent = $null -ne $previous -and $null -ne $previous.$registryName
    $data = [ordered]@{
        schema = 1; run_id = $id; state = 'PREPARING'; boot_ticks = $boot
        created_at = (Get-Date).ToString('o'); session = $session
        kernel_etl = (Join-Path $dir 'kernel-scheduler.etl')
        graphics_etl = (Join-Path $dir 'graphics-dxg-dwm.etl')
        providers_file = $providers; hangtrace_previous_present = $oldPresent
        hangtrace_previous_value = $(if ($oldPresent) { [int]$previous.$registryName } else { 0 })
        files = @(); hardware_validated = $false
        started_at = $null; stopped_at = $null; recovered_at = $null
        stop_errors = @(); paging_trace = $null; paging_trace_error = $null
    }
    $manifest = Join-Path $dir 'hang-etw-manifest.json'
    Save-Manifest $data $manifest
    $kernelStarted = $false
    $graphicsStarted = $false
    try {
        Invoke-Native $xperf @('-on','PROC_THREAD+LOADER+CSWITCH+DISPATCHER+DISK_IO+DISK_IO_INIT',
            '-stackwalk','CSwitch+ReadyThread',
            '-f',$data.kernel_etl,'-FileMode','Circular','-MaxFile','256',
            '-BufferSize','64','-MinBuffers','16','-MaxBuffers','64','-FlushTimer','1') | Out-Null
        $kernelStarted = $true
        Invoke-Native $logman @('create','trace',$session,'-ets','-o',$data.graphics_etl,
            '-f','bincirc','-max','128','-bs','64','-nb','16','64',
            '-ft','00:00:01','-pf',$providers) | Out-Null
        $graphicsStarted = $true
        Invoke-Native $logman @('query',$session,'-ets') | Out-Null
        New-Item -Path $registryPath -Force | Out-Null
        New-ItemProperty -LiteralPath $registryPath -Name $registryName `
            -PropertyType DWord -Value 1 -Force | Out-Null
        if ([int](Get-ItemPropertyValue -LiteralPath $registryPath -Name $registryName) -ne 1) {
            throw 'Readback de HangTrace falhou.'
        }
        $data.state = 'RUNNING'
        $data.started_at = (Get-Date).ToString('o')
        Save-Manifest $data $manifest
        Set-Content -LiteralPath $latestPointer -Value $manifest -Encoding Ascii
        Write-Host "ETW ativo; HangTrace=1; manifesto: $manifest"
    } catch {
        if ($graphicsStarted) { & $logman stop $session -ets 2>&1 | Out-Null }
        if ($kernelStarted) { & $xperf -stop 2>&1 | Out-Null }
        Restore-HangTrace $data
        $data.state = 'START_FAILED'
        $data.failure = $_.Exception.Message
        Save-Manifest $data $manifest
        throw
    }
    exit 0
}

if ([string]::IsNullOrWhiteSpace($ManifestPath) -and
    (Test-Path -LiteralPath $latestPointer -PathType Leaf)) {
    $ManifestPath = (Get-Content -LiteralPath $latestPointer -Raw).Trim()
}
if ([string]::IsNullOrWhiteSpace($ManifestPath) -or
    -not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw 'Informe -ManifestPath de uma captura existente.'
}
$manifest = [System.IO.Path]::GetFullPath($ManifestPath)
$data = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
$currentBoot = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
$sameBoot = [long]$data.boot_ticks -eq [long]$currentBoot

if ($Stop) {
    Assert-Admin
    if (-not $sameBoot) { throw 'Boot mudou: use -Recover; nao pare sessoes de outro boot.' }
    if ($data.state -ne 'RUNNING') { throw "Captura nao esta RUNNING: $($data.state)" }
    $stopErrors = @()
    try { Invoke-Native 'logman.exe' @('stop',[string]$data.session,'-ets') | Out-Null }
    catch { $stopErrors += $_.Exception.Message }
    try { Invoke-Native 'xperf.exe' @('-stop') | Out-Null }
    catch { $stopErrors += $_.Exception.Message }
    Restore-HangTrace $data
    $data.state = $(if ($stopErrors.Count -eq 0) { 'STOPPED' } else { 'STOP_INCOMPLETE' })
    $data.stop_errors = $stopErrors
    $data.stopped_at = (Get-Date).ToString('o')
    Add-FileResults $data
    Save-Manifest $data $manifest
    if ($stopErrors.Count -gt 0) { throw "ETW parcialmente parado: $($stopErrors -join '; '); manifesto: $manifest" }
    Write-Host "ETW parado e hashes arquivados: $manifest"
    exit 0
}

if ($Recover) {
    Assert-Admin
    if ($sameBoot -and ($data.state -eq 'RUNNING' -or $data.state -eq 'STOP_INCOMPLETE')) {
        throw 'Captura pode estar ativa neste boot: verifique/pare as sessoes antes de -Recover.'
    }
    Restore-HangTrace $data
    if (-not $sameBoot -and (Test-Path -LiteralPath 'C:\BC250_PAGING_TRACE.bin' -PathType Leaf)) {
        try {
            $pagingCopy = Join-Path (Split-Path -Parent $manifest) 'paging-trace-after-reset.bin'
            Copy-Item -LiteralPath 'C:\BC250_PAGING_TRACE.bin' -Destination $pagingCopy -Force
            $data.paging_trace = [ordered]@{
                path = $pagingCopy
                bytes = (Get-Item -LiteralPath $pagingCopy).Length
                sha256 = (Get-FileHash -LiteralPath $pagingCopy -Algorithm SHA256).Hash
            }
        } catch { $data.paging_trace_error = $_.Exception.Message }
    }
    $data.state = $(if ($sameBoot) { 'RECOVERED_SAME_BOOT' } else { 'RECOVERED_AFTER_RESET' })
    $data.recovered_at = (Get-Date).ToString('o')
    Add-FileResults $data
    Save-Manifest $data $manifest
    $edidBaseline = Join-Path (Split-Path -Parent $manifest) 'edid-PreBind.json'
    if (Test-Path -LiteralPath $edidBaseline -PathType Leaf) {
        try {
            & (Join-Path $PSScriptRoot 'capture-bc250-monitor-edid.ps1') `
                -Phase PostRecovery -EvidenceDir (Split-Path -Parent $manifest)
        } catch { Write-Warning "EDID pos-reset nao capturado: $($_.Exception.Message)" }
    }
    Write-Host "ETW recuperado; arquivo parcial pode perder a cauda: $manifest"
    exit 0
}

Write-Host "Estado: $($data.state); mesmo boot: $sameBoot"
Write-Host "Dxg/DWM: $($data.graphics_etl)"
Write-Host "CPU/thread: $($data.kernel_etl)"
