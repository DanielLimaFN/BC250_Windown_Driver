# Doc 742 — Corrida 290: a APU.2b fechou o 0x116; tela preta porque a composição foi recusada; APU.2c, pacote 3.440.1.0 e critério da Corrida 291

> **Data:** 13/10/2026, bind às 22:44:50, rollback online às 22:50:11 (rodada 2), sem reboot.
> **Candidato:** lab **3.439.1.0** (SYS `7B6DC0A4`), [doc 741](741_CORRIDA289_0x116_GFX_TRAVADO_SEM_AQUECIMENTO_DX43_APU2B_PACOTE_3439_E_CRITERIO_CORRIDA290.md).
> **Evidência:** `docs/evidence/2026-10-13_22-44-50-DESKTOP-HQAG6T4-lab-binding/` (`display-log-this-bind-FINAL.txt`, 708.139 linhas).
> **Observado pelo Daniel:** tela preta com o cursor do mouse funcionando e, às vezes, artefatos.

## 1. O que a APU.2b resolveu (P0)

| Sinal | 289 | 290 |
|---|---|---|
| `DX43_VMID_WARMUP_IB` | 21/21 `CABECA_ILEGIVEL` | **209 `Emitido=1`**, 0 ilegível |
| `[APU_ALLOC_CMD]` | — | 12 linhas (amostradas) |
| TDR / `PREEMPT_COMMAND` / `ResetFromTimeout` | 0x116 aos ~40 s | **0 / 0 / 0** em 297 s |
| GFXHUB_FAULT | 0 | 0 |
| Fim | BugCheck + 4 reinícios do Windows Update | watchdog, `RECOVERY_OK_ONLINE`, sem reboot |

Pelo critério do doc 741 o desfecho é **E**, porque nenhum desfecho previa tela preta sem TDR. O 0x116 da 289 não voltou. O desfecho A exigia MPO3 > 1.000, e não houve nenhum MPO3.

## 2. Por que a tela ficou preta (P0)

A composição do DWM passa pelo escape `COMPOSITE_BLT`: o KMD copia a superfície do DWM para a primária com o SDMA, por **endereço MC**. O resolvedor só aceita página dentro da VRAM (`PT_WALK_DONO RECUSADO … fora do segmento local`).

| | 288 (APU.1) | 290 (APU.2) |
|---|---|---|
| `COMPOSITE_BLT OK` | **4.907** | **0** |
| `COMPOSITE_BLT RECUSADO (resolucao)` | 98 | 160 |
| Superfícies de 11.059.200 bytes (`RESOURCE`, CPU) | quase todas `Preferido=1` | 256/256 `Preferido=2` |
| Flips MPO3 | 4.787 | 0 (só 129 por `SET_ADDRESS`) |

Exemplo: `SrcVa=0x2457C0000` do DWM resolve para `Phys=0x33A9B0000` e `0x40D379000`. As duas ficam abaixo de `VramMcBase=0x460000000`, isto é, na RAM (segmento 2). A recusa é fail-closed e correta. O defeito é a APU.2 ter mandado para a RAM as superfícies que a composição precisa ler por MC.

Na prática, nenhum quadro do DWM chegou à primária:
- o **cursor** aparece porque é cursor de hardware do DCN (D10), independente da composição;
- os **artefatos** são os 129 flips por `SET_ADDRESS` para primárias que nunca receberam a cópia, mostrando o que já estava naquela memória. Isso é H com bom suporte.

A UMD já declarava quem pode morar fora da VRAM: a flag `GPU_VA_ONLY` (M1, doc 491) marca recurso que ninguém lê por MC. A APU.2 ignorou essa regra, como já tinha ignorado a do `RADV_COMMAND` na 289.

Outros dados:
- 58.575 `IB EXECUTADO`; Tctl 76 °C; VRAM dedicada 140 MB (288: 231 MB);
- 2.878 despejos;
- 1.243 `NO_SPACE` no journal (a GPUVA.4 não basta);
- 12% de VSYNC perdidos (2.169/17.829);
- a T3 não rodou (o `BC250_PROCESSO_NOVO.log` arquivado é o da 288, das 20:19).

