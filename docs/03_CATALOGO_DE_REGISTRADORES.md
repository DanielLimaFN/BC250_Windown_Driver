# Catálogo de registradores e endereços BC-250

## Regra de cálculo

Para os registros GC10 do header `gc_10_1_0_offset.h`, o endereço final no BAR5 é:

```text
BAR5_byte = (segment_base_dw[BASE_IDX] + mmREG_dw) * 4

BASE_IDX 0 -> segment_base_dw = 0x1260
BASE_IDX 1 -> segment_base_dw = 0xA000
```

Exemplo de controle:

```text
mmCP_ME_CNTL = 0x0F56, BASE_IDX=0
(0x1260 + 0x0F56) * 4 = 0x086D8
```

O erro histórico recorrente foi misturar dwords e bytes, somar a base depois de multiplicar apenas `mmREG`, ou usar offsets de outra geração. O driver novo deve armazenar `mmREG` e `BASE_IDX`, gerar o byte offset em build/runtime e verificar que ele cabe no BAR mapeado.

## Classes

- **P0**: observado com evidência forte na unidade.
- **P1**: derivado de header upstream e fórmula SOC15; comportamento ainda requer teste.
- **P2**: usado/observado parcialmente, mas faltam provas ou há conflito de fonte.
- **Q**: endereço histórico em quarentena.

“Gravável” significa apenas write-stick quando indicado. Não implica que a escrita tenha o efeito funcional desejado.

## Domínios de endereço

| Domínio | Valor observado | Classe | Nota |
|---|---:|---:|---|
| tamanho BAR5 MMIO | `0x00080000` | P0 | 512 KiB; validar por PnP em cada unidade. |
| tamanho BAR0 visível | `0x10000000` | P0 | 256 MiB na unidade medida; não fixar. |
| BAR0 físico da sessão 2026-08-22 | `0xC0000000` | P0 | Abertura física CPU; não confundir com endereço GPU/MC. |
| base MC da VRAM | `0xF400000000` | P2 | Endereço GPU/MC usado nos testes; detectar no hardware. |
| base física BAR5 observada | `0xFE800000` | P0 | Confirmada novamente em 2026-08-22; nunca hardcode. |
| base física BAR0/VRAM | varia | P0 como recurso | Usar recurso PnP traduzido. |
| MP0 IP base em dwords | `0x16000` | P1 | Multiplicado por 4 produz base byte `0x58000`. |
| MP0 C2PMSG base byte | `0x58000` | P0 | Mailbox vivo demonstrado. |
| GC segmento 0 em dwords | `0x1260` | P1/P0 | Derivado de Cyan Skillfish e conferido por registros vivos. |
| GC segmento 1 em dwords | `0xA000` | P1/P0 | Segundo segmento SOC15. |

Endereços como `0xF40F800000` (TMR), offsets de ring/fence dentro da VRAM e PAs retornados por alocação são endereços de dados, não registros. Devem ser alocados/detectados a cada boot.

## Acesso indireto SMN

| Símbolo | BAR5 byte | Classe | Uso |
|---|---:|---:|---|
| `RSMU_INDEX` / `A_NB_SMN_INDEX` | `0x00038` | P0 | Escrever endereço SMN. |
| `RSMU_DATA` / `A_NB_SMN_DATA` | `0x0003C` | P0 | Ler/escrever dado SMN. |
| `MMIO_INDEX` | `0x00000` | P2 | Janela MMIO indexada usada nas tentativas. Não confundir com GPU ID. |
| `MMIO_DATA` | `0x00004` | P2 | Dado da janela MMIO indexada. |

Toda sequência `INDEX/DATA` deve ser serializada por um único lock compartilhado por SMU/SMN.

## PSP MP0 / GPCOM / caminho direto

| Registro | BAR5 byte | Classe | Evidência/uso |
|---|---:|---:|---|
| `C2PMSG_35` | `0x5818C` | P0/P2 | conclusão/estado do bootloader no caminho direto; observado `0xFFFFFFFF`. |
| `C2PMSG_36` | `0x58190` | P2 | endereço baixo do firmware no protocolo direto. |
| `C2PMSG_37` | `0x58194` | P2 | endereço alto/tamanho conforme comando direto; confirmar layout por operação. |
| `C2PMSG_64` | `0x58200` | P0 | TOS/command/ACK; `0x80020000` com ring legado e `0x80000000` no boot limpo sem ring. |
| `C2PMSG_67` | `0x5820C` | P0 | WPTR do ring; avançou 16 dwords por frame. |
| `C2PMSG_69` | `0x58214` | P0 | PA baixa do ring GPCOM. |
| `C2PMSG_70` | `0x58218` | P0 | PA alta do ring GPCOM. |
| `C2PMSG_71` | `0x5821C` | P0 | tamanho do ring. |
| `C2PMSG_81` | `0x58244` | P0 | status SOS não-zero; valor exato varia. |
| `C2PMSG_101` | `0x58294` | P1 | offset relativo `0x294` aparece no header PSP; uso não validado. |

Valores/protocolo associados:

| Item | Valor | Nota |
|---|---:|---|
| TOS/ACK bit | `0x80000000` | Testar com máscara, preservar campos de tipo/status. |
| tipo de ring KM | `2` (`0x00020000`) | Eco observado em C2PMSG_64. |
| destruir ring GPCOM | `0x000C0000` | Usado antes de recriar ring velho. |
| tamanho de frame | `64` bytes / `16` dwords | Estrutura PSP. |
| tamanho de ring usado | `0x1000` | Alocação de teste; não é registrador. |
| tamanho declarado do command buffer | `0x400` | Campo `buf_size`; resposta em byte `+864`. |

### Endereços PSP em quarentena

| Endereço antigo | Classe | Motivo |
|---:|---:|---|
| `0x103D0`, `0x1055C`, `0x10560`, `0x10564`, `0x105D0`, `0x10614` | Q | Candidatos antigos resultantes de base/unidade errada. Retornavam valores plausíveis, mas não eram o mailbox vivo. |

## SMU/MP1 por SMN

### Queue 0

| Campo | endereço SMN | Classe |
|---|---:|---:|
| CMD / C2PMSG_66 | `0x03B10A08` | P0 |
| ARG / C2PMSG_82 | `0x03B10A48` | P0 |
| RSP / C2PMSG_90 | `0x03B10A68` | P0 |

### Queue 3

| Campo | endereço SMN | Classe |
|---|---:|---:|
| CMD | `0x03B10A20` | P0/P2 |
| RSP | `0x03B10A80` | P0/P2 |
| ARG | `0x03B10A88` | P0/P2 |

### Queue 2 / GFXOFF - requer nova auditoria

| Campo | endereço SMN | Classe | Nota |
|---|---:|---:|---|
| Q2 CMD / registro citado para GFXOFF | `0x03B10528` | P2 | Foi tratado tanto como mailbox quanto como registro com bit 2. Confirmar no código SMU/firmware antes de escrever. |
| Q2 RSP | `0x03B10564` | P2 | Usado nas tentativas. |
| Q2 ARG | `0x03B10998` | P2 | Usado nas tentativas; relação com CMD/RSP precisa ser revalidada. |
| Q2 ARG_HI | `0x03B1099C` | P2 | Usado nas tentativas. |

Mensagens são IDs, não endereços. As mais relevantes registradas foram Q0 `0x01/02/03/0F/1E/37/38/3D`, controle `0x39/3A/3B/3C`, e Q3 `0x1E/8B/8C/8F/98/9A`. Cada ID é dependente da queue e deve ser whitelisted com faixa de argumentos.

O BC-250 `1002:13FE` e identificado pelo upstream como Cyan Skillfish2 e usa
SMU 11.8. O header `smu_v11_8_ppsmc.h` nao define `AllowGfxOff` nem
`DisallowGfxOff`: `0x1D` e `ClearTelemetryMax`, e `0x1E` e
`QueryActiveWgp`. Portanto `0x14`, `0x15`, `0x1D` e `0x1E` ficam proibidos
como comandos de GFXOFF neste ASIC. Os IDs 0x1D/0x1E do ABI Van Gogh/SMU
11.5 nao podem ser transplantados para Cyan Skillfish2.

### SMUIO - readback físico de GFXOFF

| Registro | base byte | mmREG | BAR5 byte | Classe | Estado observado |
|---|---:|---:|---:|---:|---|
| `SMUIO_GFX_MISC_CNTL` | `0x5A000` | `0x00C5` | `0x5A314` | Q | Derivacao e semantica pertencem ao caminho Van Gogh; leituras `0` no BC-250 nao decodificam estado GFXOFF e nao autorizam escrita. |

Derivacao historica: `0x5A000 + 0x00C5 * 4 = 0x5A314`. A decodificacao
`PWR_GFXOFF_STATUS[2:1]` existe no `vangogh_ppt.c`, mas nao no
`cyan_skillfish_ppt.c`; por isso ela nao e evidencia valida no BC-250.

## GC10.1.3 - status, seleção e topologia

