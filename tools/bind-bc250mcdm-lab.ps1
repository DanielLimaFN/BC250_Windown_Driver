[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptBlackScreenRisk,
    [switch]$EnableShaderDispatch,
    [ValidateRange(30,600)][int]$WatchdogTimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $root 'driver\wdk\package\output_lab'
$packageInf = Join-Path $packageDir 'bc250mcdm.inf'
$packageSys = Join-Path $packageDir 'bc250mcdm.sys'
$packageCat = Join-Path $packageDir 'bc250mcdm.cat'
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$watchdogScript = Join-Path $root 'tools\watch-bc250mcdm-lab.ps1'
# Gate proprio do laboratorio: o de producao exige SkipHwBringup=1 e por isso
# rejeita este binding sempre, por construcao (doc 95 secao B3).
$confirmScript = Join-Path $root 'tools\confirm-bc250mcdm-lab.ps1'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$recoveryPath = 'C:\BC250_RECOVERY.cmd'
$hardwareId = 'PCI\VEN_1002&DEV_13FE'

function Assert-ArtifactHash([string]$Path, [string]$ExpectedHash, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Artefato ausente ($Label): $Path"
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actualHash -ne $ExpectedHash) {
        throw "Hash divergente ($Label): observado=$actualHash esperado=$ExpectedHash"
    }
}

if (-not (Test-Path -LiteralPath $packageInf) -or -not (Test-Path -LiteralPath $manifestPath)) {
    throw "Pacote de laboratorio nao encontrado. Execute tools\package-bc250mcdm-lab.ps1 primeiro."
}
if (-not (Test-Path -LiteralPath $recoveryPath)) {
    throw "C:\BC250_RECOVERY.cmd nao encontrado."
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado (Administrador).'
}

$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like "$hardwareId*" })
if ($devices.Count -ne 1) {
    throw "Dispositivo BC-250 nao encontrado ou multiplos encontrados."
}
$device = $devices[0]
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.BuildKind -ne 'MCDM_LAB_BRINGUP' -or [bool]$manifest.HardwareValidated -or
    [uint32]$manifest.TelemetrySchemaVersion -ne 114 -or [uint32]$manifest.SkipHwBringup -ne 0) {
    throw 'Contrato do manifesto de laboratorio invalido (kind/validacao/schema/bring-up).'
}
Assert-ArtifactHash $packageSys $manifest.PackageSysSha256 'SYS'
Assert-ArtifactHash $packageInf $manifest.PackageInfSha256 'INF'
Assert-ArtifactHash $packageCat $manifest.PackageCatSha256 'CAT'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_sdma.bin') $manifest.SdmaFirmwareSha256 'SDMA firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_rlc.bin') $manifest.RlcFirmwareSha256 'RLC firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_pfp.bin') $manifest.PfpFirmwareSha256 'PFP firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_me.bin') $manifest.MeFirmwareSha256 'ME firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_ce.bin') $manifest.CeFirmwareSha256 'CE firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_mec.bin') $manifest.MecFirmwareSha256 'MEC firmware'
Assert-ArtifactHash $recoveryPath $manifest.RecoverySha256 'recuperacao'
foreach ($signedArtifact in @($packageSys, $packageCat)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $signedArtifact
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
        $signature.SignerCertificate.Thumbprint -ne $manifest.SignerThumbprint) {
        throw "Assinatura invalida ou signer divergente: $signedArtifact"
    }
}

$infText = Get-Content -LiteralPath $packageInf -Raw
$expectedInfValues = [ordered]@{
    'Parameters\Probe,SchemaVersion' = 114
    'Parameters\Driver,ActiveMode' = 1
    'Parameters,SkipHwBringup' = 0
    'Parameters,ShaderDispatch' = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    'Parameters,CpSoftReset' = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    'Parameters,Vmid0PageTable' = 0
    'Parameters,SdmaAutoCtxSw' = $(if ($EnableShaderDispatch) { 0 } else { 1 })
}
foreach ($entry in $expectedInfValues.GetEnumerator()) {
    $escapedName = [regex]::Escape($entry.Key)
    if ([regex]::Matches(
            $infText,
            "(?m)^HKR,$escapedName,0x00010001,$($entry.Value)\s*$").Count -ne 1) {
        throw "INF diverge do contrato solicitado: $($entry.Key)=$($entry.Value)."
    }
}
if ($EnableShaderDispatch -and -not [bool]$manifest.ShaderDispatchEnabled) {
    throw 'Pacote nao habilita shader: reempacote com package-bc250mcdm-lab.ps1 -EnableShaderDispatch.'
}
if ($EnableShaderDispatch -and
    (-not [bool]$manifest.CpSoftResetEnabled -or
     [bool]$manifest.Vmid0PageTableEnabled -or
     [bool]$manifest.SdmaAutoCtxSwEnabled)) {
    throw 'Receita F8b invalida no manifesto: exige CpSoftReset=1, Vmid0PageTable=0 e SdmaAutoCtxSw=0.'
}