## 3. Correção: APU.2c (pacote 3.440.1.0)

Política em fonte única, `Bc250AllocationApuSegmentPolicy` ([bc250_allocation_abi_contract.c](../driver/core/bc250_allocation_abi_contract.c)):

| Alocação | Conjunto | Preferido | Motivo |
|---|---|---|---|
| primária | segmento 1 (ramo existente) | 1 | scanout do DCN |
| buffer de comando (`RADV_COMMAND` ou classe `COMMAND`) | 0x1 | 1 | DX.43 lê o DW0 do IB pela VRAM (289) |
| classe da UMD sem `GPU_VA_ONLY` | 0x3 | **1, 2** | a composição do KMD lê por MC (290) |
| resto (RADV e `GPU_VA_ONLY`) | 0x3 | 2, 1 | APU.2 como planejada |

- O miniport aplica a política num único ramo e registra `[APU_ALLOC] COMANDO_SO_VRAM | UMD_COMPOSICAO_VRAM_PRIMEIRO | SISTEMA_PRIMEIRO`.
- Teste: `TestApuMayPreferSystem` estendido, com os quatro casos mais o contraponto `GPU_VA_ONLY`. `build-contract` EXIT=0.
- Pacote lab **3.440.1.0**: SYS assinado `2C206FB5…` (fonte `4A10C846…`), INF `2653BBF9…`, CAT `460AF17E…`. UMD `B73F9BDA` e RADV `B4AE2E14` iguais. Auditoria do pacote e auditoria de segurança PASS.
- Roteiro: `tools/EXECUTAR_CORRIDA291_APU2C_300S.cmd`, com o preflight `tools/prepare-corrida291-apu2c.ps1`.

Limite declarado: sob pressão, o VidMm ainda pode despejar uma superfície da UMD para o 2. Nesse caso a composição recusa aquele quadro, como as 98 recusas da 288. A correção definitiva é o `COMPOSITE_BLT` copiar pelo VA do processo (SDMA na VMID dele), sem depender de onde a página mora. Fica como próximo passo.

> **Atualização (13/10, mesmo dia):** o pacote 3.440.1.0 não foi instalado. A Corrida 291 usa o **3.441.1.0** (APU.2c + COMP.VA), e o critério que vale é o do [doc 743](743_COMPVA_COMPOSICAO_PELO_VA_DO_PROCESSO_PELA_JANELA_DA_GART_PACOTE_3441_E_CRITERIO_CORRIDA291.md).

## 4. Critério da Corrida 291 (escrito antes)

| Desfecho | Assinatura no log | Implicação |
|---|---|---|
| **A** | `[APU_ALLOC] UMD_COMPOSICAO_VRAM_PRIMEIRO` presente, `COMPOSITE_BLT OK` > 1.000, `Emitido=1` sem `CABECA_ILEGIVEL`, zero TDR, desktop visível (operador) | APU.2c fecha a tela preta. Medir a fluidez contra a 288: despejos, uso do Copy, VRAM dedicada |
| **B** | `COMPOSITE_BLT OK` > 1.000 e tela ainda preta | a composição roda e o defeito está na apresentação (flip/primária); minerar `SET_ADDRESS` e MPO3 |
| **C** | `COMPOSITE_BLT RECUSADO` continua alto e a origem ainda está no segmento 2 | a origem não é classe da UMD sem `GPU_VA_ONLY`; minerar o token da origem; ir para a composição pelo VA |
| **D** | sem `[APU_ALLOC]` | a APU não armou ou o pacote não carregou |
| **E** | algo fora do mapa | descrever antes de julgar |

Antes: desligar e ligar. Depois que o desktop subir, rodar `tools\RODAR_PROCESSO_NOVO.cmd` como administrador.

## 5. Recuperação e pendências

- Driver ativo no fim: BasicDisplay em Código 0 (`RECOVERY_OK_ONLINE`, rodada 2, sem reboot).
- **NÃO VALIDADO EM HARDWARE:** APU.2c, DCN.4, LOG.2.
- A GPUVA.4 foi medida e é insuficiente: 1.243 `NO_SPACE`.
