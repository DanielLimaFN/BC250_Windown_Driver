param (
    [Parameter(Position = 0)]
    [ValidateSet("Host", "Target", "Auto")]
    [string]$Role = "Auto",

    [Parameter(Position = 1)]
    [string]$HostIP = "192.168.0.169",

    [Parameter(Position = 2)]
    [int]$Port = 50000,

    [Parameter(Position = 3)]
    [string]$Key = "1.2.3.4"
)

# 0. Verificacao de privilegios de Administrador
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Warning "Execute este script abrindo o PowerShell como ADMINISTRADOR."
}

Write-Host ""
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "       DIAGNOSTICO E AUDITORIA KDNET (Kernel Debug)     " -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host ""

# 1. Deteccao automatica do papel se Auto
$bcdOutput = bcdedit /enum "{current}" 2>&1 | Out-String
$bcdDbgSettings = bcdedit /enum "{dbgsettings}" 2>&1 | Out-String
$isKernelAdapterPresent = Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object { $_.InterfaceDescription -match "Kernel Debug" -or $_.Name -match "Depurador de Kernel" }

if ($Role -eq "Auto") {
    if ($isKernelAdapterPresent -or ($bcdOutput -match "debug\s+Yes")) {
        $Role = "Target"
    } else {
        $Role = "Host"
    }
}

Write-Host ("Modo Selecionado: [" + $Role + "]") -ForegroundColor Yellow
Write-Host ("Parametros -> Host IP: " + $HostIP + " | Porta: " + $Port + " | Chave: " + $Key)
Write-Host ""

# ---------------------------------------------------------
# MODO HOST (Onde o WinDbg roda)
# ---------------------------------------------------------
if ($Role -eq "Host") {
    Write-Host "--- [1/4] Validando Adaptadores de Rede do Host ---" -ForegroundColor Green
    $localIPs = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -notmatch "^127\." -and $_.InterfaceAlias -notmatch "vEthernet" }
    
    $ipFound = $false
    foreach ($item in $localIPs) {
        if ($item.IPAddress -eq $HostIP) {
            $ipFound = $true
            Write-Host ("[OK] IP " + $HostIP + " encontrado na interface: " + $item.InterfaceAlias) -ForegroundColor Green
        } else {
            Write-Host ("[INFO] Outro IP detectado: " + $item.IPAddress + " (" + $item.InterfaceAlias + ")") -ForegroundColor Gray
        }
    }

    if (-not $ipFound) {
        Write-Host ("[ERRO CRITICO] O IP informado (" + $HostIP + ") NAO pertence a esta maquina Host!") -ForegroundColor Red
        Write-Host " -> Use o IP real retornado acima ao configurar o Target." -ForegroundColor Yellow
    }

    if ($isKernelAdapterPresent) {
        Write-Host ""
        Write-Host "[AVISO GRAVE] Adaptador 'Depurador de Kernel' foi encontrado nesta maquina Host!" -ForegroundColor Red
        Write-Host " -> Esta maquina foi configurada como Target anteriormente." -ForegroundColor Red
        Write-Host " -> Execute como Admin: bcdedit /debug off e reinicie a maquina." -ForegroundColor Yellow
    } else {
        Write-Host "[OK] Nenhum adaptador corrompido de KDNET presente no Host." -ForegroundColor Green
    }

    Write-Host ""
    Write-Host ("--- [2/4] Validando Firewall para Porta UDP " + $Port + " ---") -ForegroundColor Green
    $fwRules = Get-NetFirewallRule -Direction Inbound -Enabled True -ErrorAction SilentlyContinue | 
               Get-NetFirewallPortFilter | 
               Where-Object { $_.Protocol -eq "UDP" -and ($_.LocalPort -contains "$Port" -or $_.LocalPort -eq "Any") }

    if ($fwRules) {
        Write-Host ("[OK] Regra de entrada permitindo UDP na porta " + $Port + " encontrada.") -ForegroundColor Green
    } else {
        Write-Host ("[ALERTA] Nenhuma regra explicita para UDP " + $Port + " no Firewall.") -ForegroundColor Yellow
        Write-Host " -> Criando regra agora..." -ForegroundColor Yellow
        try {
            New-NetFirewallRule -DisplayName ("KDNET-WinDbg-UDP-" + $Port) -Direction Inbound -Protocol UDP -LocalPort $Port -Action Allow | Out-Null
            Write-Host "[OK] Regra criada com sucesso!" -ForegroundColor Green
        } catch {
            Write-Host "[FALHA] Nao foi possivel criar regra. Execute como Admin." -ForegroundColor Red
        }
    }

    Write-Host ""
    Write-Host "--- [3/4] Verificando Processos do WinDbg ---" -ForegroundColor Green
    $dbgProcs = Get-Process -Name "*windbg*" -ErrorAction SilentlyContinue
    if ($dbgProcs) {
        Write-Host ("[INFO] WinDbg ja em execucao (PID: " + ($dbgProcs.Id -join ', ') + ").") -ForegroundColor Cyan
    } else {
        Write-Host "[INFO] Nenhum WinDbg rodando no momento." -ForegroundColor Gray
    }

    Write-Host ""
    Write-Host "--- [4/4] Comando para iniciar o Host ---" -ForegroundColor Green
    Write-Host ("windbgx -k net:port=" + $Port + ",key=" + $Key) -ForegroundColor Magenta
    Write-Host ""
}

