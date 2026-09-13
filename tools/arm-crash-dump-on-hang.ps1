# =============================================================================
# C3.13 - Dump em TRAVAMENTO (nao em BugCheck)
#
# POR QUE EXISTE:
#   Na corrida 12/09 16:31 a maquina CONGELOU e exigiu reset fisico. Nao houve
#   BugCheck, logo nao houve dump: C:\Windows\MEMORY.DMP continuou sendo o das
#   13:12. Ficamos sem nenhuma forma de nomear a causa, e essa foi a SEGUNDA vez
#   no dia em que um travamento nos deixou cegos.
#
#   Dump so' nasce de BugCheck. Num congelamento o sistema nao chega a
#   BugCheckar sozinho -- e preciso FORCAR, e a unica forma pratica sem hardware
#   extra e' o crash iniciado pelo teclado:
#
#       Ctrl DIREITO segurado + Scroll Lock duas vezes
#       -> BugCheck 0xE2 (MANUALLY_INITIATED_CRASH) + dump gravado
#
#   Isso exige CrashOnCtrlScroll=1 no driver do teclado e REBOOT para valer.
#
# O QUE ESTE SCRIPT MUDA (somente com -Arm, e exige elevacao):
#   1. CrashOnCtrlScroll=1 em i8042prt (PS/2) e kbdhid (USB) -- os dois, porque
#      qual esta em uso depende do teclado ligado na hora.
#   2. CrashDumpEnabled: 2 (kernel) por padrao, ou 1 (completo) com -Complete.
#      Kernel basta para hang de driver e nao exige pagefile do tamanho da RAM.
#   3. AlwaysKeepMemoryDump=1 -- impede que o Windows APAGUE o dump por espaco.
#
# O QUE ELE NAO FAZ:
#   Nao mexe em pagefile. Dump COMPLETO exige pagefile em C: maior que a RAM
#   (aqui ~15,2 GB); se voce pedir -Complete, o script AVISA e nao ajusta nada.
#
# ATENCAO: sao configuracoes de SISTEMA. Rode elevado e de forma deliberada.
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Arm,
    [switch]$Disarm,
    [switch]$Complete
)

$ErrorActionPreference = 'Stop'

$crashKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl'
$kbdKeys = @(
    'HKLM:\SYSTEM\CurrentControlSet\Services\i8042prt\Parameters',
    'HKLM:\SYSTEM\CurrentControlSet\Services\kbdhid\Parameters'
)

