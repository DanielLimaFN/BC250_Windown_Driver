# 290 — Critério da Corrida 36 — Onde a onda para

> **Escrito em 19/09/2026, ANTES do ensaio.** Candidato lab **3.194.1.0**, com a sonda de onda **ARMADA**.
> **Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3) | Windows 10 Pro 22H2 build 19045
> **Pergunta única:** *as seis ondas presas estão paradas em qual instrução, e esperando o quê?*
> **Critério anterior:** [docs/289](289_CRITERIO_CORRIDA_35_ONDE_O_CP_PARA.md) — fechou no Desfecho 1.

---

## 1. De onde viemos, em uma linha

A Corrida 35 respondeu *onde o CP para*: ele **não** para esperando sincronização. Mediu
`SQ_DEBUG_STS_GLOBAL=0x00030031 WaveLevelSA0=3 WaveLevelSA1=3` — **seis ondas em voo que não
retiram** — com `CB_CLEAN=1` nos dois shader engines e `SemPolling=0 EopDoneBusy=0
CoherCntNeqZero=0`. Esta corrida pergunta a próxima coisa: **onde essas ondas estão**.

**Tela preta continua sendo o resultado esperado e correto.** Nada nesta faixa produz pixel.
Qualquer imagem deve ser atribuída ao rollback/BasicDisplay até prova em contrário.

---

## 2. O que mudou desde a Corrida 35

| # | Mudança | Onde | Efeito |
|---|---|---|---|
| 1 | Sonda de onda (PC, IB_STS, INST_DW0, TRAPSTS) | KMD, quiesce do TDR | linhas `[TDR_GFX_WAVEPC]` |
| 2 | Despejo de IB alcança o `dwm.exe` | RADV + runner, via `HKLM\SOFTWARE\BC250\Umd\GfxIbDumpDir` | `.bin` na pasta de evidência |
| 3 | Runner recusa exit 0 sem prova de quadro | `test-d3d11-native-conformance.ps1` | `[SEM MEDICAO]`, exit 3 |

### ⚠️ A sonda de onda precisa ser ARMADA, e ela ESCREVE registrador

É a única parte desta faixa que não é somente leitura. Escreve dois **seletores** —
`GRBM_GFX_INDEX` fora de broadcast e `SQ_IND_INDEX` — e restaura o broadcast com readback.

**Risco declarado:** se a restauração falhar, o soft-reset que o quiesce dispara logo em seguida
pode atingir **um único shader engine** em vez de todos. O contrato devolve `SELECTOR_STUCK` e o
log grita `[TDR_GFX_WAVEPC] ALERTA`, mas o risco é real e é por isso que o default é 0.

**NESTE CANDIDATO ELA ESTÁ ARMADA.** O INF empacotado traz
`HKR,Parameters,GfxWaveProbe,0x00010001,1`, e o manifesto deriva disso
(`GfxWaveProbe=1`, `GfxWaveProbeArmed=True`). Autorizado pelo dono do projeto em 19/09.

Todos os demais tunables de risco continuam em **0** no mesmo INF — `TdrInduceHang`,
`GpuvaLabPromotion`, `SdmaPlanoAFallback` e `GfxRingBringup`. Uma variável se move nesta corrida,
não cinco.

---

## 3. Critério, escrito antes — quatro desfechos

### Desfecho A — A ONDA TEM ENDEREÇO (o alvo vira o ISA)

```
[TDR_GFX_WAVEPC] onda #0 SE=.. SA=.. Inst=.. Wave=.. STATUS=0x........
                 PC=0x................ IB_STS=0x........ INST_DW0=0x........
```

`INST_DW0` é a instrução em que a onda está. Se for `0xBF8C....` a onda está num **`s_waitcnt`**
— e `IB_STS` diz por qual contador (`VM_CNT` nos bits 3:0, `EXP_CNT` 6:4, `LGKM_CNT` 11:8). Isso
nomeia a espera: `vmcnt` aponta para busca de memória vetorial que não volta (vertex fetch ou
descriptor), `lgkmcnt` para `s_load` de descritor.

**Próximo passo:** `RADV_DEBUG=asm` na corrida seguinte e casar o PC com o disassembly.

### Desfecho B — NENHUMA ONDA LOCALIZADA (a varredura rasa não alcança)

```
[TDR_GFX_WAVEPC] NENHUMA onda localizada nesta varredura rasa -- STATUS zero e
                 INDISTINGUIVEL de slot vazio, entao isto NAO refuta as N ondas que o SQ contou
```

A varredura visita 2 SE × 2 SA × 4 instâncias, apenas wave 0. As ondas podem estar em outras
instâncias ou em slots de wave maiores. **Isto não refuta nada** — e o log diz isso, de propósito.
**Próximo passo:** ampliar a varredura, com o orçamento recalculado.

### Desfecho C — O SELETOR NÃO VOLTOU (o pior, e tem de ser visível)

```
[TDR_GFX_WAVEPC] ALERTA: GRBM_GFX_INDEX NAO voltou a broadcast (0x........)
```

O soft-reset subsequente é suspeito. **Toda medição posterior desta corrida fica sob ressalva**, e
a sonda deve ser desarmada até a causa ser entendida.

### Desfecho D — SEM ONDA (ou, se aparecer, DESARMADO)

```
[TDR_GFX_WAVEPC] NAO EXECUTADO: SqLido=.. OndasEmVoo=0
```

