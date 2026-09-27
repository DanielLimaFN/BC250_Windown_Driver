# Corrida 95 — DX.49b: ~3× mais apresentações, desktop com defeito e travamento

> 23/09/2026, bind às 23:30:42, janela de 300 s.
> **Candidato:** lab **3.255.1.0**, SYS `C8B469FC…`, UMD `725279BA…`.
> **Evidência:** `docs/evidence/2026-09-23_23-30-42-DESKTOP-HQAG6T4-lab-binding/`.
> **Critério:** [doc 390](390_DX49B_LOG_DA_UMD_DURAVEL_E_CRITERIO_CORRIDA95.md).

## 1. Relato e máquina

**Relato do Daniel:** ficou "um pouco mais rápido, porém ainda travado"; depois
de um tempo, o desktop ficou com defeito visual.

**Máquina (P0):**
- Kernel-Power 41 às 23:37:31 (reinício sem desligamento correto);
- no boot seguinte o fusível recusou o StartDevice (`Boots=2`);
- rollback `RECOVERY_OK_ONLINE` às 23:37:51;
- BasicDisplay em Código 0.

## 2. Medido

**Harness (P0):** 60/60, código 0, zero Present falho.

**DWM** (snapshot do binder, 23:30:50–23:32:17):

| | Corrida 93 | Corrida 95 |
|---|---|---|
| `DXGI Present LEAVE hr=0` por minuto | 35–38 | **109** (minuto 23:31; nos 17 s de 23:32 foram 38, ~134/min) |
| Falhas de Present | — | zero |

**Lote no DWM (P0):**
- 971 draws e 905 flushes;
- 394 flushes levaram draws, com mediana de **4** por flush e máximo de 18;
- esperas de fence somaram 3,4 s em ~87 s;
- um único dreno (`destruir recurso`).

**Staging (P0):** criado uma vez com 11.075.584 bytes (`AlocUs=9958`) e nunca
cresceu.

**KMD, log resgatado** (175.656 linhas, sha256 `A419A78A…4800`):
- 486 Presents (426 flip, 60 blt) e 397 `COMPOSITE_BLT OK`, 397/397
  `SAMPLED_PASS`;
- dirty rects: 384 ocorrências aceitas, zero recusadas, provas amostradas 14/14
  `Match=5/5`;
- 2366 submissões GFX, IB máximo de 4816 DW;
- 496 warm-ups DX.43, zero TDR, zero `CP_RESET_DETECTED`, zero `GFXHUB_FAULT`,
  zero timeout;
- DX.48: sem `AddDevice #2` e sem `Type=29` no meio da corrida.

**Fim do log do KMD:** para no meio do registro de uma submissão SDMA de
paginação (`PT_WALK SRC`). A submissão anterior fechou o tripé
(RPTR==WPTR, fence sobre poison). Mesma forma de corte da 94.

**Logs da UMD que sobreviveram ao reboot:**
- Explorer (pid 6620) até **23:34:05.661**;
- harness até 23:32:01, quando terminou normalmente;
- o log do dwm.exe está em `C:\Windows\Temp`, sem acesso de leitura nesta
  sessão, e **ainda precisa ser copiado com elevação**.

## 3. Leitura

**Vazão (P0):** ~3× a da 93. O relato ("um pouco mais rápido, ainda travado")
casa com ~2 quadros/s.

**Critério:**
- **Não é A:** houve travamento e o relato não é de desktop fluido;
- **Não é B:** B exige não travar;
- **C ou D:** depende de o log do dwm.exe ter sobrevivido ao reboot, o que ainda
  não foi verificado. O log do Explorer, gravado pelo mesmo sink durável,
  sobreviveu até 23:34:05.

**Travamento duro, duas corridas seguidas com a DX.49:** 94 (~35 s depois do
bind) e 95 (entre 23:34:05 e 23:37:31). Antes da DX.49 foram 2 em 7 (88 e 92),
com outra assinatura (QAI `Type=29` no fim). **H:** a DX.49 aumenta a chance do
travamento, pela vazão maior (mais submissões por segundo) ou por um dos
comportamentos novos:
- IBs grandes;
- cópias SDMA por retângulo (dirty rects);
- escrita no scanout por dois caminhos, a cópia por dirty rect do KMD e o
  `radv_bc250_present_scanout` direto da UMD.

**Defeito visual "depois de um tempo" (H):** candidatos são os dirty rects
(atualização parcial do scanout, que também recebe a escrita direta da UMD) ou
um erro de vida útil no lote. Sem foto e sem o log do DWM, não se separa.

## 4. Próximos passos

1. **Custo zero:** Daniel copia, em terminal elevado, os
   `C:\Windows\Temp\BC250_UMD_LOG_*` para a pasta de evidência da 95. O da 94
   ainda pode estar lá, se o pid não tiver sido reaproveitado.
2. **Com o log do DWM,** escolher a bissecção de uma variável. Candidato
   preferido: **desligar os dirty rects** (voltar à cópia de tela cheia da 93)
   mantendo o lote. Os dirty rects mexem no scanout, que é onde aparece o
   defeito visual, e as cópias por retângulo são o comportamento de hardware
   mais novo e com menos corridas.
