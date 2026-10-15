# Doc 746 — Corrida 292: a GART não subiu (reserva da tabela deu `NO_SPACE`); COMP.VA.3b, pacote 3.443.1.0 e critério da Corrida 292b

> **Data:** 13/10/2026, bind às 23:46:26, rollback online (`RECOVERY_OK_ONLINE`, rodada 2, sem reboot).
> **Candidato:** lab **3.442.1.0** (SYS `D6D57307`), [doc 745](745_COMPVA2_JANELA_QUE_ESPERA_COMPVA3_GART_32MB_E_JANELA_PERSISTENTE_PACOTE_3442_E_CRITERIO_CORRIDA292.md).
> **Evidência:** `docs/evidence/2026-10-13_23-46-26-DESKTOP-HQAG6T4-lab-binding/`.
> **Observado pelo Daniel:** tela preta e, às vezes, artefatos.

## 1. O que aconteceu (P0)

- `[D2_GART] NAO TENTADO: reserva de 65536 bytes para a tabela da GART falhou (status=6)`. O status 6 é `BC250_VRAM_NO_SPACE`: o offset fixo `0x0FF10000` e a busca `TOP_DOWN` foram recusados.
- **Sem a GART da VMID 0, a cascata de sempre:**
  - `[P4_SDMA1]`, `[P3B_MMHUB]`, `[P3_IH]` e `[P7_GFX_GTT]` ficaram `NAO TENTADO`;
  - anel do SDMA0 e do GFX na VRAM;
  - IH desligado, por isso 0 de 110 latches vieram pela IRQ do flip.
- **A composição foi recusada:** `COMPOSITE_BLT VIA_VA RECUSADO … GART da VMID 0 desligada` (≥16, amostrado), 164 escapes de composição e zero OK. O resultado foi o mesmo quadro da 290: tela preta. Os artefatos são flips para primárias sem cópia (H, como na 290).
- **O que continuou bem:**
  - 215 aquecimentos da DX.43;
  - zero TDR, fault ou BugCheck;
  - OTG a 59,998 Hz;
  - rollback sem reboot;
  - a T3 não rodou (o log arquivado é o da 291).

Pelo critério do doc 745 o desfecho é **C**: a GART não subiu.

## 2. Causa: defeito meu

Escolhi `0x0FF10000` lendo o mapa DX.67 da 291 como se fosse o mapa inteiro do alocador. Mas esse mapa percorre só `r < RegionCount`, e as regiões ocupam slots esparsos, então ele mostra uma parte delas. O topo da VRAM (`0x0F400000..0x10000000`) tem TMR, firmware, MQD, anéis e tabelas. O `TOP_DOWN` esgotou o limite de 64 tentativas (`BC250_VRAM_MAX_REGIONS`) descendo de região em região e devolveu `NO_SPACE`.

## 3. Correção: COMP.VA.3b (pacote 3.443.1.0)

A tabela de 64 KB passa a ser reservada pela **primeira fatia livre** encontrada numa varredura do alto para baixo:
- só `ReserveFixed`, que recusa sobreposição;
- passos de 64 KB a partir de `0x0F3F0000`, logo abaixo da faixa reservada do topo, até o piso `0x0E000000`;
- no máximo 512 tentativas.

A D2 roda antes de o segmento do VidMm ser publicado (linha 87 contra a linha 384 do log). Por isso, se a fatia cair dentro do espaço do segmento, o corte DX.67 encolhe o segmento para baixo dela, e o VidMm perde 64 KB. O log passa a dizer quantas tentativas foram feitas e onde a tabela ficou.

- `build-contract` EXIT=0; auditoria de segurança e auditoria do pacote PASS.
- **Lab 3.443.1.0:** SYS assinado `ABAFF3ED…` (fonte `5D1B98D6…`), INF `E2D645D4…`, CAT `9765D0AC…`.
- **Roteiro:** `tools/EXECUTAR_CORRIDA292B_GART_VARREDURA_300S.cmd` (preflight `tools/prepare-corrida292b-gart-varredura.ps1`).

## 4. Critério da Corrida 292b (o do doc 745, mais)

| Desfecho | Assinatura no log | Implicação |
|---|---|---|
| **A** | `[D2_GART] TABELA RESERVADA (VARREDURA, N tentativas)` e `CONTEXTO0 … END=0x1FFF`; `COMPOSITE_BLT OK VIA_VA … pedacos=1`; `reuso` > 0; zero `VIA_VA RECUSADO`; zero `Present RECUSADO` no SearchApp; zero TDR; desktop visível | passos 1 e 2 validados |
| **B** | tabela reservada e GART ligada, mas o desktop cai (TDR, fault ou trava) | a GART de 32 MB afetou o MEC, o anel ou o IH; voltar à tabela de 4 KB |
| **C** | `NAO TENTADO: reserva…` de novo | a VRAM abaixo de `0x0F3F0000` também está ocupada; ler o número de tentativas no log |
| **D** | tela com lixo e `OK VIA_VA` | PTE errada na janela |
| **E** | algo fora do mapa | descrever antes de julgar |

**Dívida declarada:** sem a GART não existe composição pelo VA, então qualquer falha da reserva volta a dar tela preta. A rede de segurança certa seria cair na tabela antiga de 4 KB; isso ainda não foi feito.

**NÃO VALIDADO EM HARDWARE.**
