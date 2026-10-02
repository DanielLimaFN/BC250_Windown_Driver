# 482 — DX.90: batimento do GPU no rastro DX.56, e o critério da Corrida 142

> 27/09/2026, offline. Origem: docs 480 e 481. A trava dura das 139/140 vem ~6 s depois do início do harness D3D11, com o journal grande. Os logs não dizem se a GPU travou primeiro, se o barramento parou de responder ou se o problema está do lado da CPU. A última gravação BCR1 não tem acesso em andamento, mas quem a grava é a mesma thread que pararia numa leitura presa.

## 1. O que mudou (somente instrumentação)
- **`Bc250ActiveGpuHeartbeatProbe`** (`bc250_active_miniport.c`): lê só `GRBM_STATUS` (0x08010), `CP_STAT` (0x08680), `CP_RB0_RPTR` (0x08700) e `CP_RB0_WPTR` (0x0C150), todos catalogados no doc 03. Lê também o writeback de RPTR e a fence gráfica em memória, e copia `LastSubmittedFence`. Não escreve nada nem faz espera.
  - O acesso segue o de `Bc250ActiveGfxProbeResponsive`: diretório de adaptadores em modo compartilhado, transporte mapeado e `GfxEngine.State==1`.
  - `ReadMask` diz quais leituras voltaram.
- **Registro `BGH1`** (`bc250_mcdm_callbacks.c`, 72 bytes): gravado pela thread do rastro depois do BPX6/BCR1 de cada período de 100 ms.
  - **Fase ANTES** é gravada e descarregada (`ZwFlushBuffersFile`) antes da primeira leitura. **Fase DEPOIS** traz os valores.
  - Se a leitura não voltar, o último registro em disco é um ANTES sem DEPOIS. Esse é o dado que o BCR1 não consegue dar.
- **Decodificador** (`tools/decode-bc250-paging-trace.py`): bloco `gpu_heartbeat` com `read_never_returned`, os últimos 5 DEPOIS e `gfx_ring_idle` (RPTR==WPTR). Testado com registros sintéticos: sequência normal, ANTES órfão no fim e arquivo truncado.
- **Portão `audit_dx90_gpu_heartbeat_read_only`:** exige ANTES antes da sonda, flush em cada registro, batimento depois do BPX6/BCR1, zero escrita ou espera na sonda, o conjunto exato dos quatro registradores, sonda instalada e decodificador com o campo. **Cinco mutações, cinco reprovações.**
- **Custo:** 4 leituras de registrador e 2 gravações com flush a cada 100 ms, na thread do rastro.
- **Build:** `build-contract` EXIT 0, build de laboratório EXIT 0, auditoria de segurança PASS, auditoria de pacote PASS.

| Artefato (3.296.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `B0809726B1007FCDD79084E243D9D6282B30ABA6455D7D865F02427D7FB8A30E` |
| `bc250mcdm.sys` fonte lab | `D058619957919BD72E085257FBC8F0E7A34BED9A2E4BD918BB2EC794A56DBCA0` |
| `bc250umd.dll` (inalterada) | `3EB5806557EF3DD9E41D5FD45CD5737D8A2D6FFC06038824946BF5C327F9CFB8` |
| `bc250mcdm.inf` | `C1A5909F7E5878FA408835157F48760E8740C0F3693DE865CD81D8E0F003872D` |
| `bc250mcdm.cat` | `974A6550254AB174FCB9333C2C29008A56734632445DF059CAF9F1FF7D0212B5` |

O SYS assinado contém o magic `BGH1`; o pacote 3.295.1.0 não continha.

## 2. Critério da Corrida 142 (escrito antes)
**Runner:** `tools\EXECUTAR_CORRIDA142_DX90_600S.cmd`, **com** o harness, como nas 139/140. Boot frio antes. Usar o desktop igual.

Leitura: `python tools/decode-bc250-paging-trace.py` sobre o segmento do bind, bloco `gpu_heartbeat`. "Ocupado" é `GRBM_STATUS` com o bit GUI_ACTIVE (31) ligado.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Trava, e o último registro é **ANTES** (`read_never_returned=true`) | O barramento/GPU parou de responder a MMIO. Os DEPOIS anteriores dizem em que estado o anel gráfico estava. |
| **B** | Trava; últimos DEPOIS com `CP_RB0_RPTR` parado atrás de `WPTR` por vários ticks, fence em memória parada e `GRBM_STATUS` ocupado | A GPU travou no trabalho gráfico antes da máquina. Próximo: IB/estado do harness (offline). |
| **C** | Trava; últimos DEPOIS com anel ocioso (RPTR==WPTR, fence == `LastSubmittedFence`) até o fim | GPU ociosa: o problema está do lado da CPU/SO. Próximo: caminho de CPU do harness e do Present. |
| **D** | 600 s sem trava, com o harness | A trava não reproduziu, ou a própria instrumentação mudou o tempo. Não conclui. Repetir ou revisar. |
| **E** | Algo fora do mapa, inclusive BGH1 ausente ou `measured=0` em todos | Descrever antes de julgar. BGH1 ausente = defeito da instrumentação. |

Controle a não regredir: desktop visível, DWM apresentando, paginação ENTER==LEAVE, zero TDR.

**NADA DA DX.90 FOI VALIDADO EM HARDWARE.**
