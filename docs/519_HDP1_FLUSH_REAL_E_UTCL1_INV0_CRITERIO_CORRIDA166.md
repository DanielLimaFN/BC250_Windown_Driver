# 519 — HDP.1 (T1): o flush de HDP estava no registrador errado; critério da Corrida 166

> 28/09/2026, offline. Candidato **3.311.1.0**. Tarefa T1 do [plano de trabalho](../PLANO_DE_TRABALHO_TRAVA_E_DESKTOP.md).
> **Nada desta faixa foi validado em hardware.**

## 1. O que a análise achou

O plano supunha que o driver fazia o flush de escrita do HDP no caminho do SDMA e só esquecia de fazer no FLUSH_TLB. A leitura de fonte mostrou algo maior: **o driver nunca fez esse flush em lugar nenhum**, e o endereço que ele chamava de flush pertence a outra peça da GPU.

| # | Achado | Confiança | Prova |
|---|---|---|---|
| 1 | `0x4A80`, chamado de `HDP_FLUSH`, é **`SDMA0_UTCL1_INV0`**: o controle de invalidação da cache de tradução (UTCL1) do SDMA | P1 | `inc/linux_ref/gc_10_1_0_offset.h`: `mmSDMA0_UTCL1_INV0 = 0x40`, base GC seg0 `0x1260` dw = `0x4980`. É a mesma conta de `UTCL1_CNTL=0x4A70`, `F32_CNTL=0x4A28` e `STATUS2=0x4A60` no driver. O `dbg/hw.c:791` já o chamava de `A_SDMA0_UTCL1_INV0`. |
| 2 | Escrevíamos **0** nele em 31 pontos de `bc250_sdma_present.c`, em 6 de `bc250_sdma_ring_work.c` (mais a restauração de snapshot), a cada publicação do MEC (`bc250_submission_engine.c`) e no serviço legado de cópia | P1 | Leitura de código |
| 3 | Os campos incluem `CPF_INVREQ_EN` (bit 0) e `GPUVM_INVREQ_EN` (bit 1) | P1 para os nomes; **H** para o efeito | `gc_10_1_0_sh_mask.h` |
| 4 | O upstream **nunca** escreve `UTCL1_INV0` | P1 | `sdma_v5_0.c`, `gfx_v10_0.c`, `gmc_v10_0.c`, `gfxhub_v2_0.c` |
| 5 | `0x3FC4` levava o nome `HDP_MEM_COHERENCY_FLUSH_CNTL` em quatro arquivos, mas é `HDP_READ_CACHE_INVALIDATE`, a invalidação de **leitura**. O "drenar antes de invalidar" do DX.14 nunca drenou nada, e o gate DX.14 **exigia** essa escrita errada | P1 | `hdp_5_0_0_offset.h`; `tools/audit-bc250mcdm-safety.py` (antes da HDP.1) |
| 6 | O flush de escrita real é **`BIF_BX_PF_HDP_MEM_COHERENCY_FLUSH_CNTL` = `0x385C`**: escrever 0 e depois ler `RCC_CONFIG_MEMSIZE` (`0x378C`) | P1 | `nbio_2_3_offset.h` (`0xF7` e `0xC3`, BASE_IDX 2); `cyan_skillfish_ip_offset.h` (`NBIO seg2 = 0xD20`); `amdgpu_discovery.c` (Cyan Skillfish usa `nbio_v2_3` e `hdp_v5_0`); `nbio_v2_3.c` v5.3/v5.4 (esse registrador nativo, em bare metal, na Navi10); `amdgpu_hdp.c` (`amdgpu_hdp_generic_flush`: escrita 0 e leitura de memsize) |
| 7 | A base NBIO seg2 está validada | **P0** | O driver já lê `0x378C` e recebe `0x200` (512 MiB), coerente com `FB_LOCATION 0xF400..0xF41F` (sonda `VramTotalMiB` da Corrida 165) |
| 8 | O Linux faz `mb()` e o flush **ao fim de toda atualização de tabela pela CPU**, e não só no FLUSH_TLB | P1 | `amdgpu_vm_cpu.c`, `amdgpu_vm_cpu_commit` |
| 9 | A "zona de congelamento `0x3400–0x8100`" do outro driver Windows (doc 509 §4) não vale como prova | P0 contra | Já escrevemos dezenas de registradores nessa faixa sem travar (HDP, SDMA0, GRBM) |

**Leitura das migalhas da 165, corrigida:** o slot "CPU04 em FLUSH_TLB, fase ANTES" (seq 48.774 de 90.814) não indica uma chamada presa. O log tem 4.385 `PAGING_DDI ENTER` e 4.385 `LEAVE` de FLUSH_TLB, todos fechados. O slot deve ser memória que se degradou (há slots `INVALIDA` no mesmo dump). A leitura "uma CPU presa no FLUSH_TLB", do §2.2 do plano, está fraca.

