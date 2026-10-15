# 764 — C01 a C07 do Nagi: paridade pelo rastro do amdgpu, pacote 3456 e critério da Corrida 302

> **Data:** 15/10/2026. **Origem:** prioridades 1 e 2 do [doc 763](763_CHECKLIST_DO_QUE_COPIAR_DO_NAGI_AMDGPU_WDDM_PARA_AVANCAR_MAIS_RAPIDO.md).
> **Pacote:** lab **3.456.1.0** = 3.455.1.0 (Corrida 301) + C01 + C03 + C05 + C06 + C07.
> SYS fonte `E746D15E`, SYS assinado `3EB03C0F`, INF `E3EA90D9`, CAT `6AD83376`, UMD `30D56611` e RADV `296F3F8F` iguais à 301.
> `build-contract.bat` EXIT 0; `build-bc250mcdm-lab-bringup.ps1` OK; `audit-bc250mcdm-lab-package.ps1` PASS;
> `audit-bc250mcdm-safety.py`, `audit-mcdm-ddi-matrix.py` e `audit-ddi-noop-callbacks.py` PASS.
> **NÃO VALIDADO EM HARDWARE.**

A Corrida 301 (pacote 3.455.1.0) ainda não rodou. A 302 leva tudo da 301 mais estes itens, e pode rodar no lugar dela.

## 1. Resultado por item

| Item | Estado | O que foi feito |
|---|---|---|
| C01 | ✅ código, ⬜ hardware | Rastro E03 comparado com o KMD; 3 grupos de estado de init que o Linux deixa e nós não, aplicados |
| C02 | 🟡 offline | Dumps de anel do Linux vazios; só os MQDs comparados, sem mudança de código |
| C03 | ✅ código, ⬜ hardware | RLC.1b religada |
| C04 | ❌ bloqueado nesta tacada | Copiar o paging buffer para o anel não funciona com endereços virtuais (§4) |
| C05 | ✅ código, ⬜ hardware | Wave kill (SQ_CMD) no quiesce do TDR do GFX |
| C06 | ✅ código, ⬜ hardware | VSYNC só pela VUPDATE sem lock, na sequência do Linux; erros do CP contados no IH |
| C07 | ✅ código e teste offline, ⬜ hardware | Pool de VMID por LRU |

## 2. C01 — o rastro do amdgpu contra o nosso KMD

**Ferramenta nova:** `tools/c01-comparar-rastro-e03.py`. Ela lê `referencias/amdgpu-wddm/evidence/linux/2026-09-21-E03-init-trace/`. Nomeia os registradores pelo `regcalc` do Nagi e procura cada um no fonte do KMD (`driver/core`, `driver/wdk`), pelo nome e pelo offset.

Saídas em `docs/evidence/c01-rastro-e03/`:
- `c01-comparacao.csv`: 539 registradores escritos pelo `amdgpu` no init, fora DMU, mailboxes e portas índice/dado;
- `c01-estado-mudado-pelo-amdgpu.csv`: 1.066 registradores cujo valor depois do init difere do POST (sweep antes/depois).

Filtro final: escrito pelo `amdgpu`, valor diferente do POST e nunca citado no KMD. Sobram estes grupos (P1 pelo rastro, P1 pelo `gfx_v10_0.c` local):

| Grupo | POST | Linux | Nosso (até 301) | C01 |
|---|---|---|---|---|
| `SH_MEM_CONFIG`, VMIDs 0..15 | 0 | `0x0000C00C` (alinhamento UNALIGNED, prefetch de instrução 3) | 0 | **aplicado** |
| `SH_MEM_BASES`, VMIDs 1..15 | 0 | `0x20001000` (1..7), `0x60006000` (8..15) | 0 | **aplicado** |
| `SDMA0/1_PHASE0/1/2_QUANTUM` | `0x00010002` | `0x2000` | POST | **aplicado** |
| `GCVM_INVALIDATE_ENG2..16_ADDR_RANGE` | 0 | `0xFFFFFFFF`/`0x1F` | POST (0, 1 e 17 já tinham escritor) | **aplicado** |
| `GDS_VMIDn_*`, `GDS_GWS/OA_VMIDn`, n=1..15 | SIZE `0x10000`, GWS `0x400000` | 0 | POST | **fora, de propósito** |
| `CP_INT_CNTL_RING0` bits 22–24 | 0 | 1 | 0 | **aplicado pelo C06** |
| `CPC_INT_CNTL`, `CP_ME1_PIPEn_INT_CNTL` | 0 | `0x05800000` | 0 | fora (MEC sem interrupção aqui) |
| `SDMAn_RLCn/PAGE_RB_WPTR_POLL_CNTL` | `0x00401000` | `0x00403000` | POST | fora (filas não usadas) |
| `SPI_GDBG_TRAP_*`, doorbell self-ring do BIF | — | — | — | fora (depurador e KFD) |

