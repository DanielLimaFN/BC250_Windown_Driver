# 471 — Corrida 134 (DX.87): 600 s completos sem trava, rollback online no mesmo boot

> **Corrida:** 26/09/2026, bind 19:50:44, rollback online 20:00:55, mesmo boot (19:49). **Candidato:** DX.87 / lab 3.292.1.0 (SYS `F5C9B85E…`), boot `NODEBUG`.
> **Evidência:** `docs/evidence/2026-09-26_19-50-44-DESKTOP-HQAG6T4-lab-binding` (log do KMD `-FINAL` completo, `complete_at_capture=true`, 88.912.268 bytes; logs da UMD `-FINAL`), rastro DX.56+BCR1 a partir do offset 42.027.072.
> **Relato visual do Daniel:** pendente ("pronto rodei").

## Relato obrigatório
**1. Houve avanço?** Sim, funcional, contra a 133: primeira janela de 600 s completa desde a 127, agora sem o defeito visual da 127 no que foi medido, e com rollback no mesmo boot.

**2. Sucessos (P0):**
- Sem trava em 600 s. `RECOVERY_OK_ONLINE` às 20:00:55 com BasicDisplay Código 0 em 5 amostras, sem reboot.
- DWM vivo de 19:50:53 a 20:00:35: **1.506 Presents `hr=0`**, `pfnPresentCb` p50 2,7 ms, p90 12,9 ms, p99 91 ms (máx 6,6 s).
- `FLUSH_TLB`: 154.414 chamadas, p50 1,44 ms, p90 2,08 ms, 2 com ≥ 400 ms. Wrapper 154.430/154.430.
- KMD: 2.146 Presents, 36 `DESKTOP REAL DETECTADO`, 4 `BYTE_PROOF PASS` no scanout.
- Harness D3D11 60/60 `PRESENT_OK`, 1,8 FPS.
- Zero TDR_QUIESCE, IB_TRIPOD TIMEOUT, GFXHUB_FAULT, `DEVICE_REMOVED` e `WaitPaging TIMEOUT`. Nenhum evento de display/TDR no log System.
- **DX.87 validada como mecanismo:** 5.431 registros `BCR1` válidos (um por tick do escritor, ~9 Hz) e nenhum acesso em fase ANTES no último, coerente com parada limpa.

**3. Falhas:**
- Explorer caiu **5 vezes** (instâncias 5680, 8788, 3364, 2004, 7384, 11740), `SearchApp` 21 vezes, mais 1 outro processo; 6 LiveKernelEvent. É o defeito antigo da textura compartilhada recusada pelo runtime (docs 469). Sem Explorer estável não há desktop interativo confiável.
- 413 `NO_SPACE` e 5 `PUBLICACAO_FALHA` de GPUVA.
- Relato visual ainda não recebido.

**4. Veredito frente ao critério do doc 470: D — 600 s sem trava.** Pelo próprio critério, uma corrida não prova estabilidade: a trava dura é intermitente (90 s a 8 min nas anteriores, e a 127 também passou dos 600 s). O mecanismo de migalhas em disco ficou validado e pronto para a próxima trava.

**5. Recuperação e pendências:** BasicDisplay Código 0 no mesmo boot, `CLEAN_BOOT_REQUIRED` armado. **NÃO VALIDADOS EM HARDWARE:** estabilidade repetível, Explorer estável, 60 FPS e jogos.

## Próximo passo proposto
Atacar o Explorer offline, antes de gastar corrida: descobrir por que o runtime recusa com `E_INVALIDARG` a alocação da textura compartilhada 1024x832 BGRA (`Misc=0x2`, `Bind=0xA8`), depois de o KMD aceitá-la (`KmdStatus=0`). A repetição de 600 s pode ser acumulada nas mesmas corridas que testarem essa correção.

## Critério da Corrida 135 (repetição, mesmo pacote 3.292.1.0, escrito antes)
Pré-requisitos: boot frio; runner `tools\EXECUTAR_CORRIDA134_DX87_600S.cmd` (mesmo candidato, mesmos hashes). Daniel observa a tela e anota o que viu e quando.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem trava, rollback online, desktop visível relatado | Segunda janela de 600 s; estabilidade sobe de "uma vez" para "reproduzida duas vezes". Seguir para o Explorer/textura compartilhada. |
| **B** | Trava; último `BCR1` com acesso ANTES em ≥ 2 registros | Acesso nomeado é o suspeito direto (critério 470 A). |
| **C** | Trava; `BCR1` sem acesso ANTES persistente | Critério 470 B/C: ampliar cobertura ou acumular recorrência. |
| **D** | 600 s sem trava, mas visual ruim relatado (preto, congelado, artefatos) | Estabilidade do kernel sem entrega visual; investigar a cadeia Present→scanout na janela relatada. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |
