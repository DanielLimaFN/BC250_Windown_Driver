# make-testcert.ps1 — cria o certificado de teste e o torna confiavel na maquina.
#
# Rodar UMA VEZ, como Administrador, antes do primeiro install.
#
# Por que isto e necessario: `bcdedit /set testsigning on` nao faz o Windows
# aceitar qualquer assinatura. Ele continua exigindo que o certificado que
# assinou o driver encadeie ate uma raiz confiavel DA MAQUINA. Um certificado
# auto-assinado e a propria raiz dele — entao precisa ser importado em
# LocalMachine\Root (e em TrustedPublisher, para o carregamento nao parar em
# prompt). Sem isso o driver compila, assina e mesmo assim falha ao carregar,
# com um erro que nao diz que o problema e confianca.
#
# SPDX-License-Identifier: MIT

$ErrorActionPreference = 'Stop'
$subject = 'CN=BC250-WIN Test'

# precisa de elevacao para escrever nos armazenamentos LocalMachine
$admin = (New-Object Security.Principal.WindowsPrincipal(
            [Security.Principal.WindowsIdentity]::GetCurrent())
         ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    Write-Host "ERRO: rode como Administrador." -ForegroundColor Red
    exit 1
}

$cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq $subject } |
        Select-Object -First 1

if ($cert) {
    Write-Host "certificado ja existe: $($cert.Thumbprint)"
} else {
    Write-Host "criando certificado de teste..."
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $subject `
        -CertStoreLocation Cert:\CurrentUser\My `
        -KeyUsage DigitalSignature `
        -KeyExportPolicy Exportable `
        -NotAfter (Get-Date).AddYears(5)
    Write-Host "criado: $($cert.Thumbprint)"
}

$cer = Join-Path $PSScriptRoot 'bc250-test.cer'
Export-Certificate -Cert $cert -FilePath $cer -Force | Out-Null

Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
Write-Host "importado em LocalMachine\Root"

Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
Write-Host "importado em LocalMachine\TrustedPublisher"

Write-Host ""
Write-Host "OK. Agora: dbg\build.bat  e depois  dbg\install.bat" -ForegroundColor Green
Write-Host ""
Write-Host "Lembre que o testsigning tambem precisa estar ligado:"
Write-Host "  bcdedit /set testsigning on   (e reiniciar)"