**Por que o GDS fica de fora:** o Linux zera o GDS das VMIDs 1..15 porque o reprograma por tarefa, no anel (`gfx_v10_0_ring_emit_gds_switch`), quando a tarefa pede GDS. Nós não temos essa troca. O POST deixa 64 KB em todas as VMIDs, e a RADV do GFX10 usa GDS. Zerar tiraria o GDS da RADV.

**Leitura (H):** com `SH_MEM_CONFIG=0` nós rodávamos com alinhamento DWORD, e o ACO assume acesso desalinhado no GFX10. Com `SH_MEM_BASES=0` as aberturas de LDS e privada ficavam na base 0, onde estão os nossos endereços. Isso só afeta instruções `flat` genéricas: o ACO usa `global_*` para a memória global. Nenhuma das duas explica sozinha uma trava com a GPU ociosa; é paridade.

**Código:**
- `driver/core/bc250_c01_init_parity.h`: tabela, valores e allowlist de escrita, com procedência;
- `Bc250ActiveC01ApplyInitParity`/`RestoreInitParity` em `bc250_active_miniport.c`: logo depois da G.1, antes do PSP, do SDMA0 e do CP. Snapshot, escrita, readback e restauração do que não conferir; devolve o POST no teardown;
- chave `BC250_C01_INIT_PARITY` (1).

## 3. C02 — anéis e MQDs

- Os dumps de anel do E03 (`rings/amdgpu_ring_*`) estão **vazios**: zero dwords não nulos. Não há fluxo de pacotes do Linux para comparar.
- Os MQDs têm conteúdo. O do GFX (`v10_gfx_mqd`):
  - `cp_gfx_hqd_cntl=0x00A0080A`;
  - `cp_gfx_hqd_quantum=0x00000A01`;
  - `cp_rb_doorbell_control=0x40000458`.
  O Linux usa o anel gráfico assíncrono, mapeado pela KIQ; nós programamos o `CP_RB0` direto. A doorbell é a mesma (0x116 << 2, C1). O quantum não existe no nosso caminho.
- O MQD de compute traz `cp_hqd_pq_control=0xF030890A` e `cp_hqd_ib_control=0x00300000`.
- Sem mudança de código. A diferença de arquitetura (CP_RB0 direto contra MQD/KIQ) já era conhecida.

## 4. C04 — por que não é uma cópia

Os pacotes do nosso paging buffer usam **endereços virtuais da VMID do processo de paginação** (4c.4, `GPU_VIRTUAL`). Um pacote que roda **dentro do anel** do SDMA0 roda na **VMID 0** (GART); só o `INDIRECT` carrega a VMID do IB (`sdma_v5_0_ring_emit_ib`). Copiar o paging buffer para o anel faria o SDMA0 escrever nos endereços certos na tabela errada. Isso pode corromper RAM do Windows, o mesmo tipo de dano que procuramos.

O Nagi consegue porque monta comandos **com endereço físico (MC)** a partir das páginas capturadas no build ("physical SDMA commands generated from captured page identities", `docs/design/sdma-virtual-paging.md` deles). Para nós, o C04 é refazer o builder da paginação para emitir MC, com a resolução de cada página no build. É uma faixa própria, não uma bissecção barata. Também existe o limite do anel do SDMA0 (1.024 dwords) contra paging buffers de até 64 KB. **Não implementado.** Proposta: desenho separado, depois da 302.

## 5. C05, C06 e C07

**C05:** `Bc250ActiveC05WaveKill`, como `amdgpu_ring_soft_recovery` com `gfx_v10_0_ring_soft_recovery`. `SQ_CMD` (`0x8DEC`) recebe CMD=3, MODE=1 e, com VMID de usuário conhecida (`LastSubmitVmid` 1..15), CHECK_VMID e VM_ID. Repete a cada 100 µs até o `GRBM_STATUS` mostrar o GFX ocioso, ou por 10 ms. Se ficar ocioso, o GFXRST.1 vê "nada ocupado" e não pulsa o reset. Linha `[C05_WAVEKILL]`. Escrita do `SQ_CMD` aberta só para essa função.

**C06, VSYNC:** o rastro do Linux nesta placa (t=124.694285–124.697173) mostra a sequência completa:
1. arma só a VUPDATE sem lock, em duas escritas: só o ack (`0x00104104 → 0x00114104`), releitura `0x00100104`, depois só o enable (`0x00101104`);
2. a interrupção chega 2,9 ms depois (ack `0x0011D104` no handler);
3. nenhum registrador de roteamento é tocado: `OTG0_INTERRUPT_DEST` não aparece no rastro, nem em leitura.