function Test-Elevated {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Show-State {
    $c = Get-ItemProperty -LiteralPath $crashKey -ErrorAction SilentlyContinue
    $map = @{ 0 = 'nenhum'; 1 = 'COMPLETO'; 2 = 'kernel'; 3 = 'small'; 7 = 'automatico' }
    $t = if ($null -ne $c.CrashDumpEnabled) { $map[[int]$c.CrashDumpEnabled] } else { '?' }
    Write-Host ("  CrashDumpEnabled     : {0} ({1})" -f $c.CrashDumpEnabled, $t)
    Write-Host ("  AlwaysKeepMemoryDump : {0}" -f $(if ($null -eq $c.AlwaysKeepMemoryDump) { 'ausente (o Windows pode apagar o dump)' } else { $c.AlwaysKeepMemoryDump }))
    Write-Host ("  DumpFile             : {0}" -f $c.DumpFile)
    foreach ($k in $kbdKeys) {
        $name = Split-Path (Split-Path $k -Parent) -Leaf
        if (-not (Test-Path -LiteralPath $k)) {
            Write-Host ("  CrashOnCtrlScroll[{0}] : chave ausente" -f $name)
            continue
        }
        $v = (Get-ItemProperty -LiteralPath $k -ErrorAction SilentlyContinue).CrashOnCtrlScroll
        $s = if ($null -eq $v) { 'ausente -> travamento NAO gera dump' }
             elseif ([int]$v -eq 1) { '1 -> Ctrl DIREITO + Scroll Lock x2 forca dump' }
             else { "$v -> desarmado" }
        Write-Host ("  CrashOnCtrlScroll[{0}] : {1}" -f $name, $s)
    }
}

Write-Host '=== C3.13 dump em travamento ==='

if ($Arm -and $Disarm) { throw 'Escolha -Arm ou -Disarm, nao os dois.' }

if (-not $Arm -and -not $Disarm) {
    Write-Host 'Estado atual:'
    Show-State
    Write-Host ''
    Write-Host 'Para armar   : -Arm            (kernel dump + crash por teclado)'
    Write-Host 'Dump completo: -Arm -Complete  (exige pagefile em C: maior que a RAM)'
    Write-Host 'Para desarmar: -Disarm'
    Write-Host 'Exige PowerShell ELEVADO e REBOOT para o teclado passar a valer.'
    exit 0
}

if (-not (Test-Elevated)) {
    throw 'Este script muda configuracao de sistema: rode em PowerShell ELEVADO.'
}

if ($Disarm) {
    foreach ($k in $kbdKeys) {
        if (Test-Path -LiteralPath $k) {
            Set-ItemProperty -LiteralPath $k -Name 'CrashOnCtrlScroll' -Value 0 -Type DWord -Force
        }
    }
    Set-ItemProperty -LiteralPath $crashKey -Name 'CrashDumpEnabled' -Value 7 -Type DWord -Force
    Write-Host 'Desarmado: CrashDumpEnabled volta a 7 (automatico) e o crash por teclado sai.'
    Show-State
    Write-Host 'Reboot para o teclado voltar ao normal.'
    exit 0
}

$dumpType = if ($Complete) { 1 } else { 2 }
if ($Complete) {
    $ram = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
    Write-Host ('  AVISO: dump COMPLETO exige pagefile em C: maior que a RAM ({0:N1} GB).' -f ($ram / 1GB)) -ForegroundColor Yellow
    Write-Host '  Este script NAO ajusta pagefile. Se ele for menor, o Windows grava dump menor ou nenhum.' -ForegroundColor Yellow
}

Set-ItemProperty -LiteralPath $crashKey -Name 'CrashDumpEnabled' -Value $dumpType -Type DWord -Force
Set-ItemProperty -LiteralPath $crashKey -Name 'AlwaysKeepMemoryDump' -Value 1 -Type DWord -Force

$armed = 0
foreach ($k in $kbdKeys) {
    if (-not (Test-Path -LiteralPath $k)) {
        New-Item -Path $k -Force | Out-Null
    }
    Set-ItemProperty -LiteralPath $k -Name 'CrashOnCtrlScroll' -Value 1 -Type DWord -Force
    $readback = (Get-ItemPropertyValue -LiteralPath $k -Name 'CrashOnCtrlScroll')
    if ([int]$readback -ne 1) { throw "Readback falhou em $k" }
    $armed++
}

Write-Host ''
Write-Host 'ARMADO. Estado gravado e conferido por readback:'
Show-State
Write-Host ''
Write-Host ("  teclados armados: {0}" -f $armed)
Write-Host '  REBOOT e' -NoNewline; Write-Host ' OBRIGATORIO' -ForegroundColor Yellow -NoNewline
Write-Host ' -- o driver do teclado le CrashOnCtrlScroll so no start.'
Write-Host ''
Write-Host 'NA PROXIMA VEZ QUE A MAQUINA CONGELAR:' -ForegroundColor Cyan
Write-Host '  segure o Ctrl DIREITO e pressione Scroll Lock DUAS vezes.'
Write-Host '  A maquina vai BugCheckar em 0xE2 e gravar C:\Windows\MEMORY.DMP.'
Write-Host '  Isso e o dump que faltou na corrida 16:31.'
Write-Host ''
Write-Host 'NAO VALIDADO EM HARDWARE (configuracao, nao medicao).'
