Get-CimInstance Win32_DeviceMemoryAddress | Where-Object { $_.Name -like "*Video*" -or $_.Name -like "*Display*" -or $_.Name -like "*PCI*" } | Format-List Name, StartingAddress, EndingAddress