Na 273 nós armávamos três fontes numa escrita só e nenhuma chegou. Agora (`BC250_C06_VSYNC_SO_VUPDATE_COMO_LINUX` = 1) é exatamente a sequência do Linux, sem VSTARTUP nem VLINE0. Linha `[C06_VSYNC]`.

**C06, erros do CP:** `CP_INT_CNTL_RING0` ganha PRIV_INSTR, PRIV_REG e OPCODE_ERROR (bits 22–24), como o valor final do Linux (`0x05C00000`). O ISR conta as fontes 0xB4, 0xB7, 0xB8 e 0xB9 do cliente 0x14 e guarda a última entrada. O relatório do IH escreve `[C06_CP_ERRO]` quando alguma conta passa de zero. O desarme devolve os bits.

**C07:** `Bc250VmidPoolAcquire` escolhe o VMID livre devolvido há mais tempo (nunca usado primeiro), como `amdgpu_vmid_grab` e o pool do Nagi. Até a 301 era o livre de número mais baixo, e o VMID recém-devolvido voltava no próximo contexto novo. Teste novo em `vmid_pool_contract_test.c`: devolver e pedir de novo dá outro VMID. "Menor livre" reprova por construção (o segundo pedido devolveria o mesmo número). A mutação não foi rodada.

## 6. Critério da Corrida 302 (escrito antes)

**Roteiro:** `tools\EXECUTAR_CORRIDA302_NAGI_C01_C03_C05_C06_C07_600S.cmd`, elevado; preflight `tools\prepare-corrida302-nagi-c01-c03-c05-c06-c07.ps1`. Antes: fusível (`inspect-boot-fuse.ps1 -Reset`), desligar e ligar. O roteiro é o mesmo da 301: menu Iniciar 3×, vkcube, TFM, GravityMark (opções da 300), **sem memtest**, desktop até o fim. Se travar: anotar a hora e mandar `C:\BC250_TDR_PASSOS.txt`.

| Item | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| C01 | `[C01_INIT] RESUMO: ... SH_MEM 16/16 VMIDs (mascara 0xFFFF, 0 divergentes); quantum/engines 36 escritos ... -> ARMADO` e `DESARMADO` no fim | estado de init em paridade; `DIVERGE` num grupo = o registrador não aceita a escrita nesse ponto do boot, mover para depois do PSP |
| C03 | `[RLC1] ... ATIVO ... handshake SMU DESLIGADO (RLC.1b)` | RLC rodando como o Nagi |
| C05 | só com TDR do GFX: `[C05_WAVEKILL] ... GFX ocioso` ou `ainda ocupado` | ocioso = soft recovery funcionou; ocupado = o hang não é de onda, segue o soft-reset |
| C06 VSYNC | `[C06_VSYNC] ARMADO ... 0x00104104 -> ack -> 0x001001xx -> enable -> 0x001011xx` e `vupdate=` > 0 no `[DCN2_RESUMO]` | o VSYNC por interrupção chega (D06); com 0 entregas e a mesma sequência do Linux, o defeito está do nosso lado do IH |
| C06 CP | `[CP_EOP] ARMADO ... + C06 PRIV_INSTR/PRIV_REG/OPCODE_ERROR`; `[C06_CP_ERRO]` só se houver erro | erro do CP contado = um IB nosso com pacote inválido, com o `ring_id` |
| C07 | sem linha própria; os `CTX_PROBE`/VMID dos contextos novos não repetem o VMID recém-devolvido | LRU em efeito |
| Trava | tempo até a trava contra a régua (`tools/regua-fills.py`) | n=1: só a repetição separa efeito de dispersão |
| **E** | fora do mapa: descrever antes de julgar | — |

**Riscos declarados:**
- O C01 muda o `SH_MEM_CONFIG` de todas as VMIDs. Shader com resultado errado, ou TDR novo logo depois do bind, entra como suspeita direta do C01.
- O C06 tira o VSTARTUP e a VLINE0. O VSYNC segue pela consulta de 4 ms se a VUPDATE não chegar, como na 273.
- O C03 já rodou uma vez (248, trava em ~8 s, n=1).

## 7. Tabela viva do DCN

Sem mudança de estado. D06 (VSYNC pela interrupção) muda de abordagem: agora segue a sequência do Linux (§5). Prontos: D01–D05, D08, D10, D15, D32–D33. Abertos: D06/D07, D11, D12–D14, D18–D23, D25–D29, D31.