Write-Host "=== BINDING DE LABORATORIO (BC-250 MCDM) COM WATCHDOG ===" -ForegroundColor Magenta
Write-Host "  Dispositivo: $($device.PNPDeviceID)"
Write-Host "  Servico atual: $($device.Service)"
Write-Host "  Pacote: $packageInf"
Write-Host "  Watchdog Timeout: $WatchdogTimeoutSeconds segundos"
Write-Host ""

if (-not $Execute -or -not $IAcceptBlackScreenRisk) {
    Write-Host "Modo Dry-Run. Para executar:" -ForegroundColor Yellow
    # A dica precisa repetir -EnableShaderDispatch: sem ela o contrato do
    # INF exige ShaderDispatch=0 e o binding falha antes deste ponto.
    $shaderFlag = $(if ($EnableShaderDispatch) { ' -EnableShaderDispatch' } else { '' })
    Write-Host ("powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1" +
        " -Execute -IAcceptBlackScreenRisk" + $shaderFlag)
    exit 0
}

# 1. Limpeza de marcador OEM e pacotes anteriores
if (Test-Path -LiteralPath $markerPath) {
    $oldOem = (Get-Content -LiteralPath $markerPath -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ($oldOem -match '^oem\d+\.inf$') {
        & pnputil.exe /delete-driver $oldOem /uninstall /force 2>&1 | Out-Null
    }
    Remove-Item -LiteralPath $markerPath -Force -ErrorAction SilentlyContinue
}

# 2. Stage do pacote de laboratorio
Write-Host "[1/4] Estagiando pacote de laboratorio via pnputil..." -ForegroundColor Yellow
$output = (& pnputil.exe /add-driver $packageInf 2>&1) -join "`n"
$exitCode = $LASTEXITCODE
$output | Write-Host
if ($exitCode -ne 0) { throw "pnputil /add-driver falhou: exit=$exitCode" }

$publishedNames = @(
    [regex]::Matches($output, '(?i)\boem\d+\.inf\b') |
        ForEach-Object { $_.Value.ToLowerInvariant() } |
        Select-Object -Unique
)
if ($publishedNames.Count -ne 1) {
    throw 'Nome oemNN.inf nao identificado de forma unica.'
}
$publishedName = $publishedNames[0]
$publishedName | Set-Content -LiteralPath $markerPath -Encoding ascii
Write-Host "  Pacote publicado como: $publishedName" -ForegroundColor Green

# 3. Armar o Watchdog em segundo plano
#
# Dois watchdogs simultaneos brigam pelo mesmo dispositivo: o mais antigo dispara
# o rollback (que remove o pacote OEM) no meio do binding do mais novo, e o
# UpdateDriverForPlugAndPlayDevicesW falha com win32=2. Aconteceu em 29/08 18:14,
# com os PIDs 3092 e 908 armados ao mesmo tempo.
$liveWatchdogs = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*watch-bc250mcdm-lab.ps1*' })
if ($liveWatchdogs.Count -gt 0) {
    Write-Host ''
    Write-Host 'ERRO: ja existe watchdog de laboratorio armado:' -ForegroundColor Red
    foreach ($w in $liveWatchdogs) { Write-Host ("  PID {0}" -f $w.ProcessId) -ForegroundColor Red }
    Write-Host 'Confirme o binding anterior, deixe-o expirar, ou encerre o processo:' -ForegroundColor Yellow
    Write-Host ("  Stop-Process -Id {0} -Force" -f $liveWatchdogs[0].ProcessId) -ForegroundColor Yellow
    throw 'Binding abortado: watchdog concorrente ativo.'
}

Write-Host "`n[2/4] Armando Watchdog de Rollback ($WatchdogTimeoutSeconds s)..." -ForegroundColor Yellow
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-lab-binding"
New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null

$watchdogToken = [guid]::NewGuid().ToString('D')
$watchdogArguments = "-NoProfile -ExecutionPolicy Bypass -File `"$watchdogScript`" " +
    "-EvidenceDir `"$evidenceDir`" -InstanceId `"$($device.PNPDeviceID)`" " +
    "-TargetInf `"$publishedName`" -Token `"$watchdogToken`" " +
    "-TimeoutSeconds $WatchdogTimeoutSeconds -Execute"

$watchdogProcess = Start-Process -FilePath powershell.exe -ArgumentList $watchdogArguments `
    -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 500
if ($watchdogProcess.HasExited) {
    throw "Watchdog falhou ao inicializar (exit=$($watchdogProcess.ExitCode))."
}
Write-Host "  Watchdog ativo (PID: $($watchdogProcess.Id))" -ForegroundColor Green

# 4. Executar o binding via NewDev ou pnputil
Write-Host "`n[3/4] Vinculando driver ao hardware..." -ForegroundColor Yellow
if (-not ('Bc250LabNewDev.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Bc250LabNewDev
{
    public static class NativeMethods
    {
        [DllImport("newdev.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool UpdateDriverForPlugAndPlayDevicesW(
            IntPtr hwndParent,
            string hardwareId,
            string fullInfPath,
            UInt32 installFlags,
            [MarshalAs(UnmanagedType.Bool)] ref bool rebootRequired);
    }
}
'@
}

$installFlagForce = [uint32]0x00000001
$rebootRequired = $false
$nativeOk = [Bc250LabNewDev.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW(
    [IntPtr]::Zero,
    $hardwareId,
    $packageInf,
    $installFlagForce,
    [ref]$rebootRequired)

$win32Error = if ($nativeOk) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
Write-Host "  UpdateDriverForPlugAndPlayDevicesW: success=$nativeOk win32=$win32Error reboot=$rebootRequired"

if (-not $nativeOk) {
    # 'pnputil /install /driver <oemN.inf>' nao existe: so' imprime a tela de
    # uso. Parecia fallback e nao era.
    Write-Warning "NewDev falhou (win32=$win32Error), tentando fallback valido..."
    $fallback = (& pnputil.exe /add-driver $packageInf /install 2>&1) -join "`n"
    $fallback | Write-Host
    if ($LASTEXITCODE -ne 0) { Write-Warning "Fallback pnputil tambem falhou: exit=$LASTEXITCODE" }
}

# 5. Configurar SkipHwBringup = 0 nos parametros do servico
Write-Host "`n[4/4] Configurando parametros de execucao no registro..." -ForegroundColor Yellow
$paramKey = "HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters"
if (-not (Test-Path $paramKey)) {
    New-Item -Path $paramKey -Force | Out-Null
}
Set-ItemProperty -Path $paramKey -Name "SkipHwBringup" -Value 0 -Type DWord -Force
Write-Host "  SkipHwBringup = 0 configurado." -ForegroundColor Green
Set-ItemProperty -Path $paramKey -Name "ShaderDispatch" `
    -Value ($(if ($EnableShaderDispatch) { 1 } else { 0 })) -Type DWord -Force
Write-Host ("  ShaderDispatch = {0} configurado." -f
    $(if ($EnableShaderDispatch) { 1 } else { 0 })) -ForegroundColor Green
Set-ItemProperty -Path $paramKey -Name "CpSoftReset" `
    -Value ($(if ($EnableShaderDispatch) { 1 } else { 0 })) -Type DWord -Force
Set-ItemProperty -Path $paramKey -Name "Vmid0PageTable" -Value 0 -Type DWord -Force
Set-ItemProperty -Path $paramKey -Name "SdmaAutoCtxSw" `
    -Value ($(if ($EnableShaderDispatch) { 0 } else { 1 })) -Type DWord -Force
Write-Host ("  Receita F8b: CpSoftReset={0}, Vmid0PageTable=0, SdmaAutoCtxSw={1}." -f
    $(if ($EnableShaderDispatch) { 1 } else { 0 }),
    $(if ($EnableShaderDispatch) { 0 } else { 1 })) -ForegroundColor Green

# Verificar estado do dispositivo
Start-Sleep -Seconds 3
$devAfter = Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like "$hardwareId*" }
Write-Host "`nEstado do dispositivo:" -ForegroundColor Cyan
Write-Host "  Service: $($devAfter.Service)"
Write-Host "  Status: $($devAfter.Status)"
Write-Host "  ConfigManagerErrorCode: $($devAfter.ConfigManagerErrorCode)"

$driverInstalledPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$pkgSysPath = Join-Path $root 'driver\wdk\package\output_lab\bc250mcdm.sys'
if (Test-Path $driverInstalledPath) {
    $instHash = (Get-FileHash -LiteralPath $driverInstalledPath -Algorithm SHA256).Hash
    $pkgHash = (Get-FileHash -LiteralPath $pkgSysPath -Algorithm SHA256).Hash
    Write-Host "  Hash instalado: $($instHash.Substring(0,16))"
    Write-Host "  Hash do pacote: $($pkgHash.Substring(0,16))"
    if ($instHash -eq $pkgHash) {
        Write-Host "  Binario em System32\drivers ATUALIZADO com sucesso!" -ForegroundColor Green
    } else {
        Write-Host "  AVISO: Binario no System32 nao atualizou automaticamente; copiando..." -ForegroundColor Yellow
        Copy-Item -LiteralPath $pkgSysPath -Destination $driverInstalledPath -Force
    }
}

# Vincular NAO e' ter vinculado: o script ja' anunciou "INSTALADO!" com o
# dispositivo em BasicDisplay e, depois, sem servico nenhum. Conferir aqui, e
# desarmar o proprio watchdog se falhou - deixa-lo armado so' produz mais um
# rollback em cima de um estado que ja' esta quebrado.
if ($devAfter.Service -ne 'bc250mcdm') {
    Write-Host ''
    Write-Host "BINDING NAO PEGOU: dispositivo em '$($devAfter.Service)', esperado bc250mcdm." -ForegroundColor Red
    if ($win32Error -eq 1072) {
        Write-Host 'win32=1072 = ERROR_SERVICE_MARKED_FOR_DELETE.' -ForegroundColor Red
        Write-Host 'O servico bc250mcdm esta pendente de exclusao e nao pode ser recriado' -ForegroundColor Yellow
        Write-Host 'ate' + [char]39 + ' o sistema soltar a referencia. Isso exige REINICIAR.' -ForegroundColor Yellow
    }
    Write-Host 'Desarmando o watchdog desta tentativa para nao empilhar rollbacks.' -ForegroundColor Yellow
    if ($null -ne $watchdogProcess -and -not $watchdogProcess.HasExited) {
        Stop-Process -Id $watchdogProcess.Id -Force -ErrorAction SilentlyContinue
    }
    throw "Binding de laboratorio falhou (win32=$win32Error)."
}

# Captura imediata da telemetria, ANTES de o watchdog poder disparar.
#
# No ciclo 3.30 o watchdog expirou e fez rollback automatico antes de o
# confirm rodar: o servico bc250mcdm sumiu levando a chave Probe junto e a
# evidencia canonica do ensaio se perdeu. O ensaio e caro e so cabe uma
# submissao por boot; a captura nao pode depender de uma acao humana dentro
# da janela do watchdog.
$probeKeyPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
try {
    if (Test-Path -LiteralPath $probeKeyPath) {
        $probeSnapshot = Get-ItemProperty -LiteralPath $probeKeyPath -ErrorAction Stop
        $probeOrdered = [ordered]@{}
        foreach ($probeProperty in ($probeSnapshot.PSObject.Properties |
                Where-Object { $_.Name -notlike 'PS*' } |
                Sort-Object Name)) {
            $probeOrdered[$probeProperty.Name] = $probeProperty.Value
        }
        $probeOrdered | ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath (Join-Path $evidenceDir 'probe-registry-at-bind.json') -Encoding UTF8
        $probeRegPath = 'HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
        & reg.exe export $probeRegPath (Join-Path $evidenceDir 'probe-registry-at-bind.txt') /y | Out-Null
        Write-Host ("  Telemetria capturada no bind: {0} valores." -f $probeOrdered.Count) -ForegroundColor Cyan
    } else {
        Write-Host '  AVISO: chave de telemetria ausente logo apos o binding.' -ForegroundColor Yellow
    }
} catch {
    # Best-effort: a captura nunca pode derrubar o ensaio nem o watchdog.
    Write-Host ("  AVISO: falha ao capturar telemetria no bind: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
}

Write-Host "`n==========================================================================" -ForegroundColor Green
Write-Host "DRIVER DE LABORATORIO INSTALADO!" -ForegroundColor Green
Write-Host "O Watchdog continua armado por $WatchdogTimeoutSeconds segundos." -ForegroundColor Yellow
Write-Host "Se o video estiver visivel, confirme para desarmar o rollback:" -ForegroundColor Yellow
Write-Host "powershell -ExecutionPolicy Bypass -File tools\confirm-bc250mcdm-lab.ps1 -EvidenceDir `"$evidenceDir`" -InstanceId `"$($device.PNPDeviceID)`" -Token `"$watchdogToken`" -IConfirmDisplayIsVisible"
Write-Host "==========================================================================" -ForegroundColor Green

[ordered]@{
    evidence_dir = $evidenceDir
    watchdog_token = $watchdogToken
    instance_id = $device.PNPDeviceID
    published_inf = $publishedName
    # Registro duravel do que foi realmente instalado. A quarentena
    # depende disto para reconhecer o residuo mesmo depois de o
    # pacote ter sido reconstruido para um candidato posterior.
    package_sys_sha256 = (Get-FileHash -LiteralPath $pkgSysPath -Algorithm SHA256).Hash
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'tools\_last_lab_binding.json') -Encoding UTF8
