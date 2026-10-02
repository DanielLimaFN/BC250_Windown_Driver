# 478 — Corrida 138 (DX.89): quarta janela de 600 s; a hipótese (a) não foi testada porque o KMD forçava CpuVisible; DX.89b e critério da Corrida 139

> **Corrida:** 26/09/2026, bind 23:15:49 após boot frio, rollback online 23:26:03 no mesmo boot. **Candidato:** DX.89 / 3.294.1.0.
> **Evidência:** `docs/evidence/2026-09-26_23-15-49-DESKTOP-HQAG6T4-lab-binding` (log `-FINAL` completo).
> **Relato do Daniel:** "desktop visível durante os 10 min".

## 1. Relato obrigatório
**1. Houve avanço?** Estabilidade sim: quarta janela seguida de 600 s (134, 136, 137, 138). Na textura compartilhada, só aprendizado: a variável não chegou ao KMD.

**2. Sucessos (P0, exceto o visual):**
- 600 s sem trava; `RECOVERY_OK_ONLINE` no mesmo boot; desktop visível durante os 10 min.
- DWM com **2.903 Presents `hr=0`** (recorde; 137: 2.426). KMD com 3.900 Presents, 3 `BYTE_PROOF PASS` no scanout. Harness 60/60.
- Wrapper de paginação 152.785/152.785; zero TDR_QUIESCE, IB_TRIPOD TIMEOUT, GFXHUB_FAULT, `DEVICE_REMOVED`, `InsideIb`, `Blt Timeout`.
- A regra da UMD disparou: 524 linhas `Allocate SHARED: recurso compartilhado sem CPU_VISIBLE`.

**3. Falhas:**
- As 524 texturas compartilhadas continuaram recusadas (`E_INVALIDARG`). A consulta DX.59 mostrou **`Flags=0x00000001`** (`CpuVisible=1`) devolvido pelo KMD nas 107 consultas de 3.407.872 bytes.
- Explorer caiu 5 vezes; SearchApp 19; 6 LiveKernelEvent. 383 `NO_SPACE`.

**4. Veredito frente ao critério do doc 477: E — fora do mapa.** A e B exigiam a textura sair com `Flags=0` no KMD, e isso não ocorreu. A hipótese (a) **não foi testada**, nem confirmada nem refutada.

**5. Recuperação e pendências:** BasicDisplay Código 0 no mesmo boot, `CLEAN_BOOT_REQUIRED` armado. Explorer estável: **NÃO VALIDADO**.

## 2. Por que a variável não chegou (P1 pelo código)
`Bc250AllocationRequiresCpuAccess` (`driver/core/bc250_allocation_abi_contract.c`) devolvia 1 para **toda** alocação da classe `RESOURCE` pelo token, independentemente da flag. A UMD omitiu `CPU_VISIBLE` e o KMD ligou `CpuVisible=1` de qualquer jeito. Dois testes congelavam essa regra: `allocation_abi_contract_test.c` e `active_miniport_test.c:2180`, que esperava `CpuVisible=1` para um blob `RESOURCE` sem flag.

## 3. DX.89b (offline)
- Na classe `RESOURCE`, quem decide o acesso da CPU é a flag; a classe deixou de forçá-lo. Recursos comuns continuam `CpuVisible=1`, porque a UMD manda a flag; o compartilhado sai `CpuVisible=0`.
- Testes: `allocation_abi_contract_test` com os dois lados (RESOURCE sem flag → não; com flag → sim). `active_miniport_test` com recurso comum (flag, `CpuVisible=1`) e caso novo de recurso compartilhado (sem flag, `CpuVisible=0`). `build-contract` EXIT 0, suítes da UMD PASS.
- Portão DX.89 estendido: a classe `RESOURCE` não pode voltar à lista do token; provado por mutação.

| Artefato (3.295.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `19785AC0CEECB1032DA0EA7AF6B10406AE654B1CEC0EBD837D4B708D06DC56CE` |
| `bc250mcdm.sys` fonte lab | `B9401410B013693B4041E80BA6019982C46D4EB96745CF4D3344CF1DB23BA20D` |
| `bc250umd.dll` | `3EB5806557EF3DD9E41D5FD45CD5737D8A2D6FFC06038824946BF5C327F9CFB8` |
| `bc250mcdm.inf` | `926DBF6150049DBEADA1BC7356E7D371211B6315C2A43323A6C2CF6C306B28DB` |
| `bc250mcdm.cat` | `0456598C48C2A2CFFE8F711C3A579D2679A9B04B867AF16F0F1EF05E191EB353` |

## 4. Critério da Corrida 139 (escrito antes)
Runner `tools\EXECUTAR_CORRIDA139_DX89B_600S.cmd`, boot frio antes, Daniel usando o desktop.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | `DX59_ALLOC_QUERY ... Size=3407872` com `Flags=0x00000000` **e** `AllocateHr=0x00000000`; zero quedas do Explorer com uso real; 600 s sem trava | Hipótese (a) confirmada. Próximo: abertura em outro processo (`OpenResource`/DWM) e conteúdo correto. |
| **B** | `Flags=0x00000000` na consulta, mas `AllocateHr=0x80070057` continua | Hipótese (a) refutada. Testar (b): incluir o segmento de memória do sistema no conjunto da alocação compartilhada. |
| **C** | A alocação passa (`hr=0`), mas o Explorer ainda cai ou aparece erro novo (lock, abertura, render) | O próximo obstáculo sai nomeado nos logs; investigar ali. |
| **D** | Recurso comum (não compartilhado) regride: falha de lock/upload ou imagem errada | A mudança de classe afetou recurso comum; reverter e isolar. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Controle a não regredir: desktop visível, DWM apresentando, `FLUSH_TLB` p50 ~1,5 ms, harness 60/60.