Nenhuma escrita foi feita. Se for `OndasEmVoo=0` com o CP travado, o quadro da Corrida 35 **não
se reproduziu** e o suspeito volta a ser o pacote.

Se aparecer `[TDR_GFX_WAVEPC] DESARMADO: GfxWaveProbe=0`, algo apagou o tunable entre o install e
o `StartDevice` — o pacote saiu com 1, então essa linha seria um achado sobre o caminho de
registro, não sobre a GPU.

### E, em paralelo, o despejo do IB

Se `gfx-ib-dump/` tiver arquivos, a linha `[TDR_GFX_PACKET]` do KMD cruza com eles:

```bash
python tools\decode-gfx-ib.py docs\evidence\<pasta>\gfx-ib-dump\<arquivo>.bin --stopped-at-opcode 0x<OPCODE> --remaining-dw <N>
```

Se continuar vazio **mesmo com a chave de registro armada**, o defeito é da resolução por registro
e não do processo — e aí a linha `radv/wddm2: BC-250 IB_DUMP armado em <dir>` dirá se o RADV a viu.

---

## 4. Medições de controle

| Controle | Valor da Corrida 35 |
|---|---|
| `GFXHUB_FAULT` | zero |
| Node 0 SDMA | 202 IBs, 188 `TRIPE_PROVADO` |
| BugCheck | zero |
| `TDR_GFX_RESTART` | aprovado, `CpMeCntl=0x00000000` |
| Rollback | online, `BasicDisplay Código 0` |

Regressão em qualquer um manda no veredito.

---

## 5. O que esta corrida NÃO responde

- **Não acende a tela** e não prova rasterização. O harness não lê pixel de volta (Lei 2).
- **Não corrige o descriptor set que falta.** A leitura offline de 19/09 constatou, por código,
  que `vkAllocateDescriptorSets`, `vkUpdateDescriptorSets`, `vkCmdBindDescriptorSets` e
  `vkCreateDescriptorPool` são **carregados e nunca chamados** na UMD. É defeito real (P0 por
  leitura), mas **não é causa suficiente** do travamento: o triângulo do harness não usa constant
  buffer nem textura e travou igual na Corrida 34. Fica como **dívida declarada e aberta**.
- **Não dá o ISA.** `RADV_FORCE_FAMILY` foi removido neste Mesa e não há compilador ACO
  standalone; o disassembly exige `RADV_DEBUG=asm` numa corrida, que é o passo seguinte.

---

## 6. Preparação e comandos

```bash
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
```

```bash
powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode
```

```bash
powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-lab-package.ps1
```

**O pacote 3.194.1.0 já está construído, assinado e auditado (§7). NÃO INSTALADO.** Os dois
primeiros comandos acima já foram executados; ficam registrados para reprodução.

O arme saiu por `-ArmGfxWaveProbe -IAcceptSelectorWritesDuringTdr`, que materializa `1` no INF do
candidato **antes** do empacotamento. Isso é deliberado: o KMD lê o tunable uma única vez, no
`StartDevice`, e escrever no registro depois do bind só valeria no start seguinte — a mesma ordem
que custou uma corrida no ensaio de TDR em 07/09 (doc 186). O manifesto **deriva do INF final** e
o empacotador recusa se os dois divergirem em qualquer direção.

Antes do bind: `tools\inspect-boot-fuse.ps1` (e `-Reset` se `TRIPPED`) e **boot frio obrigatório**.
Recuperação: `tools\BC250_RECOVERY.cmd` elevado.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**

---

## 7. Pacote — 3.194.1.0, assinado, auditado, NÃO INSTALADO

`MCDM_LAB_BRINGUP` · `DISPLAY_RENDER_LAB` (RenderNode=1, RenderOnlyLab=0) · WDDM 2.7 ·
P1 PERSISTENT SUBMISSION · schema 135 · **GfxWaveProbe=1 (ARMADO)**

| Artefato | SHA-256 |
|---|---|
| SYS (pacote, assinado) | `60FD95A55DA6050357C97D143DE3D16F1D8FB8B30E2000C5D7110732C977740A` |
| UMD | `372FEBCFD24C7B2217A0413B3DFEDF35A9D475D02DADF5CAADD8787DF412D678` |
| INF | `661AEC0D27BBCE6BDE9A9C0876B00D1E2E6F885112BA23E5D76ACD8AC788D3FB` |
| CAT | `C5FBA1CCEBBA7A321847C47A9577BA5F70C9EAB836EBBD994380AE1BA51C8A4D` |
| `vulkan_radeon.dll` (em `C:\BC250_LIB\`) | `6ff3202f29f643d8b5f791ee53867109022a4e255dd15518930531700b747bd8` |

`audit-bc250mcdm-lab-package.ps1`: **PASS**.

**Conferido por string no SYS assinado** (o empacotador copia, não compila): `TDR_GFX_WAVEPC`,
`DESARMADO: GfxWaveProbe`, `ALERTA: GRBM_GFX_INDEX`, `selector_stuck`, mais as marcas da faixa
anterior `TDR_GFX_PACKET` e `TDR_GFX_CPWAIT`. A UMD é a mesma desde 3.191.1.0 — nenhuma fonte de
UMD foi tocada, de propósito.

**Nota de versão:** o 3.193.1.0 também existe em disco, com a sonda DESARMADA e INF
`9FD1BC19…`. São dois artefatos distintos, cada um com seu `DriverVer` — a confusão de dois
binários sob a mesma versão já foi erro nosso uma vez (3.92.1.0) e não se repete aqui. Use o
**3.194.1.0**.
