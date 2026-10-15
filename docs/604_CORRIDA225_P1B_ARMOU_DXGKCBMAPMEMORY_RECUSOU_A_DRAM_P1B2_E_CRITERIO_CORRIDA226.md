# 604 — Corrida 225: a P1b armou, o DxgkCbMapMemory recusou a DRAM e o StartDevice caiu; P1b.2 e critério da Corrida 226

**Corrida 225:** 05/10/2026 15:39:42 · lab **3.373.1.0** (SYS `6AB0D683…`) · critério: [doc 603](603_CORRIDA224_P1B_NAO_ARMOU_PAGINA_PRETA_NA_PROVA_P1B1_E_CRITERIO_CORRIDA225.md) §4
**Evidência:** `docs/evidence/2026-10-05_15-39-42-DESKTOP-HQAG6T4-lab-binding/` (`display-log-this-bind-FINAL.txt`, 12.397 bytes).

## 1. O que aconteceu (P0)

**A P1b.1 armou na primeira tentativa:**
- `[P1B_VRAM_DRAM] ATIVO ... BAR 0xC0000000 -> DRAM 0x460000000 (+0x10000000); 7 faixas de RAM do Windows, nenhuma sobreposta; alias provado por escrita (padroes A e B 1024/1024, offset 0xFFFF000, conteudo restaurado)`.
- Primeira prova no silício de que a VRAM vista pela BAR e a DRAM em `GCMC_VM_FB_OFFSET<<24` são a mesma memória, nos dois padrões.

**Funcionou pela DRAM:** os mapeamentos que usam `MmMapIoSpace`/`MmMapIoSpaceEx`:
- tabela da GART D2 (`divergentes=0`);
- anel do SDMA0;
- tripé de boot;
- D2 ATIVO;
- `[P2_DOORBELL] ATIVA`.

**Falhou:** `[StartDevice] PfpDirectLoad falhou: status=6, PhysicalReady=0`.
- Status 6 é `BC250_PFP_DIRECT_LOAD_MAP_FAILED`. O `DxgkCbMapMemory` recusou o endereço traduzido para a DRAM.
- `StartDevice LEAVE status=0xC00000BB`, `RemoveDevice`, Código 43 (`CM_PROB_FAILED_POST_START`).
- O teardown da P2, da D2 e da P1b rodou inteiro.
- Rollback para BasicDisplay em Código 0. Sem trava e sem IB: a corrida não mediu nada do regime.

## 2. Veredito: desfecho E (fora do mapa)

O critério previa a falha de arme (C), mas não a falha **depois** do arme. **O defeito é meu.**
- O `DxgkCbMapMemory` só mapeia faixas dos recursos do adaptador, as BARs (P0 nesta corrida).
- Eu mandei por ele o endereço da DRAM em sete sítios.
- Os testes offline não pegavam isso, porque a P1b nunca é armada neles.

## 3. P1b.2 (candidato 3.374.1.0)

**Fonte única** em `bc250_vram_cpu_path.h`:
- `Bc250VramCpuMapMemory`:
  - se a tradução muda o endereço, usa `MmMapIoSpaceEx` na DRAM (equivale ao `ioremap` do `aper_base` no amdgpu);
  - senão, usa o `DxgkCbMapMemory` pela BAR, como antes.
- O mapeamento pela DRAM fica registrado numa tabela de 64 entradas. Com a tabela cheia, falha fechada sem órfão.
- `Bc250VramCpuUnmapMemory` decide pela tabela, não pelo estado da P1b. Esse estado pode mudar entre map e unmap, porque o teardown do display desarma antes dos demais.

**Sítios trocados:**
- `bc250_active_miniport.c`: os 2 HQD (MQD inativo e ativo);
- `bc250_gcvm_backing_transaction.c`;
- `bc250_pfp_direct_load.c`;
- `bc250_sdma_ring_work.c` (anel e Work);
- `bc250_submission_engine.c` (bloco do MEC);
- `bc250_vram_probe_transaction.c`.

Os `DxgkCbMapMemory` que sobraram mapeiam só a BAR de doorbell.

