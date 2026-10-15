# Doc 741 — Corrida 289: BugCheck 0x116, o GFX travou sem o aquecimento da DX.43; APU.2b, pacote 3.439.1.0 e critério da Corrida 290

> **Data:** 13/10/2026. Bind às 22:01:00, BugCheck às ~22:01:40 (dump às 22:02:08).
> **Candidato:** lab **3.438.1.0** (SYS `FB1B74BF`, UMD `B73F9BDA`, RADV `B4AE2E14`), [doc 740](740_CORRIDA288_APU_UNIFIED_MEM_E_CRITERIO_CORRIDA289_FLUIDEZ_DESKTOP.md).
> **Evidência:** `docs/evidence/2026-10-13_22-00-47-DESKTOP-HQAG6T4-lab-binding/` (log do KMD recuperado: `bc250_display_log-tail-recovered.txt`, sha256 `BB3CDF02…`), Event Log do Sistema, `C:\Windows\MEMORY.DMP` (974.874.739 bytes, não analisado: exige console elevado).
> **Observado pelo Daniel:** tela preta e reinício sozinho.

## 1. O que a corrida testava

Quatro mudanças juntas (doc 740 §5): APU.2 (não primárias preferem o segmento 2), GPUVA.4 (journal de 8.192 faixas), DCN.4 (Present com formato 28) e LOG.2 (sem flush síncrono do log). A T3 não rodou (o `BC250_PROCESSO_NOVO.log` termina às 20:19, na 288).

## 2. Linha do tempo (P0)

