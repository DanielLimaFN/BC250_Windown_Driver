# Corrida 295 (doc 754): reinicia a Pesquisa depois do bind, para o WebView2 dela nascer na BC-250.
#
# Por que: na Corrida 294 o WebView2 da Pesquisa (Chromium) nasceu as 10:36:56, antes do bind
# (10:53:05), no adaptador basico. O Chromium escolhe a GPU quando nasce e nao troca depois:
# nenhum msedgewebview2 carregou a nossa UMD, e a Pesquisa da 294 nao mediu o nosso driver.
#
# O que faz (so processos do usuario, nada no driver):
#   1. espera a BC-250 (PCI 1002:13FE) estar com o servico bc250mcdm e Status OK (teto 240 s);
#   2. espera mais 20 s (desktop e DWM no nosso driver);
#   3. encerra SearchApp.exe/SearchHost.exe e os msedgewebview2.exe do pacote da Pesquisa;
#   4. espera 15 s e registra quem voltou (a Pesquisa renasce ao ser aberta).
# Log: C:\BC250_PESQUISA_REINICIO.log (o EXECUTAR copia para a pasta de evidencia).
[CmdletBinding()]
param(
    [int]$TetoBindSegundos = 240,
    [int]$EsperaDesktopSegundos = 20
)

$ErrorActionPreference = 'Continue'
$log = 'C:\BC250_PESQUISA_REINICIO.log'
function Log([string]$m) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $m
    Write-Host $line
    Add-Content -LiteralPath $log -Value $line -Encoding utf8
}
Set-Content -LiteralPath $log -Value '' -Encoding utf8
Log "PESQUISA_REINICIO: inicio (teto do bind $TetoBindSegundos s, espera do desktop $EsperaDesktopSegundos s)"

function Get-Bc250Bound {
    $dev = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -match 'VEN_1002&DEV_13FE' } | Select-Object -First 1
    if (-not $dev) { return $null }
    $svc = (Get-PnpDeviceProperty -InstanceId $dev.InstanceId -KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue).Data
    [pscustomobject]@{ Status = $dev.Status; Service = $svc; InstanceId = $dev.InstanceId }
}

$t0 = Get-Date
$bound = $null
while (((Get-Date) - $t0).TotalSeconds -lt $TetoBindSegundos) {
    $b = Get-Bc250Bound
    if ($b -and $b.Service -eq 'bc250mcdm' -and $b.Status -eq 'OK') { $bound = $b; break }
    Start-Sleep -Seconds 2
}
if (-not $bound) {
    $b = Get-Bc250Bound
    Log ("PESQUISA_REINICIO: DESISTIU -- a BC-250 nao ficou em bc250mcdm/OK em {0} s (agora: servico={1} status={2})" -f
        $TetoBindSegundos, $(if ($b) { $b.Service } else { '?' }), $(if ($b) { $b.Status } else { '?' }))
    exit 2
}
Log ("PESQUISA_REINICIO: BC-250 em bc250mcdm/OK apos {0:N0} s; esperando {1} s pelo desktop" -f ((Get-Date) - $t0).TotalSeconds, $EsperaDesktopSegundos)
Start-Sleep -Seconds $EsperaDesktopSegundos

$pkg = 'Microsoft.Windows.Search_cw5n1h2txyewy'
function Get-SearchProcs {
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -in @('SearchApp.exe', 'SearchHost.exe') -or
        ($_.Name -eq 'msedgewebview2.exe' -and [string]$_.CommandLine -match [regex]::Escape($pkg))
    }
}
$antes = @(Get-SearchProcs)
foreach ($p in $antes) {
    Log ("PESQUISA_REINICIO: antes pid={0} {1} criado={2}" -f $p.ProcessId, $p.Name, $p.CreationDate)
}
if ($antes.Count -eq 0) {
    Log 'PESQUISA_REINICIO: nenhum processo da Pesquisa vivo; ela nasce na proxima abertura (ja no nosso driver)'
} else {
    foreach ($p in $antes) {
        try {
            Stop-Process -Id $p.ProcessId -Force -ErrorAction Stop
            Log ("PESQUISA_REINICIO: encerrado pid={0} {1}" -f $p.ProcessId, $p.Name)
        } catch {
            Log ("PESQUISA_REINICIO: falhou encerrar pid={0} {1}: {2}" -f $p.ProcessId, $p.Name, $_.Exception.Message)
        }
    }
}
Start-Sleep -Seconds 15
$depois = @(Get-SearchProcs)
foreach ($p in $depois) {
    Log ("PESQUISA_REINICIO: depois pid={0} {1} criado={2}" -f $p.ProcessId, $p.Name, $p.CreationDate)
}
Log ("PESQUISA_REINICIO: FEITO -- {0} encerrados, {1} vivos agora. Abra a Pesquisa: o WebView2 nasce no nosso driver." -f $antes.Count, $depois.Count)
exit 0