**Por que isto pode ser a trava (H, não provado):**
- **(a)** Se o 0 em `GPUVM_INVREQ_EN` desliga o recebimento de invalidações pela UTCL1 do SDMA, o SDMA passa a usar traduções velhas depois que o VidMm move memória. É justamente na partida que ele mais move. O código já tinha um remendo para "UTCL1 retendo traduções antigas" (`FORCE_INVALIDATION`, T3.3).
- **(b)** Sem flush de escrita, a GPU pode ler PDE/PTE, anel ou pacote ainda preso no HDP. O projeto já mediu esse sentido do problema (Corrida 60).

## 2. A mudança (um candidato, decidido pelo Daniel em 28/09)

1. **Fonte única** `driver/core/bc250_hdp_contract.h`: `0x385C` (flush), `0x378C` (leitura de confirmação), `0x3FC4` (invalidação de leitura) e `0x4A80` com o nome verdadeiro, **somente leitura**.
2. **Ninguém escreve `0x4A80`.** Saiu da allowlist de escrita do transporte, da sequência do SDMA, do ring work (inclusive da lista de snapshot/restauração), da publicação do MEC e do serviço legado.
3. **Flush real em todos os sítios de coerência:** `_mm_mfence()`, depois `0x385C=0`, depois a leitura de `0x378C`, depois a invalidação `0x3FC4=1` com leitura, como antes. Vale para `Bc250SdmaPresentHdp`, `Bc250SdmaHdp` e a publicação do MEC.
4. **Miniport:** helper único `Bc250ActiveHdpWriteFlush`, chamado **antes** do laço de invalidação do FLUSH_TLB e **ao fim de todo `UpdatePageTable`** (como `amdgpu_vm_cpu_commit`). Falha sempre se declara (`[HDP_PT_FLUSH] FALHA #n`); sucesso é amostrado (#1–#4 e a cada 16.384). É a thread de paginação, então nada de log por chamada.
5. **Medição de graça:** `[HDP1_INIT]` lê `UTCL1_INV0` uma vez, antes de qualquer flush, o valor nunca antes medido. `[HDP1_FLUSH]` repete a leitura amostrada (#1–#4 e a cada 65.536) com a referência de memsize. `[HDP1_FLUSH] DIVERGENCIA` se a leitura de confirmação mudar.
6. **Só nomes, sem mudança de comportamento:** anel gráfico (`BC250_GFX_REG_HDP_READ_CACHE_INVALIDATE`), PFP direct load (`R_HDP_READ_CACHE_INVALIDATE`) e comentários do serviço legado.
7. **Catálogo** `docs/03`: `0x4A80` passou a `SDMA0_UTCL1_INV0` (P1, somente leitura); `0x385C` é P1 com base NBIO P0; `0x378C` é P0.

**Dívidas declaradas e não tocadas:**
- O PFP direct load (StartDevice) não recebe flush real antes de o CP ler o firmware. Funciona hoje (P0) e fica como está.
- O serviço legado `bc250_compute_service.c` ainda alterna `HDP_MISC`.
- `dbg/hw.c` (outro driver) ainda escreve `0x4A80`.
- O `FORCE_INVALIDATION` do UTCL1 (T3.3) segue ligado. Não foi removido, para manter uma variável.

## 3. Provas offline

- `python tools/audit-bc250mcdm-safety.py`: **PASS**. Gate novo `audit_hdp1_write_flush_is_real`. O gate DX.14 foi reescrito porque exigia a escrita em `0x3FC4`.
- **10 mutações, todas reprovaram**, e a auditoria voltou a PASS depois de restaurar (`scratchpad/mutate_hdp1.py`):
  - M1: `SdmaPresentHdp` volta a escrever INV0;
  - M2: `0x4A80` volta à allowlist de escrita;
  - M3: `UpdatePageTable` sem flush;
  - M4: FLUSH_TLB sem flush;
  - M5: constante do MEC volta a `0x4A80`;
  - M6: ring work sem `mfence`;
  - M7: nome falso volta no anel gráfico;
  - M8: helper sem `mfence`;
  - M9: MEC lê o flush em vez de `0x378C`;
  - M10: `SdmaPresentHdp` sem a leitura de confirmação.
- `tools/build-bc250mcdm-lab-bringup.ps1`: EXIT 0 (`/W4 /WX`, `build-contract` com as suítes offline).
- `tools/audit-bc250mcdm-lab-package.ps1`: **PASS**.
- Strings conferidas no SYS assinado: `HDP1_INIT`, `HDP1_FLUSH] DIVERGENCIA`, `HDP1_FLUSH] #%lld`, `HDP_PT_FLUSH] #%ld OK`, `HDP_PT_FLUSH] FALHA #%ld` (2) e `UPDATE_PAGE_TABLE`.
- `tools/prepare-corrida166-hdp1.ps1` (leitura): EXIT 0. Boot sem `DEBUG`.

## 4. Candidato

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `76AEDDDEBA2ACD8CE059A309C432088D8A7E8EB967E2B47DDFEB5B64913C141C` |
| `bc250umd.dll` (**idêntica à 3.310**; UMD-fonte `8623E38A…`) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `3DA11330A49178B97ABCF111EFDE4E1A275EFC369824E1956B61278171947E70` |
| `bc250mcdm.cat` | `BAFD3E207C0A0A09DF0B65DC4D4ABA9390EB409F61FE707990A5802296C34F62` |

- DriverVer: `3.311.1.0` (fonte em `3.311.0.0`; o próximo candidato sobe para `3.312`).
- Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, P1 PERSISTENT SUBMISSION, schema 135.
- Runner: `tools/EXECUTAR_CORRIDA166_HDP1_600S.cmd`. Mesmo roteiro da 165: sem harness, operador parado, menu Iniciar clicado umas vezes ~20 s depois do desktop.

## 5. Critério da Corrida 166 (escrito antes; Lei 10)

**Base estatística (plano §3.5):** 13 travas em 15 corridas, todas antes de 200 s. Uma corrida limpa sozinha **não prova nada** (acontece ~1 vez em 8 sem mudança). Duas limpas seguidas com o mesmo candidato são o mínimo para dizer "a trava mudou", e três para chamar de causa. Uma trava basta para dizer "não é suficiente".

| Desfecho | Assinatura (o que o log e a tela mostram) | Implicação (para onde vai a investigação) |
|---|---|---|
| **A** | 600 s sem trava **e** `[HDP1_INIT]` presente **e** `[HDP1_FLUSH] #1` com `0x378C=0x00000200` e `divergencias=0` **e** `[HDP_PT_FLUSH] #1 OK` **e** zero `FALHA`/`DIVERGENCIA` **e** `IB EXECUTADO` com tripé fechando **e** Present do desktop | Primeira limpa. Repetir **idêntico** (167). Duas seguidas: "a trava mudou". Três: o HDP/INV0 era o gatilho ou parte dele. |
| **B** | Trava antes de ~200 s, com as linhas de flush OK antes dela (flush executando sem falha) | O flush real e o fim da escrita em INV0 **não bastam**. Manter a correção (defeito real, P1) e seguir para T3.1/T3.2 e depois T2. |
| **C** | Sem trava, mas com regressão de coerência: fence no poison, `IB_TRIPOD TIMEOUT`, `GFXHUB_FAULT`, `BYTE_PROOF` divergente, TDR ou device-lost que não existiam na 165 | A coerência mudou de lado. Investigar antes de repetir (o `FORCE_INVALIDATION` passa a ser suspeito). |
| **D** | O flush em si falha: `[HDP_PT_FLUSH] FALHA`, `[HDP1_FLUSH] DIVERGENCIA`, `Bc250SdmaHdp` recusado no tripé de boot, ou trava imediata com a última migalha em `TRANSPORT_WRITE 0x385C` na fase ANTES | A escrita no NBIO não é segura ou não chega assim nesta placa. Separar: candidato só sem INV0, sem `0x385C`. |
| **E** | Fora do mapa (por exemplo: trava bem depois de 200 s, desktop diferente, `UTCL1_INV0` com valor que muda sozinho entre amostras) | Descrever o observado antes de julgar. |

- **Desfecho parcial não é desfecho:** 600 s sem as linhas de flush (log incompleto) não é A. Uma trava sem nenhuma linha `[HDP1_*]` no log recuperado não é B, é falta de evidência.
- **Informação nova em qualquer desfecho:** o valor real de `UTCL1_INV0` depois do POST (`[HDP1_INIT]`), nunca medido.
- **O que perguntar ao Daniel:** se o menu Iniciar apareceu na tela (T9).
- **Sem o reset rápido (10–15 s)** as migalhas se perdem.

## 6. Resultado da Corrida 166 (28/09/2026, bind 22:54:51, pacote 3.311.1.0)

**Evidência:** `docs/evidence/2026-09-28_22-54-51-DESKTOP-HQAG6T4-lab-binding/`
- `display-log-this-bind-RECUPERADO.txt`: 3.237.993 bytes, SHA-256 `84010D10…EEEF0`, do offset 4.210.905.234;
- `umd-logs-pos-travamento/`: 7 logs da UMD copiados à mão (AppContainer e `%TEMP%`).

Evento 41 às 22:56:20. Tela preta e reset pelo operador.

**Veredito: B.** A trava continuou. O flush real e o fim das escritas em INV0 **não bastam**.

| Sinal | Medido |
|---|---|
| Duração até a trava | **49,6 s** (165: 45,9 s; 163: 56,0 s) |
| `[HDP1_INIT]` | `UTCL1_INV0 = 0x00000800`, `RCC_CONFIG_MEMSIZE = 0x200` |
| `[HDP1_FLUSH]` #1–#4 | `0x378C = 0x200`, `divergencias = 0`, INV0 constante em `0x800` |
| `[HDP_PT_FLUSH]` | #1–#2 no FLUSH_TLB e #3–#4 no UPDATE_PAGE_TABLE, todos OK, `falhas = 0` |
| `FALHA` / `DIVERGENCIA` | 0 / 0 |
| `IB EXECUTADO` / `TRIPE_PROVADO` | 993 / 843 |
| `IB_TRIPOD TIMEOUT`, `GFXHUB_FAULT`, `TDR_QUIESCE`, `DEVICE_REMOVED`, `StopDevice` | 0 |
| `[Present #` / `BYTE_PROOF PASS` | 198 / 4 |
| FLUSH_TLB ENTER/LEAVE | 5.375 / 5.374 (o último é o que estava em curso quando o log parou) |
| Migalhas | Presentes, mesma assinatura da 165: última `SDMA_REG_READ 0xFC4` concluída (CPU00). CPU04 `PAGING 0xC` ANTES na seq 49.963 de 77.686, antiga, o mesmo padrão degradado da 165 |

**P0 novo: a hipótese (a) do §1 está REFUTADA.**
- Depois do POST, `UTCL1_INV0 = 0x800` é só o bit 11, `INVREQ_IDLE`. `CPF_INVREQ_EN` e `GPUVM_INVREQ_EN` **já vêm em 0**.
- Escrever 0 nele não desligava nada que estivesse ligado; tirar a escrita não mudou a trava.
- A escrita em INV0 continua fora do driver por ser um registrador sem motivo para escrever, mas deixou de ser suspeita.

**A hipótese (b) não basta** (uma trava já diz isso, plano §3.5). O flush de escrita real rodou nos quatro sítios amostrados sem falha, e a máquina travou no mesmo intervalo.

**Roteiro diferente da 165 (P0, logs da UMD).** Nos ~50 s abriram:
- `msedge.exe` (do `Claude.exe`) às 22:55:34;
- `msedgewebview2.exe` às 22:55:37;
- `SystemSettings.exe` às 22:55:51.

A última atividade de UMD da máquina foi o `SystemSettings.exe` criando uma textura 1024×832 de **3.670.016 bytes** às 22:55:51.666. No KMD, as últimas linhas são o `DX93_ALLOC_MAP` e o `FILL` dessa mesma alocação (`Size=3670016`, `Dst=0x5C0000`), seguidos de FLUSH_TLB.

É **correlação, não causa (H)**: nas corridas limpas, processos também nasceram sem travar (doc 518 §7). Não há log do `StartMenuExperienceHost` na janela, então o menu Iniciar não chegou a ser aberto.

**Recuperação:** a BC-250 está em Código 43 (`CM_PROB_FAILED_POST_START`) com o fusível TRIPPED (Boots=2, `LastStatus=0xC0000001`). Antes de outro bind: `BC250_RECOVERY.cmd`, `inspect-boot-fuse.ps1 -Reset` e boot frio.

**Próximo (plano §6):** a T1 fica como correção de defeito real, mas não é o gatilho. Seguir para T3.1 (aliasing WC/UC da VRAM) e T3.2 (semáforo do ENG17), offline, e oferecer a T6 (C-states), que não exige código.

## 7. Referências upstream baixadas (Linux `master`, 28/09/2026)

`nbio_v2_3.c` (e as tags v5.3, v5.4, v5.10), `hdp_v5_0.c`, `amdgpu_hdp.c`, `amdgpu_vm_cpu.c`, `amdgpu_discovery.c`, `cyan_skillfish_ip_offset.h`, `navi10_ip_offset.h`, `nbio_2_3_offset.h`, `sdma_v5_0.c`, `gfx_v10_0.c`, `gmc_v10_0.c`. Todos de `raw.githubusercontent.com/torvalds/linux/<ref>/drivers/gpu/drm/amd/`.
