# 571 — L1: buffer sem `Lock` de upload contra o `0x10E`/`0x34`, mais instrumentação, e o critério da Corrida 207

**Data:** 02/10/2026 · **Tipo:** correção + instrumentação, offline
**Antecede:** [doc 570](570_CORRIDA206_4C2_FLUSH_NO_ANEL_RODOU_E_BUGCHECK_10E_34_COMO_NA_197.md) (Corrida 206). Pedido do Daniel: corrigir o `0x34` e fazer o candidato trazer mais informação.

---

## 1. A causa, por medição (P0)

**Pilha da 206** (`docs/evidence/2026-10-02_15-36-33-…/dump-analyze.txt`), igual à da 197:
```
dwm.exe → DxgkLock → DXGDEVICE::Lock → VidMmBeginCPUAccess → SetupAllocationForCPUAccess
→ VIDMM_GLOBAL::Rotate → VIDMM_RECYCLE_HEAP_MGR::Rotate → VIDMM_RECYCLE_MULTIRANGE::Rotate+0x77 → KeBugCheckEx
```
Nenhum quadro do nosso driver, de paginação ou de flush.

**A alocação travada:**
- Argumento da pilha `0x28000` = 163.840 bytes = `hAlloc …D1` no log do KMD: `STAGING` (`Token 0xA1100003…`), `SegSet=0x1`, `CpuVisible=1`. Ela foi **despejada** (`TRANSFER Dir=0`, `LOCAL_TO_SYSTEM`) e **trazida de volta** (`Dir=1`, `SYSTEM_TO_LOCAL`, um dos últimos seis TRANSFERs).
- No log do DWM recuperado depois da trava (`umd-logs-pos-travamento/BC250_UMD_LOG_1288.txt`), o alvo de `Lock` mais frequente é `hAlloc=0x80000780`, de 163.840 bytes (25 das 70 linhas amostradas). É o recurso `0x9B2D7B0`: `Dim=1` (buffer), `Bind=0x1` (vértices), `Usage=2` (dinâmico), `Misc=0x0`, `VkRes=1`, mapeado com `MapType=5` (`NO_OVERWRITE`) e o **`VB Slot[0]` do último draw do log** (15:38:11.081).
- Na 197 a alocação era de 241.664 bytes, também classe STAGING (onde caem DYNAMIC e STAGING), também recém-trazida da RAM e também com `Lock` da UMD.

**O mecanismo no nosso código (P0, leitura):** a cada `Unmap`, a UMD copia o buffer **duas** vezes: para o `VkBuffer` da RADV, que é o que os draws leem, e por **`Lock`** para a alocação WDDM. A cópia WDDM de um buffer **não é lida por ninguém**: a composição do KMD só copia texturas. E cada `Lock` obriga o VidMm a trazer o buffer de volta à VRAM só para receber essa cópia, gastando os 256 MB e despejando outras alocações.

**O que não sei (H):** por que o VidMm chega a um estado incompatível no `Rotate`. A L1 não depende disso: ela tira o `Lock` que dispara o caminho.

## 2. As mudanças

| Sigla | O que muda | Comportamento? |
|---|---|---|
| **L1** | `Bc250UmdBufferWddmCopyIsRedundant` (`bc250_d3d_umd.c`): **buffer com `VkBuffer`, não compartilhado** deixa de receber o `Lock` de upload em `ResourceUnmap`, `ResourceUpdateSubresourceUP` e `DefaultConstantBufferUpdateSubresourceUP`. O upload ao `VkBuffer` continua igual. Textura, buffer sem `VkBuffer` e recurso compartilhado (`MISC_SHARED` ou aberto de outro processo) continuam com `Lock`. Log `L1 …: Lock de upload … PULADO #N` (8 primeiras e a cada 1024) | **Sim**, e é a única |
| **I.PG** | Registro **BPG1** no rastro durável `C:\BC250_PAGING_TRACE.bin`, a cada 1 s: despejos (contagem, KiB), voltas para a VRAM (contagem, KiB), FILLs, `FlushTlb` e flushes pelo anel, cumulativos. Só contadores de software (nenhum MMIO), gravados pela thread do rastro, que sobrevive à trava. Decodificado por `tools\decode-bc250-paging-trace.py` (`paging_stats`, com deltas por segundo dos últimos 20 s) | Não |
| **I.LK** | `Upload LEAVE … LockUs=N` (e `LOCK_LENTO` acima de 50 ms) na winsys: quanto cada `Lock` esperou | Não |
| **Coleta** | `tools\coletar-pos-trava.ps1` passa a rodar o `cdb` sozinho quando há `MEMORY.DMP` posterior ao bind, e grava `dump-analyze-RECUPERADO.txt` | Não (ferramenta) |

