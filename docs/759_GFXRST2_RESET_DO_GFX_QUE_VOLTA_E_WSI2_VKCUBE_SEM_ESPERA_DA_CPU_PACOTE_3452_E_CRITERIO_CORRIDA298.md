# Doc 759 — GFXRST.2 (reset do GFX que volta) e WSI.2 (vkcube sem espera da CPU), pacote 3452 e critério da Corrida 298

> **Data:** 14/10/2026 (offline).
> **Pacote:** lab **3.452.1.0** (SYS `6DF1EE05`, UMD `B1AAFF12`, RADV `86BF7F51` no build e em `C:\BC250_LIB`, assinada no pacote `E4868E70`), sobre a 3.451.1.0 da Corrida 297 ([doc 758](758_CORRIDA297_VKCUBE_REAL_PELA_GPU_E_DWM_MEMTEST_VULKAN_SEM_ERRO_E_0x116_POR_TDR_DO_GFX_QUE_NAO_VOLTA.md)).
> **Decisão do Daniel (14/10):** para a próxima corrida, o reset do GFX tem de voltar, e o vkcube não pode mais ter as duas esperas da CPU por quadro. Ele também observou que o vkcube só abriu quando ele mesmo o abriu.

## 1. Por que o reset do GFX não voltava (P0 pela evidência)

| Período | Corridas com TDR do GFX | `TDR_GFX_RESTART` provado |
|---|---|---|
| 20/09 a 07/10 09:16 (até a Corrida 240) | dezenas | **212 de 213** |
| Depois da Corrida 244 (GFXRST.1 + RLC.1) | 289 e 297 | **0 de 2** (297 medida: `RPTR=0 WPTR=12`, o CP não buscou nada) |

- Até a 240 o restart usava o reset `CP|GFX|CPF|CPG` com o RLC parado. O `pfp_direct_load` deixava `RLC_CNTL=0`.
- A 244 trouxe duas coisas: a GFXRST.1 (reset como `gfx_v10_0_soft_reset`, sem CPF/CPG) e a RLC.1 (RLC rodando o firmware). Desde então nenhum restart do GFX passou.
- No upstream, o `gfx_v10_0` **não tem `check_soft_reset`**: o amdgpu nunca usa esse soft-reset para recuperar o GFX10. Ele faz reset completo e `hw_init` (P1, `gfx_v10_0.c` local). A GFXRST.1 copiava um caminho que o amdgpu não usa para isso.
- O restart também não mandava o `cp_gfx_start` (CSB / clear state). Não escolhia o pipe 0 (`switch_pipe(PIPE_ID0)`) e não zerava o `CP_RB_VMID` antes de programar o `CP_RB0`, o que o `gfx_v10_0_cp_gfx_resume` faz.

Qual das duas mudanças da 244 quebrou o restart não está provado (H). Por isso a correção é uma escada que cobre as duas.

## 2. GFXRST.2 — escada do restart do GFX

`Bc250ActiveTdrRestartLifecycle` tenta até três etapas. Cada uma só vale com prova por execução: CSB + 4 NOP + `RELEASE_MEM` sobre poison, com `RPTR == WPTR`.

| Etapa | Reset | Por quê |
|---|---|---|
| 0 | o do quiesce (GFXRST.1, sem mudança) | o que a 289 e a 297 mediram |
| 1 | `rlc_stop`, halt do CP gráfico e do MEC, `CP|GFX|CPF|CPG|RLC`; depois `rlc_resume` (RLC.1) e `RLC_CSIB` reprogramado | amdgpu completo, com o RLC no reset |
| 2 | RLC parado (RLC.1 desarmada, CG/PG/SPM devolvidos) e `CP|GFX|CPF|CPG` | o regime das 212 provas até a 240 |

Em todas as etapas:
- `GRBM_GFX_CNTL=0` (pipe 0) e `CP_RB_VMID=0` antes do `CP_RB0`;
- o anel recomeça pelo CSB do boot (agora guardado em `GfxEngine.CsbMc/CsbDwords/CsbCpu`), como o `cp_gfx_start`;
- se a prova falha, sai uma linha `[TDR_GFX_RESTART_DIAG]` com CP_STAT, CP_ME_CNTL, CP_RB0_CNTL, POLL_CNTL, DOORBELL_CONTROL, RLC_CNTL, GRBM_STATUS, GRBM_GFX_CNTL, RB_ACTIVE e CP_RB_VMID (só leitura, registradores já usados pelo driver).

A primeira etapa que provar fica. Se nenhuma provar, o driver continua devolvendo `STATUS_DEVICE_NOT_READY`, sem fingir sucesso.

Se a etapa 2 for a que passa, o RLC fica parado dali em diante (como até a 240). O log diz isso: `[RLC1] DESARMADO (GFXRST2 etapa 2 …)`.

Portão novo no auditor de segurança, `audit_gfxrst2_restart_ladder`. A mutação "escada com 1 etapa" reprova.

## 3. WSI.2 — o vkcube sem as duas esperas da CPU

Na WSI.1 (297), cada quadro tinha duas esperas da CPU e três cópias:
1. a CPU esperava a fence do blit do app;
2. `CopyResource` + `Flush` da UMD (a UMD espera a GPU em todo flush);
3. no Present, a UMD ainda copiava a imagem dela para a alocação do back buffer, com outra espera.

