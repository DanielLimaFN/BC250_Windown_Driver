$subject = 'CN=BC250-WIN Test'
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $subject } | Select-Object -First 1
if ($cert) {
    Write-Host "Certificado ja existe: $($cert.Thumbprint)"
} else {
    Write-Host "Criando certificado..."
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $subject `
        -CertStoreLocation Cert:\CurrentUser\My `
        -KeyUsage DigitalSignature `
        -KeyExportPolicy Exportable `
        -NotAfter (Get-Date).AddYears(5)
    Write-Host "Criado com sucesso: $($cert.Thumbprint)"
}
$cer = Join-Path $PSScriptRoot 'bc250-test.cer'
Export-Certificate -Cert $cert -FilePath $cer -Force | Out-Null
Write-Host "Exportado para: $cer"
