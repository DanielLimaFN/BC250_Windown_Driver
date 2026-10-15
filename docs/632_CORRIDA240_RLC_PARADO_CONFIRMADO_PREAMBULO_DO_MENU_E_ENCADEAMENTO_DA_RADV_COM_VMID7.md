# 632 — Corrida 240: RLC parado confirmado, o IB que trava é o preâmbulo do menu, e o encadeamento GFX da RADV com VMID 7 fixa

**Corrida:** 07/10/2026, bind às 09:16:52. Lab **3.385.1.0** (SYS `40916F50…`) + RADV `7B82C4AE…`, CMOS 256 MB, só o menu Iniciar. Critério: [doc 631](631_INSTRUMENTACAO_RLC_IH_IB_DO_MENU_E_CRITERIO_CORRIDA240.md).
**Evidência:** `docs/evidence/2026-10-07_09-16-52-DESKTOP-HQAG6T4-lab-binding/` (coleta automática: `display-log-this-bind-RECUPERADO.txt`, `gfx-ib-dump-pos-trava\` com 788 arquivos, `ramtest-pos-trava.txt` sem erros).

## 1. Linha do tempo (P0)

| Hora | Evento |
|---|---|
| 09:17:02 | StartDevice `0x0`; `[RLC_ESTADO] boot`: **`RLC_CNTL=0x00000000`, `RLC_ENABLE_F32=0`** |
| 09:17–09:23 | menu aberto em ciclos; 11.931 IBs, 781 Presents, zero fault/timeout |
| **09:23:53** | o menu submete **Fence 751: `DmaVA=0x2407D4000`, 184 dw, VMID 5**, numa troca de VMID |
| ~09:23:55 | **TDR do GFX**: anel com 10 dw pendentes logo depois do `WAIT_REG_MEM` no ACK de invalidação (`0x28B5`, Ref `0x20` = VMID 5) e de dois `INDIRECT_BUFFER` para `0x2407D4000` (3 dw = aquecimento DX43; 184 dw); `IB1 Base=0x240A10000 RestamDW=0`; PFP fazendo parse; zero ondas; `[RLC_ESTADO] TDR`: RLC parado. Recuperação provada (fence reconcile PASS, `TDR_GFX_RESTART_PROOF PASS`) |
| **09:24:02** | **trava dura** (último registro), 9 s depois da submissão |

Zero `[P3_IH] OUTRO` (na 239 os erros do SQ vieram só depois do 2º/3º reset; aqui a trava veio antes).

## 2. Achados

1. **RLC parado em regime (P0, desfecho B do doc 631).** A leitura do doc 631 §1 está confirmada no silício.
2. **O IB que trava é o preâmbulo da RADV (P2).** VA e tamanho idênticos aos da 239 #1 e #3 (`0x2407D4000`, 184 dw). O menu despejou esse IB 10 vezes; as 5 cópias inteiras (09:17–09:23) têm o **mesmo MD5 `374DB870`**; as 5 últimas, inclusive a do instante da trava, ficaram **zeradas** (dados no cache de escrita perdidos na trava). Conteúdo decodificado (`tools/decode-gfx-ib.py`, 50 pacotes, íntegro): `CONTEXT_CONTROL`, `CLEAR_STATE`, `SET_SH_REG`/`SET_CONTEXT_REG`/`SET_UCONFIG_REG`, `ACQUIRE_MEM`, `EVENT_WRITE 0x19`, `NOP` e, no fim, `INDIRECT_BUFFER` **encadeado** para `0x240000000` com controle **`0x07504008`**: tamanho 16.392 dw, `CHAIN=1`, bit 22 = 1, **VALID (bit 23) = 0, VMID = 7**.
3. **Esse controle é uma mudança nossa, sem commit, na RADV (P0 por `git diff`).** O commit base usava `S_3F3_CHAIN(1) | S_3F3_VALID(1)` (e `PRE_ENA` no encadeamento entre CSs); as alterações locais trocaram, no GFX, por `CHAIN | (1<<22) | (7<<24)` em quatro pontos de `radv_winsys_cs.h`, com o comentário "Bit 22 = INHERIT_VMID, Bit 23 = 0 (RESERVED), VMID 7". Nenhum doc registra o motivo; a VMID 7 fixa é da época em que todo contexto rodava na VMID 7 (antes do pool de VMIDs 1–15).
4. **O amdgpu (P1, `gfx_v10_0_ring_emit_ib_gfx`, local)** monta o `INDIRECT_BUFFER` do GFX com `tamanho | (vmid << 24)`; o bit 22 (`0x400000`, "inherit vmid from mqd") **só em fila MES**; `INDIRECT_BUFFER_VALID` (bit 23, `nvd.h`) é do caminho de compute. A Mesa upstream encadeia com `CHAIN | VALID`.
5. As leituras de `RLC_STAT` e IB2 voltaram `lido=0` (o transporte não as lê); defeito da instrumentação, sem efeito no veredito.

**Hipótese (H):** depois de uma troca de VMID, o CP busca o IB encadeado do preâmbulo sob a VMID errada (7, fixa) ou com a semântica errada do bit 22, lê conteúdo inválido e o PFP fica preso (na 239 #1 o PFP mostrava `0x00000000`, tipo 0). Explica por que o TDR vem sempre no **primeiro IB de um contexto novo depois da troca de VMID** e nunca em VMID 7.

## 3. Veredito frente ao critério

**B** (RLC parado) **e A parcial → não é A** (Lei 10): houve TDR com `[RLC_ESTADO] TDR`, e o IB parado foi identificado, mas por cópia anterior idêntica, não pela cópia do instante, e o `[P3_IH] OUTRO` não ocorreu. **E (fora do mapa):** o encadeamento GFX da RADV com VMID 7 fixa e sem VALID.

## 4. Próximo passo proposto

**Corrida 241:** mesmo KMD 3.385.1.0, RADV com o encadeamento GFX **igual ao upstream** (`CHAIN | VALID`, sem bit 22 nem VMID fixa), uma variável. Mesmo roteiro (só o menu). Na 239 e na 240 o TDR veio em ~7–9 min de menu. Critério no doc da 241.

Separado, para depois: o KMD liga VALID (bit 23) nos IBs do GFX no anel, diferente do amdgpu (docs 365/367), e o RLC parado (paridade nova).

**NÃO VALIDADO EM HARDWARE:** a causa do TDR; que o encadeamento cause a trava dura.