Na WSI.2:
- **Uma cópia só:** o blit da RADV do app escreve direto na alocação WDDM do back buffer (GPUVA capturada no `CreateSwapChain`/`ResizeBuffers`, layout linear com o pitch da UMD).
- **Ordem pela GPU, não pela CPU:** a UMD cria duas fences monitoradas compartilhadas (`pfnCreateSynchronizationObject2Cb` com `Shared|NtSecuritySharing` + `D3DKMTShareObjects`), e a RADV as importa como semáforos timeline (`D3D12_FENCE`).
  - o blit do quadro N espera `present pronto >= N-1` e sinaliza `render pronto = N`;
  - no Present, a UMD faz `pfnWaitForSynchronizationObjectFromGpuCb(render pronto >= N)` no contexto de apresentação, chama o `pfnPresentCb` sem copiar nada, e faz `pfnSignalSynchronizationObjectFromGpuCb(present pronto = N)`.
- **Redimensionamento:** a swapchain antiga para de submeter (`retired`), o trabalho dela em voo termina (espera única por recriação, não por quadro), e o back buffer novo é recapturado.
- **Recuo:** se qualquer peça faltar (interface v2 da UMD, captura, fence, importação), a swapchain usa a WSI.1, também toda pela GPU. O log diz qual: `WSI2: ATIVO …` ou `WSI2: RECUSADO (motivo); … WSI.1`.
- Continua a espera normal do Vulkan por imagem em uso (a imagem N só é reusada quando o quadro N−3 terminou), igual em qualquer driver.

**Interface privada v2** (`driver/umd/bc250_umd_wsi.h`, espelho na Mesa): `Bc250UmdWsiCreateSharedFence`, `Bc250UmdWsiArmPresent`. A captura agora também devolve o device da UMD.

## 4. Offline

| Verificação | Resultado |
|---|---|
| `build-umd.bat` | EXIT 0 |
| RADV, rebuild completo (198 passos, `.obj` apagados) | OK |
| `build-contract.bat` | EXIT 0 |
| `build-bc250mcdm-lab-bringup.ps1` | OK |
| Pacote 3.452.1.0 (`audit-bc250mcdm-lab-package.ps1`) | PASS |
| `audit-bc250mcdm-safety.py` (com `audit_gfxrst2_restart_ladder`) | PASS |
| Mutação "escada com 1 etapa" | reprova |
| `audit-mcdm-ddi-matrix.py`, `audit-ddi-noop-callbacks.py` | PASS |

Nada disso exercita um TDR, a WSI.2 ou as fences na placa: **NÃO VALIDADO EM HARDWARE**.

## 5. Critério da Corrida 298 (escrito antes)

**Roteiro:** `tools\EXECUTAR_CORRIDA298_GFXRST2_WSI2_600S.cmd`, elevado. Antes: `inspect-boot-fuse.ps1 -Reset` se o fusível estiver disparado, desligar e ligar.
1. Esperar `VULKAN_VERIFICADO`.
2. vkcube ~30 s: cubo girando? mais fluido que na 297? Redimensionar, minimizar, fechar.
3. vkcubepp.
4. Desktop normal até o fim.

**A. WSI.2 (vkcube):**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A1** | no log da RADV do vkcube, `WSI2: ATIVO`; na UMD do vkcube, `[WSI2] fence compartilhada #0/#1` e `[WSI2] Present #N … hr=0x00000000` (espera e sinal); Presents S_OK; ritmo bem acima dos ~11 quadros/s da 297; cubo girando (operador) | as esperas da CPU saíram; o próximo gargalo é a GPU |
| **A2** | `WSI2: RECUSADO (motivo)` e o vkcube roda pela WSI.1 como na 297 | a linha nomeia a peça que faltou |
| **A3** | `WSI2: ATIVO`, mas o vkcube congela, a janela fica preta ou a imagem rasga | erro de ordem nas fences (espera que não chega ou chega antes); os valores no log dizem qual |
| **A4** | `[WSI2] … hr` de falha, ou `importacao da fence da UMD recusada` | a ponte de fences entre os dois devices não funciona assim |

**B. GFXRST.2 (só se houver TDR do GFX; a corrida não induz TDR):**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **B0** | sem `TDR_GFX_QUIESCE` | o reset não foi exercitado; segue aberto |
| **B1** | `[TDR_GFX_RESTART] … comprovado (GFXRST2 etapa K)` e `[GFXRST2] … PROVADO`, sem 0x116, desktop volta | o reset do GFX volta; K diz qual mudança da 244 quebrava |
| **B2** | `[GFXRST2] … NENHUMA ETAPA PROVOU` e 0x116 | as três falham; as linhas `[TDR_GFX_RESTART_DIAG]` mostram o estado do CP |

**E.** Fora do mapa: descrever o observado antes de julgar.

Desfecho parcial não conta: A1 exige `WSI2: ATIVO` **e** cubo girando **e** ritmo maior.

**Riscos declarados:**
- A WSI.2 depende de o escalonador ordenar uma fence de outro device do mesmo processo (P1 pela API, NÃO VALIDADO). Se a ordem falhar, o sintoma é visual (rasgo/imagem velha) ou o app parado, não a máquina.
- A etapa 1 reseta o RLC com ele rodando. A etapa 2 deixa o RLC parado até o fim do bind.

## 6. Tabela viva do DCN

Sem mudança (o DCN não foi alterado). Prontos: D01–D05, D08, D10, D15, D32–D33. Abertos: D06/D07, D11, D12–D14, D18–D23, D25–D29, D31.