**Portão (`audit_p1b_vram_cpu_pela_dram`):**
- físico montado pela BAR só pode seguir para o helper;
- endereço traduzido nunca chega ao `DxgkCbMapMemory`;
- unmaps pelo helper ≥ maps pelo helper;
- o helper separa DRAM de BAR e não deixa órfão;
- o unmap consulta a tabela antes do `DxgkCbUnmapMemory`.

**Mutações:**
- 6 novas reprovadas. Uma passou na primeira versão do portão, então endureci a regra de unmap.
- As 3 da P1b.1 continuam reprovando.

**Suítes offline:** stubs `__imp_MmMapIoSpaceEx`/`__imp_MmUnmapIoSpace`, que falham fechados, em `dxgk_readonly_ddi_test`, `wddm_submission_pipeline_test`, `bc250_unified_wddm_submission_test`, `readonly_miniport_test` e `active_miniport_test`. O `#include "bc250_vram_cpu_path.h"` passou para depois dos demais includes.

**Provas offline:**
- `build-contract` EXIT=0;
- laboratório "TODOS OS TESTES APROVADOS (0 falhas)";
- auditoria de segurança PASS e auditoria do pacote PASS;
- `alias provado por escrita` e `MmMapIoSpaceEx` no SYS assinado.

| Arquivo | SHA-256 |
|---|---|
| SYS | `8FC974FAFAA806B071C050B25DC6332B09AA7F52A6096D7C47398645D09E17AE` |
| INF | `7FEA8E44D4E51635AAEF88958DB3A438AC1E1B96A9FC6FDDAFB5CD7802B426DD` |
| CAT | `AF81A382FBC260E8E037C33ECDB592C906C4EF6D8C8CFA647CFBBFABFEF304BA` |

**Scripts:** `tools/prepare-corrida226-p1b2-mapeamento-pela-dram.ps1` e `tools/EXECUTAR_CORRIDA226_P1B2_VRAM_PELA_DRAM_600S.cmd`. O preflight a seco parou só na CMOS com mais de 15 min, como deve.

## 4. Critério da Corrida 226

**Pré-condição:**
- `[P1B_VRAM_DRAM] ATIVO ... alias provado por escrita (padroes A e B 1024/1024 ...)`;
- **sem** `PfpDirectLoad falhou`;
- `[GfxEngine] ONLINE SUCESSO`;
- desktop;
- zero `[HDP1_FLUSH]`/`[HDP_PT_FLUSH]` depois do arme.

**Desfechos:**

- **A — 600 s sem trava**
  - *Assinatura:* rollback limpo.
  - *Implicação:* conta só com repetição.
- **B — trava igual**
  - *Assinatura:* GPU ociosa, `RESET_REASON=0x40080400`.
  - *Implicação:* a BAR/HDP sai das causas; próximo, P3 (IH).
- **C — StartDevice cai de novo**
  - *Assinatura:* outro `MAP_FAILED`/Código 43.
  - *Implicação:* o log nomeia o sítio; é defeito de host, não do silício.
- **D — corrupção ou artefatos novos**
  - *Assinatura:* pixels errados, faults ou timeouts que a 223 não tinha.
  - *Implicação:* a coerência pela DRAM sem HDP difere da BAR. Comparar os sítios WC/UC.
- **E — fora do mapa**
  - *Assinatura:* descrever o observado antes de julgar.
  - *Implicação:* a definir.

**H declarada:**
- O VidMm recebe `CpuPhysicalStart` traduzido para a DRAM e mapeia o segmento por conta própria.
- Se ele também validar contra os recursos do adaptador, a falha aparece no `Lock` (`0x10E` ou recusa do DWM), e não no StartDevice. Isso seria desfecho C ou E, com o sítio identificado.

## 5. Pendências

- **Daniel:** fusível `-Reset` (se disparado), boot frio, CMOS `-Ler` a menos de 15 min e `tools\EXECUTAR_CORRIDA226_P1B2_VRAM_PELA_DRAM_600S.cmd`. A recuperação já aconteceu: BasicDisplay em Código 0.
- **NÃO VALIDADO EM HARDWARE:**
  - P1b em regime;
  - 600 s sem trava de forma repetível;
  - composição entre processos.
