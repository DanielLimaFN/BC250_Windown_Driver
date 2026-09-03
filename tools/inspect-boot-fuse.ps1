<#
.SYNOPSIS
    Inspeciona, valida, arma ou reseta o fusivel anti-boot-loop persistente da BC-250.

.DESCRIPTION
    Le o arquivo binario C:\BC250_BOOT_FUSE.dat usado pelo miniport bc250mcdm.sys
    para impedir ciclos de reinicializacao em caso de falha no StartDevice ou travamento.

.PARAMETER Path
    Caminho do arquivo do fusivel. Padrao: C:\BC250_BOOT_FUSE.dat

.PARAMETER Reset
    Reseta o fusivel para o estado CLEAN (State = 0).

.PARAMETER Arm
    Simula o estado ARMED (State = 1) para ensaios offline.

.PARAMETER Trip
    Forca o estado TRIPPED (State = 2) para ensaios de recusa de boot.

.PARAMETER Status
    NTSTATUS a ser gravado (padrao: 0 para Reset/Arm, 0xC0000001 para Trip).
#>
[CmdletBinding()]
param(
    [string]$Path = 'C:\BC250_BOOT_FUSE.dat',
    [switch]$Reset,
    [switch]$Arm,
    [switch]$Trip,
    [int64]$Status = 0
)

$ErrorActionPreference = 'Stop'

function Compute-Bc250FuseChecksum([byte[]]$Bytes) {
    # Checksum aditivo dos primeiros 32 bytes (8 inteiros de 32 bits little-endian)
    $sum = [uint32]0x5A5A5A5A
    for ($i = 0; $i -lt 32; $i += 4) {
        $val = [BitConverter]::ToUInt32($Bytes, $i)
        $sum = [uint32]((($sum -shl 3) -bor ($sum -shr 29)) -bxor $val)
    }
    return $sum
}

function Create-Bc250FuseRecord([uint32]$State, [uint32]$BootCount, [uint32]$LastStatus) {
    $bytes = New-Object byte[] 40
    # Magic: 'FUSE' = 0x45535546
    [BitConverter]::GetBytes([uint32]0x45535546).CopyTo($bytes, 0)
    # Version: 1
    [BitConverter]::GetBytes([uint32]1).CopyTo($bytes, 4)
    # State
    [BitConverter]::GetBytes([uint32]$State).CopyTo($bytes, 8)
    # BootCount
    [BitConverter]::GetBytes([uint32]$BootCount).CopyTo($bytes, 12)
    # LastStatus
    [BitConverter]::GetBytes([uint32]$LastStatus).CopyTo($bytes, 16)
    # Flags: 0
    [BitConverter]::GetBytes([uint32]0).CopyTo($bytes, 20)
    # Timestamp: Windows FileTime (UTC)
    $fileTime = (Get-Date).ToFileTimeUtc()
    [BitConverter]::GetBytes([int64]$fileTime).CopyTo($bytes, 24)
    # Reserved: 0 (offset 36)
    [BitConverter]::GetBytes([uint32]0).CopyTo($bytes, 36)
    # Checksum: offset 32
    $chk = Compute-Bc250FuseChecksum $bytes
    [BitConverter]::GetBytes([uint32]$chk).CopyTo($bytes, 32)
    return $bytes
}

$statusU32 = [uint32]($Status -band 0xFFFFFFFFL)

if ($Reset) {
    $recordBytes = Create-Bc250FuseRecord 0 0 $statusU32
    [System.IO.File]::WriteAllBytes($Path, $recordBytes)
    Write-Host "[FUSE] Arquivo $Path resetado para estado CLEAN (0)." -ForegroundColor Green
} elseif ($Arm) {
    $recordBytes = Create-Bc250FuseRecord 1 1 $statusU32
    [System.IO.File]::WriteAllBytes($Path, $recordBytes)
    Write-Host "[FUSE] Arquivo $Path gravado como ARMED (1) para ensaio." -ForegroundColor Yellow
} elseif ($Trip) {
    $tripStatus = if ($Status -ne 0) { $statusU32 } else { [uint32]0xC0000001L }
    $recordBytes = Create-Bc250FuseRecord 2 1 $tripStatus
    [System.IO.File]::WriteAllBytes($Path, $recordBytes)
    Write-Host "[FUSE] Arquivo $Path forcado para TRIPPED (2) com Status 0x$($tripStatus.ToString('X8'))." -ForegroundColor Red
}

Write-Host "=== BC-250 BOOT FUSE INSPECTOR ===" -ForegroundColor Cyan
Write-Host "Arquivo: $Path"

if (-not (Test-Path -LiteralPath $Path)) {
    Write-Host "Estado: ARQUIVO INEXISTENTE (Primeiro boot assumira CLEAN)" -ForegroundColor DarkYellow
    return
}

$raw = [System.IO.File]::ReadAllBytes($Path)
if ($raw.Length -ne 40) {
    Write-Host "ERRO: Tamanho invalido ($($raw.Length) bytes, esperado 40 bytes) - CORROMPIDO!" -ForegroundColor Red
    return
}

$magic = [BitConverter]::ToUInt32($raw, 0)
$version = [BitConverter]::ToUInt32($raw, 4)
$stateVal = [BitConverter]::ToUInt32($raw, 8)
$bootCount = [BitConverter]::ToUInt32($raw, 12)
$lastStatus = [BitConverter]::ToUInt32($raw, 16)
$flags = [BitConverter]::ToUInt32($raw, 20)
$fileTime = [BitConverter]::ToInt64($raw, 24)
$checksumRead = [BitConverter]::ToUInt32($raw, 32)
$checksumCalc = Compute-Bc250FuseChecksum $raw

$magicStr = if ($magic -eq 0x45535546) { 'FUSE (OK)' } else { "0x$($magic.ToString('X8')) (INVALIDO)" }
$stateStr = switch ($stateVal) {
    0 { 'CLEAN (0) - Desligamento limpo' }
    1 { 'ARMED (1) - Em execucao / Sujo se persistente' }
    2 { 'TRIPPED (2) - FUSIVEL DISPARADO (Bloqueia StartDevice)' }
    default { "DESCONHECIDO ($stateVal)" }
}

$timeStr = try { [DateTime]::FromFileTimeUtc($fileTime).ToLocalTime().ToString('yyyy-MM-dd HH:mm:ss') } catch { 'Invalido' }
$chkStr = if ($checksumRead -eq $checksumCalc) { "0x$($checksumRead.ToString('X8')) (VALIDO)" } else { "0x$($checksumRead.ToString('X8')) != 0x$($checksumCalc.ToString('X8')) (CORROMPIDO)" }

Write-Host "  Magic:        $magicStr"
Write-Host "  Versao:       $version"
Write-Host "  Estado:       $stateStr" -ForegroundColor $(if ($stateVal -eq 0) { 'Green' } elseif ($stateVal -eq 1) { 'Yellow' } else { 'Red' })
Write-Host "  Boot Count:   $bootCount"
Write-Host "  Last Status:  0x$($lastStatus.ToString('X8'))"
Write-Host "  Timestamp:    $timeStr"
Write-Host "  Checksum:     $chkStr"
