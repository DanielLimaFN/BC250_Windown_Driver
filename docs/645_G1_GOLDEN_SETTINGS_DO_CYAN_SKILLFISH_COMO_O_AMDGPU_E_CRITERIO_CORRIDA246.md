# 645 — G.1: golden settings do Cyan Skillfish como o amdgpu; critério da Corrida 246

**Escrito antes do ensaio (Lei 10), 07/10/2026, offline.** Continua o [doc 644](644_CORRIDA245_RLC_NA_ROM_TRAVA_VOLTA_AOS_5MIN_DESFECHO_A.md) §4.1.

## 1. Por quê

- **O amdgpu (P1, `gfx_v10_0.c:3581` e `:3981`):** no `gfx_v10_0_hw_init`, a primeira coisa é `gfx_v10_0_init_golden_registers`. Para a GC 10.1.3, ele chama `soc15_program_register_sequence(golden_settings_gc_10_0_cyan_skillfish)`, com 34 registradores, antes do RLC e do CP. No boot, o gfx `hw_init` vem antes do `sdma hw_init`.
- **Nós (P0, snapshot DX.26 da Corrida 243):** 30 dos 34 registradores estão diferentes do upstream, porque nunca aplicamos nenhum. Alguns estão no caminho de memória do GC, por onde passam SDMA, CP e shaders:
  - roteamento e casamento de endereço do L2: `GL2C_ADDR_MATCH_MASK`, `GL2A_ADDR_MATCH_MASK`, `GL2_PIPE_STEER_0/1`, `GL2C_CTRL2/3`;
  - L1 e canal: `GL1_PIPE_STEER`, `CH_PIPE_STEER`, `CH_VC5_ENABLE`, `*_DRAM_BURST_CTRL`;
  - `UTCL1_CTRL` e `TA_CNTL_AUX`.
- **Ligação com a trava (H):** um caminho de memória fora do configurado pelo fabricante pode explicar uma trava dura com a GPU ociosa e sem erro. Isso **não está provado**.

## 2. Mudança (uma variável)

`BC250_G1_GOLDEN_LIKE_AMDGPU 1`. O resto fica igual à 245: RLC na ROM, P3.3=0, MEC.1=1 e GFXRST.1=1, que só age em TDR.

- **Tabela:** é a de `bc250_golden_settings_contract.c`, conferida campo a campo contra o upstream. O portão `audit_g1_golden_like_amdgpu` reprova se ordem, máscara ou valor divergirem.
- **Momento:** no StartDevice, **antes** do trecho do PSP, ou seja, antes da carga do SDMA0, do anel e do CP. É a ordem do amdgpu. O PFP direct load, que mexe temporariamente em `GE_FAST_CLKS`, `CGTT_CPF_CLK_CTRL` e `CP_SD_CNTL`, restaura o valor que leu, e esse valor passa a ser o golden.
- **Lei 5 e Lei 6 (`Bc250ActiveG1ApplyGolden`):**
  - snapshot em duas passadas; o plano do contrato recusa leitura instável;
  - RMW pela fórmula SOC15, na ordem da tabela;
  - readback de cada registrador nos bits da máscara; o que não conferir volta ao original e é contado.
- **`GRBM_GFX_INDEX`:** recebe `0xE0000000` (broadcast), mas a leitura desse seletor não confere (DX.26, Corrida 37). Por isso ele é escrito sem readback e não é restaurado.
- **Desarme:** `Bc250ActiveG1RestoreGolden`, no teardown da GART, depois de SDMA0, SDMA1 e IH pararem. Devolve os originais em ordem inversa.
- **Transporte:** escrita liberada só para os offsets da tabela, por `Bc250MmioTopologyG1GoldenWriteAllowed`. O portão DX.26 foi ajustado para aceitar exatamente esse escritor.
- **Auditoria:** PASS. 5 mutações reprovam:
  1. valor do upstream trocado;
  2. arme sem readback;
  3. arme fora do lugar;
  4. desarme removido;
  5. escrita de golden fora da G.1.
- **Diferença declarada com o amdgpu:** o amdgpu nunca restaura os goldens; nós restauramos no teardown para devolver a placa ao BasicDisplay.

| Artefato | Valor |
|---|---|
| Pacote | **3.390.1.0** |
| SYS lab | `7D95F4A7…` |
| SYS assinado | `55255FB0…` |
| INF | `916F2B9A…` |
| CAT | `CCE37CE7…` |
| UMD | `8782A5E5…` (a mesma) |
| RADV | `B4AE2E14` (a mesma) |
| Auditoria do pacote | PASS |
| Pacote 3.389 (245) guardado em | `driver\wdk\package\output_lab_3.389.1.0_245\` |
| Script | `tools\EXECUTAR_CORRIDA246_G1_GOLDEN_MENU_600S.cmd` |

O roteiro é o mesmo: só o menu até o rollback.

## 3. Pré-condição de validade

1. `[G1_GOLDEN] RESUMO: 34 registradores, N aplicados, ... -> ARMADO`, com N perto de 30. O snapshot da 243 tinha 30 diferentes, e `GRBM_GFX_INDEX` conta entre eles.
2. Mais adiante, `[GOLDEN_SNAPSHOT] RESUMO` com no máximo 1 `DIFERENTE` (o `GRBM_GFX_INDEX`, que lê 0) mais os divergentes listados no `[G1_GOLDEN]`.
3. `[RLC_ESTADO] boot` com `RLC_ENABLE_F32=0` e `rptr_por_doorbell=0`.

## 4. Desfechos

Base: 245 = ~289 s; histórico sem P3.3 nem RLC = 45–584 s.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | Pré-condição OK, **600 s sem trava dura** e rollback limpo, com o menu em uso | Os goldens fora do upstream são candidatos fortes a causa da trava dura. Exige repetição (a 222 e a 226 também passaram 600 s) |
| B | Pré-condição OK e trava dura com a assinatura de sempre depois de 60 s | Os goldens não são causa suficiente. A G.1 fica, por paridade. Próximo: RLC.1b (RLC com o handshake do SMU desligado) e o inventário restante |
| B1 | Pré-condição OK e trava antes de 60 s | Os goldens, como estão, encurtam a vida da máquina, como a P3.3 e o RLC. Desligar e investigar |
| C | `[G1_GOLDEN] RECUSADO`, `NAO TENTADO` ou `NADA APLICADO` | O ensaio não aconteceu; registrar o motivo |
| D | Regressão no boot: PSP, tripé do SDMA0 ou PFP sem prova, Código 43 no StartDevice, desktop sem subir, ou fault ou timeout cedo | Um golden quebra o bring-up na nossa ordem. Desligar e bissectar a tabela (os do L2 primeiro) |
| E | Fora do mapa | Descrever antes de julgar |

**Teto:** 2 corridas. Uma repetição só se der A.

**NÃO VALIDADO EM HARDWARE:** tudo deste doc.