| Registro | mmREG | idx | BAR5 byte | Classe | Estado observado |
|---|---:|---:|---:|---:|---|
| `GRBM_STATUS2` | `0x0DA2` | 0 | `0x08008` | P1/P0 | observado `0x00000008` em V3. |
| `GRBM_STATUS` | `0x0DA4` | 0 | `0x08010` | P1/P0 | observado `0x00003028`; usar bits, não igualdade textual. |
| `GRBM_GFX_CNTL` | `0x0DC2` | 0 | `0x08088` | P1 | seleciona ME/PIPE/QUEUE no caminho GFX; não é `GRBM_GFX_INDEX`. |
| `CP_MEC_CNTL` | `0x0E2D` | 0 | `0x08234` | P1/P0 | observado `0x50000000` (MEC halted). |
| `CP_STAT` | `0x0F40` | 0 | `0x08680` | P1/P0 | observado `0`. |
| `CP_ME_CNTL` | `0x0F56` | 0 | `0x086D8` | P1/P0 | observado `0x15000000` nos baselines contaminado e limpo; decodificar máscaras antes de qualquer escrita. |
| `CC_GC_SHADER_ARRAY_CONFIG` | `0x100F` | 0 | `0x089BC` | P1/P0 | `0xFFF80000`; fonte da contagem de 24 CU na unidade. |
| `SPI_PG_ENABLE_STATIC_WGP_MASK` | `0x1277` | 0 | `0x0935C` | P1/P2 | observado `0x0000FFFF`; write-stick aceitou `0x1F`, efeito funcional não provado. |
| `COMPUTE_DISPATCH_INITIATOR` | `0x1BA0` | 0 | `0x0B800` | P1 | não cria fila sozinho. |
| `COMPUTE_PGM_LO` | `0x1BAC` | 0 | `0x0B830` | P1 | programa endereço baixo do shader conforme formato GC10. |
| `COMPUTE_USER_DATA_0` | `0x1BE0` | 0 | `0x0B900` | P1 | início de user data. |
| `SCRATCH_REG0` | `0x2040` | 1 | `0x30100` | P1/P0 | round-trip host MMIO demonstrado. |
| `SCRATCH_REG1` | `0x2041` | 1 | `0x30104` | P1/P0 | round-trip/adjacente usado em diagnóstico. |
| `GRBM_GFX_INDEX` | `0x2200` | 1 | `0x30800` | P1/P2 | seleção SE/SH/instance; restaurar broadcast após teste. |
| `RLC_PG_ALWAYS_ON_WGP_MASK` | `0x4C53` | 1 | `0x3B14C` | P1/P2 | observado `0x00000003`. |
| `RLC_CP_SCHEDULERS` | `0x4CAA` | 1 | `0x3B2A8` | P1/P2 | leitura física Windows `0x58504840`; candidato 3.14 decodifica `scheduler0` existente e acrescenta somente `ENABLE`, resultando neste boot em `0x585048C0` e seletor `ME2/P0/Q0=0x8`. Dump Linux BC-250 mede `0x5850C0C8`. |

### Diagnóstico read-only MEC/HQD pós-doorbell (candidato 3.11)

| Registro | mmREG | idx | BAR5 byte | Leitura diagnóstica |
|---|---:|---:|---:|---|
| `CPC_UTCL1_ERROR` | `0x1DFF` | 0 | `0xC17C` | halt/fault do caminho de tradução CPC. |
| `CP_ME1_PIPE0_INT_STATUS` | `0x1E2D` | 0 | `0xC234` | interrupções/faults do pipe MEC1/P0. |
| `CP_ME2_PIPE1_INT_STATUS` | `0x1E32` | 0 | `0xC248` | interrupções/faults da KIQ Linux MEC2/P1. |
| `CP_ME2_PIPE0_INT_STATUS` | `0x1E31` | 0 | `0xC244` | interrupções/faults da atribuição scheduler0 deste boot MEC2/P0. |
| `CPC_INT_STATUS` | `0x1E55` | 0 | `0xC2D4` | status global de interrupção CPC. |
| `CPC_UTCL1_STATUS` | `0x1F55` | 0 | `0xC6D4` | atividade/tradução UTCL1 do CPC. |
| `CPF_UTCL1_STATUS` | `0x1F56` | 0 | `0xC6D8` | atividade/tradução UTCL1 do CPF. |
| `CP_HQD_HQ_STATUS0` | `0x1FC9` | 0 | `0xC8A4` | `DEQUEUE_STATUS[1:0]`, `DEQUEUE_RETRY_CNT[3:2]`, `SCRATCH_RAM_INIT[7]`, `TCL2_DIRTY[8]`, `PG_ACTIVATED[9]`, `QUEUE_IDLE[30]`, `DB_UPDATED_MSG_EN[31]` (`gc_10_1_0_sh_mask.h`). **Evidência diferencial 3.29 → 3.32:** `0x40008000` → `0x00008000`, `QUEUE_IDLE` 1→0 com a fila realmente ocupada. |
| `CP_HQD_HQ_CONTROL0` | `0x1FCA` | 0 | `0xC8A8` | controle observável do HQD selecionado. |
| `CP_HQD_ERROR` | `0x1FDC` | 0 | `0xC8F0` | faults PQ/IB/EOP/RPTR/WPTR/UTCL1 por fila. |

Os nove registros de fault/status permanecem somente leitura. Apenas
`RLC_CP_SCHEDULERS=0x3B2A8` recebe escrita no candidato 3.12, limitada pelo
contrato a RMW do byte baixo, readback integral e restauração do snapshot.

### Bloco COMPUTE completo derivado

| Registro | mmREG | BAR5 byte | Classe |
|---|---:|---:|---:|
| `COMPUTE_DISPATCH_INITIATOR` | `0x1BA0` | `0x0B800` | P1 |
| `COMPUTE_DIM_X/Y/Z` | `0x1BA1/1BA2/1BA3` | `0x0B804/0B808/0B80C` | P1 |
| `COMPUTE_START_X/Y/Z` | `0x1BA4/1BA5/1BA6` | `0x0B810/0B814/0B818` | P1 |
| `COMPUTE_NUM_THREAD_X/Y/Z` | `0x1BA7/1BA8/1BA9` | `0x0B81C/0B820/0B824` | P1 |
| `COMPUTE_PGM_LO/HI` | `0x1BAC/1BAD` | `0x0B830/0B834` | P1 |
| `COMPUTE_PGM_RSRC1/2` | `0x1BB2/1BB3` | `0x0B848/0B84C` | P1 |
| `COMPUTE_RESOURCE_LIMITS` | `0x1BB5` | `0x0B854` | P1 |
| `COMPUTE_STATIC_THREAD_MGMT_SE0/1` | `0x1BB6/1BB7` | `0x0B858/0B85C` | P1 |
| `COMPUTE_TMPRING_SIZE` | `0x1BB8` | `0x0B860` | P1 |
| `COMPUTE_USER_DATA_0` | `0x1BE0` | `0x0B900` | P1 |

Esses registros configuram estado de dispatch; eles não substituem firmware, MQD/HQD, VMID, ring compute ou `MAP_QUEUES`.

## CP ring GFX

| Registro | mmREG conhecido | BAR5 byte usado/corrigido | Classe | Nota |
|---|---:|---:|---:|---|
| `CP_RB0_RPTR` | `0x0F60` idx0 | `0x08700` | P1/P0 | Deve ser comparado também ao writeback em memória. |
| `CP_RB_WPTR_POLL_CNTL` | `0x0F62` idx0 | `0x08708` | P1/P2 | Tentativas usaram `0x00400100`; bits precisam do header mask. |
| `CP_RB0_BASE` | `0x1DE0` idx0 | `0x0C100` | P1/P0 | observado `0xF40FE000` em V3; formato contém shift. |
| `CP_RB0_CNTL` | `0x1DE1` idx0 | `0x0C104` | P1/P0 | observado `0x50000709`. |
| `CP_RB0_RPTR_ADDR_LO` | `0x1DE3` idx0 | `0x0C10C` | P1/P2 | writeback RPTR. |
| `CP_RB0_RPTR_ADDR_HI` | `0x1DE4` idx0 | `0x0C110` | P1/P2 | writeback RPTR high. |
| `CP_RB0_WPTR` | `0x1DF4` idx0 | `0x0C150` | P1/P2 | host write foi ignorado em algumas medições. |
| `CP_RB0_WPTR_HI` | `0x1DF5` idx0 | `0x0C154` | P1/P2 | ordem de latch deve vir do header/upstream. |
| `CP_RB0_BASE_HI` | `0x1E51` idx0 | `0x0C2C4` | P1/P2 | high separado no layout GC10.1. |
| `CP_RB_WPTR_POLL_ADDR_LO/HI` | `0x1E8B/1E8C` idx0 | `0x0C3AC/0C3B0` | P1/P2 | GPUVA do shadow WPTR. |
| `CP_RB_DOORBELL_CONTROL` | `0x1E8D` idx0 | `0x0C3B4` | P1/P2 | habilitação/índice de doorbell. |
| `CP_DEVICE_ID` | `0x1DEB` idx0 | `0x0C12C` | P1/P2 | Reconciliado com `gc_10_1_0_offset.h`; ainda exige snapshot/readback no ensaio. |
| `CP_MEC_DOORBELL_RANGE_LOWER/UPPER` | `0x1DFC/1DFD` idx0 | `0x0C170/0C174` | P1/P2 | Faixa global MEC; inclui base GC `0x4980`. `0x77F0/0x77F4` era cálculo incompleto e fica em quarentena. |
| `CP_PQ_WPTR_POLL_CNTL` | `0x1E23` idx0 | `0x0C20C` | P1/P2 | `PERIOD[7:0]`, `DISABLE_PEND_REQ_ONE_SHOT[29]`, `POLL_ACTIVE[30]` (status) e `EN[31]`. `0x788C` era cálculo sem base GC e fica em quarentena. **`0x00403004` é do SDMA (`SdmaRingPollCntl`) e está PROIBIDO neste registrador.** |
| `CP_PQ_WPTR_POLL_CNTL1` | `0x1E24` idx0 | `0x0C210` | P1/P2 | `QUEUE_MASK[31:0]`, um bit por fila (`gc_10_1_0_sh_mask.h`). Base `+0x1260` dw confirmada por `0x1E23→0xC20C`, `0x1E2D→0xC234` e `0x1E55→0xC2D4`. Escrito por `kgd_gfx_v9_hqd_load` antes de `INIT_FETCHER`/`ACTIVE`. Candidato 3.30. |
| `CP_PQ_STATUS` | `0x1E58` idx0 | `0x0C2E0` | P1/P2 | `DOORBELL_ENABLE` é bit 1; Linux sweep observou `0x2`. `0x7960` fica em quarentena. |

