# docs/254 — Corrida 19:27: watchdog validado e autoridade GPUVA física

**Corrida:** `2026-09-12_19-27-19-DESKTOP-HQAG6T4-lab-binding`  
**Candidato executado:** `3.159.1.0`  
**Desfecho visual:** barras RADV com badge `BC2501` permaneceram estáticas até o rollback.  
**Recuperação:** watchdog SYSTEM restaurou `BasicDisplay`, Código 0, sem reboot.

## Veredito da corrida

A imagem fotografada é exatamente o padrão produzido pelo harness. A cadeia abaixo foi
provada na mesma execução:

1. o primeiro IB Vulkan/RADV foi aceito pelo Node 1 e executou no MEC/VMID9;
2. a leitura de retorno encontrou os 8/8 pixels esperados;
3. `D3DKMTPresent` chegou ao KMD;
4. o SDMA copiou a superfície para o scanout, sem fallback de CPU;
5. as barras apareceram no monitor ligado à BC-250.

O padrão ficou parado porque o segundo submit do loop foi recusado. Não houve timeout,
TDR nem fault da GPU nessa transição.

## Watchdog: VALIDADO

O guardião persistente passou por toda a máquina de estados:

```text
19:27:25  OBSERVING
19:29:30  ROLLBACK_STARTED
19:29:37  RECOVERY_OK_ONLINE
```

`C:\BC250_RECOVERY.cmd` terminou com código 0, o pacote experimental foi removido e cinco
amostras consecutivas encontraram `BasicDisplay`, Código 0. `shutdown_exit_code=null`:
nenhum reboot de emergência foi necessário. O prebinding posterior passou limpo.

## A causa imediata do `0xC01E0200`

O primeiro submit do harness fechou:

```text
GATE ABERTO
SUBMIT SUCESSO: AssignedFence=18
FENCE COMMIT
```

Antes do submit seguinte, o publicador tentou reutilizar o único slot inativo do snapshot:

```text
PublishStatus=16 Attempts=16 Busy=16
OCUPACAO ... Mapped=250 Reusable=774
GpuvaMappingsOperational=0
```

O segundo submit foi recusado pelo KMD e o dxgkrnl expôs isso ao RADV como
`STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE` (`0xC01E0200`). A GPU não gerou a exceção:
o KMD recusou antes de publicar o descriptor.

O desenho de dois snapshots era incompatível com a própria retenção: a fence do primeiro
submit mantinha um slot fixado até o drain; após uma publicação, esse slot virava o próximo
alvo do escritor. Retentar 16 vezes não resolve uma referência cujo ciclo de vida é a fence.
Mais importante, essa retenção nunca protegeu a tradução física: `UpdatePageTable` grava as
PTEs antes de tentar publicar o espelho lógico.

## `NO_SPACE`: confirmado como limite estrutural

Na fatia integral desta corrida:

| Medida | Resultado |
|---|---:|
| `Journal NO_SPACE` | 192 updates |
| publicações falhas por `NO_SPACE` | 7 |
| publicação falha por `BUSY` | 1 |
| ocupação nas falhas reais | 853–879 / 1024 MAPPED |
| `PT_RANGE ... PASS` | 21.489 |
| `PT_RANGE ... FALHA` | 0 |
| IBs SDMA executados | 8.013 |
| timeout / GFXHUB fault | 0 / 0 |

O overlay eliminou histórico obsoleto, mas não elimina fragmentação real: blocos de 512
PTEs podem representar centenas de páginas físicas não contíguas. Elevar 1024 de novo só
move o teto. As page tables físicas, por outro lado, suportaram toda a corrida e são a fonte
de verdade consumida pelo hardware.

## C3.17 implementada offline

O caminho de Node 1 foi realinhado com essa autoridade:

- `GpuvaLabPromotion` continua sendo o aceite explícito; produção permanece fail-closed;
- o snapshot lógico virou diagnóstico e é liberado antes de reservar/publicar a fence;
- a fence não retém mais um snapshot que não protege as PTEs;
- imediatamente antes da publicação, o KMD caminha **todas** as páginas do IB na root do
  próprio contexto, exigindo permissão de leitura;
- falha na caminhada recusa antes de tocar no MEC;
- `BUSY`, `NO_SPACE` ou atraso do espelho lógico deixam de falsificar uma exceção de GPU
  quando a tradução física está integralmente válida.

Build completo e auditoria de segurança passaram. O portão novo foi provado por mutação:
alterar a chamada `NODE1_IB` fez a auditoria reprovar, e a restauração voltou a passar.

Pacote LAB `3.160.1.0`, assinado, auditado e **não instalado**:

| Artefato | SHA-256 |
|---|---|
| SYS fonte LAB | `A340AA0B84EE8A1033FDFF7EAEF57B0CF32B7E17916E39C756633E1BC06620A3` |
| SYS empacotado | `C3D4539498AA4AD6A3F6BE65ED04F3274AC99C3821333A5A4850156041FA4829` |
| UMD empacotada | `DD4DFED4DDC0A8E18E133FB2C82F1D2AE788F268799383BE6D91A9C36A5C13B1` |
| INF | `C436F94F0912405992BF93F63AA3DAF938D5FB753316131F9CF076F50BC136E6` |
| CAT | `9748EDEAFB924BBF988898354AEC32D00580527B12FDE783F00915930FAE310E` |

## Próxima pergunta física

Uma corrida curta do mesmo Marco 2 deve verificar se:

1. o submit seguinte ao primeiro ultrapassa o antigo `BUSY`;
2. `GATE FISICO PASS` aparece para cada IB do Node 1;
3. o loop chega além do quadro 2 e produz mudança visível;
4. o watchdog volta a recuperar automaticamente no prazo.

Mesmo com sucesso, desktop D3D11 e jogos continuam bloqueados pelo backend de draw real:
o log do DWM ainda contém `DrawIndexed: RECUSADO -- compilador/shader-state/draw PM4 ausente`.

**C3.17 NÃO VALIDADA EM HARDWARE.**
