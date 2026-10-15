# 620 — P4.1: SDMA1 vivo e ocioso como o amdgpu, e critério da Corrida 235

**Origem:** a Corrida 234 (doc 619) tirou a P6 e o EOP do CP das causas suficientes da trava dura. O próximo item do inventário de paridade (doc 593 §4) é a **P4: SDMA1**.

**A diferença medida:**
- No Linux da própria placa, o `amdgpu` sobe as duas instâncias do SDMA, e as duas trabalham (doc 552: fences `sdma0 0x7A6`, `sdma1 0x656`; `ring sdma1 uses VM inv eng 13 on hub 0`).
- Nós deixávamos o SDMA1 como o POST deixa (P0, Corridas 199/200): F32 parado, anel desligado, `SDMA1_CLK_CTRL=0x000C0100`.

**A P4 em duas etapas:**
- **P4.1 (esta):** SDMA1 com firmware, anel ligado e provado, e **ocioso**. É o estado em que o `amdgpu` deixa a instância entre trabalhos.
- **P4.2 (depois):** colocar trabalho no SDMA1, como o escalonador do `amdgpu` faz com as atualizações de VM e cópias.

## 1. O que muda (candidato 3.382.1.0)

**Firmware:**
- `cyan_skillfish2_sdma1.bin` (33.792 bytes, SHA-256 `BD1C0B0F…6255`, ucode 0x34, feature 50) entra no pacote: INF, os dois empacotadores, as duas auditorias de pacote e o binder.
- É a mesma versão que o `amdgpu` carregou na placa (doc 552). A identidade byte a byte com o linux-firmware **não** foi verificada.
- **Carga:** pelo PSP, `GFX_FW_TYPE_SDMA1 = 10` (`psp_gfx_if.h` v6.6, P1), logo depois do SDMA0, no mesmo trecho de uso do PSP (antes do arme do IH, regra da P3.0b.1).
- **Provas da carga:** SHA-256 do arquivo e do payload no KMD, e `SDMA1_UCODE_CHECKSUM` (`0x6224`) não nulo depois da carga.

**Contrato novo `driver/core/bc250_sdma1_bringup.{h,c}`:**
- **Offsets:** do `gc_10_1_0_offset.h`, conferidos como SDMA0 + 0x600 dw (`SDMA1_REG_OFFSET`). Catálogo docs/03, seção SDMA1.
- **PREFLIGHT:** checksum do firmware não nulo e anel desligado (senão recusa sem tocar).
- **SNAPSHOT:** F32, RB, IB, CNTL, CLK, STATUS.
- **APPLY:** na ordem de `sdma_v5_0_gfx_resume`, com os mesmos valores do SDMA0 provado:
  - anel de 4 KB (`RB_SIZE=10`), writeback do RPTR, poll do WPTR `0x00403000|F32_POLL_ENABLE`;
  - golden do cyan skillfish (`CHICKEN_BITS`, `GB_ADDR_CONFIG`, `GB_ADDR_CONFIG_READ`);
  - `MINOR_PTR_UPDATE`, doorbell desligada, `UTC_L1_ENABLE`, UTCL1 (`RESP_MODE=3`, `REDO_DELAY=9`, `UTCL1_PAGE`);
  - F32 destravado, anel e IB ligados.
- **READBACK:** de todos os campos, antes da prova.
- **PROVA:** um `FENCE` (MTYPE_UC) sobre poison na página Work, e `RPTR == WPTR` (64 bytes).
- **Relógio:** os overrides de `SDMA1_CLK_CTRL` ficam ligados só durante a configuração, como no SDMA0, e o valor do POST volta depois da prova. O Linux não mexe nesse registrador na Cyan Skillfish (doc 557).
- **Falha depois do APPLY:** desliga a engine (`Bc250Sdma1Shutdown`: anel, IB, poll e writeback desligados, F32 parado, com readback). A memória só volta ao sistema com o desligamento provado.

**Miniport:**
- `Bc250ActiveP4Sdma1Start`, chamada no fim do StartDevice, depois da GART da D2 e antes do IH:
  - anel e Work em RAM não cacheada pela GART da VMID 0, entradas **224/225** (VA `0xE0000`/`0xE1000`, MTYPE_UC);
  - readback das PTEs, HDP e invalidação da VMID 0.
- `Bc250ActiveP4Sdma1Stop` no desligamento da D2, **antes** do IH, do MMHUB e da GART.
- **Transporte:** o SDMA1 entra nas listas de leitura e escrita só pela fonte única `Bc250Sdma1IsRegister`. `POWER_CNTL` fica fora.

**Chave:** `BC250_P4_SDMA1_ENABLED=1`.

**O que não muda:**
- nenhuma submissão vai para o SDMA1;
- o SDMA1 não emite TRAP;
- a RADV e a UMD são as mesmas da 234 (RADV `C6BEC54B`).

## 2. Provas offline

- **Suíte:** `build-contract` EXIT=0 sem warnings. Teste novo `sdma1_bringup_test` com um SDMA1 simulado:
  1. sucesso com o tripé e o relógio de volta ao POST;
  2. sem firmware;
  3. anel herdado ligado;
  4. escrita perdida pega no readback, com a engine desligada;
  5. engine que não executa (timeout, fence no poison, memória liberável);
  6. F32 que não para (memória **não** liberável);
  7. argumentos e lista de registradores.
