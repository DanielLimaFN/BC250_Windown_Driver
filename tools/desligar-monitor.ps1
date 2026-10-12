# Corrida 272 (doc 713), DCN.2 item 6: pede ao Windows para desligar o monitor
# (SC_MONITORPOWER = 2). O Windows chama DxgkDdiSetPowerState(filho 1, D3) e o
# driver apaga o OTG0. Mexer o mouse ou apertar uma tecla acende de novo (D0).
# So' envia a mensagem de sistema; nao toca no driver nem no registro.
Add-Type -Namespace Bc250 -Name Monitor -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern System.IntPtr SendMessage(System.IntPtr hWnd, uint msg, System.IntPtr wParam, System.IntPtr lParam);
'@
Write-Host ("{0:HH:mm:ss} desligando o monitor; mexa o mouse depois de ~5 s para acender" -f (Get-Date))
Start-Sleep -Seconds 1
[void][Bc250.Monitor]::SendMessage([System.IntPtr]0xFFFF, 0x0112, [System.IntPtr]0xF170, [System.IntPtr]2)
