# 660 — Corrida 250: repetição da 249, 1.754 FILLs, trava aos ~3,5 min, e os dados do DCN

**Data:** 08/10/2026, bind às 15:39:38. **Pacote:** lab 3.395.1.0 (SYS `C987E574…`, UMD `B73F9BDA…`, RADV `B4AE2E14`), com KD.
**Evidência:** `docs/evidence/2026-10-08_15-39-38-DESKTOP-HQAG6T4-lab-binding/`. O log foi cortado no 2º `AddDevice ENTER #1` (linha 261.220), e o `monitor-corrida.csv` foi copiado de `C:\BC250_MONITOR.csv`.
**Roteiro:** o Daniel abriu o menu Iniciar várias vezes até travar.

## 1. Pré-condição (P0)

| Item | Resultado |
|---|---|
| TMR.1 | `TMR persistente: VRAM [0xF400000,+0x400000) PA_DRAM=0x46F400000`; PSP `aceitos=6/6`, `SETUP_TMR resp=0` |
| RLC rodando | `EXEC_ROM_CODE=0`, `RLC_GPM_STAT=0x00B40016`, escritas 5/5, handshake SMU ligado (RLC.1b fora) |
| G.1 | 31 linhas `[G1_GOLDEN]` |
| Sonda DCN | **com saída pela primeira vez**: `[DCN_PROBE]`, `[DCN_BATTERY_A]`, `[DCN_BATTERY_B]` e `[DCN_BATTERY_C]` presentes (§4) |

A pré-condição foi cumprida.

## 2. Tempo e eventos (P0)

| Medida | 250 | 249 | Base com RLC rodando (244/247/248/248b) |
|---|---|---|---|
| Tempo até a trava (QPC) | 249,0 s | 461,9 s | 40 / 108 / 8 / 22 s |
| Pausas do KD | ~33–38 s (saltos de 9,3 e 13,7 s no QPC; buracos de 8 e 30 s no monitor às 15:42:08–15:42:50) | ~45 s | — |
| Tempo real estimado | **~211–216 s** | ~417 s | — |
| **FILLs até a trava** (BPG1 pelo monitor) | **1.754** | 4.429 | ≤ ~722 |
| FILLs/s | ~8,2 | ~10,6 | — |
| IBs executados | 13.210 | 37.029 | — |
| Presents | 3.377 (~16/s) | 10.807 (~23/s) | 246b: 7,6/s |
| `COMPOSITE_BLT OK` | 2.799 | 9.598 | — |
| TDR / fault / timeout / BugCheck | 0 / 0 / 0 / 0 | 0 | — |

Sobre as contagens de "TDR" e "FAULT":
- As 10 linhas com "TDR" são a reconstrução do anel pela D2 e o `TDR_RESTART PROVADO` do boot. As 9 com "FAULT" são fotos de `FAULT_STATUS=0`. Nenhuma é um evento.
- As 128 linhas `[ActiveOpenAlloc] RECUSADO` são a recusa de sempre.

