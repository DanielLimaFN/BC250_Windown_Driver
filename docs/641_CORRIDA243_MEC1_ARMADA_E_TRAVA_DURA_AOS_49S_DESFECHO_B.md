# 641 — Corrida 243: MEC.1 armada, P3.3 ativa, trava dura aos ~49 s (desfecho B)

**07/10/2026, bind 17:08:23, trava ~17:09:12, boot 17:09:43.** Lab 3.387.1.0 (SYS assinado `E8125C34…`) + RADV `B4AE2E14`. Evidência: `docs/evidence/2026-10-07_17-08-23-DESKTOP-HQAG6T4-lab-binding/`. O log do KMD foi lido do cumulativo pelo offset 532.170.706; a coleta automática gravou o resto. Critério: [doc 640](640_MEC1_FAIXA_DE_DOORBELL_DO_MEC_COMO_O_AMDGPU_E_CRITERIO_CORRIDA243.md).

## 1. Pré-condição: cumprida (P0)

```
[MEC1] FOTO: CP_MEC_DOORBELL_RANGE=0x00000110..0x0FFFFFFC CP_RB_DOORBELL_RANGE=0x00000000..0x00000108 CP_RB_DOORBELL_CONTROL=0x00000000
[MEC1] ARMADO: CP_MEC_DOORBELL_RANGE_UPPER 0x0FFFFFFC -> 0x00000450 (lido)
[P3_3_IH_DOORBELL] ATIVO: ... RPTR publicado pela doorbell=0x20 == IH_RB_RPTR lido=0x20
```

**Leitura nova (P0):** a faixa do anel do GFX (`CP_RB_DOORBELL_RANGE`) é `0x000..0x108`, e a doorbell do GFX está desligada. Ela não cobre `0x800` nem `0xBC0`. Agora nenhuma faixa do CP cobre as doorbells do SDMA0 e do IH.

## 2. Medido (P0)

- **IH em regime com a doorbell e a faixa do MEC corrigida:** última linha `ISR=1924 = escritas_doorbell 1924`, TRAPs 1851:1851, EOP 80:80, zero overflow, RPTR = WPTR.
- **Desktop:** 284 Presents e 2.231 IBs, com zero TDR, fault ou timeout.
- **Trava dura:**
  - os logs do DWM e do Explorer param às 17:09:12.2;
  - log do KMD: ~40 s por QPC;
  - ~49 s depois do bind, ~40 s depois de a UMD carregar (17:08:32).
- **Plataforma:** `RESET_REASON=0x40080400` (o de sempre); RAM 8.012 MB × 2 passadas sem erro; placa em Código 43 pelo fusível.
- **Comparação:**

| Corrida | P3.3 | MEC.1 | Trava depois do bind | Depois de a UMD carregar |
|---|---|---|---|---|
| 242 | sim | não | ~11 s | ~2 s |
| 242b | sim | não | ~22 s | ~14 s |
| **243** | sim | **sim** | **~49 s** | **~40 s** |
| 241b | não | não | ~330 s | — |

## 3. Veredito pelo critério

**Desfecho B:** pré-condição OK e trava em menos de 60 s depois do bind.

- A trava veio mais tarde que na 242 e na 242b. Com n=1, dentro da dispersão que se espera de tempos curtos, isso **não** é diferença demonstrada.
- **A faixa do MEC sai das causas da trava precoce da P3.3.** O mecanismo pelo qual a doorbell do IH acelera a trava continua desconhecido (H).

**Ações (escritas antes, doc 640 §3):**
- `BC250_P33_IH_RPTR_BY_DOORBELL` volta a **0**, como divergência declarada.
- `BC250_MEC1_DOORBELL_RANGE_LIKE_AMDGPU` **fica em 1**, porque é paridade e não regrediu nada medido.
- Auditoria PASS. Nada foi empacotado; o próximo candidato sai com essas chaves.

## 4. Próximo

- **Recuperação:** `BC250_RECOVERY.cmd` elevado, `shutdown /s /t 0` e ligar pelo botão.
- **Próximo item de paridade com mecanismo concreto:** o **RLC rodando** como no amdgpu. Hoje o `pfp_direct_load` deixa `RLC_CNTL=0` (P0 na 240, 241b e 243).
- O fato de o RPTR do IH pela doorbell, sozinho, encurtar a vida da máquina fica como pista registrada para a trava dura. Não há como explorá-lo sem instrumento novo.

**NÃO VALIDADO EM HARDWARE:** causa da trava dura; mecanismo da aceleração pela P3.3.
