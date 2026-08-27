# Marco zero do desenvolvimento

## Resultado

O laboratório `bc250dbg.sys` e seus consumidores voltaram a ser reconstruíveis
com uma ABI compartilhada v2. O marco zero não inicializa nem aprova engines; ele
estabelece uma base fail-closed para os próximos ensaios físicos.

## Mudanças de segurança

- `studio` conversa somente com `bc250dbg.sys`; o fallback D3DKMT foi removido.
- identidade, clock, WGP e conclusão de comandos não recebem valores fabricados.
- ausência da BC-250 ou de um BAR5 PCI confirmado impede o mapeamento MMIO.
- acessos SDMA ficam limitados ao comprimento do BAR0 efetivamente mapeado.
- a ABI possui versão e tamanho, e o Studio rejeita revisão incompatível.
- o device de laboratório aceita handles somente de SYSTEM e administradores.
- os blobs de firmware possuem manifesto reproduzível com hashes e cabeçalho.

## Build verificado

```powershell
cmd /c dbg\build.bat
cmd /c studio\build.bat
python tools\fwinspect.py firmware --output firmware\manifest.json
```

O driver compila com WDK 10.0.26100. A assinatura depende do certificado local
`BC250-WIN Test`; sem ele, o `.sys` é produzido, mas não pode ser carregado.

## Primeiro ensaio na máquina BC-250

1. Criar/instalar o certificado de teste e reconstruir o driver assinado.
   Não desligar IOMMU ou alterar BIOS como pré-requisito presumido.
2. Carregar apenas `bc250dbg.sys`, sem armar escrita.
3. Executar `studio\output\bc250ctl.exe info`.
4. Confirmar `VEN_1002`, `DEV_13FE`, BAR0/BAR5 e ABI v2.
5. Registrar `info`, `regs` e leituras individuais P0/P1. Não executar o
   comando legado `audit`: ele contém escritas em scratch e VRAM.
6. Preservar saída, build e hashes em uma pasta de evidência datada.

Este marco prova compilação e fechamento de fallbacks. Acesso e execução no
silício ainda exigem teste na unidade física: **NÃO VALIDADO EM HARDWARE**.

## Coletor de baseline

O serviço é `DEMAND_START` por segurança e fica parado depois de cada boot.
Inicie explicitamente o driver já instalado, sem reinstalá-lo:

```powershell
dbg\start.bat
```

Depois, no mesmo terminal elevado:

```powershell
powershell -ExecutionPolicy Bypass -File tools\capture-baseline.ps1
```

Para acrescentar apenas as mensagens SMU classificadas como consultas seguras:

```powershell
powershell -ExecutionPolicy Bypass -File tools\capture-baseline.ps1 -IncludeSmuQueries
```

O segundo modo primeiro lê CMD/ARG/RSP. Somente quando RSP indica fila pronta
ele escreve o protocolo dos mailboxes SMU Q0 para realizar consultas; se RSP
estiver zero, omite todas as mensagens. Nenhum modo arma escrita genérica,
inicializa engine, altera clock ou carrega firmware. O pacote é salvo em
`docs/evidence/<data>-<host>-baseline/`.
