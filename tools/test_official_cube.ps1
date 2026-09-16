$env:VK_ICD_FILENAMES = "c:\Users\Daniel\Desktop\NamixWDDM\tools\bc250-radv-test-icd.json"
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "C:\VulkanSDK\1.4.357.0\Bin\vkcube.exe"
$psi.Arguments = "--c 1"
$psi.WorkingDirectory = "C:\VulkanSDK\1.4.357.0\Bin"
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$p = [System.Diagnostics.Process]::Start($psi)
$out = $p.StandardOutput.ReadToEnd()
$err = $p.StandardError.ReadToEnd()
$p.WaitForExit()
Write-Host "ExitCode: $($p.ExitCode)"
Write-Host "STDOUT:`n$out"
Write-Host "STDERR:`n$err"
