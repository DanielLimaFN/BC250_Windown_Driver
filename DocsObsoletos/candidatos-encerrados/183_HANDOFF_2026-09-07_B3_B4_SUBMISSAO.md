# 183 — B3 e B4: o `INDIRECT` virou o default, e a recusa do Node 1 deixou de ser muda

**Data:** 2026-09-07, depois das corridas 17:43 e 18:16
**Corridas gastas nesta sessão:** **zero** (tudo offline)
**Estado:** mudança de default, de contrato e de telemetria. **NÃO VALIDADO EM HARDWARE.**

Continuação de [`182_HANDOFF_2026-09-07_17-43_B1_FECHADO_E_0x119.md`](182_HANDOFF_2026-09-07_17-43_B1_FECHADO_E_0x119.md).

---

## 0. Registro da corrida 18:16, que autorizou o B3

Antes de mudar o default, o que a última corrida mediu (`P0`), do log vivo íntegro
arquivado em `docs/evidence/2026-09-07_18-16-03-…/bc250_display_log_full_pos_rollback.txt`
(SHA-256 `479B251D78E69F7D62BD374D1A4D1BC3C4C373AA3A205C8630D16BA2030BE7A1`, 2.708 linhas
contra 1.965 no que o coletor havia salvo):

```text
32 CAMINHO_ESCOLHIDO: INDIRECT      32 IB EXECUTADO (INDIRECT)
 0 IB_TRIPOD TIMEOUT                0 BugCheck        Codigo 0 ate o rollback
 2 IB_TRIPE_VEREDITO: TRIPE_PROVADO (Canaries=5 Divergentes=0)
 0 REJECT source contract           0 SpaceResolveAddress failed
 4 [DestroyProcess] GPUVA: faixas invalidadas
30 Present, 10 SDMA=YES, 4 BYTE_PROOF INCONCLUSIVO, BlankStreak=4
10 falhas de dwm.exe (e0464645) e 2 dumps
```

**Ressalvas registradas:** (1) o `RPTR` casou com o alvo **cru** nas submissões pós-wrap,
então a assinatura de falha da 17:43 (`RPTR` além de uma volta) **não reapareceu** — a A15
não foi contrariada, mas também não foi exercitada no modo em que importa; (2) a imagem que
voltou foi o **rollback** do watchdog às 18:18, não o nosso caminho de pixel.

O watchdog passou a arquivar o log vivo antes do rollback
([`watch-bc250mcdm-binding.ps1:127`](../tools/watch-bc250mcdm-binding.ps1)) — na 17:43 as
linhas do `0x119` existiam **somente** ali.

---

## 1. B3 — os `PLANO A` saíram da precedência

**Antes:** `SubmitCommandVirtual` do Node 0 tentava `PLANO A DIRECT FILL`, depois
`PLANO A SYNC NOP`, e só então `INDIRECT`. Os dois primeiros reimplementam a operação em
pacotes do **anel** e a GPU nunca lê o DMA buffer do `dxgkrnl`. Enquanto tivessem
precedência, um `INDIRECT` que falhasse ficava **escondido** atrás de uma reimplementação
que funciona — fallback disfarçado de sucesso (Lei 4). Foi assim que a corrida 16:19
absorveu 14 de 14 submissões e não mediu nada.

**Agora:** o default é **somente `INDIRECT`**. O campo do contrato deixou de ser
`IndirectOnly` e passou a ser `PlanoAFallbackEnabled`, com a polaridade invertida:

| Valor | Comportamento |
|---|---|
| `0` (default, e o que o INF grava) | somente `INDIRECT`; GPUVA que não resolve vira **RECUSA** |
| `1` | restaura a precedência antiga, **apenas para bissecção** |

- Tunable renomeado: `SdmaIndirectOnly` → **`SdmaPlanoAFallback`**, default `0`,
  fail-closed (valor ausente/ilegível **não** liga o fallback).
- INF do pacote ativo: `HKR,Parameters,SdmaPlanoAFallback,0x00010001,0`.
- Manifesto: `"SdmaPlanoAFallback": 0`, `"SdmaPlanoAFallbackEnabled": false` — **derivado do
  INF**, e o empacotador recusa o pacote se a linha faltar.
- Script: `tools/arm-sdma-plano-a-fallback.ps1` (substitui `arm-sdma-indirect-only.ps1`),
  com `-Arm -IAcceptHiddenFallbackRisk` e `-Disarm`.
- O aviso do binder foi reescrito para a nova polaridade.

**O que autoriza essa inversão:** em 17:43 e 18:16 o `INDIRECT` executou o DMA buffer real
sob VMID8 com o tripé completo (5 canários substituídos, `Divergentes=0`). O caminho que
antes era experimental é o único que prova execução.

**Risco declarado:** submissão cuja GPUVA de IB não resolva agora sai como
`STATUS_IO_DEVICE_ERROR` **por default**, e sem TDR (item 25) esse é o caminho conhecido do
`0x119`. Isso é deliberado: falhar visível é melhor que suceder por reimplementação.

## 2. B4 — a recusa do Node 1 era muda, e o gate não podia abrir

Duas coisas distintas, ambas medidas:

**(a) A recusa não dizia nada.** Toda submissão de Node 1 saía de
```c
if (InterlockedCompareExchange(&context->GpuvaMappingsOperational, 1, 1) != 1)
    return STATUS_NOT_SUPPORTED;
```
sem uma linha de log. Nas corridas o KMD criou dois contextos Node 1 e recebeu zero
submissões — e **não havia como distinguir "o scheduler não submeteu" de "nós recusamos"**.
Agora a recusa nomeia o motivo, com o `BlockMask` decodificado bit a bit:

```text
[SubmitVirtual Node1] RECUSA: GpuvaMappingsOperational=0 -- Eligible=%ld BlockMask=0x%08X
   (JOURNAL=. HIER=. INATIVO=. CTXABI=. ALLOCABI=. ROOTABI=. ENGINE_OFF=. SNAPSLOT=.
    SNAPSIG=. SNAPVAZIO=.) LabPromotion=%u (present=%u) FenceId=%llu DmaSize=%u
```

e a abertura também aparece: `[SubmitVirtual Node1] GATE ABERTO: … publicando IB … no anel
persistente MEC`.

**(b) O gate não podia abrir no pacote de produção.** `GpuvaMappingsOperational` só vai a 1
em `ActiveFlushTlb` quando `GpuvaLabPromotion` está **presente e em 1** e a elegibilidade
fecha — e essa leitura existia **somente** no `#else` de
`BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP`, isto é, fora do pacote ativo. Mesma classe de
defeito do `SdmaIndirectOnly` da A13: tunable declarado no INF e nunca lido. Agora é lido no
caminho de produção, com `present`/`requested` na telemetria de probe.

**Ler não abre nada:** o default é `0`, o INF ativo grava `0`, e a elegibilidade
(`BlockMask`) ainda precisa fechar. Abrir é decisão do dono do projeto.

> O caminho de submissão Node 1 depois do gate **já existe e está completo**: view imutável
> de contexto, snapshot de GPUVA fixado, `Bc250GpuvaSpaceValidateRangeCoverage` sem gaps,
> reserva de fence WDDM e `Bc250SubmissionEngineSubmit` no anel persistente MEC. O que
> faltava era poder chegar nele — e saber por que não chegava.

## 3. Testes

| Item | Teste nomeado | Resultado |
|---|---|---|
| B3 | `submit_command_contract_test` — a precedência antiga agora exige `PlanoAFallbackEnabled=1`; com o campo em zero (default) fill de 11 MB e sync de 4 bytes saem `INDIRECT`, e GPUVA que não resolve sai `RECUSADO`, **nunca** PLANO A | PASS |
| B3/B4 | `audit-bc250mcdm-safety.py`: `audit_indirect_only_tunable` (reescrito para a nova polaridade, e **proíbe** a string `SdmaIndirectOnly`), `audit_node1_gate_telemetry` | PASS |
| todos | `build-contract.bat` (suíte + SYS), `active_miniport_test`, `blt_proof_contract_test` | `EXIT=0` |

**Mutação verificada:** voltar o tunable para `SdmaIndirectOnly` → `FAILED` em dois portões;
remover o log da recusa do Node 1 → `FAILED: recusa de Node 1 com motivo nomeado` +
`BlockMask no log da recusa`.

## 4. Pacote `3.85.0.0` — assinado, auditado, NÃO INSTALADO

| Item | Valor |
|---|---|
| SYS fonte | `359B1C881485E1401C3A98FA34A5D99956BAFF022F82177EF8C615A3293BBDF2` |
| SYS assinado | `39B767882531DA201EEFF793CC38BA5ACAC17FEB7CC161CDF9A03E94E61CD082` |
| Catálogo | `8CE3C2727610B17EF68EF4CFD07F095E0ADDEDE89FF9793744581A5AD0F72A9B` |
| `SdmaPlanoAFallback` | `0` (somente `INDIRECT`) |
| `GpuvaLabPromotion` | `0` (gate do Node 1 fechado) |

Frescor no SYS assinado: `SdmaPlanoAFallback`, `PlanoAFallback=%u`,
`RECUSA: GpuvaMappingsOperational=0`, `GATE ABERTO: GpuvaMappingsOperational=1`,
`GpuvaLabPromotion=%u (present=%u)` presentes; **`SdmaIndirectOnly` ausente**.

### 4.1 A próxima corrida, e a decisão que ela exige

Com este pacote como está (`GpuvaLabPromotion=0`), a corrida mede o B3: o `INDIRECT` como
default, sem tunable armado. Ler `CAMINHO_ESCOLHIDO: INDIRECT` sem nenhum
`PLANO A CONCLUIDO`, e `[StartDevice] SdmaPlanoAFallback=0 (present=1)`.

**Para medir o B4 é preciso armar o gate** — decisão sua, porque abre submissão de compute
originada pelo scheduler:

```
powershell -ExecutionPolicy Bypass -File tools\test-gpuva-lab-promotion.ps1 -Action Prepare
```
(ou gravar `GpuvaLabPromotion=1` em `…\Services\bc250mcdm\Parameters` antes do bind)

Aí a corrida responde, no log, uma de três: `GATE ABERTO` seguido de submissão real;
`RECUSA … BlockMask=0x…` dizendo **qual** condição falta; ou silêncio, que passa a significar
"o scheduler não submeteu" — porque a recusa deixou de ser muda.

Desktop continua fora de alcance: a UMD não mudou, `BC250_D3D10_11_NATIVE_READY` segue `0`.
Node 1 executando, TDR, desktop acelerado e jogos — **NÃO VALIDADOS EM HARDWARE**.