| Hora | Fato | Fonte |
|---|---|---|
| 22:01:00 | bind, watchdog OBSERVING | `safe-bind-watchdog-journal.jsonl` |
| 22:01:02 | dwm, Explorer e dxgiadaptercache carregam a UMD | `umd-logs-pos-travamento/` |
| 22:01:23 | Antigravity.exe abre (D3D12 recusado, segue em D3D11) | `BC250_UMD_LOG_11092.txt` |
| ~22:01:35 | GFX trava na fence interna 119 (FenceId 118, IB de 680 DW, contexto `A18`), primeiro IB depois da troca de VMID 6→2; a fence 120 fica atrás | log do KMD |
| depois | `PREEMPT_COMMAND #1` no Node 1 e 32 `PREEMPT_WAIT … no ainda nao drenou` (`EmVoo=2`) | log do KMD |
| ~22:01:40 | **BugCheck 0x116** (`0xffffe0818f49a010, 0xfffff8053b6a54d0, 0xffffffffc00000a3, 0x4`) | Event Log 1001 |
| 22:02:02 | boot sujo (Kernel-Power 41) | Event Log |
| 22:02:12 → 22:03:30 | **4 reinícios do TrustedInstaller**, "Sistema operacional: atualização (planejada)" (1074) | Event Log |
| boots 2–6 | fusível recusa o StartDevice (`State=2, Boots=2..6`) | log do KMD |
| 22:05:53 | watchdog: rollback, `RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em 5 amostras | `safe-bind-final.json` |

O "reiniciou sozinho" foi um reboot pelo 0x116 seguido de quatro reinícios do **Windows Update**, que não têm relação com o driver.

Arg3 = `0xC00000A3` (`STATUS_DEVICE_NOT_READY`) é a resposta fail-closed do nosso `ResetFromTimeout` ou `RestartFromTimeout` ([bc250_mcdm_callbacks.c:4695/4711](../driver/wdk/bc250_mcdm_callbacks.c)). As linhas `[McdmResetFromTimeout]` não chegaram ao arquivo, porque o BugCheck veio antes da drenagem. Elas estão no anel de log dentro do `MEMORY.DMP` (método do [doc 607](607_FB1_MOTIVO_DA_FALHA_DO_BLT_SOBREVIVE_AO_BUGCHECK_E_CRITERIO_CORRIDA228.md)). Qual DDI recusou, e com qual engine, segue **sem resposta**.

## 3. Causa: a APU.2 tirou o IB da VRAM e desligou a DX.43

A DX.43 ([doc 373](373_CORRIDA86_DX43_RESULTADO.md)) emite um IB de aquecimento a cada troca de VMID do GFX e eliminou a classe de TDR "primeiro IB depois da troca de VMID" (7 em 96 trocas → 0). Para decidir, o KMD lê o DW0 do IB com `Bc250ActiveGfxIbReadDword` ([bc250_active_miniport.c:14502](../driver/wdk/bc250_active_miniport.c)), que **só lê dentro da VRAM** (`FrameBufferVirtual`). O contrato já dizia isso em `BC250_ALLOCATION_FLAG_RADV_COMMAND` ("O KMD le o DW0 do IB pela VRAM (DX.43…); por isso fica no 1"), e `Bc250AllocationRadvSystemSegmentPolicy` excluía o buffer de comando. A APU.2 aplicou `PreferredSegment = 2,1` a **toda** não primária, por cima dessa regra.

| | Corrida 288 (APU.1) | Corrida 289 (APU.2) |
|---|---|---|
| `DX43_VMID_WARMUP_IB` | **6.247/6.247 `EMITIDO`** | **21/21 `CABECA_ILEGIVEL`** (`Head=0`, `lido=0`) |
| TDR do GFX | 0 em 296 s | trava na 20ª troca, ~35 s depois do bind |

Confiança: a correlação é P0 (contagem nos dois logs) e o mecanismo é P1 (código e contrato). Que a falta do aquecimento seja a causa suficiente é H, com n=1.

Outros dados (P0):
- 20 flips por `SET_ADDRESS` e **zero MPO3** em 34 s (a 288 fez 4.787 MPO3): o DWM quase não compôs, o que bate com a tela preta. A relação com a APU.2 é H.
- O journal ainda teve 88 `NO_SPACE` com 8.192 faixas: a GPUVA.4 não basta.
- DCN.4 e LOG.2 não deixaram marca no log e seguem sem medida.
- OTG a 60,016 Hz; 37 de 2.046 VSYNC perdidos; latch pela IRQ do flip em 19 de 20.

## 4. Correção: APU.2b (pacote 3.439.1.0)

Decisão do Daniel (13/10): não voltar a uma mudança por corrida, e corrigir o defeito em vez de reverter a APU.2.

- Contrato puro novo, `Bc250AllocationApuMayPreferSystem` ([bc250_allocation_abi_contract.c](../driver/core/bc250_allocation_abi_contract.c)). Devolve falso para a primária, para a flag `RADV_COMMAND` e para a classe `COMMAND`.
- O ramo da APU.2 em `Bc250ActiveCreateAllocationLifecycle` usa esse contrato. O buffer de comando cai no ramo `AllocationSegmentSet` (só segmento 1) e é contado em `[APU_ALLOC_CMD]`. Texturas e recursos continuam preferindo o segmento 2.
- Teste `TestApuMayPreferSystem` em `allocation_abi_contract_test.c`, com o contraponto (textura da RADV e recurso da UMD continuam indo ao 2). `build-contract` EXIT=0.
- Pacote lab **3.439.1.0**: SYS fonte `FE4BD24E…`, SYS assinado `7B6DC0A4A4ED1DCE…`, INF `DDDD17F7…`, CAT `4EA11E55…`, UMD `B73F9BDA` e RADV `B4AE2E14` iguais. Manifesto: `DebugTrace=1`, `DcnFlip=1`, `DcnExt=125`, `ApuUnifiedMemory=1`, `GfxWaveProbe=0`. Auditoria do pacote e `audit-bc250mcdm-safety.py` PASS. A string `APU_ALLOC_CMD` está no SYS.
- Roteiro: `tools/EXECUTAR_CORRIDA290_APU2B_300S.cmd` (preflight `tools/prepare-corrida290-apu2b.ps1`).

## 5. Critério da Corrida 290 (escrito antes)

| Desfecho | Assinatura no log | Implicação |
|---|---|---|
| **A** | `[APU_ALLOC_CMD]` presente, `DX43_VMID_WARMUP_IB` com `Emitido=1` em ≥ 99% das trocas, zero TDR do GFX, MPO3 > 1.000 em 300 s | a APU.2b fecha o 0x116 da 289; seguir para a fluidez (cópias do SDMA0, DCN.4 com a T3) |
| **B** | `Emitido=1` em ≥ 99% e ainda TDR/0x116 | a DX.43 não basta com a APU.2; extrair do dump qual DDI de TDR recusou |
| **C** | `CABECA_ILEGIVEL` continua | o IB não vem de alocação `RADV_COMMAND`/`COMMAND` (outra classe ou outro dono); minerar o token da alocação do IB |
| **D** | sem `[APU_ALLOC_CMD]` | o pacote não carregou ou a APU.2 não armou (`[APU_MEM] ATIVO` ausente) |
| **E** | algo fora do mapa | descrever antes de julgar |

Antes: desligar e ligar (boot frio), CMOS em 256 MB e T3 depois do desktop (`tools\RODAR_PROCESSO_NOVO.cmd`, como administrador). Se travar, extrair o anel de log do `MEMORY.DMP` antes de nova corrida. **Também é bom deixar o Windows Update terminar antes da corrida:** os quatro reinícios do TrustedInstaller desta vez mostram que havia uma atualização pendente.

## 6. Recuperação e pendências

- Driver ativo no fim: BasicDisplay em Código 0 (`RECOVERY_OK_ONLINE`). O fusível foi removido pelo recovery.
- **NÃO VALIDADO EM HARDWARE:** APU.2b, GPUVA.4, DCN.4 e LOG.2.
- Ressalva sobre o doc 740 (outra IA): a telemetria "VDDGFX 100 mV" a 1.500 MHz não é plausível. Conferir a decodificação da SMU antes de usar como métrica.
