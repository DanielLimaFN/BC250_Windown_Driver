param([string]$OutputRoot, [ValidateRange(2,30)][int]$CommandTimeoutSeconds = 8)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
$device = Get-PnpDevice -PresentOnly | Where-Object InstanceId -like 'PCI\VEN_1002&DEV_13FE*' | Select-Object -First 1
if (-not $device -or $device.Service -ne 'BasicDisplay') { throw 'BC-250 nao esta em BasicDisplay.' }
if (Get-Service bc250v4 -ErrorAction SilentlyContinue) { throw 'bc250v4 ainda existe.' }
if ((Get-Service bc250dbg).Status -ne 'Running') { throw 'bc250dbg parado.' }
$hash = (Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if ($hash -ne (Get-FileHash $installedDriver -Algorithm SHA256).Hash) { throw 'Build instalado diverge.' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-psp-rollback"
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$events = [System.Collections.Generic.List[object]]::new()

function Run([string]$Name,[string[]]$Arguments,[switch]$AllowFailure) {
    $psi=New-Object Diagnostics.ProcessStartInfo
    $psi.FileName=$cli
    $psi.Arguments=(($Arguments|ForEach-Object{'"'+$_.Replace('"','\"')+'"'}) -join ' ')
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true
    $psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    $p=New-Object Diagnostics.Process;$p.StartInfo=$psi
    if(-not $p.Start()){throw "Falha ao iniciar $Name"}
    $ot=$p.StandardOutput.ReadToEndAsync();$et=$p.StandardError.ReadToEndAsync()
    $to=-not $p.WaitForExit($CommandTimeoutSeconds*1000)
    if($to){$p.Kill();$p.WaitForExit()}
    $ec=if($to){1460}else{$p.ExitCode}
    $r=[pscustomobject]@{name=$Name;command='bc250ctl.exe '+($Arguments-join ' ');exit_code=$ec;timed_out=$to;output=@(($ot.Result+$et.Result)-split "`r?`n"|Where-Object{$_})}
    $events.Add($r)
    if(($to-or $ec-ne 0)-and-not $AllowFailure){throw "$Name falhou: exit=$ec timeout=$to"}
    return $r
}

$regs=[ordered]@{c2pmsg64='0x58200';wptr='0x5820C';ring_lo='0x58214';ring_hi='0x58218';ring_size='0x5821C'}
function ReadSet([string]$Prefix){$s=[ordered]@{};foreach($e in $regs.GetEnumerator()){$r=Run "$Prefix`_$($e.Key)" @('reg','r',$e.Value);$t=$r.output-join ' ';if($t-notmatch '=\s*0x([0-9A-Fa-f]{8})'){throw "parse $($e.Key)"};$s[$e.Key]='0x'+$Matches[1].ToUpperInvariant()};[pscustomobject]$s}

$pre=$null;$post=$null;$failure=$null;$armed=$false
try {
    Run 'info_pre' @('info')|Out-Null
    Run 'psp_status_pre' @('psp','status')|Out-Null
    $pre=ReadSet 'pre'
    if($pre.wptr-ne'0x00000000'-or$pre.ring_lo-ne'0x00000000'-or$pre.ring_hi-ne'0x00000000'-or$pre.ring_size-ne'0x00000000'){throw 'Ring PSP externo presente.'}
    Run 'arm_write' @('arm','write')|Out-Null;$armed=$true
    Run 'psp_init' @('psp','init')|Out-Null
    Run 'psp_status_armed' @('psp','status')|Out-Null
} catch {$failure=$_.Exception.Message}
finally {
    if($armed){$rb=Run 'psp_rollback' @('psp','rollback') -AllowFailure;if($rb.exit_code-ne 0-and-not $failure){$failure='rollback PSP falhou'};Run 'arm_readonly' @('arm','readonly') -AllowFailure|Out-Null}
}
try {$post=ReadSet 'post';Run 'psp_status_post' @('psp','status')|Out-Null}catch{if(-not $failure){$failure=$_.Exception.Message}}
$diff=@();$c2pmsg64Transition=$null
if($pre-and$post){
    foreach($n in $regs.Keys){
        if($n-eq'c2pmsg64'){
            # ring_stop() do PSP v11 deixa o ID DESTROY_RINGS nos bits de tipo.
            # Para rollback sem ring, o que importa aqui e preservar ACK/status
            # (bit 31 e bits 15:0); nao inventamos uma escrita para apagar o eco.
            $before=[Convert]::ToUInt32($pre.$n.Substring(2),16)
            $after=[Convert]::ToUInt32($post.$n.Substring(2),16)
            $mask=[uint32]0x8000FFFF
            $c2pmsg64Transition=[pscustomobject]@{
                before=$pre.$n;after=$post.$n;comparison_mask='0x8000FFFF'
                destroy_type_echo_allowed=$true
            }
            if(($before-band$mask)-ne($after-band$mask)){
                $diff+=[pscustomobject]@{register=$n;before=$pre.$n;after=$post.$n;mask='0x8000FFFF'}
            }
        }elseif($pre.$n-ne$post.$n){
            $diff+=[pscustomobject]@{register=$n;before=$pre.$n;after=$post.$n;mask='0xFFFFFFFF'}
        }
    }
}
if($diff.Count-and-not $failure){$failure="$($diff.Count) registradores divergiram"}
$verdict=('N{0}O VALIDADO EM HARDWARE'-f[char]0x00C3)
[pscustomobject]@{schema_version=2;captured_at=(Get-Date).ToString('o');objective='PSP GPCOM create/destroy semantic rollback only; no firmware load';verdict=$verdict;passed=(-not $failure);failure=$failure;driver_sha256=$hash.ToLowerInvariant();c2pmsg64_transition=$c2pmsg64Transition;pre=$pre;post=$post;differences=$diff;commands=$events}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'rollback.json') -Encoding UTF8
$events|ForEach-Object{"=== $($_.name) ===";"`$ $($_.command)";$_.output;"exit=$($_.exit_code) timeout=$($_.timed_out)";''}|Set-Content (Join-Path $dir 'rollback.txt') -Encoding UTF8
Write-Host "Evidencia: $dir"
if($failure){Write-Error $failure;exit 1}
Write-Host 'PASSOU: PSP init/destroy e rollback sem ring; eco DESTROY em C2PMSG64 e permitido.'
exit 0
