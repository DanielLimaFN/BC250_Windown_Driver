# 204 — Handoff 2026-09-08 14:26 · o `CommitVidPn` aconteceu · resta uma parede

> **Corrida:** 08/09 14:26 (P0, candidato lab **3.94.1.0**, SYS `8CA949DC`)
> **Resultado:** Código 0 · **cadeia de display completa pela 1ª vez no perfil lab** · tela preta
> **Node 1:** NÃO VALIDADO EM HARDWARE

---

## 1. A hipótese foi confirmada pelo critério escrito antes da corrida

No handoff 202 o critério ficou registrado: `DmaBuf=0x10000, SegSet=0x2`, o ciclo
`CreateProcess → DestroyProcess` parar, e `CommitVidPn` aparecer. Bateu inteiro.

| | 14:06 (antes) | **14:26 (depois)** |
|---|---|---|
| `DmaBuf` / `SegSet` publicados | `0x0` / `0x0` | **`0x10000` / `0x2`** |
| `CreateProcess` / `DestroyProcess` | 8 / 7 | **2 / 0** |
| convergências / reinícios do VidPn | 39 / 46 | **8 / 8** |
| `CommitVidPn` | 0 | **1** |
| `RECUSADO` (qualquer DDI) | 0 | 0 |

O laço que consumiu quatro corridas **acabou**. A causa era a ABI de contexto
publicada zerada, corrigida pela fonte única `Bc250ActiveInitializeContextAbi`
(ver doc 202 §4).

Evidência: `docs/evidence/2026-09-08_14-26-09-DESKTOP-HQAG6T4-lab-binding/`.

---

## 2. A cadeia de display inteira acordou

Pela primeira vez no perfil `DISPLAY_RENDER_LAB`:

```
=== CommitVidPn Entered: AffectedSource=0, Started=1, hAlloc=... ===
[VidPn] Path: Src=0, Tgt=1, Scale=1, Rot=1, ReqPinned=1
[VidPn] Pinned SourceMode: 2560x1080, fmt=21, stride=10240
[VidPn] TargetMode matches GOP -> Supported=TRUE!
[SetTargetAdjustedColorimetry2] TargetId=1, SdrWhiteLevel=80
=== SetVidPnSourceAddress (SourceId=0, Segment=1, Addr=0xAAC000, Flags=0x00000001) ===
=== SetVidPnSourceVisibility (Visible=1) ===
```

E depois, com status 0 em todas:

| DDI | ocorrências |
|---|---|
| `GetStandardAllocationDriverData` | 60 |
| `CreateAllocation` | 20 |
| `OpenAllocation` | 30 |
| `CloseAllocation` | 8 |
| `ControlInterrupt` VSYNC | **367** |

`V5StartDeviceStatus=0`, `RuntimeCapsStatus=0`, `V5LifecycleState=1`, **zero
`StopDevice`**, zero BugCheck. O adaptador ficou vivo e estável até o rollback
programado do watchdog.

O estado bom das faixas anteriores se manteve: `SecondPreflightStage=8`,
`gate=1`, base `0xA8C000` e segmento `0xF531000`.

---

## 3. A parede nova — que é a parede **antiga**, da produção

```
14:26:12 … 14:26:54   dwm.exe   0xe0464645   KERNELBASE.dll+0x12d862   (8 falhas)
```

O `dwm.exe` **agora roda** neste perfil, e fail-fasta com exatamente a assinatura
já documentada para a corrida de produção. `Present = 0` porque o DWM não
sobrevive para apresentar.

**O que isso significa em termos de projeto:** até hoje havia **duas** paredes —
a de produção (backend D3D10/11, conhecida) e a de laboratório (um processo que
nascia e morria, **sem causa identificada**). Os dois perfis **convergiram**.
Resta **uma** parede para o desktop, e ela tem nome, arquivo e linha:
`Bc250Umd10CreateDevice` devolve `E_NOTIMPL` porque
`BC250_D3D10_11_NATIVE_READY = 0` — fail-closed deliberado pela Lei 4.

### 3.1 Ressalva que manda no veredito

**Nenhum `BC250_UMD_LOG_*.txt` foi criado nesta corrida**, apesar de o `dwm.exe`
ter rodado e morrido oito vezes. Portanto:

- que o DWM fail-fasta é **P0** (evento de aplicação, oito registros);
- que ele fail-fasta **por causa da recusa da nossa UMD** é **P2**: a assinatura
  é idêntica à da produção, onde há cinco logs provando o `E_NOTIMPL` no
  `OpenAdapter10/10_2`. Mas se nesta corrida o `OpenAdapter` chegou a ser
  consultado antes do fail-fast é **INDETERMINADO**.

Não inferir isso é deliberado. O módulo da falha é `KERNELBASE` (`RtlFailFast`),
compatível com o caminho `CreateD3D11Device → DXGI_ERROR_UNSUPPORTED → fail-fast`
já documentado por dump, mas compatibilidade não é prova.

O `LiveKernelEvent P1=193 (0x193 VIDEO_DXGKRNL_LIVEDUMP) P2=804 P3=c0000001 P4=108`
reapareceu às 14:28:32. **O subcódigo 804 continua não decodificado** — não há
fonte para isso neste repositório.

---

## 4. Estado de build e da máquina

```
driver/wdk/build-contract.bat          EXIT=0
tools/audit-bc250mcdm-safety.py        PASS
tools/audit-mcdm-ddi-matrix.py         PASS (STRUCTURAL ONLY)
tools/audit-ddi-noop-callbacks.py      98 anunciadas, 0 falhas
```

Pacote **3.94.1.0** em disco (SYS assinado `8CA949DC`), revertido pelo watchdog.
BasicDisplay em Código 0 e `C:\BC250_REQUIRE_CLEAN_BOOT.json` **armado**.

---

## 5. Próxima etapa, e ela ficou barata

O gate `compute-display-prerequisite-gate` do runner do Node 1 exige primária,
`CommitVidPn` e `SetVidPnSourceAddress`. **Os três agora acontecem.** Em todas as
corridas anteriores o runner era corretamente marcado `UMD_NODE1_SKIPPED` porque
o pré-requisito não existia.

O pacote 3.94.1.0 **já está em disco e é o mesmo** que produziu o `CommitVidPn`.
Basta boot frio e o mesmo bind, acrescentando `-RunUmdNode1Tripod`.

O **Node 1 está sem qualquer medição desde o início do projeto**. Qualquer
desfecho — tripé provado, recusa nomeada, ou timeout — é informação nova.

Depois disso, o backend D3D10/11 deixa de ser "uma das paredes" e passa a ser
**a** parede. Ele não é uma corrida: é construção, e merece fase própria com
escopo e critério de aceitação escritos antes de começar.

**Node 1: NÃO VALIDADO EM HARDWARE. Jogos: NÃO VALIDADOS EM HARDWARE.**