A 4c.2 continua **armada** (a pilha não a implicou). Contra a 206, a única mudança de comportamento é a L1.

**Testes e portões:**
- `build-umd` EXIT=0, `build-contract` EXIT=0, auditoria de segurança PASS, lab package audit PASS.
- Portão novo `audit_l1_buffer_lock_skipped_and_paging_stats`. **Quatro mutações verificadas**, todas reprovadas: a política aceitar recurso compartilhado; a política aceitar textura; o `ResourceUnmap` voltar a travar buffer sem a política; o TRANSFER deixar de contar.
- A suíte offline da UMD não tem ponte Vulkan, então a L1 não dispara nela. A política é travada pelo portão, não por teste de execução.

**Pacote (assinado, auditado, NÃO INSTALADO):** lab **3.354.1.0**.
- SYS `26781F18…3ECE`, INF `AB50208E…4FC5`, CAT `58564B57…8167`, **UMD `A1BD1657…DEB7B`** (fonte `1BE6D3DE…2D22`, nova); RADV `5447A17D…` igual.
- Strings conferidas: `L1 %s: Lock de upload`, `LockUs=%llu` e `LOCK_LENTO` na UMD; `[RING_TLB_FLUSH] #%ld no anel` e o magic `BPG1` no SYS.

## 3. Critério da Corrida 207 (escrito antes, Lei 10)

**Roteiro:** recuperação da 206, boot frio, CMOS em 256 MB, `tools\EXECUTAR_CORRIDA207_L1_BUFFER_SEM_LOCK_600S.cmd`, mesmo uso das 201–206. Se travar ou der tela azul: botão de reset e, no boot seguinte, `tools\coletar-pos-trava.ps1` **elevado e antes de tudo** (agora ele também lê o dump).

**Sinais:** `L1 … PULADO` nos logs do DWM; `[RING_TLB_FLUSH] ARMADO` e `#N no anel`; BugCheck (Event Log 1001 e dump); registros BPG1 no rastro; `LockUs` e `LOCK_LENTO` nos logs da UMD.

| Desfecho | Assinatura (o que o log mostra) | Implicação (para onde vai) |
|---|---|---|
| **A** | `L1 … PULADO` no DWM, `ARMADO` com flushes crescendo, **~600 s sem `0x10E` e sem trava dura** | O gatilho do `0x34` saiu, e a 4c.2 rodou uma corrida inteira sem trava. É n=1 (a 201 e a 205 também não travaram): **repetir** antes da 4c.3 |
| **B** | `L1 … PULADO`, sem `0x10E`, e **trava dura** | A 4c.2 sozinha não tira a trava. Os últimos BPG1 dizem como estava a paginação antes da parada. Seguir para a 4c.3 |
| **C** | **`0x10E`/`0x34` de novo** | Pela pilha: `Lock` sobre **textura** (as de 11 MB e 2,6 MB do DWM continuam com `Lock`) → a L1 é insuficiente e o próximo passo é tirar o `Lock` também das texturas; `Lock` sobre **buffer** → a política não pegou aquele buffer (conferir `VkRes`/`Misc` no log) |
| **D** | Nenhuma linha `L1 … PULADO` no DWM | A L1 não foi exercitada; a corrida não testa a correção |
| **E** | Algo fora do mapa | Descrever antes de julgar |

**Regras:**
- Desfecho parcial não conta: A exige as três coisas (L1 aplicada, 4c.2 emitindo, 600 s limpos).
- Os BPG1 são a **primeira** medição desse tipo; esta corrida não tem com o que comparar. Eles entram no relato como linha de base: despejos e voltas por minuto, e se o vai-e-volta cresce antes de uma parada.

**NÃO VALIDADO EM HARDWARE.**
