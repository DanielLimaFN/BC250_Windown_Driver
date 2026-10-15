# 612 — Corrida 230: a GART do MMHUB ligou e derrubou o PSP; P3.0b.1 e critério da Corrida 231

**Corrida:** 06/10/2026, bind às 09:45:29. Candidato lab 3.377.1.0 (SYS `8B6D60B5…`), CMOS em 256 MB, boot frio.
**Evidência:** `docs/evidence/2026-10-06_09-45-29-DESKTOP-HQAG6T4-lab-binding/`; o log é o do `C:\BC250_DISPLAY_LOG.txt` a partir do offset 282.013.831.
**Critério:** doc 611 §4.

## 1. O que aconteceu (P0)

**1. A P3.0b funcionou, e exatamente como previsto:**
- `[P3B_MMHUB] resultado: LIGADA (34/34 conferidos, ACK da VMID 0 no MMHUB) PROTECTION_FAULT_STATUS=0x00000000`;
- `semTimeouts=0`;
- antes e depois:

  | Registrador | Antes | Depois |
  |---|---|---|
  | `CONTEXT0_CNTL` | `0x007FFE80` | `0x007FFE01` |
  | `MX_L1_TLB_CNTL` | `0x501` | `0x1D59` |
  | `L2_CNTL` | `0x00080602` | `0x00080E01` |
  | `L2_CNTL3` | `0x80100007` | `0x80130009` |
  | `L2_CNTL4` | `0xC1` | `0x01` |
  | `AGP BOT` | `0` | `0xFFFFFF` |
  | `SYS_APERTURE` | `0x3D0000..0x3D0400` | `0x3D0000..0x3D03FF` |

  Os valores de `L2_CNTL3`, `L2_CNTL4` e `MX_L1_TLB_CNTL` ficaram iguais aos alvos já provados no GFXHUB.

**2. Logo depois, o StartDevice terminou com `0xC00000BB` e a placa ficou em Código 43.** A cadeia do laboratório refaz o probe do anel do PSP depois do arme da P3, e esse probe falhou:
- `PspRingStatus=6`;
- `PspRingCreateAck=0x8002000F`;
- `PspRingCreatePolls=500`;
- `PspRingCreatePassed=0`.

Com isso, o PFP direct load, o HQD ativo e a submissão persistente não rodaram (`PfpDirectStatus=1`, `HqdActiveStatus=1`). Rodapé: `Submissao persistente pedida e NAO ENSAIADA`.

**Na 229, sem a P3.0b, o mesmo probe passou:** `PspRingStatus=0`, ACK `0x80020000` em 1 poll.

**3. Teardown do caminho de falha:**

| Bloco | Linha no log | Escritas de restauração que falharam |
|---|---|---|
| MMHUB | `[P3B_MMHUB] DESLIGADA ... status=6 falhas=69` | 69 |
| D2 | `[D2_GART] DESLIGADA ... falhas=15` | 15 |
| P2 | releitura com zeros | — |

O transporte MMIO estava vazio (`MmioState=0`), porque tinha sido devolvido ao segundo preflight. A restauração não chegou ao silício, então **o MMHUB e a GFXHUB ficaram programadas até o boot frio**.

## 2. Leitura

**P0:** o PSP parou de responder ao comando de criação do anel quando, e só quando, a GART do MMHUB estava ligada.

**Mecanismo:**
- **P1:** o PSP (MP0) é cliente do MMHUB.
- **P0, do código:** os nossos buffers do PSP (anel, comando, fence, payload) estão em RAM do Windows **por endereço físico** (`0x7E614000`, `0x5FFF5000`…), fora da abertura de sistema (`0xF400000000..`).
- **H:** com `ADVANCED_DRIVER_MODEL=1`, `SYSTEM_ACCESS_MODE=3` e a VMID 0 do MMHUB ligada, um endereço fora da abertura passa a ser traduzido pela GART de 2 MB e cai fora dela.
- **H, porque o código do PSP do amdgpu não está em `inc/linux_ref`:** o amdgpu põe os buffers do PSP em VRAM ou GTT, com endereço MC, e liga a GART antes do PSP. Para ele a ordem não importa.

**Não houve trava, tela azul nem efeito no display:** o BasicDisplay e o GOP seguiram. Vale como medição de que o DCN tolera a GART do MMHUB ligada, mas só durante o StartDevice. **NÃO VALIDADO EM HARDWARE** em regime.

