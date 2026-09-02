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

---

## Tela preta ou loop de boot: limpeza offline pelo WinRE

Quando **nao da' para entrar no Windows** (nem em Modo de Seguranca), a limpeza e'
feita de fora, pelo Ambiente de Recuperacao, com midia externa.

**Preparacao (fazer agora, com o sistema bom):** copie
[`tools/BC250_WINRE_CLEAN.cmd`](tools/BC250_WINRE_CLEAN.cmd) para a **raiz de um
pendrive**. Um arquivo, sem dependencias.

**Na hora do problema:**

1. Ligar a maquina e deixar falhar o boot 3 vezes (ou usar um pendrive de
   instalacao do Windows) para cair em **Reparar o computador**.
2. **Solucao de problemas -> Opcoes avancadas -> Prompt de Comando**.
3. Descobrir a letra do pendrive (normalmente `D:` ou `E:`) e rodar:

```
D:\BC250_WINRE_CLEAN.cmd /list
```

Isso **nao altera nada** — so' mostra o que existe. Conferido o plano:

```
D:\BC250_WINRE_CLEAN.cmd
```

4. Fechar o prompt, escolher **Continuar** e iniciar o Windows. O video volta
   pelo Microsoft Basic Display Adapter.
5. Ja' no Windows, rodar `tools\clean_all_drivers.ps1` elevado para o que so' da'
   para limpar online (SCM), e fazer **boot frio** antes do proximo bind.

O script grava `BC250_WINRE_CLEAN.log` ao lado de si mesmo no pendrive — traga o
log de volta, ele entra na evidencia.

### Por que nao usar o metodo manual

O procedimento manual tem tres armadilhas reais:

| Comando manual | Problema |
|---|---|
| `dism /image:C:\ ...` | **Em WinRE o Windows raramente e' `C:`.** O WinRE ocupa `X:` e remapeia as letras. Apontar para o volume errado nao limpa nada |
| `/driver:oem11.inf` | **O numero muda** a cada reinstalacao. Remover o `oemNN` errado desinstala o driver de outro dispositivo |
| `del C:\Windows\System32\drivers\bc*.sys` | **`bc*.sys` casa com `bcmfn2.sys`**, que e' driver **inbox da Microsoft** e faz parte do Windows. Verificado nesta maquina em 2026-09-02 |

O script resolve os tres: detecta o volume pelos marcadores
`\Windows\System32\ntoskrnl.exe` e `\Windows\explorer.exe`; descobre o `oemNN.inf`
buscando `bc250`/`BC-250`/`amdbc250`/`DEV_13FE` dentro de `\Windows\INF\oem*.inf`
(independente do idioma do DISM, cuja saida e' traduzida); e apaga **apenas**
`bc250*`, `amdbc250*` e `cyan_skillfish2*`, recusando por seguranca qualquer outro
nome.

### Parametros

| | |
|---|---|
| `/list` | So' mostra o que faria. Nao altera nada |
| `/force` | Nao pergunta |
| `/win:D` | Forca a letra do volume do Windows (use se houver mais de uma instalacao) |
| `/purge` | Apaga as chaves de servico em vez de so' desabilitar (`Start=4`) |
| `-h` | Ajuda |

Codigos de saida: `0` limpo · `1` erro ou abortado · `2` nada encontrado · `3` parcial.

**Estado:** logica validada offline em modo `/list` nesta maquina (detectou os 7
residuos reais e recusou `bcmfn2.sys`). **Ainda nao exercitado numa sessao real de
WinRE** — enquanto isso nao acontecer, o item permanece `NAO VALIDADO EM HARDWARE`.