## MEC / HQD / MQD de compute (GC10.1)

Derivação autoritativa local: `gc_10_1_0_offset.h`, todos com `BASE_IDX=0`.
O bloco `gc_cpphqddec` começa em `BAR5+0x0C800`. Estes endereços estão
classificados para **preflight readonly**; ativação ou dequeue continuam P2 e
exigem snapshot integral da HQD selecionada.

O banco é indireto e depende de `GRBM_GFX_CNTL`. No primeiro preflight, o
miniport lê o seletor antes e depois de cada passagem e não o altera. A seleção
canônica candidata para MEC1/pipe0/queue0/VMID0 é `0x00000004`; seletor `0`
identifica outro banco e deve produzir `SELECTOR_NOT_COMPUTE`, nunca sucesso MEC.

| Registro | mmREG | BAR5 byte | Classe | Nota |
|---|---:|---:|---:|---|
| `CP_MQD_BASE_ADDR` | `0x1FA9` | `0x0C824` | P1/P2 | endereço MQD baixo; não é `0x9104`. |
| `CP_MQD_BASE_ADDR_HI` | `0x1FAA` | `0x0C828` | P1/P2 | endereço MQD alto; não é `0x9108`. |
| `CP_HQD_ACTIVE` | `0x1FAB` | `0x0C82C` | P1/P2 | bit ativo; não é `0x910C`. |
| `CP_HQD_VMID` | `0x1FAC` | `0x0C830` | P1/P2 | VMID da fila; primeiro ensaio limitado a VMID 0. |
| `CP_HQD_PERSISTENT_STATE` | `0x1FAD` | `0x0C834` | P1/P2 | `PRELOAD_SIZE` vem do MQD. |
| `CP_HQD_PIPE_PRIORITY` | `0x1FAE` | `0x0C838` | P1/P2 | prioridade estática da pipe. |
| `CP_HQD_QUEUE_PRIORITY` | `0x1FAF` | `0x0C83C` | P1/P2 | prioridade estática da fila. |
| `CP_HQD_QUANTUM` | `0x1FB0` | `0x0C840` | P1/P2 | quantum da HQD. |
| `CP_HQD_PQ_BASE` | `0x1FB1` | `0x0C844` | P1/P2 | base do ring deslocada por 8. |
| `CP_HQD_PQ_BASE_HI` | `0x1FB2` | `0x0C848` | P1/P2 | parte alta da base. |
| `CP_HQD_PQ_RPTR` | `0x1FB3` | `0x0C84C` | P0/P1 | cursor físico primário da prova. |
| `CP_HQD_PQ_RPTR_REPORT_ADDR` | `0x1FB4` | `0x0C850` | P1/P2 | writeback de RPTR baixo. |
| `CP_HQD_PQ_RPTR_REPORT_ADDR_HI` | `0x1FB5` | `0x0C854` | P1/P2 | writeback de RPTR alto. |
| `CP_HQD_PQ_WPTR_POLL_ADDR` | `0x1FB6` | `0x0C858` | P1/P2 | shadow WPTR baixo. |
| `CP_HQD_PQ_WPTR_POLL_ADDR_HI` | `0x1FB7` | `0x0C85C` | P1/P2 | shadow WPTR alto. |
| `CP_HQD_PQ_DOORBELL_CONTROL` | `0x1FB8` | `0x0C860` | P1/P2 | índice e enable de doorbell. |
| `CP_HQD_PQ_CONTROL` | `0x1FBA` | `0x0C868` | P1/P2 | `QUEUE_SIZE[5:0]`, `WPTR_CARRY[6]`, `RPTR_CARRY[7]`, `RPTR_BLOCK_SIZE[13:8]`, `QUEUE_FULL_EN[14]`, **`PQ_EMPTY[15]`**, `ENDIAN_SWAP[17:16]`, `SLOT_BASED_WPTR[18]`, `MIN_AVAIL_SIZE[22:20]`, `EXE_DISABLE[23]`, `CACHE_POLICY[25:24]`, `PQ_VOLATILE[26]`, `NO_UPDATE_RPTR[27]`, `UNORD_DISPATCH[28]`, `TUNNEL_DISPATCH[29]`, `PRIV_STATE[30]`, `KMD_QUEUE[31]` (`gc_10_1_0_sh_mask.h`). **Evidência diferencial 3.29 → 3.32:** `0xD030890D` → `0xD030090D`, `PQ_EMPTY` 1→0. Decodificado no 3.32: `QUEUE_SIZE=13` (ring 64 KB), `RPTR_BLOCK_SIZE=9`, `NO_UPDATE_RPTR=0`, `EXE_DISABLE=0`. |
| `CP_HQD_DEQUEUE_REQUEST` | `0x1FC1` | `0x0C884` | P1/P2 | teardown; aguardar `ACTIVE=0`. |
| `CP_MQD_CONTROL` | `0x1FCB` | `0x0C8AC` | P1/P2 | VMID do MQD. |
| `CP_HQD_EOP_BASE_ADDR` | `0x1FCE` | `0x0C8B8` | P1/P2 | EOP base deslocada por 8. |
| `CP_HQD_EOP_BASE_ADDR_HI` | `0x1FCF` | `0x0C8BC` | P1/P2 | EOP base alta. |
| `CP_HQD_EOP_CONTROL` | `0x1FD0` | `0x0C8C0` | P1/P2 | tamanho do EOP. |
| `CP_HQD_EOP_RPTR` | `0x1FD1` | `0x0C8C4` | P1/P2 | `INIT_FETCHER[31]`; candidato 3.29 segue `kgd_gfx_v9_hqd_load`: snapshot, escrita do gatilho antes de `CP_HQD_ACTIVE`, readback sem RPTR/RESET/DEQUEUE inesperados (`RPTR_EQ_CSMD_WPTR[30]` e permitido) e rollback verificado. |
| `CP_HQD_PQ_WPTR_LO` | `0x1FDF` | `0x0C8FC` | P0/P2 | cursor baixo; não é `0x9130`. |
| `CP_HQD_PQ_WPTR_HI` | `0x1FE0` | `0x0C900` | P0/P2 | cursor alto e latch conforme upstream. |

Os offsets `0x9104`, `0x9108`, `0x910C` e `0x9130` ficam em quarentena para
este ASIC: não correspondem ao mapa GC10.1 derivado pelo SOC15.

| `CP_CPC_STATUS` | `0x0E24` idx0 | `0x08210` | P1/P2 | Estado do CPC. **Somente leitura** no transporte. Candidato 3.34. |
| `CP_CPC_BUSY_STAT` | `0x0E25` idx0 | `0x08214` | P1/P2 | Unidades ocupadas do CPC. **Somente leitura**. |
| `CP_CPC_STALLED_STAT1` | `0x0E26` idx0 | `0x08218` | P1/P2 | Motivo de stall do CPC. **Somente leitura**. |
| `CP_CPF_BUSY_STAT` | `0x0E28` idx0 | `0x08220` | P1/P2 | Ocupacao do prefetch/HQD; candidato 3.39 captura antes, depois e no polling final. **Somente leitura**. |
| `CP_MEC_ME1_HEADER_DUMP` | `0x0E2E` idx0 | `0x08238` | P1/P2 | FIFO de cabecalhos PM4 processados pelo ME1; **cada leitura avanca o ponteiro**, por isso e drenado uma unica vez apos o polling. **Somente leitura**. Adjacente a `CP_MEC_CNTL` (`0x0E2D` -> `0x08234`), que ja e escrito com sucesso no silicio — ancora forte do offset. |
| `CP_MEC_ME2_HEADER_DUMP` | `0x0E2F` idx0 | `0x0823C` | P1/P2 | FIFO de cabecalhos PM4 do ME2, motor ativo observado no candidato 3.34. No 3.36 e drenado antes de `ACTIVE` e novamente apos o polling para separar resíduo de conteúdo produzido no ciclo; ME1 permanece como controle. **Somente leitura**; escrita proibida pelo transporte e por teste dedicado. |
| `CP_STALLED_STAT1` | `0x0F3D` idx0 | `0x08674` | P1/P2 | Stall do CP graficos. **Somente leitura**. |
| `CP_STAT` | `0x0F40` idx0 | `0x08680` | P1/P2 | Estado agregado do CP. **Somente leitura**. |
## Golden/context usados nas tentativas

| Registro | BAR5 byte | Classe | Nota |
|---|---:|---:|---|
| `VGT_NUM_INSTANCES` | `0x30934` | P1/P2 | comentário V3: mm `0x224D`, idx1. |
| `PA_SC_LINE_STIPPLE_STATE` | `0x30A04` | P1/P2 | comentário V3: mm `0x2281`, idx1. |

Não aplicar golden settings isolados. Importar a tabela/máscaras da versão IP exata e executar somente após prerequisites.

## GCVM/GMC usados nas tentativas

