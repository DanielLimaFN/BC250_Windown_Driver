# 201 — Handoff 2026-09-08 13:00 · segmento validado no silício · o laço do VidPn

> **Corrida:** 08/09 13:00 (P0, candidato lab 3.92.1.0, SYS `7B34393F`)
> **Resultado:** Código 0 · segmento sealed **VALIDADO EM HARDWARE** · tela preta
> **Node 1:** NÃO VALIDADO EM HARDWARE

---

## 1. O que foi validado

`V5StartDeviceStatus = 0`, dispositivo em **Código 0**. Os dois números do
segmento saíram exatos, previstos **antes** da corrida:

```
RuntimeCapsBaseLow32   = 11.059.200   (0xA8C000)
RuntimeCapsLocalLow32  = 257.101.824  (0xF531000)
RuntimeCapsStatus      = 0    RuntimeCapsReady = 1
McdmFullCapsReady = 1    McdmSegment4Ready = 1

[McdmQAI] Type=1, OutSize=584, CapsStatus=0x00000000
[McdmQAI] QUERYSEGMENT4: 2 segments filled (VRAM=0xF531000, GART=512MB, PagingSeg=2)
[SetRootPageTable] VramBase=0x450000000 + LocalSegBase=0xA8C000 -> PT=0x450A8C000
[SetRootPageTable] [Vmid8Install] readback OK: PT_BASE=450a8c000 (PhysReady=1, Flush=1)
```

`0xF531000` = 257.101.824 = `268.161.024 − 11.059.200`, a conta da correção do
ramo sealed. Root page table instalada sob VMID8 com readback físico na base
nova. **Duas faixas fechadas no silício hoje:** a ordem do segundo preflight
(A16, corrida 12:36) e o segmento sealed (esta).

*(Correção de registro: no aviso pré-corrida eu escrevi o alvo como `0xF52A000`.
O hex estava errado; o valor decimal previsto, 257.101.824, é o correto e é o
que apareceu.)*

Evidência: `docs/evidence/2026-09-08_13-00-53-DESKTOP-HQAG6T4-lab-binding/`.

---

## 2. A tela preta, e a correção de dois enquadramentos meus

### 2.1 O VidPn **não** é a parede — ele converge

Eu havia lido "98 IsSupportedVidPn, 0 CommitVidPn" como *não converge*. Errado.
A mineração mostra o driver chegando ao mesmo estado final da corrida de
produção que comitou:

```
[VidPn] Path: Src=0, Tgt=1, Scale=1, Rot=1, ReqPinned=0
[VidPn] Pinned SourceMode: 2560x1080, fmt=21, stride=10240
[VidPn] Pinned TargetMode: 2560x1080, scanline=1
[VidPn] TargetMode matches GOP -> Supported=TRUE!
```

Contagem desta corrida:

| | |
|---|---|
| convergências completas (`TargetMode matches GOP`) | **39** |
| reinícios do zero (`Source unpinned`, `Scale=254 Rot=254`) | **46** |
| `CommitVidPn` / `SetVidPnSourceAddress` / `Present` | 0 / 0 / 0 |
| `GetStandardAllocationDriverData` | 0 |

É um **laço**: converge, é descartado, recomeça.

### 2.2 O que acontece entre a convergência e o descarte

```
=== IsSupportedVidPn (Call #7): Supported=1 ===
=== EnumVidPnCofuncModality (PivotType=4, PivotSrc=0, PivotTgt=1) ===
[McdmCreateProcess] ...
[McdmCreateDevice]  ...
[McdmCreateContext] Entered: Node=0
[DestroyProcess] GPUVA: faixas do processo ... invalidadas
[VidPn] Path: Src=0, Tgt=1, Scale=254, Rot=254   <- recomecou
```

8 `CreateProcess`, 9 `CreateDevice`, 8 `CreateContext` (**todos Node 0**),
7 `DestroyProcess`. Zero Node 1, zero submissão.

E o dxgkrnl registrou às 13:02:30 um `LiveKernelEvent` com
`P1=193` (0x193 `VIDEO_DXGKRNL_LIVEDUMP`), `P2=804`,
`P3=c0000001` (`STATUS_UNSUCCESSFUL`), `P4=108`. **O subcódigo 804 não foi
decodificado** — não há fonte para isso neste repositório e não vou inferir.

### 2.3 A hipótese óbvia foi REFUTADA na mesma corrida

Minha primeira leitura foi: o processo apresentador carrega `bc250umd.dll`,
recebe `E_NOTIMPL` (porque `BC250_D3D10_11_NATIVE_READY=0`) e morre.

