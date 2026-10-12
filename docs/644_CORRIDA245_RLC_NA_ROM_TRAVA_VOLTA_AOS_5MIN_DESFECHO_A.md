# 644 — Corrida 245: RLC de volta na ROM, a trava volta aos ~5 min (desfecho A)

**07/10/2026.**
- **Horários:** bind às 22:25:05; trava por volta de 22:30; reinício pelo Daniel.
- **Candidato:** lab 3.389.1.0 (SYS assinado `6623270E…`) + RADV `B4AE2E14`.
- **Critério:** [doc 643](643_CORRIDA244_RLC_RODANDO_PELA_PRIMEIRA_VEZ_TRAVA_AOS_40S_B1_E_CRITERIO_CORRIDA245.md) §4.
- **Evidência:**
  - pasta `docs/evidence/2026-10-07_22-25-05-DESKTOP-HQAG6T4-lab-binding/`;
  - log do KMD lido do cumulativo a partir do offset 541.376.632 (25,5 MB).
- **Lacuna:** quando este doc foi escrito, a coleta automática pós-trava (RESET_REASON, RAM, RECUPERADO) ainda não estava na pasta.

## 1. Pré-condição: cumprida (P0)

- **RLC parado:** nenhuma linha `[RLC1] ATIVO`, e `[RLC_ESTADO] boot` mostra `RLC_CNTL=0x00000000 RLC_ENABLE_F32=0`.
- **P3.3 desligada:** `rptr_por_doorbell=0`.

## 2. Medido (P0)

- **Duração:** ~289 s do StartDevice ao último registro, por QPC; ~5 min depois do bind.
- **Atividade:** 12.254 IBs no SDMA0 e 1.519 Presents.
- **IH:** 1:1 até o fim. Última linha: ISR 13.273, TRAPs 11.885, EOP 1.411:1.411, `outros=0` e zero overflow.
- **Falhas:** zero TDR (a GFXRST.1 continua não exercitada) e zero fault.
- **Fim do log:** atividade normal. A última linha é um `SUBMIT SUCESSO` do GFX (fence 1468, 2.176 dw, VMID 1); antes dele, IBs do SDMA0 executados.
  - É a assinatura de sempre: a máquina morre entre duas observações.
- **Boot seguinte:** fusível disparado e Código 43.

| Corrida | P3.3 | MEC.1 | RLC rodando | Trava depois do bind |
|---|---|---|---|---|
| 241b | não | não | não | ~330 s |
| 242 / 242b | sim | não | não | ~11 / ~22 s |
| 243 | sim | sim | não | ~49 s |
| 244 | não | sim | **sim** | ~40 s |
| **245** | não | sim | **não** | **~289 s** |

## 3. Veredito pelo critério

**Desfecho A:** a corrida passou de 60 s.

- **Correlação, n=1 por configuração:** tanto a P3.3 quanto o RLC rodando encurtam a vida da máquina de minutos para menos de 1 min. Sem nenhuma das duas, a trava volta aos minutos (~5 min).
- **Mesmo fim nos três casos:** todas as travas têm a mesma assinatura, com a GPU sem erro e a máquina morrendo entre duas observações. Uma leitura possível (**H**) é que as duas mudanças aceleram o mesmo defeito de fundo, em vez de criar defeitos novos.
- **O que elas têm em comum:** as duas aumentam a atividade ou o tráfego de controle fora do CP. A P3.3 escreve doorbells do IH pelo BIF; o RLC rodando faz o firmware do RLC conversar com SMU, GFX e power.
- **Diferença de estado com o Linux** (amdgpu_pm_info da coleta de 01/10):
  - no Linux o SMU está ativo (`SMC Feature Mask 0xffffffffffffffff`, SCLK 7 MHz em repouso);
  - nós não falamos com o SMU;
  - com `RLC_PG_CNTL` bit 23 em 0 (handshake ligado, como o upstream com GFXOFF na máscara), o RLC espera respostas do SMU que ninguém do nosso lado coordena. Isso é **H**.
- **A RLC.1 fica desligada,** como divergência declarada com o amdgpu, até haver mecanismo.

## 4. Próximos passos propostos (decisão do Daniel)

1. **G.1 — golden settings do Cyan Skillfish** (30 de 34 nunca aplicados; tabela conferida com o upstream; doc 642 §1).
   - Uma variável, com o RLC na ROM e a P3.3 em 0.
   - É o `hw_init` do amdgpu e a maior diferença restante de estado do GC.
2. **RLC.1b:** RLC rodando com o handshake RLC↔SMU desligado (`RLC_PG_CNTL` bit 23 = 1, o ramo do upstream sem GFXOFF). Testa a H acima.
3. Repetir a 245 só se o 1 ou o 2 derem resultados ambíguos.

**NÃO VALIDADO EM HARDWARE:**
- a causa da trava dura;
- o mecanismo pelo qual a P3.3 e o RLC a aceleram;
- a GFXRST.1.