| Registro | BAR5 byte | Classe |
|---|---:|---:|
| `GCVM_L2_CNTL/2/3` | `0x0A100/0A104/0A108` | P1/P2 |
| `GCVM_L2_CNTL4/5` | `0x0A160/0A184` | P1/P2 |
| `GCVM_L2_PROTECTION_FAULT_STATUS` | `0x0A130` | P1/P2 |
| identity aperture low LO/HI | `0x0A148/0A14C` | P1/P2 |
| identity aperture high LO/HI | `0x0A150/0A154` | P1/P2 |
| identity physical offset LO | `0x0A158` | P1/P2 |
| `GCVM_CONTEXT0_CNTL` | `0x0A200` | P1/P2 |
| `INVALIDATE_ENG0_REQ/ACK` | `0x0A28C/0A2D4` | P1/P2 |
| `INVALIDATE_ENG17_SEM/REQ/ACK` | `0x0A288/0A2D0/0A318` | P1/P2 |
| context0 PT base LO/HI | `0x0A3AC/0A3B0` | P1/P2 |
| context0 PT start LO/HI | `0x0A42C/0A430` | P1/P2 |
| context0 PT end LO/HI | `0x0A4AC/0A4B0` | P1/P2 |
| `GCMC_VM_FB_OFFSET` | `0x0A5AC` | P1/P2 |
| system aperture default LSB/MSB | `0x0A5B0/0A5B4` | P1/P2 |
| FB location BASE/TOP | `0x0A600/0A604` | P1/P2 |
| system aperture LOW/HIGH | `0x0A614/0A618` | P1/P2 |
| `GCMC_VM_MX_L1_TLB_CNTL` | `0x0A61C` | P1/P2 |

Antes de usar, reconciliar cada símbolo com os headers `gfxhub_v2_0/mmhub_v2_0` da versão local. O fato de alguns desses números terem sido usados com sucesso parcial não valida a programação completa da MMU.

## GCVM/GCMC - bloco de tradução para o gate de memória

Derivados em 2026-08-23 de `gc_10_1_0_offset.h`, com a aritmética
`byte = dw * 4 + 0x4980` — a mesma que já reproduzia `GCVM_L2_CNTL` (`0x0A100`),
`GCVM_CONTEXT0_CNTL` (`0x0A200`) e `GCVM_L2_PROTECTION_FAULT_STATUS`
(`0x0A130`), que o laboratório lia desde antes. Todos **somente leitura** no
`gcvm dump`. Classe **P1**.

| Registro | `dw` | Byte BAR0 | Para que serve no gate |
|---|---|---|---|
| `GCVM_CONTEXT1_CNTL` | `0x1621` | `0x0A204` | contexto 1, usado pelo modo identidade |
| `GCVM_L2_PROTECTION_FAULT_CNTL2` | `0x15e9` | `0x0A124` | `ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY` |
| `GCVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32` | `0x15ef` | `0x0A13C` | página dummy da falha |
| `GCVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32` | `0x15f0` | `0x0A140` | página dummy da falha |
| `GCMC_VM_AGP_TOP` | `0x1722` | `0x0A608` | abertura AGP |
| `GCMC_VM_AGP_BOT` | `0x1723` | `0x0A60C` | abertura AGP |
| `GCMC_VM_AGP_BASE` | `0x1724` | `0x0A610` | abertura AGP |
| `GCVM_INVALIDATE_ENG0_SEM` | `0x1631` | `0x0A244` | semáforo da invalidação — **ler ADQUIRE**, ver aviso abaixo |
| `GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO32` | `0x1667` | `0x0A31C` | faixa de endereço da engine 0 |
| `GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI32` | `0x1668` | `0x0A320` | faixa de endereço da engine 0 |
| `GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32` | `0x15f7` | `0x0A15C` | deslocamento físico da abertura de identidade |
| `GCVM_INVALIDATE_ENG0_REQ` | `0x1643` | `0x0A28C` | requisição de invalidação |
| `GCVM_INVALIDATE_ENG0_ACK` | `0x1655` | `0x0A2D4` | confirmação da invalidação |

Semântica de bit para `GCVM_L2_CNTL`, `L2_CNTL2..5`, `GCVM_CONTEXT0_CNTL`,
`GCVM_CONTEXT1_CNTL`, `GCVM_L2_PROTECTION_FAULT_CNTL`/`CNTL2`/`STATUS`,
`GCMC_VM_MX_L1_TLB_CNTL`, `GCMC_VM_FB_LOCATION_BASE`/`TOP` e
`GCVM_INVALIDATE_ENG0_REQ`/`ACK` foi acrescentada ao extrato
`gc_10_1_0_sh_mask_extract.h`, do mesmo header autoritativo (SHA-256
`cba20414ecf02e91bafdcfbff7d4ba686326f0bdaf236d1a883364f08410c167`).

### AVISO: `GCVM_INVALIDATE_ENG0_SEM` não tem leitura segura

Medido em 2026-08-23. O upstream documenta literalmente em `gmc_v10_0.c`:

- `/* a read return value of 1 means semaphore acuqire */`
- `/* write with 0 means semaphore release */`

**Ler esse registrador adquire o semáforo da engine de invalidação.** Não é
observação, é ação. O `gcvm dump` do laboratório o lia e por isso mantinha o
semáforo retido até o reboot, sem declarar.

Consequências práticas:

- o registrador foi **removido** de qualquer caminho de leitura;
- o estado do semáforo é observado pelo bit `SEMAPHORE` (16) de
  `GCVM_INVALIDATE_ENG0_ACK`, que é leitura pura;
- liberar exige ação explícita: `gcvm semrelease` escreve 0 e confere pelo `ACK`.

Classe **P0** para o comportamento: medido nos dois estados, com o efeito
aparecendo como diferença entre dois dumps consecutivos.

Regra que fica para o catálogo: **antes de colocar um registrador num dump, é
preciso saber se a leitura dele tem efeito.** Registrador de semáforo, de fila,
de FIFO ou de "read to clear" não entra em ferramenta de observação.

`tools/decode-gcvm.py` decodifica o dump e compara com o que
`gfxhub_v2_0.c` do upstream programa em `init_tlb_regs`, `init_cache_regs` e
`enable_system_domain`. Divergência ali **não é defeito**: o POST configurou a
placa para o próprio uso, não para o modelo de driver do amdgpu. A lista é
escopo de trabalho.

## Formato de PTE para GC 10.1 - fontes autoritativas

Necessário para o gate de memória. Nenhum valor abaixo é suposição; cada um tem
arquivo de origem.

| Item | Valor | Fonte |
|---|---|---|
| Escrita do PTE | `valor = (PA & pte_addr_mask) \| flags`, 64 bits em `indice * 8` | `amdgpu_gmc.c`, `amdgpu_gmc_set_pte_pde()` |
| `pte_addr_mask` | `0x0000FFFFFFFFF000` (48 bits de PA) | `gmc_v10_0.c:846` |
| `AMDGPU_PTE_VALID` | `1 << 0` | `amdgpu_vm.h:57` |
| `AMDGPU_PTE_SYSTEM` | `1 << 1` | `amdgpu_vm.h:58` |
| `AMDGPU_PTE_SNOOPED` | `1 << 2` | `amdgpu_vm.h:59` |
| `AMDGPU_PTE_EXECUTABLE` | `1 << 4` | `amdgpu_vm.h:65` |
| `AMDGPU_PTE_READABLE` | `1 << 5` | `amdgpu_vm.h:67` |
| `AMDGPU_PTE_WRITEABLE` | `1 << 6` | `amdgpu_vm.h:68` |
| `MTYPE` no PTE | bits `50:48` | `amdgpu_vm.h:114`, `AMDGPU_PTE_MTYPE_NV10_SHIFT` |
| `MTYPE_UC` | `3` | `navi10_enum.h:309` |
| `gart_pte_flags` do GC 10.1 | `MTYPE_UC \| EXECUTABLE` | `gmc_v10_0.c:741` |
| PA de página de VRAM | `MC - vram_start + vram_base_offset` | `amdgpu_gmc.c`, `amdgpu_gmc_vram_mc2pa()` |
| `vram_base_offset` | `GCMC_VM_FB_OFFSET << 24` | `gfxhub_v2_0.c:117`, `get_mc_fb_offset()` |

### O que isso dá nesta placa

Com `GCMC_VM_FB_OFFSET = 0x00000450` medido em 2026-08-23 e `vram_start`
confirmado em `0xF4_0000_0000`:

- `vram_base_offset = 0x450 << 24 = 0x4_5000_0000`;
- uma página em VRAM offset `0x0FF20000` tem
  `PA = 0x0FF20000 + 0x450000000 = 0x4_5FF2_0000`;
- com `VALID|EXECUTABLE|READABLE|WRITEABLE|MTYPE_UC` = `0x0003000000000071`, o
  PTE fica **`0x000300045FF20071`**.

Esse valor foi calculado independentemente do driver antes da primeira execução,
justamente para que a medição possa confirmar ou desmentir a aritmética em vez de
concordar com ela por construção.

O driver **lê** `GCMC_VM_FB_OFFSET` do silício em cada construção; não usa
constante. Se o registrador mudar entre plataformas, o PTE acompanha.

## HDP

| Registro | BAR5 byte usado | Classe |
|---|---:|---:|
| `HDP_READ_CACHE_INVALIDATE` | `0x03FC4` | P2 |
| `HDP_MISC_CNTL` | `0x03FCC` | P2 |
| `HDP_MEM_COHERENCY_FLUSH_CNTL` | `0x04A80` | P2 |

Os endereços devem ser derivados novamente do header HDP do IP da BC-250. Seu efeito de coerência precisa ser testado com memória, não por write-stick.

## SDMA0 usados nas tentativas e no `dbg`