**Veredito frente ao critério:**
- **Não é A:** sem desktop.
- **Não é B:** a P3.0b não falhou.
- **Não é C:** o display não foi afetado.
- **D aconteceu em parte:** teardown sujo, mas por causa do transporte, não da ordem de restauração.
- **O resultado principal é E — fora do mapa:** a P3.0b quebra outro cliente do MMHUB, o PSP, que usa endereço físico.

## 3. P3.0b.1 (candidato 3.378.1.0)

**1. O arme da P3 mudou de lugar.** O chamado de `Bc250ActiveP3ArmIh` saiu de depois da P2 e foi para o caminho de sucesso do StartDevice:
- depois do último uso do PSP (`Bc250ActiveProbePspRing` da cadeia do laboratório);
- depois do remapeamento do transporte;
- antes de `IsActive=1`.

Depois desse ponto o driver não fala mais com o PSP. Diferença de ordem em relação ao amdgpu: **declarada**. Se algum dia o PSP for usado em regime, os buffers dele terão de ir para a GART ou para a VRAM.

**2. Teardown com transporte vazio:** `Bc250ActiveD2GartDisable` mapeia o transporte, em PASSIVE, só para restaurar o IH, o MMHUB e o GFXHUB, e o devolve no fim. Linha nova no log: `[D2_GART] teardown com transporte vazio: mapeado para restaurar = sim`.

**3. Portão,** duas regras novas:
- o arme da P3 é chamado uma vez só, depois de todo `= Bc250ActiveProbePspRing(` e `= Bc250ActiveLoadSdmaFirmware(`;
- o desligamento da D2 remapeia o transporte vazio.

**Provas offline:**
- `build-contract` EXIT=0 e auditorias PASS;
- **10 mutações reprovadas**: as 8 da P3.0b e mais as 2 novas.
- **SYS:** contém a string nova.
- **Pacote 3.378.1.0:** perfil `DISPLAY_RENDER_LAB`, auditoria PASS.

| Arquivo | SHA-256 |
|---|---|
| SYS | `B31B621A2FD17C0F01DCB624E6733CC1603038D20626687FCB5E33FE053AE9CA` |
| INF | `49F0D9912A83612E6CFC9E61FD3FCA2B5104F2E6C72D18D9CD72715BCF8DA902` |
| CAT | `690306A83DD2EE6B8A832DC37CFC5DB15ECB5EFAB61AEE30ABD416AA503786B2` |
| UMD | `8782A5E5…` (inalterada) |

**Scripts:** `tools/prepare-corrida231-p30b1-mmhub-depois-do-psp.ps1` e `tools/EXECUTAR_CORRIDA231_P30B1_MMHUB_DEPOIS_DO_PSP_600S.cmd`.

## 4. Critério da Corrida 231

**Pré-condição:** boot frio, porque a 230 deixou o MMHUB e a GFXHUB programadas. O menu e a Pesquisa são abertos como na 201–228.

**Desfechos:**

- **A — GART do MMHUB em regime e display intacto**
  - *Assinatura:* todos os sinais abaixo.
    - `PspRingStatus=0` (o probe do PSP passa antes da P3);
    - `[P3B_MMHUB] resultado: LIGADA ... PROTECTION_FAULT_STATUS=0x00000000` depois de `Runtime MmioTransport map status`;
    - StartDevice `status=0` e o desktop sobe;
    - no fim, `[P3B_MMHUB] DESLIGADA ... falhas=0`.
  - *Implicação:* próxima corrida, a P3.1 (IH com `TRAP`), só a chave do IH.
- **B — o PSP ainda falha**
  - *Assinatura:* `PspRingStatus` diferente de 0 com a P3 já depois do PSP.
  - *Implicação:* a causa não é a GART do MMHUB; comparar com a 229.
- **C — display afetado em regime**
  - *Assinatura:* `LIGADA`, e depois tela preta, corrompida ou congelada, ou `PROTECTION_FAULT_STATUS` diferente de zero.
  - *Implicação:* bissectar a abertura de sistema e o modo de acesso, como no doc 611.
- **D — teardown sujo**
  - *Assinatura:* `falhas` diferente de zero no MMHUB ou na D2.
  - *Implicação:* rever a restauração.
- **E — fora do mapa**
  - *Assinatura:* descrever o observado antes de julgar.
  - *Implicação:* a definir.

**A trava dura continua possível.** Se vier, conta como amostra com uso normal.

## 5. Pendências

**NÃO VALIDADO EM HARDWARE:**
- a GART do MMHUB em regime;
- a correção do teardown;
- a P3.1.

**Divida declarada:** o PSP não pode ser usado depois da P3, porque os buffers dele estão em RAM física.
