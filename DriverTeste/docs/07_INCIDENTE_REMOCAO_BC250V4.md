# Incidente: remoção ao vivo do bc250v4

## Veredito atual

A remoção ao vivo do pacote `oem48.inf` não é segura. Ela reproduziu uma tela
azul `DRIVER_VERIFIER_DETECTED_VIOLATION` (`0xC4`), subtipo `0x62`, durante o
descarregamento de `bc250wddm.sys`.

O pacote foi posteriormente removido offline. Após o boot de 2026-08-22
22:48:37, a BC-250 foi confirmada em `BasicDisplay`/`display.inf`; o serviço
`bc250v4` não existe e o primeiro baseline limpo registrou
`legacy_kmd_absent=true`. Não reinstalar `bc250v4`.

O Driver Verifier está persistente e seleciona somente `bc250wddm.sys`. A opção
de rastreamento de pool está ativa. Esse subtipo significa que o driver tentou
descarregar mantendo alocações de pool pendentes. O quarto argumento observado,
`0x113EB0`, é o total reportado de alocações não liberadas e precisa ser validado
contra o dump.

## O que isto não indica

Não há evidência de que uma chave incorreta no Registro seja a causa da tela
azul. As chaves do serviço e o binding PnP explicam por que o Windows volta a
carregar o pacote após reiniciar, mas não explicam o bugcheck `0xC4/0x62`.

## Identidade do binário

O binário que disparou a falha pertence ao projeto antigo BC250-V4. Seu registro
CodeView aponta para `BC250-V4\build\bc250wddm.pdb`. A cópia observada após o
reboot tem GUID CodeView `{B69773E4-D02A-4D88-B226-18A434A635DE}` e idade 47.
O PDB atualmente disponível tem idade 48; portanto, ele não é uma correspondência
exata para essa cópia. A cópia de backup exportada antes da remoção também é da
idade 48. As diferenças devem permanecer registradas e não podem ser mascaradas.

## Contenção e estado preservado

- Não repetir `pnputil /delete-driver oem48.inf /uninstall /force` na sessão viva.
- Não apagar manualmente o serviço, o `.sys` ou chaves de classe/display.
- Preservar `C:\Windows\MEMORY.DMP` e o backup em `recovery`.
- Usar `tools\analyze-bc250v4-dump.ps1` em PowerShell elevado.
- A remoção offline já foi escolhida e concluída; qualquer correção futura do
  teardown deve ocorrer apenas em cópia isolada, sem reinstalar o pacote na
  máquina de laboratório.

## Comando de análise

```powershell
cd C:\Users\Daniel\Desktop\NamixWDDM
powershell -ExecutionPolicy Bypass -File tools\analyze-bc250v4-dump.ps1
```

A evidência será gravada em `docs\evidence\<data>-bugcheck-c4\analysis.txt`.
