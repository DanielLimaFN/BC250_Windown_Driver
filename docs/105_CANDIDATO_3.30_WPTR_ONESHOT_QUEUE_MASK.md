# Candidato 3.30 — protocolo one-shot de WPTR e máscara de fila (offline)

Data local: 30/08/2026
Pacote: **3.30.0.0 / SchemaVersion 101**
Estado: **construído, testado e auditado offline. NÃO VALIDADO EM HARDWARE.**

Continuação direta de [`104_HANDOFF_IA_APOS_CANDIDATO_3.29.md`](104_HANDOFF_IA_APOS_CANDIDATO_3.29.md).

---

## 1. O que o 3.29 deixou provado

O ensaio físico do 3.29 não foi um resultado vazio. Ele fixou quatro fatos:

| Fato físico | Consequência |
|---|---|
| `INIT_FETCHER` chegou ao silício (`applied=0x80000000`, auto-clear, `RPTR_EQ_CSMD_WPTR` aceso) | A etapa EOP não é a que falta |
| `HqdActiveWptrPollCntlBefore = 0x0` | `CP_PQ_WPTR_POLL_CNTL.EN` **nunca** esteve ligado |
| Doorbell tocado, `DOORBELL_ENABLE=1`, `WptrAfterKick` não latcheou | O doorbell sozinho não publica o cursor |
| WPTR direto: readback imediato `40`, amostras seguintes `0`; `PQ_EMPTY` aceso | A escrita direta não é retida pela fila |

O denominador comum é o mesmo: **nada publica o WPTR de forma persistente**.

## 2. A peça que faltava

`CP_PQ_WPTR_POLL_CNTL1` nunca foi catalogado nem escrito. Ele é o registrador
que o helper `kgd_gfx_v9_hqd_load` programa — com `QUEUE_MASK`, um bit por fila —
imediatamente antes de `INIT_FETCHER` e `CP_HQD_ACTIVE`.

### 2.1 Offset derivado, não adivinhado

`OldDBG/inc/linux_ref/gc_10_1_0_offset.h:4839` define
`mmCP_PQ_WPTR_POLL_CNTL1 = 0x1E24`, BASE_IDX 0. A constante dw→BAR5 deste bloco
é `+0x1260` dwords, e fecha em três registradores já catalogados de forma
independente:

| Registrador | dw | `(dw + 0x1260) * 4` | BAR5 catalogado |
|---|---|---|---|
| `CP_PQ_WPTR_POLL_CNTL` | `0x1E23` | `0xC20C` | `0xC20C` ✓ |
| `CP_ME1_PIPE0_INT_STATUS` | `0x1E2D` | `0xC234` | `0xC234` ✓ |
| `CPC_INT_STATUS` | `0x1E55` | `0xC2D4` | `0xC2D4` ✓ |
| **`CP_PQ_WPTR_POLL_CNTL1`** | **`0x1E24`** | **`0xC210`** | **novo** |

### 2.2 Máscaras vindas só do header

`gc_10_1_0_sh_mask.h:18225-18236`:

```text
CP_PQ_WPTR_POLL_CNTL   PERIOD[7:0]  DISABLE_PEND_REQ_ONE_SHOT[29]  POLL_ACTIVE[30]  EN[31]
CP_PQ_WPTR_POLL_CNTL1  QUEUE_MASK[31:0]
```

`POLL_ACTIVE[30]` é status do próprio CP: fica fora da escrita e da comparação
de rollback.

### 2.3 O literal proibido

A evidência do 3.29 encerra a contradição histórica do repositório:
`SdmaRingPollCntl = 0x403004`. **`0x00403004` é do SDMA**, não do CP. Ele está
agora marcado como proibido no catálogo e não aparece em nenhum caminho do CP.

## 3. O que o 3.30 muda

Uma única mudança de protocolo, na ordem do `kgd_gfx_v9_hqd_load`:

