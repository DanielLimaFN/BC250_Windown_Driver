$runnerScript = "C:\Users\Daniel\Desktop\NamixWDDM\tools\_bind_runner.ps1"
$argList = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $runnerScript
)

Write-Host "Invocando bind-bc250mcdm-lab.ps1 elevado..." -ForegroundColor Cyan
$p = Start-Process powershell.exe -ArgumentList $argList -Verb RunAs -Wait -PassThru
Write-Host "Processo finalizado com ExitCode: $($p.ExitCode)" -ForegroundColor $(if ($p.ExitCode -eq 0) { "Green" } else { "Yellow" })