# ---------------------------------------------------------
# MODO TARGET (Maquina a ser depurada)
# ---------------------------------------------------------
if ($Role -eq "Target") {
    Write-Host "--- [1/5] Validando Status do BCD ---" -ForegroundColor Green
    
    $debugEnabled = $bcdOutput -match "debug\s+Yes"
    if ($debugEnabled) {
        Write-Host "[OK] Modo Debug habilitado no BCD." -ForegroundColor Green
    } else {
        Write-Host "[ERRO] Modo Debug NAO esta ativo." -ForegroundColor Red
        Write-Host " -> Executar: bcdedit /debug on" -ForegroundColor Yellow
    }

    Write-Host ""
    Write-Host "--- [2/5] Validando DbgSettings de Rede ---" -ForegroundColor Green
    Write-Host $bcdDbgSettings.Trim() -ForegroundColor Gray

    $hasNetType = $bcdDbgSettings -match "debugtype\s+NET"
    $hasHostIP  = $bcdDbgSettings -match ("hostip\s+" + [regex]::Escape($HostIP))
    $hasPort    = $bcdDbgSettings -match ("port\s+" + $Port)
    $hasKey     = $bcdDbgSettings -match ("key\s+" + [regex]::Escape($Key))

    if (-not $hasNetType) { Write-Host "[ERRO] debugtype nao esta definido como NET." -ForegroundColor Red }
    if (-not $hasHostIP)  { Write-Host ("[ERRO] hostip nao aponta para " + $HostIP + ".") -ForegroundColor Red }
    if (-not $hasPort)    { Write-Host ("[ERRO] port nao esta configurado como " + $Port + ".") -ForegroundColor Red }
    if (-not $hasKey)     { Write-Host ("[ERRO] key nao corresponde a " + $Key + ".") -ForegroundColor Red }

    if ($hasNetType -and $hasHostIP -and $hasPort -and $hasKey) {
        Write-Host "[OK] Parametros de dbgsettings conferem com o Host!" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "[ACAO] Execute no Target para corrigir:" -ForegroundColor Yellow
        Write-Host ("bcdedit /dbgsettings net hostip:" + $HostIP + " port:" + $Port + " key:" + $Key) -ForegroundColor Magenta
    }

    Write-Host ""
    Write-Host "--- [3/5] Verificando Adaptador KDNET ---" -ForegroundColor Green
    if ($isKernelAdapterPresent) {
        Write-Host ("[OK] Adaptador detectado: " + $isKernelAdapterPresent.Name) -ForegroundColor Green
        Write-Host ("     Status: " + $isKernelAdapterPresent.Status + " | Velocidade: " + $isKernelAdapterPresent.LinkSpeed) -ForegroundColor Gray
    } else {
        Write-Host "[ALERTA] Nenhum adaptador de Depurador de Kernel ativo." -ForegroundColor Yellow
    }

    Write-Host ""
    Write-Host "--- [4/5] Checando Fast Startup (Inicializacao Rapida) ---" -ForegroundColor Green
    $hiberEnabled = (Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power" -Name "HiberbootEnabled" -ErrorAction SilentlyContinue).HiberbootEnabled
    if ($hiberEnabled -eq 1) {
        Write-Host "[FALHA CRITICA] Fast Startup esta ATIVADO!" -ForegroundColor Red
        Write-Host " -> Desative executando: powercfg /h off" -ForegroundColor Yellow
    } else {
        Write-Host "[OK] Fast Startup desativado." -ForegroundColor Green
    }

    Write-Host ""
    Write-Host ("--- [5/5] Teste de Rota/Ping para o Host (" + $HostIP + ") ---") -ForegroundColor Green
    $pingTest = Test-Connection -ComputerName $HostIP -Count 2 -Quiet -ErrorAction SilentlyContinue
    if ($pingTest) {
        Write-Host "[OK] Host responde ao ping normalmente." -ForegroundColor Green
    } else {
        Write-Host "[AVISO] Sem resposta de ping do Host (pode ser ICMP bloqueado no firewall do Host)." -ForegroundColor Yellow
    }
    Write-Host ""
}

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "                 FIM DO DIAGNOSTICO                     " -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host ""