**Refutada por ausência de evidência positiva:** nenhum `BC250_UMD_LOG_*.txt`
foi criado — em `C:\Windows\Temp` nem em lugar nenhum. A UMD **nunca foi
carregada** nesta corrida. A DLL está instalada em `System32` (63.776 bytes),
então não é falta de arquivo.

O contraste fecha o argumento. Na corrida de **produção** de hoje 08:44, a mesma
DLL carregou e recusou, e há cinco logs para provar:

```
08:44:40.659 [pid=1212 dwm.exe] === bc250umd.dll CARREGADA ===
08:44:40.660 [pid=1212 dwm.exe] OpenAdapter10_2: Interface=0x000B0011, Version=0x00060000
08:44:40.661 [pid=1212 dwm.exe] OpenAdapter10_2: D3D10/11 nativo indisponivel -> E_NOTIMPL
```

Ou seja: **são duas paredes distintas.**

- **Produção:** VidPn comita → primária existe → DWM roda → carrega a UMD →
  `E_NOTIMPL` no `OpenAdapter` → fail-fast. Essa é a parede do D3D10/11.
- **Laboratório:** VidPn converge → um processo nasce e morre → o commit nunca
  acontece → o DWM nunca chega a rodar → a UMD nunca carrega. Essa parede é
  **anterior** e ainda **não tem causa identificada**.

Confiança: a ausência de logs é **P2** (parcialmente observado) — ela seria
também compatível com alguém apagando os arquivos. O contraste com a corrida de
produção é que a torna forte.

---

## 3. A instrumentação que responde isso sem chutar

O log só tinha handles opacos: não havia como saber **quem** morre.

`DXGKARG_CREATEPROCESS` carrega `pProcessName` e `Flags` desde WDDM 2.4
(`d3dkmddi.h`), e declaramos 2.7 — o nome vem do próprio contrato, sem API não
documentada. `Bc250McdmCreateProcess` passou a registrar:

```
[McdmCreateProcess] Entered: hAdapter=..., hDxgkProcess=..., Nome=<imagem>,
                    Flags(System=u Gdi=u Vm=u VmWorker=u Isolated=u)
```

Guardas: `pProcessName` pode ser `NULL` por contrato, `ProcessNameLength` conta
**WCHARs** (não bytes), e o nome é truncado em 63 caracteres com terminador
explícito.

**Armadilha encontrada e registrada:** a cópia caractere a caractere foi
reconhecida pelo `/O2` e reemitida como `memcpy`, que não existe no link offline
das suítes de contrato — `LNK2019`. `RtlCopyMemory` tem o mesmo destino. Resolvido
com ponteiro de destino `volatile`, que desliga a redução.

---

## 4. Estado de build

```
driver/wdk/build-contract.bat          EXIT=0
tools/audit-bc250mcdm-safety.py        PASS
tools/audit-mcdm-ddi-matrix.py         PASS (STRUCTURAL ONLY)
tools/audit-ddi-noop-callbacks.py      98 anunciadas, 0 falhas
```

A instrumentação **NUNCA RODOU**: NÃO VALIDADA EM HARDWARE. Exige rebuild do
SYS, pacote novo (o INF fonte já está em 3.93.0.0, então o candidato sai
**3.93.1.0**) e uma corrida.

---

## 5. Estado da máquina

- 3.92.1.0 revertido pelo watchdog; **BasicDisplay em Código 0**.
- `C:\BC250_REQUIRE_CLEAN_BOOT.json` **ARMADO** — boot frio antes do próximo bind.

### Nota de registro: `3.92.1.0` designa **dois** artefatos

| DriverVer | SYS assinado | conteúdo |
|---|---|---|
| 3.92.1.0 | `A1973547...` | A16 (corrida 12:36) |
| 3.92.1.0 | `7B34393F...` | A16 + segmento sealed (corrida 13:00) |

Causado por eu não ter subido o INF fonte antes do empacotamento. Desambiguável
pelo hash, que o binder imprime e o manifesto registra. O INF fonte já foi para
3.93.0.0 para a colisão não se repetir.

---

## 6. Próxima corrida

Pergunta única: **quem é o processo que nasce e morre entre a convergência do
VidPn e o descarte dela?**

Critério de leitura: as linhas `[McdmCreateProcess]` passam a trazer `Nome=` e
`Flags(...)`. Com o nome em mãos a causa fica atacável — hoje ela não está.

Expectativa honesta: **a tela continua preta**. Esta corrida é de diagnóstico,
não de display.
