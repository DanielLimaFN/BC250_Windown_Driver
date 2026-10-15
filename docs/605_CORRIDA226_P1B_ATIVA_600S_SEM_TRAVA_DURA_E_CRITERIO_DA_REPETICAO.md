# 605 — Corrida 226: P1b ativa em regime, 600 s sem trava dura; critério da repetição

**Corrida 226:** 05/10/2026 16:15:43 · lab **3.374.1.0** (SYS `8FC974FA…`) · critério: [doc 604](604_CORRIDA225_P1B_ARMOU_DXGKCBMAPMEMORY_RECUSOU_A_DRAM_P1B2_E_CRITERIO_CORRIDA226.md) §4
**Evidência:** `docs/evidence/2026-10-05_16-15-43-DESKTOP-HQAG6T4-lab-binding/` (`display-log-this-bind-FINAL.txt`, 51,4 MB e 405.363 linhas; logs da UMD -FINAL).

## 1. Pré-condição: válida (P0)

- `[P1B_VRAM_DRAM] ATIVO ... alias provado por escrita (padroes A e B 1024/1024, offset 0xFFFF000, conteudo restaurado)` na linha 9 do log.
- `StartDevice LEAVE status=0x00000000`, sem `PfpDirectLoad falhou` e sem `MAP_FAILED`. A P1b.2 corrigiu a queda da 225.
- Base igual à da 223:
  - `[D2_ANEL_SISTEMA] ATIVO`;
  - `[P2_DOORBELL] ATIVA`;
  - `[P7_GFX_GTT] ATIVO`;
  - `[GfxEngine] ONLINE SUCESSO`.
- **Zero `[HDP1_FLUSH]`/`[HDP_PT_FLUSH]` em regime.** A única linha `[HDP_PT_FLUSH] #1` aparece no teardown, **depois** de `[P1B_VRAM_DRAM] TEARDOWN: desarmada`, quando o caminho já voltou à BAR, como deve. Contador da P1b: `HDP omitido 228376 vezes`.

## 2. O que aconteceu (P0)

- **600 s sem trava dura.** O watchdog chegou ao prazo às 16:25:43 e fez `RECOVERY_OK_ONLINE` às 16:25:56: rollback sem reboot, BasicDisplay em Código 0 em 5 amostras.
- **Trabalho em regime:**
  - 20.838 IBs aposentados (`NotifiedTotal=20838`, `NotifyFailed=0`);
  - 5.152 Presents;
  - 4.278 `COMPOSITE_BLT OK`;
  - SDMA0 `Submitted=12723 Completed=12723 Fallbacks=0 HostCpuCopies=0`;
  - doorbell `Kicks=33651`.
- **Falhas:** zero TDR, zero `DEVICE_REMOVED`, zero fault de GCVM, zero falta de memória.
- **Teardown limpo e na ordem:** P7 desligado, GFX offline, P2 desarmada, D2 desligada com `falhas=0`, anel devolvido à VRAM, P1b desarmada, MMU global restaurada, fusível em CLEAN, `RemoveDevice status=0`.
- **Observado pelo Daniel:** "uma leve travada e voltou" durante a corrida. No log vivo daquele momento não havia TDR, timeout nem evento de vídeo no System. Pausa transitória sem assinatura de engine. Que tenha sido rajada de paginação ou do DWM recriando superfícies é H, não minerado.

**Ressalvas medidas, todas já conhecidas:**
- As 4 linhas `[RLC_SAFE_MODE] ENTER/EXIT TIMEOUT` são do soft-reset do teardown. A 222, a outra corrida que chegou ao teardown por StopDevice, teve as mesmas 4. É padrão do teardown, não deste candidato.
- 5 `COMPOSITE_BLT RECUSADO (resolucao)`, 23 `ResolveGpuVa RECUSADO` (`de_outro_dono`) e 12 `ActiveOpenAlloc RECUSADO`. É a lacuna das superfícies compartilhadas entre processos (doc 601), a mesma dos artefatos do Clima.

## 3. Veredito: desfecho A

Pelo critério escrito antes (doc 602/604), **600 s conta só com repetição.**
- Na configuração anterior (BAR + HDP) as corridas 217–221, 223 e 224 travaram, entre ~100 s e ~338 s.
- A 222 passou 600 s com a mesma configuração e virou não atribuível.

A 226 é a segunda corrida de 600 s em 9 desde a 217, e a primeira com a P1b. **Que a P1b tire a trava dura é H**, fortalecida mas não provada.

**Avanço funcional que é P0 por si:** primeira corrida em que a CPU acessa a VRAM pela DRAM, sem a BAR e sem HDP, como o amdgpu em APU, com desktop, composição, paginação e teardown de pé por 600 s.

## 4. Critério da repetição (Corrida 227)

**Mesmo pacote 3.374.1.0, sem rebuild:** boot frio, CMOS `-Ler` a menos de 15 min, `tools\EXECUTAR_CORRIDA226_P1B2_VRAM_PELA_DRAM_600S.cmd`. A pasta de evidência leva o carimbo de hora da nova corrida.

**Pré-condição:** a mesma da 226 (P1b ATIVO, StartDevice 0, zero HDP em regime).

**Desfechos:**

- **A — 600 s de novo**
  - *Assinatura:* rollback online limpo, zero TDR.
  - *Implicação:* duas seguidas com P1b contra 1 em 8 sem ela. A BAR/HDP passa a causa provável da trava dura (H forte). Repetir uma terceira vez antes de fechar.
- **B — trava dura igual**
  - *Assinatura:* GPU ociosa, `RESET_REASON=0x40080400`.
  - *Implicação:* a P1b não é suficiente e a 226 foi sorte como a 222. Próximo da paridade: P3 (IH).
- **C — StartDevice cai ou a P1b não arma**
  - *Assinatura:* Código 43 ou `NAO ATIVA`.
  - *Implicação:* defeito de host; o log nomeia o sítio.
- **D — artefatos ou corrupção novos**
  - *Assinatura:* pixels errados, faults ou timeouts em regime que a 223 não tinha.
  - *Implicação:* coerência pela DRAM sem HDP.
- **E — fora do mapa**
  - *Assinatura:* descrever o observado antes de julgar.
  - *Implicação:* a definir.

**Pedido ao Daniel:** anotar a hora de qualquer pausa, para cruzar com o rastro de paginação.

## 5. Pendências

**NÃO VALIDADO EM HARDWARE:**
- que a P1b elimine a trava dura (falta repetição);
- composição entre processos (superfícies compartilhadas);
- a causa da pausa breve observada.