Os números abaixo são o conjunto efetivamente usado por `wddm/V2/V3/dbg`. Há uma pendência: o header local `sdma5_4_2_2_offset.h` usa nomes `SDMA5_*` e `BASE_IDX=1`, enquanto as tentativas tratam vários offsets como segmento 0. Portanto eles são **P2 até reconciliação completa com o arquivo/versão SDMA correta**.

| Registro | BAR5 byte usado | Classe |
|---|---:|---:|
| `CLK_CTRL` | `0x049EC` | P2 |
| `CNTL` | `0x049F0` | P2 |
| `CHICKEN_BITS` | `0x049F4` | P2 |
| `GB_ADDR_CONFIG` / `_READ` | `0x049F8/049FC` | P2 |
| `SEM_WAIT_FAIL_TIMER_CNTL` | `0x04A04` | P2 |
| `STATUS_REG` | `0x04A14` | P2 |
| `UCODE_CHECKSUM` | `0x04A24` | P2 |
| `F32_CNTL` | `0x04A28` | P2 |
| `STATUS2_REG` | `0x04A60` | P2 |
| `UTCL1_CNTL` / `UTCL1_PAGE` | `0x04A70/04AA0` | P2 |
| `GFX_RB_CNTL` | `0x04B80` | P2 |
| `GFX_RB_BASE/BASE_HI` | `0x04B84/04B88` | P2 |
| `GFX_RB_RPTR/RPTR_HI` | `0x04B8C/04B90` | P2 |
| `GFX_RB_WPTR/WPTR_HI` | `0x04B94/04B98` | P2 |
| `GFX_RB_WPTR_POLL_CNTL` | `0x04B9C` | P2 |
| `GFX_RB_RPTR_ADDR_HI/LO` | `0x04BA0/04BA4` | P2 |
| `GFX_IB_CNTL` | `0x04BA8` | P2 |
| `GFX_DOORBELL` | `0x04BC8` | P2 |
| `GFX_DOORBELL_OFFSET` | `0x04C2C` | P2 |
| `GFX_RB_WPTR_POLL_ADDR_HI/LO` | `0x04C48/04C4C` | P2 |
| `GFX_RB_AQL_CNTL` | `0x04C50` | P0 para endereço; P2 para escrita. Derivado do `gc_10_1_0_offset.h`: dword `0x00B4`, base idx 0; observado `0x00004000` no boot limpo. |
| `GFX_MINOR_PTR_UPDATE` | `0x04C54` | P0 para endereço; P2 para escrita. Resolvido pelo `gc_10_1_0_offset.h`: dword `0x00B5`, base seg0 `0x1260`; `(0x1260 + 0x00B5) * 4 = 0x04C54`. O antigo `0x04BD0` fica em quarentena e não deve ser escrito. |
| `UCODE_ADDR/DATA` | `0x3E200/3E204` | P2 | caminho direct load do `dbg`; reconciliar segmento/versão. |

Capturas contaminadas observaram um ring externo em `GPU 0xF40FD00000`, com
`GFX_RB_RPTR_ADDR` e `GFX_RB_WPTR_POLL_ADDR` ambos em
`GPU 0xF40FD02000`. No baseline de 2026-08-22 22:54, depois da remoção offline
do `bc250v4`, base, ponteiros e esses dois endereços estavam zerados e o ring
estava desabilitado. A configuração anterior era resíduo do WDDM legado, não
estado de POST reutilizável; os endereços permanecem dados históricos, não P0.

### Registros SDMA de diagnóstico - derivados em 2026-08-23

A aritmética é `byte = dw * 4 + 0x4980`, a mesma base já confirmada por seis
registros que o laboratório usa e mede desde 2026-08-19: `CNTL` (`dw 0x001c` ->
`0x049F0`), `STATUS_REG` (`0x0025` -> `0x04A14`), `F32_CNTL` (`0x002a` ->
`0x04A28`), `STATUS2_REG` (`0x0038` -> `0x04A60`), `UTCL1_CNTL` (`0x003c` ->
`0x04A70`) e `UTCL1_PAGE` (`0x0048` -> `0x04AA0`). A mesma base reproduz
`GRBM_STATUS` (`dw 0x0da4` -> `0x08010`) e `GCVM_L2_PROTECTION_FAULT_STATUS`
(`dw 0x15ec` -> `0x0A130`), que também já eram lidos.

Todos são **somente leitura** no laboratório. Classe **P1**: derivados de
header com aritmética verificada, ainda sem semântica de bit confirmada nesta
unidade.

| Registro | `dw` | Byte BAR0 | Por que está aqui |
|---|---|---|---|
| `SDMA0_POWER_CNTL` | `0x001a` | `0x049E8` | estado de energia da engine |
| `SDMA0_CHICKEN_BITS` | `0x001d` | `0x049F4` | já é escrito no bring-up; ler para conferir |
| `SDMA0_STATUS1_REG` | `0x0026` | `0x04A18` | o upstream usa `& 0x3FF == 0x3FF` como "idle" antes do soft reset |
| `SDMA0_FREEZE` | `0x002b` | `0x04A2C` | caminho de parada do upstream antes do reset |
| `SDMA0_EDC_COUNTER` | `0x0036` | `0x04A58` | erros de ECC/EDC acumulados |
| `SDMA0_STATUS3_REG` | `0x004c` | `0x04AB0` | estado adicional da engine |
| `SDMA0_ERROR_LOG` | `0x0050` | `0x04AC0` | log de erro da engine |
| `SDMA0_UTCL1_WATERMK` | `0x003d` | `0x04A74` | configuração da TLB da SDMA |
| `SDMA0_UTCL1_RD_STATUS` | `0x003e` | `0x04A78` | estado de leitura da TLB |
| `SDMA0_UTCL1_WR_STATUS` | `0x003f` | `0x04A7C` | estado de escrita da TLB |
| `SDMA0_UTCL1_INV0/1/2` | `0x0040-0x0042` | `0x04A80/84/88` | invalidação da TLB |
| `SDMA0_UTCL1_RD_XNACK0/1` | `0x0043-0x0044` | `0x04A8C/90` | leitura recusada pela tradução |
| `SDMA0_UTCL1_WR_XNACK0/1` | `0x0045-0x0046` | `0x04A94/98` | **escrita recusada pela tradução** |
| `SDMA0_UTCL1_TIMEOUT` | `0x0047` | `0x04A9C` | timeout de tradução |
| `GCVM_L2_PROTECTION_FAULT_CNTL` | `0x15e8` | `0x0A120` | controle da falha de proteção |
| `GCVM_L2_PROTECTION_FAULT_ADDR_LO32` | `0x15ed` | `0x0A134` | endereço da falha latchada |
| `GCVM_L2_PROTECTION_FAULT_ADDR_HI32` | `0x15ee` | `0x0A138` | endereço da falha latchada |

Motivo de existirem: em 2026-08-23 a engine passou a consumir o anel sem
escrever memória depois de um pacote com opcode reservado, e nenhum registro
que o laboratório lia explicava isso.

#### Valores medidos numa engine SAUDÁVEL (2026-08-23 01:05)

Referência tirada logo após uma submissão comprovadamente correta:

| Registro | Valor saudável |
|---|---|
| `STATUS_REG` | `0x46DEED57` |
| `STATUS1_REG` | `0x000003FF` (idle pelo critério do upstream) |
| `STATUS2_REG` | `0x00050604` |
| `STATUS3_REG` | `0x03F00000` |
| `FREEZE` | `0x00000000` |
| `ERROR_LOG` | `0x0000000F` |
| `EDC_COUNTER` | `0x00000000` |
| `POWER_CNTL` | `0x40000050` |
| `CNTL` | `0x000400E2` |
| `CHICKEN_BITS` | `0x03AB0107` |
| `UTCL1_CNTL` | `0xD0000793` |
| `UTCL1_WATERMK` | `0xFFFBD9FB` |
| `UTCL1_RD_STATUS` | `0x01891555` |
| `UTCL1_WR_STATUS` | `0x51811555` |
| `UTCL1_RD_XNACK0/1` | `0x0045FE00` / `0x00686B00` |
| `UTCL1_WR_XNACK0/1` | `0x0045FE00` / `0x01050500` |
| `UTCL1_TIMEOUT` | `0x00000000` |
| `UTCL1_PAGE` | `0x004C5C20` |
| `GCVM_L2_PROTECTION_FAULT_STATUS` | `0x00000000` |

#### Leitura revogada: XNACK e ERROR_LOG não são flags de erro

A hipótese que motivou este bloco era `WR_XNACK != 0` indicar tradução
recusada. A medição acima **revoga** isso: numa engine que acabou de funcionar,
`WR_XNACK` e `ERROR_LOG` já são diferentes de zero. São configuração ou
contadores, não flag binário. Classe **Q** para essa interpretação; os
registros em si continuam **P1** e úteis por comparação.

Nenhum bit é decodificado por suposição. O consumo correto é comparar o dump de
uma sessão saudável com o da engine quebrada e olhar o que mudou.

### Soft reset de engine SDMA - desbloqueado em 2026-08-23

O `sh_mask` autoritativo foi obtido:

- fonte: `drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_sh_mask.h` do
  kernel Linux;
- SHA-256 do arquivo completo:
  `cba20414ecf02e91bafdcfbff7d4ba686326f0bdaf236d1a883364f08410c167`;
- extrato dos registros usados, com a mesma procedência no cabeçalho:
  `referencias/BC250-windowsDriverTest/docs/upstream/generated/gc_10_1_0_sh_mask_extract.h`.

