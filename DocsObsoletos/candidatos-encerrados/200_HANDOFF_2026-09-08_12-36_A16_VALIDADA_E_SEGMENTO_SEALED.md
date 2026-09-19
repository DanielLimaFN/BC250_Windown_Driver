# 200 — Handoff 2026-09-08 12:36 · A16 validada no silício · a parede do segmento sealed

> **Corrida:** 08/09 12:36 (P0, candidato lab **3.92.1.0**)
> **Resultado:** A16 **VALIDADA EM HARDWARE** · Código 43 por defeito **distinto e anterior**
> **Node 1:** NÃO VALIDADO EM HARDWARE · **Display:** não medido

---

## 1. A pergunta da Fase 2 foi respondida: PASS

A corrida perguntava uma coisa só — *o StartDevice volta a Código 0 com a MMU
global ligada?* As duas linhas novas responderam:

```
[StartDevice] Transporte devolvido ao preflight: unmap=ok (State=0)
[StartDevice] Segundo preflight: status=0xC00000BB, stage=8, gate=1, MmioState=0
```

| probe | 11:48 (3.90.1.0) | **12:36 (3.92.1.0)** |
|---|---|---|
| `SecondPreflightStage` | 3 (`RUNTIME_MISSING`) | **8** (`PREFLIGHT_READY`) |
| `SecondReadonlyGatePassed` | 0 | **1** |
| `V5StartDeviceStatus` | 0xC00000BB | **0** (`STATUS_SUCCESS`) |
| `ReadonlyGatePassed` | 0 | **1** |

`StartDevice` **devolveu sucesso** e o bring-up prosseguiu: `ChildRelations`
chamado, `DP ChildUid=1` devolvido. A faixa A16 está fechada no silício.

O `0xC00000BB` que aparece no `SecondPreflightStatus` agora é o
`STATUS_NOT_SUPPORTED` **legítimo** do fim do gate readonly — a DDI declara que
não anuncia adaptador. O probe novo é justamente o que distingue esse
`NOT_SUPPORTED` intencional da recusa de guarda de entrada da 11:48. Sem ele, os
dois seriam o mesmo número.

Evidência: `docs/evidence/2026-09-08_12-36-39-DESKTOP-HQAG6T4-lab-binding/`.

---

## 2. A parede nova, fechada por aritmética

```
[McdmQAI] Type=1, OutSize=584, CapsStatus=0xC0000182
```

`CapsStatus` é o retorno de `Bc250ActiveQueryRuntimeCaps`, **não** o retorno do
QueryAdapterInfo. `0xC0000182` é `STATUS_DEVICE_CONFIGURATION_ERROR`; o
`DRIVERCAPS` então devolveu `STATUS_DEVICE_NOT_READY`, que é o `c00000a3` que o
fusível registrou. O dxgkrnl derrubou o adaptador — Código 43.

### 2.1 Por que só agora

O ramo **sealed** de `Bc250ActiveQueryRuntimeCaps` exige
`SecondPreflightStage == BC250_DXGK_STAGE_PREFLIGHT_READY`. Até a A16 destravar
o segundo preflight, esse ramo **nunca tinha executado num bind real** — todas
as corridas anteriores caíam no `else if` de fallback.

**O defeito é anterior à A16.** A correção do preflight não o criou; tornou-o
alcançável.

### 2.2 A conta

No ramo sealed, `localEnd` = offset do plano de firmware, e o laço empurrava
`localStart` para depois do fim de **cada** reserva:

```
janela de CPU ........... 268.435.456   (CpuVisibleMiB=256)
localEnd (firmware) ..... 268.161.024   (StagingOffsetLow32)

reservas (ReservationCount=3):
  scanout .............. offset           0, len  11.059.200
  dummy page da MMU .... offset 268.431.360, len       4.096
  carveout opaco ....... offset 268.435.456, len 268.435.456  -> pulado
                                                  -------------
                          soma = 279.498.752 = RuntimeInventoryReservedLow32  ✔
```

