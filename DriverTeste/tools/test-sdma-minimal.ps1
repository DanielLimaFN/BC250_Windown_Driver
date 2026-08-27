param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,30)][int]$CommandTimeoutSeconds = 10
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$firmware = Join-Path $root 'firmware\cyan_skillfish2_sdma.bin'
$firmwareManifest = Join-Path $root 'firmware\manifest.json'
$destination = [uint32]0x0FF04000
$patterns = @(
    [Convert]::ToUInt32('BC250001',16),
    [Convert]::ToUInt32('13579BDF',16),
    [Convert]::ToUInt32('2468ACE0',16),
    [Convert]::ToUInt32('5AA55AA5',16)
)
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute sabendo que o firmware SDMA permanece ativo ate o proximo reboot.'
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
foreach($path in @($cli,$workspaceDriver,$installedDriver,$firmware,$firmwareManifest)) {
    if(-not(Test-Path -LiteralPath $path)){throw "Arquivo ausente: $path"}
}
$device = Get-PnpDevice -PresentOnly | Where-Object InstanceId -like 'PCI\VEN_1002&DEV_13FE*' | Select-Object -First 1
if(-not $device-or$device.Service-ne'BasicDisplay'){throw "BC-250 nao esta em BasicDisplay: $($device.Service)"}
if(Get-Service bc250v4 -ErrorAction SilentlyContinue){throw 'bc250v4 ainda existe.'}
if((Get-Service bc250dbg -ErrorAction Stop).Status-ne'Running'){throw 'bc250dbg parado.'}
$driverHash=(Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if($driverHash-ne(Get-FileHash $installedDriver -Algorithm SHA256).Hash){throw 'Build instalado diverge.'}
$manifest=Get-Content $firmwareManifest -Raw|ConvertFrom-Json
$fwEntry=$manifest.firmware|Where-Object name -eq 'cyan_skillfish2_sdma.bin'|Select-Object -First 1
$fwHash=(Get-FileHash $firmware -Algorithm SHA256).Hash.ToLowerInvariant()
if(-not$fwEntry-or-not$fwEntry.structurally_valid-or$fwHash-ne$fwEntry.sha256){throw 'Hash/estrutura do firmware SDMA diverge do manifesto.'}

if(-not$OutputRoot){$OutputRoot=Join-Path $root 'docs\evidence'}
$dir=Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-sdma-minimal"
New-Item -ItemType Directory -Path $dir -Force|Out-Null
$events=[System.Collections.Generic.List[object]]::new()
$telemetryProc=$null;$telemetryOutTask=$null;$telemetryErrTask=$null
$telemetryLines=@();$telemetryFailure=$null

function Run([string]$Name,[string[]]$Arguments,[switch]$AllowFailure){
    $psi=New-Object Diagnostics.ProcessStartInfo
    $psi.FileName=$cli
    $psi.Arguments=(($Arguments|ForEach-Object{'"'+$_.Replace('"','\"')+'"'})-join' ')
    $psi.WorkingDirectory=$root;$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true
    $psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    $p=New-Object Diagnostics.Process;$p.StartInfo=$psi
    if(-not$p.Start()){throw "Falha ao iniciar $Name"}
    $ot=$p.StandardOutput.ReadToEndAsync();$et=$p.StandardError.ReadToEndAsync()
    $to=-not$p.WaitForExit($CommandTimeoutSeconds*1000)
    if($to){$p.Kill();$p.WaitForExit()}
    $ec=if($to){1460}else{$p.ExitCode}
    $r=[pscustomobject]@{name=$Name;command='bc250ctl.exe '+($Arguments-join' ');exit_code=$ec;timed_out=$to;output=@(($ot.Result+$et.Result)-split"`r?`n"|Where-Object{$_})}
    $events.Add($r)
    if(($to-or$ec-ne0)-and-not$AllowFailure){throw "$Name falhou: exit=$ec timeout=$to"}
    return $r
}
function Read-Reg([string]$Name,[string]$Offset){
    $r=Run $Name @('reg','r',$Offset);$t=$r.output-join' '
    if($t-notmatch'=\s*0x([0-9A-Fa-f]{8})'){throw "Parse de registrador falhou: $Name"}
    return '0x'+$Matches[1].ToUpperInvariant()
}
function Read-Vram([string]$Name,[uint32]$Offset){
    $r=Run $Name @('vram','r',('0x{0:X8}'-f$Offset));$t=$r.output-join' '
    if($t-notmatch'=\s*0x([0-9A-Fa-f]{8})'){throw "Parse de VRAM falhou: $Name"}
    return [Convert]::ToUInt32($Matches[1],16)
}
function Start-TelemetryWatch {
    $psi=New-Object Diagnostics.ProcessStartInfo
    $psi.FileName=$cli
    $psi.Arguments='telemetry watch 30000 1 250'
    $psi.WorkingDirectory=$root;$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true
    $psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    $script:telemetryProc=New-Object Diagnostics.Process
    $script:telemetryProc.StartInfo=$psi
    if(-not$script:telemetryProc.Start()){throw 'Falha ao iniciar sampler de telemetria.'}
    $script:telemetryOutTask=$script:telemetryProc.StandardOutput.ReadToEndAsync()
    $script:telemetryErrTask=$script:telemetryProc.StandardError.ReadToEndAsync()
}
function Stop-TelemetryWatch {
    if(-not$script:telemetryProc){return}
    if(-not$script:telemetryProc.HasExited){$script:telemetryProc.Kill();$script:telemetryProc.WaitForExit()}
    $stdout=$script:telemetryOutTask.Result
    $stderr=$script:telemetryErrTask.Result
    $script:telemetryLines=@($stdout-split"`r?`n"|Where-Object{$_})
    if($stderr){$script:telemetryFailure=$stderr.Trim()}
}

$regs=[ordered]@{clk_ctrl='0x049EC';cntl='0x049F0';status='0x04A14';ucode_checksum='0x04A24';f32_cntl='0x04A28';status2='0x04A60'}
function Read-RegSet([string]$Prefix){$s=[ordered]@{};foreach($e in $regs.GetEnumerator()){$s[$e.Key]=Read-Reg "$Prefix`_$($e.Key)" $e.Value};[pscustomobject]$s}

$pre=$null;$post=$null;$saved=@();$restored=@();$failure=$null
$armed=$false;$pspCreated=$false;$sdmaCreated=$false;$firmwareAccepted=$false;$writeProof=$null
try{
    $infoPre=Run 'info_pre' @('info')
    if(($infoPre.output-join' ')-notmatch'bc250dbg v\d+\.\d+\s+ABI \d+\s+armamento=readonly'){
        throw 'Preflight recusado: info nao reportou ABI, ou armamento nao esta em readonly.'
    }
    $pspPre=Run 'psp_status_pre' @('psp','status')
    $pspPreText=$pspPre.output-join' '
    if($pspPreText-notmatch'C2PMSG_67\s+:\s+0x([0-9A-Fa-f]{8})'){throw 'Nao foi possivel validar WPTR PSP.'}
    $prePspWptr=[Convert]::ToUInt32($Matches[1],16)
    if($pspPreText-notmatch'anel\s+:\s+lo 0x([0-9A-Fa-f]{8})\s+hi 0x([0-9A-Fa-f]{8})\s+tam 0x([0-9A-Fa-f]{8})'){
        throw 'Nao foi possivel validar base/tamanho do ring PSP.'
    }
    $prePspLo=[Convert]::ToUInt32($Matches[1],16);$prePspHi=[Convert]::ToUInt32($Matches[2],16);$prePspSize=[Convert]::ToUInt32($Matches[3],16)
    if($prePspWptr-ne0-or$prePspLo-ne0-or$prePspHi-ne0-or$prePspSize-ne0){throw 'Preflight recusado: ring PSP externo/residual presente.'}
    $sdmaPre=Run 'sdma_status_pre' @('sdma','status')
    $sdmaPreText=$sdmaPre.output-join' '
    if($sdmaPreText-notmatch'RB_CNTL\s+:\s+0x([0-9A-Fa-f]{8})'){throw 'Nao foi possivel validar RB_CNTL no preflight.'}
    $preRbCntl=[Convert]::ToUInt32($Matches[1],16)
    if($sdmaPreText-notmatch'RB_BASE\s+:\s+0x([0-9A-Fa-f]+)'){throw 'Nao foi possivel validar RB_BASE no preflight.'}
    $preRbBase=[Convert]::ToUInt64($Matches[1],16)
    if($sdmaPreText-notmatch'WPTR \(reg\)\s+:\s+0x([0-9A-Fa-f]{8})'){throw 'Nao foi possivel validar WPTR no preflight.'}
    $preWptr=[Convert]::ToUInt32($Matches[1],16)
    if(($preRbCntl-band1)-ne0-or$preRbBase-ne0-or$preWptr-ne0){
        throw ('Preflight recusado: estado SDMA residual RB_CNTL=0x{0:X8} BASE=0x{1:X} WPTR=0x{2:X8}.'-f$preRbCntl,$preRbBase,$preWptr)
    }
    Run 'smu_q0_test_pre' @('smu','0','0x01','0')|Out-Null
    Run 'smu_gfxclk_pre' @('smu','0','0x0F','0')|Out-Null
    Run 'smu_active_wgp_pre' @('smu','0','0x1E','0')|Out-Null
    $pre=Read-RegSet 'pre'
    if($pre.ucode_checksum-ne'0x00000000'-or(([Convert]::ToUInt32($pre.f32_cntl.Substring(2),16)-band1)-eq0)){
        throw "Preflight recusado: firmware SDMA ja ativo (checksum=$($pre.ucode_checksum), F32_CNTL=$($pre.f32_cntl))."
    }
    for($i=0;$i-lt$patterns.Count;$i++){$saved+=Read-Vram "destination_pre_$i" ($destination+4*$i)}

    Start-TelemetryWatch
    Run 'arm_write' @('arm','write')|Out-Null;$armed=$true
    Run 'psp_init' @('psp','init')|Out-Null;$pspCreated=$true
    Run 'psp_loadfw_sdma0' @('psp','loadfw','sdma0',$firmware)|Out-Null;$firmwareAccepted=$true
    Run 'sdma_f32' @('sdma','f32')|Out-Null
    Run 'sdma_init' @('sdma','init','vram','0x0FF00000')|Out-Null;$sdmaCreated=$true
    $args=@('sdma','write',('0x{0:X8}'-f$destination))+@($patterns|ForEach-Object{'0x{0:X8}'-f$_})
    $writeProof=Run 'sdma_write_fenced' $args
    $proofText=$writeProof.output-join"`n"
    if($proofText-notmatch'FENCE.+\[ALCANCADO\]' -or $proofText-notmatch'Resultado completo\s+: VALIDADO NESTA OPERACAO'){
        throw 'A saida nao contem fence GPU e resultado completo.'
    }
    Run 'sdma_status_after_submit' @('sdma','status')|Out-Null
    Run 'smu_gfxclk_after' @('smu','0','0x0F','0')|Out-Null
    Run 'smu_active_wgp_after' @('smu','0','0x1E','0')|Out-Null
}catch{$failure=$_.Exception.Message}
finally{
    if($armed){
        if($sdmaCreated){$rb=Run 'sdma_rollback' @('sdma','rollback') -AllowFailure;if($rb.exit_code-ne0-and-not$failure){$failure='Rollback SDMA falhou.'}}
        for($i=0;$i-lt$saved.Count;$i++){Run "destination_restore_$i" @('vram','w',('0x{0:X8}'-f($destination+4*$i)),('0x{0:X8}'-f$saved[$i])) -AllowFailure|Out-Null}
        if($pspCreated){$rb=Run 'psp_rollback' @('psp','rollback') -AllowFailure;if($rb.exit_code-ne0-and-not$failure){$failure='Rollback PSP falhou.'}}
        Run 'arm_readonly' @('arm','readonly') -AllowFailure|Out-Null
    }
    Stop-TelemetryWatch
}
try{
    for($i=0;$i-lt$saved.Count;$i++){$restored+=Read-Vram "destination_post_$i" ($destination+4*$i)}
    if($saved.Count-ne$restored.Count-or(Compare-Object $saved $restored -SyncWindow 0)){if(-not$failure){$failure='Conteudo original do destino nao foi restaurado.'}}
    $post=Read-RegSet 'post'
    # O teardown tem de terminar com o F32 haltado. O firmware continua
    # carregado ate o reboot, mas o microcontrolador nao pode ficar rodando.
    if($post-and(([Convert]::ToUInt32($post.f32_cntl.Substring(2),16)-band1)-eq0)){
        if(-not$failure){$failure=('Teardown deixou o F32 fora de HALT (F32_CNTL={0}).'-f$post.f32_cntl)}
    }
    Run 'psp_status_post' @('psp','status')|Out-Null
    Run 'sdma_status_post' @('sdma','status')|Out-Null
    Run 'smu_q0_test_post' @('smu','0','0x01','0')|Out-Null
}catch{if(-not$failure){$failure=$_.Exception.Message}}

$telemetrySamples=@()
if($telemetryLines.Count-gt1){
    $telemetryLines|Set-Content (Join-Path $dir 'telemetry.csv') -Encoding ASCII
    try{$telemetrySamples=@(($telemetryLines-join"`n")|ConvertFrom-Csv)}catch{if(-not$telemetryFailure){$telemetryFailure=$_.Exception.Message}}
}
$gfxoffHistogram=@()
if($telemetrySamples.Count){$gfxoffHistogram=@($telemetrySamples|Group-Object gfxoff|ForEach-Object{[pscustomobject]@{state=$_.Name;count=$_.Count}})}
$rebootRequired=$firmwareAccepted
if($post-and$post.ucode_checksum-ne'0x00000000'){$rebootRequired=$true}
$record=[pscustomobject]@{
    schema_version=1;captured_at=(Get-Date).ToString('o')
    objective='Minimal SDMA WRITE_LINEAR with physical RPTR/WPTR, GPU fence and CPU comparison'
    verdict=$verdict;passed=(-not$failure);failure=$failure
    limitations=@('Physical GFXOFF register is sampled, but a transition during load must still be observed','Temperature is uncalibrated and physical power telemetry is unavailable','Firmware runtime state persists until reboot')
    reboot_required=$rebootRequired
    environment=[pscustomobject]@{display_service=$device.Service;legacy_bc250v4_absent=$true;driver_sha256=$driverHash.ToLowerInvariant();firmware_sha256=$fwHash}
    destination=('0x{0:X8}'-f$destination);destination_before=@($saved|ForEach-Object{'0x{0:X8}'-f$_});destination_after_restore=@($restored|ForEach-Object{'0x{0:X8}'-f$_})
    pre_registers=$pre;post_registers=$post;commands=$events
    telemetry=[pscustomobject]@{sample_count=$telemetrySamples.Count;gfxoff_state_histogram=$gfxoffHistogram;sampler_error=$telemetryFailure;power_available=$false;file=if($telemetrySamples.Count){'telemetry.csv'}else{$null}}
}
$record|ConvertTo-Json -Depth 9|Set-Content (Join-Path $dir 'result.json') -Encoding UTF8
$events|ForEach-Object{"=== $($_.name) ===";"`$ $($_.command)";$_.output;"exit=$($_.exit_code) timeout=$($_.timed_out)";''}|Set-Content (Join-Path $dir 'result.txt') -Encoding UTF8
Write-Host "Evidencia: $dir"
if($failure){Write-Error $failure;exit 1}
Write-Host 'PASSOU O GATE MINIMO: RPTR/WPTR, fence GPU e 4 dwords conferidos/restaurados.'
Write-Host "Veredito global permanece: $verdict"
Write-Host 'REBOOT NECESSARIO para recuperar o estado de firmware do boot.'
exit 0
