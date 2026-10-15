# 630 — Mineração dos TDRs do GFX: erros do SQ depois do reset e um RLC que nunca entra em safe mode

**07/10/2026, offline, zero corridas.** Continua o [doc 629](629_CORRIDA239_SO_MENU_TRES_TDR_DO_GFX_E_TRAVA_DURA_22S_DEPOIS.md) §3.
**Corpus:** todos os `display-log-this-bind-{RECUPERADO,FINAL}.txt` de 01/10 a 07/10 com a linha `TDR_GFX_QUIESCE] Snapshot` (11 TDRs em 7 corridas). Script: `tdr_mine.py` (scratchpad da sessão).

## 1. Duas famílias

| Família | Corridas | Assinatura (P0) |
|---|---|---|
| **GFX vazio** | 209 (3×), 216, 217 | `RPTR == WPTR`, zero pendente, fence 1–3, `CP_BUSY_STAT=0`. O TDR é de outra engine e o GFX só aparece na foto. Fora desta análise |
| **GFX travado** | 201, 208, 231, 239 (3×) | anel com 10–62 dw pendentes, `GUI_ACTIVE=1 CP_BUSY=1`, sem fault de VM latchado |

Na família "GFX travado":
- **todas** passaram, logo antes do ponto de parada, por um `WAIT_REG_MEM` no ACK de invalidação de TLB (`0x28B5`), isto é, por uma troca de VMID (P0, `TDR_GFX_RING_TAIL`);
- **208:** a submissão parada era a **primeira** da VMID 10 (zero submissões anteriores nela);
- **239 #1 e #3:** primeira submissão de um **contexto recém-criado** (menu Iniciar, DWM reiniciado) numa VMID reaproveitada;
- ondas: zero na 201 e na 239 #1 (estado ou pacote); 1 na 208; **240 por SA** na 231 e na 239 #3; 6+2 na 239 #2.

Que "primeiro IB depois da troca de VMID" seja a causa é **H**: falta ver o conteúdo do IB parado, que não é registrado.

## 2. Achado 1 (P0): erros do SQ só depois do primeiro reset do GFX

A 239 é a primeira corrida com TDR do GFX e o IH vivo (P3). As linhas `[P3_IH] regime` mostram:
- `outros=0` em 39 linhas, os ~9 min antes do TDR 1;
- `outros=20` (última origem `0x0AEF`) perto do TDR 2;
- `outros=992` (última origem `0x0BEF`) depois do TDR 3, antes da trava.

Decodificação com fonte local (P1): cliente `0x0A`/`0x0B` = `SOC15_IH_CLIENTID_SE0SH`/`SE1SH` (`soc15_ih_clientid.h`); fonte `0xEF` = `GFX_10_1__SRCID__SQ_INTERRUPT_ID`, "SQ Interrupt (ttrace wrap, errors)" (`irqsrcs_gfx_10_1.h:51`).

**Os shader engines só começam a reportar erros do SQ depois do nosso reset do GFX**, e vêm junto com ondas presas (240 por SA no TDR 3). O tipo do erro não foi registrado: o ISR guarda só cliente e fonte, não o `src_data`.

## 3. Achado 2 (P0): o RLC nunca confirmou o safe mode

No corpus inteiro: **0** `RLC_SAFE_MODE] ENTER confirmado` e **41** `ENTER TIMEOUT` (registro parado em `0x3`). O registrador e o protocolo são os do upstream (`mmRLC_SAFE_MODE` 0x4C05 idx 1, `CMD|MESSAGE`; a GC 10.1.3 cai no `default` de `gfx_v10_0_set_safe_mode`). Não registramos `RLC_CNTL`/`RLC_STAT` em lugar nenhum, então **não se sabe se o firmware do RLC está rodando** (o `CSIB_SNAPSHOT` só diz que o CSB veio programado de antes).

## 4. Achado 3 (P1, leitura): o nosso reset do GFX não é o do amdgpu

| Passo | amdgpu (`gfx_v10_0_soft_reset`, local) | Nós (`TDR_GFX_QUIESCE`) |
|---|---|---|
| Antes | `gfx_v10_0_rlc_stop` (para o RLC), desliga parse do CP gráfico **e do compute** | pede safe mode (nunca confirma); compute só por dequeue do HQD |
| Reset | `SOFT_RESET_CP`/`GFX` se o pipeline estiver ocupado, `SOFT_RESET_RLC` se o RLC estiver ocupado | CP + GFX + CPF + CPG, sempre; RLC nunca |
| Depois | o fluxo de recuperação refaz a inicialização dos IPs (RLC, CP, estado do GFX) | só rearma `CP_RB0` e prova com NOP + `RELEASE_MEM` |

Que o estado perdido no `SOFT_RESET_GFX` (registradores do SPI/SQ/`SH_MEM_*`, *golden*) explique os erros do SQ é **H** forte: casa com "zero erros em 9 min, 992 em 25 s depois do reset".

## 5. O que muda no quadro da trava dura

- A 239 mostra uma cadeia: **GFX trava no 1º IB de um contexto novo → nosso reset incompleto → SQ com erros e ondas presas → mais TDRs → trava dura**. A última trava teve a assinatura de sempre (GPU ociosa).
- Isso **não** explica as travas sem TDR (a maioria). Pode ser uma segunda via até o mesmo fim, ou o mesmo defeito de fundo aparecendo de outra forma (H).

## 6. Próximos passos propostos (decisão do Daniel)

1. **Instrumentação só de leitura** (barata, vai de carona em qualquer corrida):
   - `RLC_CNTL`, `RLC_STAT` e `RLC_SAFE_MODE` no boot e no TDR;
   - no ISR, os 4–8 dwords da entrada do IH para os vetores "outros" (as 8 primeiras), para saber o tipo do erro do SQ;
   - no TDR do GFX, `CP_IB1_BASE`/`CP_IB1_BUFSZ` e os primeiros dwords do IB parado (pela GPUVA do journal, sem ler tabela de página pela BAR).
2. **Reset do GFX como o amdgpu:** parar o RLC em vez de pedir safe mode, desligar o parse do compute, e reprogramar o estado do GFX depois do reset. Mais caro; depende do que a instrumentação disser sobre o RLC.
3. **Achar o defeito do 1º IB depois da troca de VMID** com o conteúdo do IB em mãos (depende do item 1).

**NÃO VALIDADO EM HARDWARE:** a causa do TDR do GFX; que o reset incompleto cause os erros do SQ; que essa cadeia cause a trava dura.