| Campo | Shift | Máscara | Uso |
|---|---:|---|---|
| `GRBM_SOFT_RESET.SOFT_RESET_SDMA0` | `0x17` | `0x00800000` | reset do bloco SDMA0 |
| `GRBM_SOFT_RESET.SOFT_RESET_GFX` | `0x10` | `0x00010000` | **não tocar**; preservado por RMW |
| `SDMA0_FREEZE.FREEZE` | `0x04` | `0x00000010` | parar a engine antes do reset |
| `SDMA0_FREEZE.FROZEN` | `0x05` | `0x00000020` | confirmação de parada |
| `SDMA0_CNTL.UTC_L1_ENABLE` | `0x01` | `0x00000002` | desligado durante o reset |
| `SDMA0_STATUS1_REG` bits `9:0` | - | `0x000003FF` | todos em 1 = ociosa (critério do upstream) |
| `SDMA0_STATUS2_REG.CMD_OP` | `0x04` | `0x00000FF0` | **último opcode decodificado, latchado** |

`mmGRBM_SOFT_RESET` fica em `dw 0x0da8` -> byte `0x08020` pela mesma aritmética
dos demais registros GC.

O reset é feito por leitura-modificação-escrita, mexendo apenas no bit da
SDMA0: os outros bits do registrador incluem `SOFT_RESET_GFX`, e a máquina usa
esta GPU para o display.

### Configuração que o soft reset apaga - medido em 2026-08-23

| Registro | Valor do POST | Depois do soft reset | Campo |
|---|---|---|---|
| `SDMA0_UTCL1_PAGE` | `0x004C5C20` | `0x000C5C20` | **`USE_BC` 1 -> 0** |
| `SDMA0_UTCL1_CNTL` | `0xD0000793` | `0xD0000191` | `RESP_MODE` 3 -> 0, `REDO_DELAY` 9 -> 8 |
| `SDMA0_CHICKEN_BITS` | `0x03AB0107` | `0x01AF0107` | `TIME_BASED_QOS` 1 -> 0, `T2L_256B_ENABLE` 0 -> 1 |
| `SDMA0_CNTL` | `0x000400E2` | `0x000400C2` | `MIDCMD_PREEMPT_ENABLE` 1 -> 0 |

**Interpretação REVOGADA (classe Q).** A tabela acima foi lida como "o soft
reset apaga `USE_BC`". Cruzando 20 amostras das cinco execuções de 2026-08-23, a
correlação real é com o estado do microcontrolador:

| Estado do F32 | `UTCL1_PAGE` lido | Amostras |
|---|---|---:|
| rodando | `0x004C5C20` | 16 |
| haltado | `0x000C5C20` | 4 |

Sem exceção. Dumps com a engine **já quebrada** e o F32 rodando continuam
mostrando `0x004C5C20`. Portanto a diferença acompanha o estado de execução do
bloco, não o reset, e `USE_BC` **não** está demonstrado como causa da perda de
escrita.

Regra de leitura que fica no catálogo: **os registradores da UTCL1 só são
comparáveis entre dumps tirados com o F32 no mesmo estado.** Ler configuração de
um bloco parado devolve valor de reset e produz diagnóstico falso.

Os valores da tabela anterior continuam válidos como medição; o que foi revogado
é a causalidade atribuída a eles.

## CMOS/port I/O usados por utilitários

| Porta | Valor | Classe | Uso |
|---|---:|---:|---|
| CMOS index | `0x72` | P2 | Seleção do byte de configuração. |
| CMOS data | `0x73` | P2 | Leitura/escrita. |

Configuração de memória em CMOS é plataforma/BIOS, não parte do caminho de renderização. Escrita deve ficar fora do miniport e exigir backup, checksum, validação e reboot explícito.

## Endereços GC históricos em quarentena

| Símbolo alegado | Endereço antigo | Correto/candidato atual | Motivo |
|---|---:|---:|---|
| `GRBM_STATUS` | `0x03040`, `0x03260` | `0x08010` | mapa/aritmética antiga. |
| `GRBM_STATUS2` | `0x0326C` | `0x08008` | mapa antigo. |
| `CP_STAT` | `0x08684` | `0x08680` | `0x8684` retornou fill `0xDEFxDEFx`. |
| `SPI_PG_ENABLE_STATIC_WGP_MASK` | `0x034FC`, `0x05C3C` | `0x0935C` | registros/gerações e unidade misturados. |
| `CC_GC_SHADER_ARRAY_CONFIG` | `0x03264`, `0x09C1C` | `0x089BC` | símbolo antigo não corresponde ao header GC10.1 analisado. |
| `SCRATCH_REG0` | `0x032D4`, `0x08500` | `0x30100` | mapas antigos/extras conflitantes. |
| `GRBM_GFX_INDEX` | `0x034D0` | `0x30800` | BASE_IDX 1 ignorado. |
| `RLC_PG_ALWAYS_ON_WGP_MASK` | `0x03D64` | `0x3B14C` | fill/unmapped no endereço antigo. |
| `COMPUTE_*` | `0x080E0+` | `0x0B800+` | base SOC15 aplicada incorretamente. |
| `CP_RB0_*` aliases | `0xDA60-0xDA7C` | `0xC100...`/`0x8700...` | offsets brutos de outro mapa foram somados como bytes. |
| `CP_ME_CNTL` | `0xC060`, `0x4A74` em docs antigas | `0x086D8` | NBIO/raw ou mmREG de versão errada. |
| `CP_MEC_CNTL` | `0xC0E0`, `0x4B14` | `0x08234` | GFX10.3/Navi ou raw NBIO misturado com GC10.1. |

Não remover esses valores dos testes de regressão: eles são úteis para detectar que um refactor reintroduziu o mapa errado.

## Headers antigos que não são fonte da verdade

`amdbc250_dream_hw.h` e especialmente `amdbc250_hw_extra.h` contêm múltiplas definições incompatíveis para os mesmos nomes (scratch, CP ring, SDMA, VM, display). Eles preservam história, mas não devem ser incluídos nem transcritos para o driver novo.

Fonte da verdade para cada novo registro:

1. versão IP detectada;
2. header upstream correspondente;
3. `mmREG` e `BASE_IDX`;
4. base do IP Cyan Skillfish;
5. cálculo automatizado;
6. teste read-only/fill detection;
7. write-stick somente quando seguro;
8. teste funcional específico.

## Valores que o passo 3 programa, e de onde cada um vem

Tudo abaixo sai de `gfxhub_v2_0.c` do upstream (cópia em
`referencias/BC250-windowsDriverTest/docs/upstream/deep/`) e de máscaras do
extrato `gc_10_1_0_sh_mask_extract.h`. Nenhum valor foi deduzido de outra
geração de hardware.

### Estágio `gart` — `gfxhub_v2_0_init_gart_aperture_regs`

| Registro | Valor |
|---|---|
| `CONTEXT0_PAGE_TABLE_BASE_ADDR_LO/HI32` | PA da tabela, 32 bits baixos e altos |
| `CONTEXT0_PAGE_TABLE_START_ADDR_LO/HI32` | `va_start >> 12` e `va_start >> 44` |
| `CONTEXT0_PAGE_TABLE_END_ADDR_LO/HI32` | `va_end >> 12` e `va_end >> 44` |

O PA da tabela usa a mesma conversão das PTEs do passo 1:
`PA = offset_de_VRAM + (GCMC_VM_FB_OFFSET << 24)`. Uma aritmética só, no kernel,
para não existirem duas no projeto.

**Inerte enquanto `CONTEXT0_CNTL.ENABLE_CONTEXT` for 0.** É por isso que este
estágio pode ser exercitado sem substituir o bypass.

### Estágio `system` — `gfxhub_v2_0_init_system_aperture_regs`

| Registro | Valor | Observação |
|---|---|---|
| `GCMC_VM_AGP_BASE` | 0 | literal do upstream |
| `GCMC_VM_AGP_BOT` | `0x00FFFFFF` | abertura vazia: BOT > TOP |
| `GCMC_VM_AGP_TOP` | 0 | idem |
| `GCMC_VM_SYSTEM_APERTURE_LOW_ADDR` | `fb_start >> 18` | `fb_start` de `FB_LOCATION_BASE` lido agora |
| `GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR` | `fb_end >> 18` | `fb_end` de `FB_LOCATION_TOP`, com os 24 bits baixos em 1 |
| `GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB/MSB` | `PA >> 12`, `PA >> 44` | página default |
| `GCVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO/HI32` | `PA >> 12`, `PA >> 44` | mesma página |
| `GCVM_L2_PROTECTION_FAULT_CNTL2` | bit 18 em 1 | `ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY` |

Sobre AGP: o POST deixou os três em zero, o que descreve uma abertura de uma
página em zero, não a ausência de abertura. A convenção do amdgpu para vazia é
`BOT > TOP`, e é ela que o estágio escreve.

Sobre a abertura de sistema: `fb_end >> 18` dá `0x003D07FF`, contra `0x003D0800`
que o POST deixou. A diferença é esperada — o upstream usa
`max(fb_end, agp_end) >> 18` e o POST arredondou para cima. A medição decide se
importa; até lá, é divergência registrada e não corrigida por suposição.

### Estágio `tlb` — `gfxhub_v2_0_init_tlb_regs` — **PERIGOSO**

| Campo de `GCMC_VM_MX_L1_TLB_CNTL` | Shift | POST | Passo 3 |
|---|---:|---:|---:|
| `ENABLE_L1_TLB` | 0 | — | 1 |
| `SYSTEM_ACCESS_MODE` | 3 | 0 | 3 |
| `SYSTEM_APERTURE_UNMAPPED_ACCESS` | 5 | — | 0 |
| `ENABLE_ADVANCED_DRIVER_MODEL` | 6 | 0 | 1 |
| `MTYPE` | 11 | — | 3 (UC) |