- **Portão novo `audit_p4_sdma1_alive`.** A regra da P3.0b.1 passou a contar também a carga do firmware do SDMA1 como uso do PSP.
- **16 mutações reprovadas**, com os fontes conferidos por hash:
  1. offset errado;
  2. tipo do PSP errado;
  3. checksum do SDMA0 para o SDMA1;
  4. sem exigir firmware;
  5. pronto sem prova;
  6. prova só pela fence;
  7. retorno sem desligar;
  8. liberável sem F32 parado;
  9. `POWER_CNTL` na lista;
  10. escrita fora da lista;
  11. SDMA1 depois do IH;
  12. sobe sem firmware;
  13. RAM livre sem desligamento;
  14. desliga depois da GART;
  15. carga do firmware depois do IH;
  16. firmware fora do INF.
- Na primeira passada, uma das 16 passou (a regra do `POWER_CNTL` não reconhecia `0x000061E8u`). Corrigi a regra e repeti a bateria.
- **Pacote 3.382.1.0:** perfil `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, auditoria do pacote PASS. Strings `[P4_SDMA1]` no SYS assinado.

| Arquivo | SHA-256 |
|---|---|
| SYS | `AA9F741D9507E2DBC56E344C9EAB6EF6AB5C7D93C6A1E20B815F29B1E877BB3C` |
| INF | `5DF74541A91D67EF306F41EC83A211038090F79A3547858FAD0D1FEEEA07B3E2` |
| CAT | `6A0081DAB863E397273C3D855456D1BB9D40224E842D4E268BA812D029E1E520` |
| `cyan_skillfish2_sdma1.bin` | `BD1C0B0F6A6A4F17EDE034F2C844B6553FC444E08915C7BB500C09FDE59D6255` |
| UMD | `8782A5E5…` (inalterada) |
| RADV | `C6BEC54B…` (a da 234) |

**Scripts:** `tools/prepare-corrida235-p41-sdma1-vivo.ps1` e `tools/EXECUTAR_CORRIDA235_P41_SDMA1_VIVO_600S.cmd`. O preflight a seco passou por todos os hashes e parou só na CMOS.

## 3. Critério da Corrida 235

**Pré-condição:**
- PSP OK, StartDevice `status=0`;
- `[P3B_MMHUB] LIGADA`, `[P3_IH] ATIVO`, `[CP_EOP] ARMADO`.

**Uso:** boot frio, menu e Pesquisa.

- **A — SDMA1 vivo e ocioso**
  - *Assinatura:* todos os sinais abaixo.
    - `[P4_SDMA1] firmware pelo PSP (tipo 10): Status=0 PhysicalReady=1` com checksum depois ≠ 0;
    - `[P4_SDMA1] BringUp=OK` com `readback=1`, `fence` igual ao esperado, `RPTR=64 WPTR=64` e `CLK de volta=0x000C0100`;
    - `[P4_SDMA1] resultado: ATIVO`;
    - zero `GFXHUB_FAULT` e TDR novos;
    - desktop normal;
    - teardown com `[P4_SDMA1] DESLIGADO`.
  - *Implicação:* a P4.1 fecha como mecanismo. Se a trava vier igual, o estado parado do SDMA1 sai das causas, e o próximo é a **P4.2** (trabalho no SDMA1) ou a P5.
- **B — o PSP não carrega o SDMA1**
  - *Assinatura:* firmware com `Status≠0`, ou `PhysicalReady=0`, ou checksum 0, e `[P4_SDMA1] NAO TENTADO: firmware do SDMA1 nao carregado`.
  - *Implicação:* o PSP recusa o tipo 10 ou o arquivo. A resposta do PSP (`Resposta=`) diz o motivo. O resto da corrida é igual à 234.
- **C — o SDMA1 não prova**
  - *Assinatura:* `BringUp=READBACK_FAILED` ou `PROOF_TIMEOUT` (fence no poison `0x5D1A0BAD` ou RPTR parado), seguido de `resultado: NAO ATIVO ... RAM devolvida`.
  - *Implicação:* um campo não pegou, ou a engine não busca pela GART. Os valores antes/aplicado/prova da linha dizem qual. O SDMA1 volta a ficar parado.
- **D — regressão**
  - *Assinatura:* qualquer um destes:
    - StartDevice falhando ou Código 43;
    - falha do PSP depois da carga do SDMA1;
    - SDMA0 com timeout novo;
    - `outros` crescendo na linha `[P3_IH] regime` (interrupção do SDMA1 sem dono);
    - `DESLIGAMENTO NAO PROVADO`.
  - *Implicação:* chave `BC250_P4_SDMA1_ENABLED=0` e ler o caso.
- **E — fora do mapa**
  - *Assinatura:* descrever o observado antes de julgar.
  - *Implicação:* a definir.

**A trava dura continua possível.** Se vier, conta como a primeira amostra com o SDMA1 vivo.

## 4. Pendências

**NÃO VALIDADO EM HARDWARE:**
- a P4.1 inteira;
- o teardown do SDMA1, do EOP do CP, do IH e da GART do MMHUB em regime;
- a P4.2.
