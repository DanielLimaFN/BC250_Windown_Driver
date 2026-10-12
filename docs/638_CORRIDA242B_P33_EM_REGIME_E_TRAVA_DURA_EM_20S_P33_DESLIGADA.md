# 638 — Corrida 242b (repetição): P3.3 em regime, trava dura de novo em ~20 s; P3.3 desligada

**07/10/2026, bind 15:22:47, trava ~15:23:10, boot 15:24:45.** Mesmo pacote da 242: lab 3.386.1.0 (SYS assinado `382C007B…`) + RADV `B4AE2E14…`. Evidência: `docs/evidence/2026-10-07_15-22-47-DESKTOP-HQAG6T4-lab-binding/`. Critério: [doc 637](637_CORRIDA242_P33_ATIVA_E_TRAVA_DURA_EM_10S_ANTES_DO_DESKTOP.md) §3.

## 1. Medido (P0)

**P3.3 em regime.** A segunda linha periódica do IH:

```
ISR=1693 reclamadas=1693 vetores=1693 traps_sdma=1652 eop_cp=41 overflows=0
traps_emitidos=1652 dpc_por_irq=1621 eop_gfx=41 eop_emitidos=41 dpc_gfx_por_irq=41
RPTR=0xD3A0 WPTR_wb=0xD3A0 rptr_por_doorbell=1 escritas_doorbell=1693
```

O RPTR pela doorbell funciona e o IH segue entregando 1 para 1. Isso responde a pergunta que a 242 deixou aberta.

**Desktop desenhado.**
- 173 Presents, 48 composições 2560×1080 com dados amostrados 5/5.
- 1.853 IBs, `CommitVidPn`.
- Zero TDR, fault ou timeout.

**Trava dura cedo, de novo.**
- O DWM e o Explorer param às 15:23:09.4, cerca de 14 s depois de a UMD carregar (15:22:55) e ~22 s depois do bind.
- O log do KMD cobre ~13,9 s; o rastro, 119 registros (tique 103).
- No fim do log: composição OK, Present #43 por flip e rajada de `operation=12`. O fim do log é a última drenagem (doc 582).

**Plataforma.**
- `RESET_REASON=0x40080400`, como sempre.
- Eventos 41 e 6008.
- RAM 8.297 MB × 2 passadas sem erro.
- Placa em Código 43 pelo fusível.

## 2. Veredito pela regra do doc 637

A regra, escrita antes desta corrida, era: se travar cedo de novo, a P3.3 é revertida.

**Travou cedo de novo:**
- 242: ~10–15 s;
- 242b: ~20 s;
- 241b, com o mesmo resto e sem a P3.3: ~330 s;
- todas as travas com 256 MB desde a 199: 45–584 s.

Duas travas seguidas em menos de 25 s seriam muito improváveis pela taxa histórica.

**Conclusão (P2, correlação forte com n=2):** a P3.3 encurta muito o tempo até a trava, mesmo funcionando corretamente como mecanismo.

O mecanismo não é conhecido (H):
- a doorbell do IH na faixa do BIF pode interagir com a do SDMA0 ou a do MEC;
- ou o ritmo de rearme do IH com `RPTR_REARM`/MSI pela doorbell pode mudar o tráfego de interrupção.

Isso pode ser uma pista para a trava dura em geral: uma mudança no caminho de doorbell e interrupção acelera a trava. Não está provado.

## 3. Ação

- `BC250_P33_IH_RPTR_BY_DOORBELL` voltou a **0** em `bc250_active_miniport.c`, com o motivo no comentário. A auditoria de segurança passa; o código da P3.3 fica no fonte, desligado.
- Divergência declarada com o amdgpu: o RPTR do IH continua por MMIO.
- Nada foi empacotado. O próximo candidato sai com a chave em 0 junto com a próxima mudança.

## 4. Próximo

1. **Recuperação:** `BC250_RECOVERY.cmd` elevado, `shutdown /s /t 0` e ligar pelo botão.
2. **Pista nova (H):** a trava ficou mais rápida com mais tráfego de doorbell/IH. Vale minerar a 242 e a 242b contra a 241b, comparando a taxa de interrupções e de doorbells por segundo antes da trava. É offline.
3. **RLC rodando** como o amdgpu continua sendo o próximo item de paridade.

**NÃO VALIDADO EM HARDWARE:** o mecanismo pelo qual a P3.3 acelera a trava, e a causa da trava dura.