`SYSTEM_ACCESS_MODE = 0` é o bypass físico que faz a SDMA alcançar memória hoje.
Este estágio o substitui.

### Estágio `cache` — `gfxhub_v2_0_init_cache_regs` — **PERIGOSO**

| Registro | Campo | Shift | POST | Passo 3 |
|---|---|---:|---:|---:|
| `L2_CNTL` | `ENABLE_L2_CACHE` | 0 | 0 | 1 |
| `L2_CNTL` | `ENABLE_L2_FRAGMENT_PROCESSING` | 1 | 1 | 0 |
| `L2_CNTL` | `L2_PDE0_CACHE_TAG_GENERATION_MODE` | 8 | — | 0 |
| `L2_CNTL` | `ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY` | 11 | 0 | 1 |
| `L2_CNTL` | `PDE_FAULT_CLASSIFICATION` | 18 | — | 0 |
| `L2_CNTL` | `CONTEXT1_IDENTITY_ACCESS_MODE` | 19 | — | 1 |
| `L2_CNTL` | `IDENTITY_MODE_FRAGMENT_SIZE` | 21 | — | 0 |
| `L2_CNTL2` | `INVALIDATE_ALL_L1_TLBS` | 0 | — | 1 |
| `L2_CNTL2` | `INVALIDATE_L2_CACHE` | 1 | — | 1 |
| `L2_CNTL3` | `BANK_SELECT` | 0 | 7 | 9 |
| `L2_CNTL3` | `L2_CACHE_BIGK_FRAGMENT_SIZE` | 15 | 0 | 6 |
| `L2_CNTL4` | `VMC_TAP_PDE_REQUEST_PHYSICAL` | 6 | 1 | 0 |
| `L2_CNTL4` | `VMC_TAP_PTE_REQUEST_PHYSICAL` | 7 | 1 | 0 |
| `L2_CNTL5` | `L2_CACHE_SMALLK_FRAGMENT_SIZE` | 0 | — | 0 |

**Divergência declarada:** o upstream monta `L2_CNTL3`, `L2_CNTL4` e `L2_CNTL5`
a partir da constante de reset `mmGCVM_L2_CNTLn_DEFAULT`, que não existe nos
headers disponíveis. O driver parte do valor do POST e altera apenas os campos
nomeados acima. Não é equivalência assumida: é diferença conhecida, cujo efeito
só pode ser julgado pela submissão traduzida do passo 5.

### Estágio `context0` — `gfxhub_v2_0_enable_system_domain` — **PONTO SEM VOLTA**

| Campo de `GCVM_CONTEXT0_CNTL` | Shift | POST | Passo 3 |
|---|---:|---:|---:|
| `ENABLE_CONTEXT` | 0 | 0 | 1 |
| `PAGE_TABLE_DEPTH` | 1 | — | 0 |
| `RETRY_PERMISSION_OR_INVALID_PAGE_FAULT` | 7 | 1 | 0 |

### Estágio `identity` — `gfxhub_v2_0_disable_identity_aperture`

Valores literais do upstream: faixa baixa em `0xF_FFFFFFFF`, faixa alta em zero
(low > high, abertura vazia), deslocamento físico em zero nos dois dwords.

### Estágio `invrange` — `gfxhub_v2_0_program_invalidation`, engines 0 e 17

`ADDR_RANGE_LO32 = 0xFFFFFFFF`, `ADDR_RANGE_HI32 = 0x1F` em cada uma. O upstream
faz isso para as 18 engines; o laboratório escreve as duas que interessam.

| Registro | dw | byte |
|---|---:|---:|
| `GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO32` | `0x1689` | `0x0A3A4` |
| `GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI32` | `0x168a` | `0x0A3A8` |

**Correção de rota, feita antes de gastar um boot.** Até a ABI 15 este estágio
escrevia só a engine 0, e o plano do passo 4 dizia "invalidar pela engine 0".
Está errado. O upstream é literal em `gmc_v10_0_flush_gpu_tlb()`:

> `/* Use register 17 for GART */`
> `const unsigned int eng = 17;`

A engine 0 não é a da GART. A engine 17 é. Ela continua na lista porque já foi
exercitada no silício em 2026-08-23 12:59 e não custa nada.

## A engine de invalidação, e três coisas que a leitura do upstream corrigiu

### 1. A engine é a 17

Ver acima. Endereços calculados pela distância entre engines, como o upstream
faz: `eng_distance = ENG1_REQ − ENG0_REQ = 1 dword`. O driver confere o
resultado do cálculo contra os offsets deste catálogo antes de escrever — se as
duas contas discordarem, o problema é de mapa e não de hardware, e disparar
invalidação no registrador errado não ajuda ninguém.

### 2. O GFXHUB não usa semáforo

`gmc_v10_0_use_invalidate_semaphore()` devolve verdadeiro **apenas para
MMHUB0**. O protocolo de adquirir por leitura e liberar escrevendo 0, que este
projeto descobriu por acidente em 2026-08-23, vale para o outro hub.

Isso **não** revoga o aviso sobre `GCVM_INVALIDATE_ENG0_SEM`: ler esse
registrador continua adquirindo o semáforo, o dump antigo continua tendo feito
isso indevidamente, e `gcvm semrelease` continua sendo a forma correta de sair
desse estado. O que muda é que a invalidação em si não precisa do protocolo.

### 3. A leitura muda de `REQ` é obrigatória neste chip

O upstream lê `REQ` e descarta o valor, logo depois de escrevê-lo, quando o hub
é GFXHUB e `GC_HWIP < IP_VERSION(10, 3, 0)`:

> *"Issue a dummy read to wait for the ACK register to be cleared to avoid a
> false ACK due to the new fast GRBM interface."*

GC 10.1.3 está abaixo de 10.3.0. **Sem essa leitura, um ACK observado não prova
invalidação.** O driver faz a leitura e guarda o valor em vez de descartar: é
barato, e diz se o `REQ` sequer aceitou o que foi escrito.

### Valor escrito em `REQ` — `gfxhub_v2_0_get_invalidate_req`

| Campo | Shift | Valor |
|---|---:|---|
| `PER_VMID_INVALIDATE_REQ` | 0 | `1 << vmid` |
| `FLUSH_TYPE` | 16 | parâmetro |
| `INVALIDATE_L2_PTES` | 19 | 1 |
| `INVALIDATE_L2_PDE0` | 20 | 1 |
| `INVALIDATE_L2_PDE1` | 21 | 1 |
| `INVALIDATE_L2_PDE2` | 22 | 1 |
| `INVALIDATE_L1_PTES` | 23 | 1 |
| `CLEAR_PROTECTION_FAULT_STATUS_ADDR` | 24 | 0 |

O último fica em 0 como no upstream: limpar o endereço da falha aqui apagaria a
evidência de uma falha anterior.

Confirmação de ACK: bit `1 << vmid` de `GCVM_INVALIDATE_ENG17_ACK`.

## E a SDMA traduz por qual hub?

Pelo **GFXHUB** — o hub que todos os registradores deste catálogo controlam.
Não é dedução: `sdma_v5_0.c` atribui literalmente

```c
ring->vm_hub = AMDGPU_GFXHUB(0);
```

Se fosse o MMHUB, todo o gate de memória estaria programando o hub errado e
nenhum passo teria efeito sobre a SDMA. Valia confirmar em fonte antes de gastar
um boot para descobrir.

### O que o passo 3 deliberadamente NÃO escreve

`gfxhub_v2_0_setup_vmid_config`, que configura os contextos 1..15 e existe para
VMs de usuário. Este laboratório submete por VMID 0. Escrever 15 contextos que
ninguém vai usar aumenta a superfície de escrita sem aumentar a prova.

## Registradores do CP, para a frente que vem

Todos derivados de `gc_10_1_0_offset.h` pela regra `byte = dw * 4 + 0x4980`.
**Nenhum foi escrito ainda.** Estão aqui porque o primeiro ensaio da frente do
CP os lê.

| Registro | dw | byte | Para que serve |
|---|---:|---:|---|
| `CP_STAT` | `0x0f40` | `0x08680` | estado agregado do CP |
| `CP_ME_CNTL` | `0x0f56` | `0x086d8` | HALT de PFP/ME/CE — o equivalente do HALT do F32 |
| `CP_PFP_INSTR_PNTR` | `0x0f45` | `0x08694` | ponteiro de instrução do PFP |
| `CP_ME_INSTR_PNTR` | `0x0f46` | `0x08698` | ponteiro de instrução do ME |
| `CP_CE_INSTR_PNTR` | `0x0f47` | `0x0869c` | ponteiro de instrução do CE |
| `CP_MEC1_INSTR_PNTR` | `0x0f48` | `0x086a0` | ponteiro de instrução do MEC1 |
| `CP_CPF_STATUS` | `0x0e27` | `0x0821c` | status do prefetch |
| `CP_CPC_STATUS` | `0x0e24` | `0x08210` | status do compute |
| `CP_CPF_BUSY_STAT` | `0x0e28` | `0x08220` | ocupação do prefetch |
| `CP_RB0_BASE` | `0x1de0` | `0x0c100` | base do anel GFX 0 |
| `CP_RB0_CNTL` | `0x1de1` | `0x0c104` | controle do anel GFX 0 |
| `CP_RB0_WPTR` | `0x1df4` | `0x0c150` | WPTR do anel GFX 0 |
| `CP_RB0_RPTR` | `0x0f60` | `0x08700` | RPTR do anel GFX 0 |
| `CP_RB_VMID` | `0x1df1` | `0x0c144` | VMID do anel |
| `RLC_CNTL` | `0x4c00` | `0x17980` | controle do RLC |
| `RLC_SAFE_MODE` | `0x4c05` | `0x17994` | modo seguro do RLC |
| `GRBM_STATUS2` | `0x0da2` | `0x08008` | ocupação por bloco, segunda palavra |

