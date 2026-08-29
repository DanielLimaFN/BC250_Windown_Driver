# ==============================================================================
# Script de Limpeza Completa: DriverStore e Servicos da AMD BC-250
# ==============================================================================
[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Execute este script em um PowerShell elevado (Executar como Administrador).'
    }
}

Assert-Administrator

Write-Host "===============================================================" -ForegroundColor Cyan
Write-Host "  INICIANDO LIMPEZA DO SISTEMA (DRIVER STORE & REGISTRO BC-250)" -ForegroundColor Cyan
Write-Host "===============================================================" -ForegroundColor Cyan

# 1. Obter todos os pacotes OEM de terceiros instalados
Write-Host "`n[1/4] Identificando pacotes no DriverStore..." -ForegroundColor Yellow
$rawDrivers = pnputil /enum-drivers
$oemList = @()
$currentOem = ""
$isMatch = $false

foreach ($line in $rawDrivers) {
    if ($line -match "Nome Publicado:\s+(oem\d+\.inf)") {
        if ($currentOem -ne "" -and $isMatch) {
            $oemList += $currentOem
        }
        $currentOem = $matches[1]
        $isMatch = $false
    }
    if ($line -match "(bc250|wddm\.inf|amdbc250|BC250-WDDM|BC250-WIN|AMD BC-250)") {
        $isMatch = $true
    }
}
if ($currentOem -ne "" -and $isMatch) {
    $oemList += $currentOem
}

$oemList = $oemList | Select-Object -Unique

if ($oemList.Count -gt 0) {
    Write-Host "Encontrados $($oemList.Count) pacote(s) para remocao: $($oemList -join ', ')" -ForegroundColor Green
    foreach ($oem in $oemList) {
        Write-Host "Removendo $oem..." -ForegroundColor White
        & pnputil /delete-driver $oem /uninstall /force | Out-Null
    }
} else {
    Write-Host "Nenhum pacote residual encontrado no DriverStore." -ForegroundColor DarkGray
}

# 2. Remover servicos residuais PELO SCM
#
#    NAO apagar HKLM:\SYSTEM\CurrentControlSet\Services\<svc> com Remove-Item.
#
#    Isso quebra o Service Control Manager: a chave some, mas o SCM continua com
#    a referencia. Na reinstalacao seguinte o INF falha em AddService com
#
#        !!! inf: Failed to query service configuration. Error = 0x00000002
#        !!! dvi: Add Service: Failed to get configuration of service 'bc250v4'
#
#    e o pnputil devolve "O sistema nao pode encontrar o arquivo especificado",
#    sem nenhuma pista do motivo real. Aconteceu em 2026-08-22 e custou um ciclo
#    inteiro de depuracao.
#
#    'sc.exe delete' marca o servico para remocao de forma consistente. Se o
#    servico ainda estiver carregado, a remocao so' completa no proximo boot —
#    e o script avisa, em vez de deixar o instalador falhar depois.
Write-Host "`n[2/4] Removendo servicos residuais pelo SCM..." -ForegroundColor Yellow
$services = @('bc250v4', 'bc250v3', 'bc250v2', 'bc250wddm', 'bc250dbg', 'amdbc250kmd')
$rebootNecessario = $false

foreach ($svc in $services) {
    $existeNoScm  = $null -ne (& sc.exe query $svc 2>$null | Select-String -Pattern 'SERVICE_NAME|STATE')
    $existeNoReg  = Test-Path "HKLM:\SYSTEM\CurrentControlSet\Services\$svc"

    if (-not $existeNoScm -and -not $existeNoReg) { continue }

    Write-Host "Removendo servico: $svc" -ForegroundColor White
    $saida = & sc.exe delete $svc 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  [+] $svc removido do SCM" -ForegroundColor Green
    } elseif ($saida -match '1072') {
        Write-Host "  [!] $svc ja' estava marcado para remocao - exige reinicio" -ForegroundColor Yellow
        $rebootNecessario = $true
    } else {
        Write-Host "  [!] sc delete $svc : $saida" -ForegroundColor Yellow
    }

    # Chave sobrevivendo ao sc delete = remocao pendente ate' o proximo boot.
    if (Test-Path "HKLM:\SYSTEM\CurrentControlSet\Services\$svc") {
        $rebootNecessario = $true
    }
}

# 3. Reiniciar o dispositivo grafico para retornar ao Microsoft Basic Display Adapter
Write-Host "`n[3/4] Reiniciando o adaptador grafico para o driver basico do Windows..." -ForegroundColor Yellow
$device = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'PCI\VEN_1002&DEV_13FE*' } | Select-Object -First 1
if ($device) {
    Write-Host "Dispositivo encontrado: $($device.InstanceId)" -ForegroundColor White
    & pnputil /restart-device $device.InstanceId | Out-Null
    Start-Sleep -Seconds 2
    $state = Get-PnpDevice -InstanceId $device.InstanceId
    Write-Host "Status atual do dispositivo: Status=$($state.Status), Problem=$($state.Problem)" -ForegroundColor Cyan
} else {
    Write-Host "Dispositivo BC-250 nao detectado no momento." -ForegroundColor DarkGray
}

# 4. Relatorio Final
Write-Host "`n[4/4] Verificando adaptadores de video restantes no sistema..." -ForegroundColor Yellow
Get-PnpDevice -Class Display | Format-Table FriendlyName, Status, Problem, InstanceId -Wrap

Write-Host "===============================================================" -ForegroundColor Green
Write-Host "  LIMPEZA CONCLUIDA" -ForegroundColor Green
Write-Host "===============================================================" -ForegroundColor Green

if ($rebootNecessario) {
    Write-Host ""
    Write-Host "===============================================================" -ForegroundColor Red
    Write-Host "  REINICIE ANTES DE INSTALAR" -ForegroundColor Red
    Write-Host "===============================================================" -ForegroundColor Red
    Write-Host "Ha' servico(s) marcado(s) para remocao pendente. Instalar agora" -ForegroundColor Yellow
    Write-Host "faz o INF falhar em AddService com erro 0x00000002, e o pnputil" -ForegroundColor Yellow
    Write-Host "devolve apenas 'O sistema nao pode encontrar o arquivo" -ForegroundColor Yellow
    Write-Host "especificado', sem indicar a causa." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  1. Reinicie o computador" -ForegroundColor White
    Write-Host "  2. Execute BC250-V3\package\install-wddm.bat" -ForegroundColor White
    Write-Host ""
    exit 2
}

Write-Host "Nenhuma remocao pendente. Pode instalar em seguida." -ForegroundColor Green
