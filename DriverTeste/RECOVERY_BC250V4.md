# Recuperação do driver de vídeo BC-250 legado

O isolamento cria um backup em `recovery\bc250v4-<data>`, contendo o pacote
completo exportado, manifesto, registro do serviço e uma cópia do restaurador.

Se o Microsoft Basic Display Adapter funcionar, restaure quando necessário em
um PowerShell elevado:

```powershell
cd C:\Users\Daniel\Desktop\NamixWDDM
powershell -ExecutionPolicy Bypass -File tools\restore-legacy-driver.ps1 -Reboot
```

Se a tela ficar preta, entre no Modo de Segurança do Windows, abra PowerShell
como administrador e execute o mesmo comando. Alternativamente, entre na pasta
de backup mais recente e execute a cópia local:

```powershell
powershell -ExecutionPolicy Bypass -File .\restore-legacy-driver.ps1 -Reboot
```

O restaurador valida que o manifesto pertence ao serviço `bc250v4` e ao PCI
`VEN_1002&DEV_13FE`, reinstala o INF exportado, reenumera o PnP e reinicia.