```text
  1. 16 registradores HQD do MQD              (inalterado)
  2. endereços de RPTR report / WPTR poll     (inalterado)
  3. CP_PQ_WPTR_POLL_CNTL: EN=1               <-- era EN=0 (caminho KIQ)
  4. GCVM Context 0 + invalidate ENG0         (inalterado)
  5. baseline WPTR = 0, IB_CONTROL, unhalt    (inalterado)
  6. shadow WPTR = cursor                     <-- movido para antes de ACTIVE
  7. HDP invalidate + GCVM L2 + REQ/ACK       <-- movido para antes de ACTIVE
  8. CP_PQ_WPTR_POLL_CNTL1 = 1 << bit         <-- NOVO
  9. CP_HQD_PQ_WPTR_LO = cursor               <-- NOVO (pré-ACTIVE)
 10. EOP_RPTR.INIT_FETCHER                    (inalterado)
 11. CP_HQD_ACTIVE = 1                        (inalterado)
```

Os passos 6 e 7 subiram porque agora a fila pode iniciar o fetch no próprio
`ACTIVE`. Deixar a coerência depois de `ACTIVE` abriria uma janela real de
leitura de ring obsoleto — hazard que o 3.29 não expunha só porque o fetch
nunca começava.

O bit da máscara vem da topologia lida em `RLC_CP_SCHEDULERS`
(`pipe * 8 + queue`), nunca de literal. Para MEC/pipe0/queue0 dá `1`.

## 4. Telemetria da primeira falha de rollback

O 3.29 reprovou `HqdActiveRestorePassed` sem dizer onde. O agregado era
escrito em 27 pontos diferentes do teardown.

Agora existe `Bc250HqdNoteRestoreFailure`, que registra apenas a **primeira**
reprovação — a causa raiz, não o último efeito colateral — e um helper
`Bc250HqdRestoreRegister` (escreve, relê, compara sob máscara) usado em todos
os estágios. Campos publicados:

- `HqdActiveFirstRestoreFailureStage` (`BC250_HQD_RESTORE_STAGE_*`, 18 estágios)
- `HqdActiveFirstRestoreFailureOffset`
- `HqdActiveFirstRestoreFailureExpected`
- `HqdActiveFirstRestoreFailureObserved`
- `HqdActiveFirstRestoreFailureTransport` (1 = a chamada MMIO falhou; 0 = valor divergiu)
- `HqdActiveRestoreFailureCount`

## 5. Arquivos alterados

- `driver/core/bc250_compute_mqd_contract.h` — offset `0xC210`, máscaras, enum de estágios, 20 campos de evidência
- `driver/core/bc250_compute_mqd_contract.c` — helpers de rollback, reordenação, `CNTL`/`CNTL1`, WPTR pré-ACTIVE
- `driver/wdk/bc250_mmio_topology_transport.c` — `0xC210` nas allowlists de leitura e escrita
- `driver/wdk/bc250_probe_telemetry.h/.c` — 15 IDs e nomes novos
- `driver/wdk/bc250_active_miniport.c` — emissão nos dois pontos de publicação
- `driver/tests/compute_mqd_contract_test.c` — fake do `CNTL1`, casos 9.2.1 e 9.2.2
- `driver/tests/mmio_topology_transport_test.c` — allowlist do `0xC210`
- `tools/package-bc250mcdm-lab.ps1` — 3.30.0.0 / Schema 101
- `tools/test-shader-dispatch-acceptance.ps1` — gates do protocolo e do estágio de rollback
- `docs/03_CATALOGO_DE_REGISTRADORES.md` — `CNTL1` catalogado, literal do SDMA proibido

## 6. Provas offline obtidas

- `driver\wdk\build-contract.bat` compila em `/W4 /WX` sem warning no código de kernel e a suíte inteira sai com `EXIT=0`.
- `9.2.1` prova que a máscara vem da topologia: `RLC_CP_SCHEDULERS` com ME2/pipe1 produz bit 8 (`0x100`), não um literal.
- `9.2.2` injeta uma falha de rollback no `CNTL1` e verifica estágio, offset, esperado, observado e o contador — o teste falha se a localização não for exata.
- O teste de transporte prova que `0xC210` é lido, escrito e restaurado dentro da allowlist.
- A aritmética dos novos gates PowerShell foi verificada nos dois sentidos (aceita o valor esperado, rejeita `EN` ausente).

