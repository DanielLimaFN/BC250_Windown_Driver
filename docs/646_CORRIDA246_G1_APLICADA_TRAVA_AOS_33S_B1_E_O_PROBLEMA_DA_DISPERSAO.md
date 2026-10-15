# 646 — Corrida 246: G.1 aplicada (29 goldens), trava aos ~33 s (desfecho B1) e o problema da dispersão

**07/10/2026.**
- **Corrida:** bind às 22:49:47, trava por volta de 22:50:20, reinício pelo Daniel.
- **Candidato:** lab 3.390.1.0 (SYS assinado `55255FB0…`) + RADV `B4AE2E14`.
- **Critério:** [doc 645](645_G1_GOLDEN_SETTINGS_DO_CYAN_SKILLFISH_COMO_O_AMDGPU_E_CRITERIO_CORRIDA246.md).
- **Evidência:** `docs/evidence/2026-10-07_22-49-47-DESKTOP-HQAG6T4-lab-binding/`, com o log do KMD tirado do cumulativo a partir do offset 566.935.500.
- **Lacuna:** a coleta automática pós-trava ainda não tinha terminado quando este documento foi escrito.

## 1. Pré-condição: cumprida (P0)

- **Goldens:** `[G1_GOLDEN] RESUMO: 34 registradores, 29 aplicados, 4 ja' iguais, 1 divergentes/restaurados -> ARMADO`.
  - Os 29 aplicados conferiram no readback, inclusive os do L2 e do canal (`GL2C/GL2A_ADDR_MATCH_MASK`, `GL2_PIPE_STEER_*`, `GL2C_CTRL2/3`, `CH_PIPE_STEER`, `CH_VC5_ENABLE`).
  - O único que divergiu foi o `RMI_SPARE`: pedido `0xFFFF3101`, lido `0xFFFF3100`. O bit 0 não fixa (P0), e o registrador foi restaurado.
- **Snapshot DX.26 mais tarde no boot:** 2 diferentes em 34, ambos esperados — o `GRBM_GFX_INDEX`, que lê 0, e o `RMI_SPARE`. Contra 30 diferentes na 243.
- **Resto da configuração:** RLC na ROM, P3.3 desligada e StartDevice `0x0`. Sem regressão do tipo D.

## 2. Medido (P0)

- **Atividade:** 2.844 IBs executados, 323 Presents e o IH 1:1 (ISR 2.832, `outros=0`).
- **Falhas:** nenhum TDR e nenhum fault.
- **Fim do log:** atividade normal, com paginação e uma submissão do SDMA0.
- **Duração:** ~27 s do StartDevice ao último registro (por QPC), ~33 s depois do bind.
- **Boot seguinte:** fusível disparado e Código 43.
- **Tipo de boot:** todas as corridas de 07/10 leram `RESET_REASON=0x40000400` logo antes do bind, ou seja, boot frio. O tipo de boot não discrimina.

## 3. Veredito pelo critério e uma correção de método

**Pelo critério, é o desfecho B1:** pré-condição cumprida e trava antes de 60 s.

**O critério tem um defeito meu.** A tabela abaixo mostra que corridas de uma só configuração não separam efeito de acaso:

| Corrida | Mudança em relação à base | Trava |
|---|---|---|
| 232 | IH ligado | ~77 s |
| 233 | conclusão por IRQ | ~584 s |
| 236 | P4.2 | ~45 s |
| 241b | encadeamento da RADV | ~330 s |
| 242 / 242b / 243 | P3.3 (+MEC.1) | ~11 / ~22 / ~49 s |
| 244 | RLC rodando | ~40 s |
| **245** | base, sem P3.3 nem RLC | **~289 s** |
| **246** | base + G.1 | **~33 s** |

- **Dispersão:** sem as mudanças "suspeitas" a trava já variou de ~45 s a ~584 s.
- **O que isso derruba:** "toda mudança nova encurta" e "a 245 voltou aos minutos" são, as duas, compatíveis com acaso. A única série com força estatística razoável é a da P3.3 (3 de 3 abaixo de 50 s).
- **Consequência:** os vereditos da RLC.1 (244) e da G.1 (246) **não são conclusivos**. Também não é conclusivo que a 245 "voltou aos minutos" por causa da ausência delas.
- **Regra escrita antes:** B1 manda desligar a G.1. Ela fica **ligada para a repetição** abaixo, como divergência declarada. Desligá-la sem repetição seria tomar uma amostra só como efeito.

## 4. Próximo (decisão do Daniel)

1. **Recomendado — repetir a 246 sem rebuild, com o mesmo pacote 3.390.1.0:**
   - se travar de novo em menos de 60 s, a G.1 encurta a vida (n=2, como a P3.3) e sai;
   - se passar dos minutos, a 246 foi dispersão e a G.1 fica, por paridade.
2. **Daqui em diante, nenhuma mudança é julgada por uma corrida só** quando o desfecho for apenas o tempo até a trava. O critério passa a exigir 2 corridas, ou uma assinatura nova no log.

**NÃO VALIDADO EM HARDWARE:**
- a causa da trava dura;
- o efeito da G.1, do RLC rodando e da P3.3 sobre o tempo até a trava, além da correlação.