A dummy page da MMU global é alocada `TOP_DOWN | CPU_VISIBLE` — `DummyPA` medido
`0x45FFFF000`, menos a base `0x450000000`, dá offset **268.431.360**, terminando
em **268.435.456**, exatamente o topo da janela de CPU. Logo:

```
localStart = 268.435.456  >=  localEnd = 268.161.024  ->  DEVICE_CONFIGURATION_ERROR
```

### 2.3 O defeito

O laço tratava **toda** reserva como *prefixo a pular*. A premissa vale para o
scanout, que nasce no offset 0, e é **falsa** para qualquer alocação top-down.

Agravante: a dummy page **já era tratada corretamente** ~60 linhas abaixo, onde
o código trunca `localEnd`. Ela era contabilizada **duas vezes**, e a
contabilidade que prevalecia era a errada.

---

## 3. A correção

Regra que preserva a intenção original — segmento **contíguo e sem buracos**:

- reserva que toca o **início** empurra `localStart` (prefixo);
- reserva **acima** do início trunca o **fim** (sufixo).

O passe de prefixo repete até estabilizar, porque a lista não é ordenada e uma
reserva pode virar prefixo depois que outra avança `localStart`. O teto de
passes é o próprio número de reservas.

Com os números medidos, o segmento passa a ser
`[11.059.200, 268.161.024)` = **257.101.824 bytes**, e a dummy page fica acima
dele — fora do que o VidMm recebe, que era a intenção declarada.

---

## 4. Como fica travado

**Teste** — `Bc250RunSealedSegmentReservationTest` em
`driver/tests/active_miniport_test.c`, com os números da corrida sem arredondar.
Três casos:

1. layout medido (dummy no topo) → `DRIVERCAPS` **não** pode devolver
   `DEVICE_NOT_READY`;
2. contraponto: reserva que cobre toda a faixa útil → **tem** de recusar, para a
   asserção 1 não virar carimbo;
3. reserva alta **abaixo** do plano de firmware → trunca o fim em vez de empurrar
   o início.

**Mutação 5** verificada: removendo `if (reservation->Offset > localStart)
continue;` a suíte reprova com `TESTE FALHOU (active_miniport_test)`.

Nota de escopo do teste: o `active_miniport_test` linka a variante **offline**
de `bc250_mcdm_callbacks.obj`, onde o corpo de `QUERYSEGMENT4` está compilado
fora. Por isso a asserção é feita por `DRIVERCAPS` — que é o gate que de fato
recusou o adaptador. A prova **byte a byte** do tamanho do segmento sai da
próxima corrida, na linha `[McdmQAI] QUERYSEGMENT4` do log.

---

## 5. Estado de build

```
driver/wdk/build-contract.bat          EXIT=0
tools/audit-bc250mcdm-safety.py        PASS
tools/audit-mcdm-ddi-matrix.py         PASS (STRUCTURAL ONLY)
tools/audit-ddi-noop-callbacks.py      98 anunciadas, 0 falhas
```

**Nenhum pacote novo foi construído.** O 3.92.1.0 instalado às 12:36 **já foi
revertido** pelo watchdog: BasicDisplay em Código 0, e
`C:\BC250_REQUIRE_CLEAN_BOOT.json` **armado**.

A correção do segmento sealed **NÃO ESTÁ VALIDADA EM HARDWARE** — ela exige
rebuild do SYS, novo pacote e uma corrida.

---

## 6. Próxima corrida

Critério de PASS, e desta vez com prova de bytes:

```
[McdmQAI] Type=1, ..., CapsStatus=0x00000000
[McdmQAI] QUERYSEGMENT4: 2 segments filled (VRAM=0xF52A000, ...)
```

`0xF52A000` = 257.101.824, o segmento calculado acima. Mais `DRIVERCAPS`,
`GetNodeMetadata`, `QUERYSEGMENT4` e o dispositivo em **Código 0**.

Expectativa honesta: **a tela deve continuar preta**. `D3D10_11NativeReady=0`, e
a parede do vídeo segue sendo o backend D3D10/11 na `bc250umd.dll`. Esta corrida
mede o adaptador, não o desktop.
