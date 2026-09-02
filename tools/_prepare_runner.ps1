# _prepare_runner.ps1
$ErrorActionPreference = "Continue"
Start-Transcript -Path "C:\Users\Daniel\Desktop\NamixWDDM\tools\_prepare_rendernode.log" -Force
try {
    & "C:\Users\Daniel\Desktop\NamixWDDM\tools\prepare-bc250mcdm-rendernode.ps1" `
        -Execute `
        -IAcceptDisplayRenderBootRisk
} catch {
    Write-Error $_
}
Stop-Transcript