### O caminho de direct load do CP vive todo no segmento 1

`byte = (segment_base_dw[BASE_IDX] + mmREG_dw) * 4`, com `0x1260` para
`BASE_IDX 0` e `0xA000` para `BASE_IDX 1`.

| Registro | dw | seg | byte |
|---|---:|---:|---:|
| `CP_PFP_IC_BASE_LO` | `0x5840` | 1 | `0x3E100` |
| `CP_PFP_IC_BASE_HI` | `0x5841` | 1 | `0x3E104` |
| `CP_PFP_IC_BASE_CNTL` | `0x5842` | 1 | `0x3E108` |
| `CP_PFP_IC_OP_CNTL` | `0x5843` | 1 | `0x3E10C` |
| `CP_ME_IC_BASE_LO` | `0x5844` | 1 | `0x3E110` |
| `CP_ME_IC_BASE_HI` | `0x5845` | 1 | `0x3E114` |
| `CP_ME_IC_BASE_CNTL` | `0x5846` | 1 | `0x3E118` |
| `CP_ME_IC_OP_CNTL` | `0x5847` | 1 | `0x3E11C` |
| `CP_CE_IC_BASE_LO` | `0x5848` | 1 | `0x3E120` |
| `CP_CE_IC_BASE_HI` | `0x5849` | 1 | `0x3E124` |
| `CP_CE_IC_BASE_CNTL` | `0x584A` | 1 | `0x3E128` |
| `CP_CE_IC_OP_CNTL` | `0x584B` | 1 | `0x3E12C` |
| `CP_PFP_UCODE_ADDR` | `0x5814` | 1 | `0x3E050` |
| `CP_PFP_UCODE_DATA` | `0x5815` | 1 | `0x3E054` |
| `CP_CE_UCODE_ADDR` | `0x5818` | 1 | `0x3E060` |
| `CP_CE_UCODE_DATA` | `0x5819` | 1 | `0x3E064` |
| `CP_MEC_ME1_UCODE_ADDR` | `0x581A` | 1 | `0x3E068` |
| `CP_MEC_ME1_UCODE_DATA` | `0x581B` | 1 | `0x3E06C` |
| `RLC_CNTL` | `0x4C00` | 1 | `0x3B000` |
| `RLC_SAFE_MODE` | `0x4C05` | 1 | `0x3B014` |

Isto corrige a entrada `UCODE_ADDR/DATA = 0x3E200/0x3E204`, que estava em classe
P2 aguardando reconciliação de segmento. Os endereços corretos, derivados do
header autoritativo com o `BASE_IDX` respeitado, são `0x3E050`/`0x3E054` para o
PFP. **`0x3E200` fica em quarentena e não deve ser escrito.**

**Nota de Hardware (Medição 2026-08-23):** Os registradores `CP_*_IC_BASE_LO`,
`CP_*_IC_BASE_HI`, `CP_*_IC_BASE_CNTL` e `CP_*_UCODE_ADDR/DATA` são portas de
escrita direta para os controladores de cache e RAM interna das microengines do
CP (não possuem barramento de readback no MMIO, lendo `0xFFFFFFFF` na leitura). O
upstream Linux (`gfx_v10_0.c`) apenas os escreve sem esperar readback.

### Base do segmento 1 conferida contra registrador vivo

O segmento 0 (`0x1260`) está amplamente verificado: 35 registradores de tradução
escritos e conferidos por readback, `GRBM_STATUS` estável em quatro boots,
`CC_GC_SHADER_ARRAY_CONFIG` batendo com o valor visto no Linux, e a aritmética de
page table fechando com `FB_OFFSET`/`FB_LOCATION`.

O segmento 1 (`0xA000`) está classificado **P1: derivado, não medido.** E todo o
caminho de direct load do CP mora nele.

Escrever `CP_PFP_IC_BASE_LO` com a base não verificada seria escrever MMIO por
suposição. `tools/test-mmio-segment1.ps1` fecha essa lacuna antes de qualquer
escrita no CP, usando o banco de rascunho:

| Alvo | dw | seg | byte |
|---|---:|---:|---:|
| `SCRATCH_REG0..7` | `0x2040`..`0x2047` | 1 | `0x30100`..`0x3011C` |
| `TCP_WATCH0_ADDR_H` (controle, só leitura) | `0x2040` | 0 | `0x0CA80` |

Por que o rascunho é o alvo certo: registrador de rascunho existe para ser
escrito — não configura nada, não dispara nada. O upstream escreve `0xCAFEDEAD`
em `mmSCRATCH_REG0` em `gfx_v10_0_ring_test_ring()` por essa exata razão.

Por que o controle discrimina: o dword `0x2040` existe nos **dois** segmentos com
nomes diferentes. Se as escritas em `0x30100` aparecerem em `0x0CA80`, as duas
derivações colidem e nenhuma está provada. Se `0x0CA80` ficar intacto e `0x30100`
guardar o que foi escrito, a base está localizada.

Dois padrões complementares são escritos, não um: um padrão só não distingue
armazenamento de valor preso que por acaso coincidiu. E oito endereços
consecutivos guardando oito valores **diferentes** descartam espelhamento.

### AVISO: `BASE_IDX` faz parte do endereço

Medido em 2026-08-23. A primeira versão de `test-cp-firmware.ps1` calculou
`RLC_CNTL` e `RLC_SAFE_MODE` com a base do segmento 0, porque a tabela só
guardava `mmREG_dw`. Os dois são `BASE_IDX 1`. Os valores relatados como RLC na
execução das 14:21 **não eram desses registradores** — eram de outro endereço
qualquer, e o gate de legibilidade passou lendo a coisa errada.

Regra: **anotar `mmREG` sem `BASE_IDX` é anotar metade do endereço.** Toda
entrada nova neste catálogo declara o segmento.

### Os ponteiros de instrução são o instrumento honesto do CP

`CP_*_INSTR_PNTR` é para o CP o que o PC do F32 foi para a SDMA: a evidência
física de que uma microengine está executando, e não apenas configurada. Foi o PC
do F32 que provou o microcontrolador da SDMA acordando, e é a mesma classe de
prova que vale aqui.

Com a lição do `UTCL1_PAGE` aplicada: **um valor único não distingue parado de em
movimento.** Os ponteiros são amostrados várias vezes, e o que se relata é a
contagem de valores distintos.

### O que já se sabe, medido nesta placa (2026-08-23 14:21)

`tools/test-cp-firmware.ps1`, evidência
`docs/evidence/2026-08-23_14-21-34-DESKTOP-HQAG6T4-cp-firmware`:

| Firmware | Tipo | Payload | RespStatus | Veredito |
|---|---:|---:|---:|---|
| `rlc` | 8 | 6272 dw | `0x00000000` | **aceito** |
| `pfp` | 2 | 65792 dw | `0x80000203` | recusado |
| `me` | 1 | 65792 dw | `0x80000203` | recusado |
| `ce` | 3 | 65760 dw | `0x80000203` | recusado |
| `mec` | 4 | 67084 dw | `0x80000203` | recusado |

Nenhum ponteiro de instrução se moveu, antes ou depois: carregar firmware não põe
o CP a executar, e nada aqui o tirou de reset.

**O PSP desta placa carrega RLC e SDMA, e recusa as microengines do CP.** As duas
metades importam:

- A metade do "aceita" **revoga** a leitura de terceiros de que RLC e SDMA
  retornariam `ITEM_NOT_FOUND`. Aquela medição usou firmware de outro chip
  (`navi12_sdma.bin`); com o firmware certo desta placa, o SOS aceita. O erro era
  de conteúdo, não de tipo não suportado.
- A metade do "recusa" **reproduz** exatamente o `0x80000203` medido de forma
  independente, com origem de payload diferente. Dois caminhos distintos
  chegando ao mesmo código tornam improvável que seja defeito de protocolo.

Conclusão histórica revogada em 2026-08-24: o fato de `psp_v11_0_8.c` conter
somente as funções do ring não elimina `LOAD_IP_FW`. O caminho genérico de
`amdgpu_psp.c`, quando `firmware.load_type == AMDGPU_FW_LOAD_PSP`, percorre a
lista de ucodes e envia `LOAD_IP_FW`, incluindo CE, PFP, ME, MEC e a Jump Table
do MEC. Portanto o `0x80000203` continua sendo fato físico daquela sequência,
mas **não prova** que a BC-250 exige direct load. A sequência usada também não
reproduzia a ordem completa do upstream (`SETUP_TMR`, CP antes de RLC e MEC/JT
separados).

### O que ainda não se sabe

1. **O CP precisa do RLC rodando antes?** No upstream o RLC é inicializado antes
   do CP. Se o CP não sai de reset sem RLC, isso ordena todo o resto.
2. **`CP_ME_CNTL` tem HALT por microengine?** Se sim, o padrão de bring-up é o
   mesmo do F32 da SDMA: ucode, tirar do HALT, conferir o ponteiro andando.
3. **A cache de instruções do CP traduz por VMID 0?** `CP_PFP_IC_BASE_CNTL` tem
   campo `VMID`, e o upstream o programa com 0. Se for assim, o gate de memória
   fechado ontem é pré-requisito direto do CP, não trabalho paralelo.
