# 202 — Handoff 2026-09-08 14:06 · quem morre, e o contexto publicado zerado

> **Corrida:** 08/09 14:06 (P0, candidato lab **3.93.1.0**, SYS `EF91F17F`)
> **Resultado:** Código 0 · tela preta · **duas perguntas respondidas**
> **Node 1:** NÃO VALIDADO EM HARDWARE

---

## 1. A corrida reproduziu o estado bom e respondeu o que faltava

Tudo que já estava validado se manteve:

```
V5StartDeviceStatus       = 0
SecondPreflightStage      = 8      SecondReadonlyGatePassed = 1
RuntimeCapsStatus         = 0
RuntimeCapsBaseLow32      = 11.059.200      RuntimeCapsLocalLow32 = 257.101.824
```

E o laço do VidPn se repetiu idêntico: **39 convergências**, **46 reinícios**,
zero `CommitVidPn`, zero `Present`.

Evidência: `docs/evidence/2026-09-08_14-06-49-DESKTOP-HQAG6T4-lab-binding/`.

---

## 2. Pergunta 1 — quem é o processo que nasce e morre

```
Nome=<sem nome>, Flags(System=1 Gdi=0 Vm=0 VmWorker=0 Isolated=0)   <- uma vez
Nome=<sem nome>, Flags(System=0 Gdi=1 Vm=0 VmWorker=0 Isolated=0)   <- ESTE repete
```

`pProcessName` veio **NULL** — permitido pelo contrato, o dxgkrnl não é obrigado
a fornecê-lo. Mas as **flags** responderam: quem repete é o cliente **GDI**, não
o `dwm.exe`.

Isso é coerente com a medição da corrida 13:00, em que nenhum
`BC250_UMD_LOG_*.txt` foi criado: o DWM nunca entra em cena neste perfil, então
a `bc250umd.dll` nunca é carregada. **A parede do D3D10/11 continua existindo,
mas ela é posterior a esta.**

---

## 3. Pergunta 2 — alguma DDI nossa recusou?

**Não. Zero `RECUSADO` em toda a corrida.** Todos os `CreateProcess`,
`CreateDevice` e `CreateContext` devolveram sucesso. Uma hipótese inteira
eliminada.

---

## 4. O achado: o `OK` estava mentindo

```
[McdmCreateContext] OK: Node=0, hContext=0x..., DmaBuf=0x0, SegSet=0x0
```

Oito vezes. Um contexto com `DmaBufferSize = 0` e `DmaBufferSegmentSet = 0` é
inútil: o dxgkrnl não tem como alocar DMA buffer para ele.

### 4.1 Por quê

`Bc250McdmCreateContext` escreve defaults sensatos em `pCreateContext->ContextInfo`
(`DmaBufferSize=0x10000`, segmento GART, `AllocationListSize=32`). Em seguida
chama o provedor, e `Bc250ActiveCreateContextObject` faz:

```c
*ContextInfo = context->ContextAbi;
```

— **sobrescrevendo** os defaults. E `context->ContextAbi` era populado **apenas**
dentro do bloco `#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)`, isto
é, somente na imagem de **produção**. Na imagem de **laboratório**,
`ContextAbiReady` ia a 1 com a estrutura inteiramente zerada.

### 4.2 Terceira vez da mesma classe

| faixa | campo | ramo em que existia |
|---|---|---|
| A13 | tunable de modo de submissão | só no `#else` |
| B4 | `GpuvaLabPromotion` | só no `#else` |
| **agora** | **`ContextAbi`** | **só no `#if !defined`** |

Campo escrito em **um só ramo do `#if`**, invisível no outro. É o padrão que
mais custou corridas neste projeto.

---

## 5. Correção e travas

Fonte única `Bc250ActiveInitializeContextAbi`, chamada nos **três** sítios que
declaram `ContextAbiReady`, sempre **antes** da declaração.

**Gate** `audit_context_abi_before_ready`, com dois invariantes:

1. entre cada `ContextAbiReady=1` e o anterior (ou o início do arquivo) tem de
   existir uma chamada à fonte única — **sem janela arbitrária de linhas**, que
   foi como a primeira versão do gate falhou;
2. `ContextAbi.DmaBufferSize` não pode ser escrito fora do helper.

**Gate** `audit_process_lifecycle_ddis_speak`: nas três DDIs de ciclo de vida,
**todo** `return` que não seja `STATUS_SUCCESS`, depois do log de entrada, exige
um `Bc250DisplayLog` nas 6 linhas anteriores.

### 5.1 Mutações verificadas

| # | Mutação | Medido |
|---|---|---|
| 6 | uma das três recusas de `CreateContext` volta a ser muda | `recusa MUDA em 'return status;'` |
| 7 | cópia do nome perde o `volatile` | `sem destino volatile ... LNK2019` |
| 8 | remove o helper de um dos três sítios | `ContextAbiReady=1 na linha 5411 sem popular ContextAbi antes` |
| 9 | escreve `DmaBufferSize` fora do helper | `segunda fonte de verdade` |

Duas notas de processo, ambas sobre portões meus que não valiam:

- a **primeira** versão de `audit_process_lifecycle_ddis_speak` exigia apenas a
  presença da string `RECUSADO:`. A mutação 6 passou — com três recusas na
  mesma DDI, remover uma ainda deixava a string. Refeito com o invariante
  por-`return`;
- a **primeira** versão de `audit_context_abi_before_ready` usava janela de 12
  linhas e reprovou o sítio de produção legítimo, onde há 19 linhas de
  comentário entre a chamada e a declaração. Refeito sem janela.

E o gate do B3 pegou o **meu próprio comentário**, que citava o nome banido do
tunable antigo — portão funcionando.

---

## 6. Estado de build

```
driver/wdk/build-contract.bat          EXIT=0
tools/audit-bc250mcdm-safety.py        PASS
tools/audit-mcdm-ddi-matrix.py         PASS (STRUCTURAL ONLY)
tools/audit-ddi-noop-callbacks.py      98 anunciadas, 0 falhas
```

A correção **NÃO ESTÁ VALIDADA EM HARDWARE**. O INF fonte já está em 3.94.0.0,
então o próximo candidato sai **3.94.1.0**.

3.93.1.0 revertido pelo watchdog; BasicDisplay em Código 0 e
`C:\BC250_REQUIRE_CLEAN_BOOT.json` armado.

---

## 7. Próxima corrida — e o que ela decide

**Confiança do que se sabe:**

- que o contexto sai com `DmaBuf=0x0` é **P0** — está no log, oito vezes;
- que *isso* mata o cliente GDI é **H**. É mecanicamente coerente e não há
  hipótese concorrente à vista, mas não está provado.

Critério de leitura:

```
[McdmCreateContext] OK: Node=0, ..., DmaBuf=0x10000, SegSet=0x2
```

- **Hipótese confirmada:** o ciclo `CreateProcess → DestroyProcess` para de se
  repetir e aparece `CommitVidPn`.
- **Hipótese refutada:** o ciclo continua mesmo com `DmaBuf` correto. A causa é
  outra, e o campo deixa de ser suspeito — o que também é resultado.

Os dois desfechos produzem informação, que é o critério da Lei 9.