**Fim da corrida (P0):**
- A última linha do KMD é uma submissão SDMA normal (`IB EXECUTADO`, RPTR==WPTR, `FromDpc=1`), e há um `X2B_FILL` ~200 linhas antes. É a assinatura de sempre.
- O monitor nos 20 s finais mostra uma rajada de 46 IB/s e 38–43 Presents/s (menu aberto), VRAM dedicada 228 MB de ~233 MB, e FILL 1.751 → 1.754.
- `RESET_REASON=0x40080400`, com os bits 30/10/19 de sempre (sem sync flood).
- Teste de RAM: 7.868 MB × 2 passadas, zero erros.
- Rollback: `RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em 5 amostras.

## 3. Veredito

O critério do doc 659 §6 não cai em nenhum dos dois lados:
- **> ~2.900 FILLs** (ganho real) **não aconteceu**: 1.754.
- **≤ 722** (a 249 foi cauda) **também não**.

Pela regra 3 da Lei 10, nenhum dos dois desfechos ocorreu. Pelo doc 658 §6 é **desfecho B** (trava antes dos 600 s, pré-condição OK). Ficou acima de 1.500 FILLs, então tecnicamente B+, mas sem a confirmação que a repetição exigia.

O que se pode dizer:
- **O ganho grande da 249 não se repetiu.** 4.429 → 1.754 FILLs, ~417 → ~213 s.
- Com RLC rodando e o TMR.1, as duas corridas passaram de 722 FILLs (4.429 e 1.754). Sem o TMR.1, as quatro ficaram em ≤722. É sinal a favor do TMR.1 na base com RLC rodando: **P2, n=2**, não prova.
- 1.754 está dentro da faixa das corridas com RLC na ROM (média 1.192, máximo 2.890). Ou seja, o TMR.1 talvez só tenha devolvido a base com RLC rodando ao nível da base com RLC na ROM, e a trava de fundo segue igual (H).
- **A trava continua, com a mesma assinatura.** O TMR.1 não é a causa da trava dura. Ele fica, porque era defeito real.

**Avanço?** Inconclusivo. Funcionalmente nada mudou (trava igual, sem TDR). No diagnóstico, a "cura parcial" da 249 caiu para "talvez ajude", e os dados do DCN saíram pela primeira vez.

## 4. Dados do DCN (P0, só leitura, uma vez no StartDevice)

**Bateria A** (OTG0/HUBP0, herança do GOP):

| Campo | Valor |
|---|---|
| Status | VALID, `OtgEnabled=1` |
| `FrameCount` | 13.500 (≈ 240 s a 56,13 Hz) |
| Viewport | 2560×1080; `Pitch=2559` (campo em pixels − 1, ou seja 2560 px = 10.240 bytes; P1) |
| Timings | `HTotal=2784`, `VTotal=1111`; HBlank [2720..160], VBlank [1108..28] |
| Posição de varredura | linha 472, pixel 1072 |
| Superfície | `Base=0x460000000`, `ActiveInUse=0x0` |
| Derivado | pixel clock 173.611.437 Hz, **56,13 Hz** |

**Bateria B** (AUX0 do DCN, BAR5 `0x15040..0x15050`, só leitura):

| Registrador | Valor |
|---|---|
| `AUX_CONTROL` | `0x01040000` (`AUX_EN=0`) |
| `AUX_SW_CONTROL` | `0x00000000` |
| `AUX_ARB_CONTROL` | `0x00000000` |
| `AUX_INTERRUPT_CONTROL` | `0x00000000` |
| `AUX_SW_STATUS` | `0x00000000` |

O bloco responde (não volta `0xFFFFFFFF`). O motor AUX está desligado: o GOP não o deixa ligado.

**Bateria C:** não tentada (`BC250_DCN_PROBE_AUX_TRANSFERS=0`). Ler o EDID real exige escrita no AUX: P9, pela governança.

**Leitura (P1/H):**
- A Bateria A confere com o que o driver já assumia. 2560×1080 a 56,13 Hz é o mesmo número do EDID sintetizado (px 173.611.437 / 2784 / 1111).
- A base `0x460000000` é a PA de DRAM do offset 0 da UMA (o TMR em +0xF400000 deu `0x46F400000`). Logo o OTG varre o framebuffer do GOP no começo da VRAM, o mesmo que o SDMA atualiza no Present.
- O DCN estava vivo e coerente no bind. **Não temos leitura do DCN perto da trava**: a sonda roda uma vez só.

## 5. Lacunas

- Sem amostra do DCN em regime ou perto da trava. Saber se o OTG ainda varria quando a máquina morreu exigiria ler OTG/HUBP periodicamente. Seriam leituras novas em regime, contra a P1 (silêncio da BAR): é variável nova.
- Tctl: sem amostra por desenho (P1).
- As pausas do KD foram estimadas por duas fontes (QPC e monitor), com ±5 s.

## 6. Próximo (proposta, para decisão do Daniel)

O pacote de correções das auditorias não fecha a trava. Restam, do doc 658 §2.4 e do 657:
1. **L1 no Linux com `amdgpu.dc=0`** (fora do driver, não conta como corrida). Separa "o dono do DCN" de "o resto". É o teste mais barato e o que mais divide o espaço.
2. **Estado de MGCG por bloco** (657 H1), só leitura, com offsets do catálogo, uma foto no boot. Sem custo de corrida se for junto com a próxima.
3. **SMU a 1 Hz pelo KMD** (657 H2): só depois do 1, porque põe um ator novo falando com o SMU.

**NÃO VALIDADO EM HARDWARE:** a causa da trava dura; se o TMR.1 muda o risco por evento (n=2, divergente); GFXRST.1 (sem TDR nas duas corridas).