## 7. O que este documento NÃO afirma

Nenhum shader executou. Nenhuma wave foi observada. O pacote não foi
instalado. Resultado e fence continuam envenenados desde o 3.29.

**NÃO VALIDADO EM HARDWARE.**

## 8. Armadilha encontrada no empacotamento (corrigida)

O `package-bc250mcdm-lab.ps1` apenas **copia** `driver\wdk\output_lab\bc250mcdm.sys`.
Quem compila essa imagem é `tools\build-bc250mcdm-lab-bringup.ps1 -Execute`.

Na primeira tentativa de empacotar o 3.30, esse passo faltou e o script assinou
em silêncio o binário do 3.29 — o manifesto saiu com `DriverVer 3.30.0.0` e
`Schema 101`, mas `PackageSysSha256` era exatamente
`20B38038…`, o hash do 3.29 registrado no handoff 104. Um ensaio físico inteiro
teria sido gasto para reproduzir o resultado anterior.

O empacotador agora **falha fechado**: compara o `mtime` do SYS de laboratório
com todo `*.c`/`*.h` de `driver\core` e `driver\wdk`, lista os arquivos mais
novos e aborta mandando rodar o build de laboratório. Verificado nos dois
sentidos (bloqueia com fonte mais nova; passa depois do rebuild).

`audit-bc250mcdm-lab-package.ps1`, `bind-bc250mcdm-lab.ps1` e
`confirm-bc250mcdm-lab.ps1` também exigiam Schema 100 em literal e teriam
bloqueado o ensaio; foram atualizados para 101.

## 9. Artefatos do pacote 3.30

```text
DriverVer              08/30/2026,3.30.0.0
Schema                 101
recipe cp/vmid0/sdma   1 / 0 / 0
Source SYS SHA-256     A322F6D256CA8AC548B1C328CF01C273153F2AF4071C9CBC77BF6B469DCD1F4B
Package SYS SHA-256    66D5929CAADFAE2BAFBC0580B609AF0429E3708F7B4EC6C961689E8ACCB49530
Package INF SHA-256    10A41AF347F874A206D1EBB595855B3EE0D514D8CC7C58E84CB040FD23322876
audit-bc250mcdm-lab-package.ps1   PASS
```

Prova de que o binário assinado contém o 3.30: os nomes de telemetria novos
(`HqdActiveWptrPollCntl1Before`, `HqdActiveQueueMaskBit`,
`HqdActivePreActiveWptrPublished`, `HqdActiveFirstRestoreFailureStage`,
`HqdActiveFirstRestoreFailureTransport`, `HqdActiveRestoreFailureCount`)
estão presentes como strings UTF-16 dentro de `output_lab\bc250mcdm.sys`.
O rebuild determinístico reproduziu o mesmo hash de SYS.

## 10. Procedimento do próximo ensaio (um único, em boot limpo)

1. Reboot manual.
2. `quarantine-bc250mcdm-lab-residue.ps1` se o SYS residual existir.
3. `audit-bc250mcdm-prebinding.ps1 -LabPackage` deve terminar em PASS.
4. `bind-bc250mcdm-lab.ps1` com watchdog.
5. `test-shader-dispatch-acceptance.ps1`.
6. Aprovar somente com o tripé completo, comparação byte a byte e rollback interno e externo.

O pacote já está construído, assinado e auditado — não reempacotar antes do
ensaio. Uma só submissão física por boot.

## 11. Como ler o resultado

| Observação | Leitura |
|---|---|
| `WptrPollCntl1Readback` sem o bit da fila | O registrador não é o que supomos — reabrir o catálogo |
| Máscara pega, `PQ_EMPTY` cai, `RPTR` avança | Hipótese confirmada; seguir para resultado e fence |
| Máscara pega, `PQ_EMPTY` permanece | O bloqueio está a jusante do fetch, não na publicação do WPTR |
| `FirstRestoreFailureStage != 0` | A falha do 3.29 fica localizada, com offset e valores |
