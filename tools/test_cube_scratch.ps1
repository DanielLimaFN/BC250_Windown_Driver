$env:VK_ICD_FILENAMES = "c:\Users\Daniel\Desktop\NamixWDDM\tools\bc250-radv-test-icd.json"
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "c:\Users\Daniel\Desktop\bc250-win\bc250-win\Vulkan-Cube\VulkanCube.exe"
$psi.Arguments = "--frames 1"
$psi.WorkingDirectory = "c:\Users\Daniel\Desktop\bc250-win\bc250-win\Vulkan-Cube"
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
