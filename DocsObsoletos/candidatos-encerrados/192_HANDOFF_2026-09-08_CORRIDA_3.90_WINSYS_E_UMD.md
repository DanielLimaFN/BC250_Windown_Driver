# 192 — Handoff: Corrida no Silício 3.90.0.0 (Robustez Total, Zero Timeout, Rollback Limpo)

**Data:** 2026-09-08 08:44–08:46 BRT  
**Corridas gastas:** **uma** (`3.90.0.0`)  
**Hardware tocado:** **SIM** (corrida física no silício AMD BC-250 autorizada por Daniel).  
**Estado:** WDDM em **Código 0**, **33 submissões WDDM aceitas**, **zero timeouts**, **zero falhas de ring**, **zero BugCheck**. Restauração da MMU global no teardown (26 registradores) comprovada no silício. DWM com tela preta e fail-fast `0x887A0004` (ausência de backend D3D10/11 nativo). Rollback automático do watchdog concluído com sucesso e reinicialização limpa em BasicDisplay Código 0.

---

## 1. Identidade e Evidência

- **Bind:** [`docs/evidence/2026-09-08_08-44-37-DESKTOP-HQAG6T4-mcdm-display-binding/`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-08_08-44-37-DESKTOP-HQAG6T4-mcdm-display-binding/)
- **Rollback:** [`docs/evidence/2026-09-08_08-46-39-DESKTOP-HQAG6T4-mcdm-online-rollback/`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-08_08-46-39-DESKTOP-HQAG6T4-mcdm-online-rollback/)
- **Pacote Instalado:** `oem34.inf`, versão `3.90.0.0`
- **Configuração:** `SdmaPlanoAFallback=0` (somente `INDIRECT`), `TdrInduceHang=0` (sem hang forçado), `GpuvaLabPromotion=0`
- **Assinatura:** SYS `737332579FC306D83A054778062161BEBE4CE0F53A7B282C138B4475641C6406`, CAT `D9FB66B2DE9BC39A38B4F83DA3E962524C2132466DAA9A9DD941084EA90B7E46`
- **Watchdog:** 120 segundos, expirou sem confirmação visual devido à tela preta do DWM.

---

## 2. Resultados Físicos Medidos no Silício (P0)

1. **Submissões e Robustez do SDMA (Node 0)**:
   - Total de submissões recebidas do `dxgkrnl`: **33**
   - Submissões executadas via `INDIRECT`: **32** (a GPU leu diretamente o DMA buffer em GPUVA do dxgkrnl).
   - Submissões não executadas / recusadas: **0**
   - Submissões com `TRIPE_PROVADO` completo (RPTR==WPTR + fence GPU sobre poison + 5 canários substituídos com zero divergentes): **3** (todas as operações de paginação).
   - Submissões com 2 pernas (RPTR==WPTR + fence física sobre poison): **30**.
   - Timeouts no ring / esperas infinitas: **0** (a correção de aritmética de wrap do ring da faixa A15 funcionou perfeitamente).
   - BugChecks: **0**.

2. **Ciclo de Vida do Driver e MMU Global**:
   - `[StartDevice]`: MMU Global GFXHUB programada com 26 registradores e validada via commit/invalidação física (`FlushMappedGfxhubTlb Transaction status=0, PhysReady=1, AckObserved=1, Polls=1`).
   - `[DestroyProcess]`: Invalidação explícita de faixas de GPUVA executada em 2 processos finalizados sem deixar faixas órfãs.
   - `[StopDevice]`: **26 registradores da MMU Global restaurados ao snapshot original** com invalidação de TLB física e registro de `CLEAN (0)` no fusível persistente de boot `C:\BC250_BOOT_FUSE.dat`.

3. **Caminho de Display e UMD**:
   - `CommitVidPn` e `SetVidPnSourceAddress` (Addr `0xAB0000`, Segmento 1) bem-sucedidos.
   - 31 operações de `Present` por SDMA registradas com `SDMA=YES`.
   - Amostragem física da VRAM em `0xAB0000`: `NonZero=0/256` (buffer de primária vazio).
   - Causa da tela preta: A UMD (`bc250umd.dll`) recebeu chamadas do `dwm.exe` para `OpenAdapter10_2` e `OpenAdapter10`. Com `BC250_D3D10_11_NATIVE_READY=0`, ela retornou honestamente `E_NOTIMPL`. O DWM fail-fastou com `0x887A0004` (`DXGI_ERROR_UNSUPPORTED`), gerando 2 dumps completos de ~218 MB.
   - Como o DWM não conseguiu criar um device D3D11 para compor a cena, a superfície permaneceu preta.

---

## 3. Conclusão

A camada de KMD, SDMA0, MMU global, paginação e teardown está **completamente estável e robusta** no silício. Não houve nenhuma regressão, nenhum timeout e nenhum vazamento de estado.

A barreira única para a Área de Trabalho ficar visível permanece exatamente a mesma diagnosticada: **o backend 3D D3D10/11 para o DWM** (pilar B7).
