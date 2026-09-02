# _bind_runner.ps1
$ErrorActionPreference = "Continue"
Start-Transcript -Path "C:\Users\Daniel\Desktop\NamixWDDM\tools\_elevated_bind_latest.log" -Force
try {
    & "C:\Users\Daniel\Desktop\NamixWDDM\tools\bind-bc250mcdm-lab.ps1" `
        -Execute `
        -IAcceptBlackScreenRisk `
        -EnableShaderDispatch `
        -EnablePersistentSubmission `
        -IAcceptDirtyEngine `
        -DisableWatchdog `
        -IAcceptNoAutomaticRollback
} catch {
    Write-Error $_
}
Stop-Transcript
