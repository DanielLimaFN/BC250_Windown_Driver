# 520 — T3.1: a mesma VRAM mapeada WC e UC ao mesmo tempo; critério da Corrida 167

> 28/09/2026, offline. Candidato **3.312.1.0** (inclui a HDP.1 do doc 519). Tarefa T3.1 do [plano](../PLANO_DE_TRABALHO_TRAVA_E_DESKTOP.md).
> **Nada desta faixa foi validado em hardware.**

## 1. Por que a T3.1 antes da T7

A Corrida 166 (doc 519 §6) tornou coerente o caminho "CPU escreve tabela de página, GPU lê" e a trava veio igual. Isso enfraqueceu o principal argumento da T7 (tabelas escritas pelo SDMA). A T3.1 é mais barata e ataca um mecanismo conhecido de trava de plataforma que se encaixa na assinatura medida: a máquina inteira para, sem BugCheck e sem WHEA, com uma CPU num acesso que não termina.

## 2. O que a análise achou

**Regra da CPU (P1, fontes externas):** Intel SDM vol. 3 (PAT) e AMD APM vol. 2 declaram **não suportado** mapear a mesma página física com tipos de memória diferentes. O comportamento é indefinido e pode derrubar o sistema. O WC ainda permite leitura especulativa.

**Regra do VidMm (P1, documentação da Microsoft):** as alocações `CpuVisible` sem `Cached` são mapeadas write-combined por padrão (`DXGK_ALLOCATIONINFOFLAGS_WDDM2_0`: *"by default, the allocation backing store is stored as write-combined memory"*). O nosso `CreateAllocation` só liga `CpuVisible`, então tudo que o VidMm mapeia no segmento é WC.

**Não documentado (H):** se o Windows repassa ao hardware o conflito de tipos em espaço de I/O (`MmMapIoSpace`/`DxgkCbMapMemory`) ou se o converte. Para páginas de RAM descritas por MDL, o tipo que já existia prevalece. A corrida vai discriminar.

**Mapa da VRAM vista pela CPU (BAR 0 = 256 MB em `0xC0000000`; P0 = medido na Corrida 166, `[DX67_VRAM_MAPA]` e `[DisplayInit]`):**

| Faixa (offset na BAR) | Dono | Mapeamento pela CPU até a 3.311 |
|---|---|---|
| `[0x0, 0xA8C000)` scanout do GOP | driver | **WC** (framebuffer) |
| `[0xA8C000, 0xFA00000)` segmento do VidMm | VidMm | **WC** (VidMm) e **WC** (framebuffer): mesmo tipo, sem conflito |
| `0xFA00000` migalhas DX.66 | driver | **UC** (`"Phys=0xCFA00000 UNCACHED"`), **dentro do WC de 256 MB** |
| `[0xFB00000, +4 MB)` papel 5 | driver | UC no boot (backing do GCVM) |
| `0xFF00000` anel e work do SDMA de apresentação | driver | **UC** (`MmMapIoSpace MmNonCached`), **dentro do WC** |
| `0xFF70000` anel gráfico | driver | **UC** (`RingMc=0xF40FF70000`), **dentro do WC** |
| `[0xFF90000, +128 KB)` papel 9, MEC | driver | **UC** (`DxgkCbMapMemory MmNonCached`), **dentro do WC** |
| `[0xFFBD000, …)` firmware do CP | driver | UC no boot |
| `0xFFFF000` página dummy da MMU | driver | — |
| BAR inteira | sonda de VRAM do StartDevice | **UC de 256 MB**, transitória, criada **depois** do WC (`Bc250DisplayInit` na linha 8128, sonda na 9604) |

**Conclusão (P1 + P0):**
- O aliasing é todo nosso e fica na faixa privada `[0xFA00000, 0x10000000)`, que o VidMm não usa.
- A origem é o comentário "256 MB cobre 100% do BAR 0" em `Bc250DisplayInit`.
- As estruturas mais quentes do driver estão nessa faixa: o anel do SDMA (escrito e sondado em todo laço de espera, por várias CPUs) e a página de migalhas (escrita a cada acesso MMIO do SDMA).

**Ressalva de tempo (H):** o aliasing existe o tempo todo, mas a trava só vem nos primeiros ~200 s. A partida é a fase de mais tráfego nessas estruturas, mas isso não explica sozinho por que as corridas limpas sobrevivem 600 s.

## 3. A mudança

1. **Fonte única** `driver/core/bc250_vram_cache_policy.h`:
   - `BC250_VRAM_WC_WINDOW_BYTES = 0xFA00000`: `[0, limite)` só WC; `[limite, 256 MB)` só UC;
   - `Bc250VramRangeIsPrivateUc()`;
   - `C_ASSERT` que amarra o limite à página de migalhas (`BC250_CRUMB_VRAM_OFFSET`).
2. **`Bc250DisplayInit`:** a janela WC passa de 256 MB para `0xFA00000`. Saíram o fallback `PAGE_NOCACHE`, que aliasaria o WC do VidMm, e o fallback `MmWriteCombined` redundante. Os leitores que usam a janela já conferem `FrameBufferSize` (planos de canário, provas de bytes, amostragem, leitura de tabela), então passam a **recusar** a faixa privada em vez de lê-la por um alias.
3. **Sonda de VRAM:** mapeia só as páginas que testa (64 bytes em `Offset`), não a BAR inteira.
4. **Medição:** todo mapeamento UC permanente declara `[T31_UC_MAP] <sítio>: VRAM [off,+len) UNCACHED privada=0/1` (SDMA de apresentação, anel gráfico online e bring-up, MQD programa e ativo, bloco do MEC, migalhas, ring work, PFP). `privada=0` sai com `SOBREPOE a janela WC`.
5. **Mapa de tipos no log:** `[T31_CACHE_MAPA] WC [0x0,0xFA00000)…` na init. Na publicação do segmento sai `…inteiro dentro da janela WC` ou `DIVERGENCIA`.

**Não tocados, com o motivo:**
- `bc250_gcvm_backing_transaction.c`: UC no boot, na faixa privada, arquivo sem logger;
- `bc250_compute_service.c`: serviço legado, fora do caminho do desktop;
- os mapeamentos WC do próprio VidMm: estão certos.

## 4. Provas offline

- Auditoria de segurança: **PASS**, com o gate novo `audit_t31_vram_no_wc_uc_alias`.
- **7 mutações, todas reprovaram**, e a auditoria voltou a PASS depois de restaurar (`scratchpad/mutate_t31.py`):
  - N1: janela volta a 256 MB;
  - N2: fallback UC volta;
  - N3: sonda volta a mapear a BAR inteira;
  - N4: amarração com as migalhas removida;
  - N5: anel gráfico perde a declaração UC;
  - N6: o helper deixa de conferir a sobreposição;
  - N7: limite da política alterado.
- Build de laboratório: EXIT 0 (`/W4 /WX` no driver; os avisos do log são de ferramentas de linha de comando que já existiam).
- Auditoria do pacote: **PASS**.
- Strings conferidas no SYS assinado: `T31_CACHE_MAPA] WC`, `T31_UC_MAP`, `SOBREPOE a janela WC`, os nomes dos sítios, `T31_CACHE_MAPA] DIVERGENCIA` e `HDP1_INIT`.
- `tools/prepare-corrida167-t31.ps1` (leitura): EXIT 0. Boot sem `DEBUG`.

## 5. Candidato

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `233795072B23A84B86AD056977C99717A4EFAD0D8C28F87551AA44D35164C72A` |
| `bc250umd.dll` (idêntica à 3.310) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `2DEC75A3824912935F871F55C4EEB79B45318CF1690AD5659458D9C43FABF9DC` |
| `bc250mcdm.cat` | `AFC40333174458FAA173397C70AE1D5B7ABB58BD2E5187872C499DF5723D8D8E` |

- DriverVer `3.312.1.0` (fonte `3.312.0.0`; o próximo sobe para `3.313`).
- Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, P1 PERSISTENT SUBMISSION.
- Runner: `tools/EXECUTAR_CORRIDA167_T31_600S.cmd`. Mesmo roteiro da 165/166.

## 6. Critério da Corrida 167 (escrito antes; Lei 10)

**Base (plano §3.5):** uma corrida limpa não prova nada; duas seguidas são o mínimo; uma trava diz "não basta".

**Controle de roteiro:** na 166 abriram `msedge` (`Claude.exe`), `msedgewebview2` e `SystemSettings.exe` antes da trava. Não abrir nada, e se possível impedir que o app do Claude abra sozinho no logon.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem trava **e** `[T31_CACHE_MAPA] WC [0x0,0xFA00000)` **e** segmento "inteiro dentro da janela WC" **e** todo `[T31_UC_MAP]` com `privada=1` **e** zero `SOBREPOE`/`DIVERGENCIA` **e** tripé e Present sem regressão (`IB EXECUTADO`, `BYTE_PROOF PASS` presente) | Primeira limpa; repetir **idêntico** (168). Duas: "a trava mudou". Três: o aliasing era o gatilho. |
| **B** | Trava antes de ~200 s com o mapa limpo (todos `privada=1`, zero `SOBREPOE`) | O aliasing WC/UC não é o gatilho, ou o Windows já o convertia. Manter a correção (a regra da CPU vale de qualquer forma) e seguir para T3.2 (semáforo do ENG17) e T2. |
| **C** | Sem trava, mas com regressão por leitor que precisava da faixa privada pela janela WC: provas de dados passam a `Usable=0`/INCONCLUSIVO onde passavam, linhas `[PT_READ]` aparecem, desktop preto sem trava, ou falha de Present | A janela ficou curta para algum leitor legítimo. Localizar o leitor pelo log antes de repetir. |
| **D** | Algum `[T31_UC_MAP] … privada=0 … SOBREPOE`, ou `[T31_CACHE_MAPA] DIVERGENCIA` | A correção está incompleta (há outro UC dentro do WC). A corrida não julga a hipótese; corrigir e repetir. |
| **E** | Fora do mapa | Descrever antes de julgar. |

- **Desfecho parcial não conta:** 600 s sem as linhas `[T31_*]` não é A.
- **Informação nova em qualquer desfecho:** o mapa de tipos de cache de todos os mapeamentos UC do driver, medido.
- **O que perguntar ao Daniel:** se o desktop e o menu Iniciar apareceram, e o que abriu na tela.

## 7. Resultado da Corrida 167 (29/09/2026, bind 08:08:57, pacote 3.312.1.0)

**Evidência:** `docs/evidence/2026-09-29_08-08-57-DESKTOP-HQAG6T4-lab-binding/display-log-this-bind-RECUPERADO.txt`, 9.807.594 bytes, SHA-256 `BE017B29…5E56A0`. Evento 41 às 08:10:32.

**Trava em 61,8 s**, com a mesma assinatura: última migalha `SDMA_REG_READ 0xFC4` concluída (CPU00, seq 179.207).

| Sinal | Medido |
|---|---|
| Janela WC | `[DisplayInit] Size=262144000` (`0xFA00000`); `[T31_CACHE_MAPA] WC [0x0,0xFA00000)` |
| Segmento | `[0xA8C000,0xFA00000)`, inteiro dentro da janela WC |
| UC permanentes, todos `privada=1` | SDMA `[0xFF00000,+0x2000)`; MQD programa `[0xFFAB000,+0x12000)`; MQD ativo e bloco do MEC `[0xFF90000,+0x20000)`; anel gráfico `[0xFF70000,+0x12000)`; migalhas `[0xFA00000,+0x1000)` |
| UC que sobrepôs | **`PFP_DIRECT_LOAD [0xF8F0000,+0x110000)` `privada=0`**, na carga de firmware do StartDevice (linha 77 do log). É desfeito e a VRAM liberada no fim da própria carga (`bc250_pfp_direct_load.c:1464` e `:1485`), antes da publicação do segmento (linha 316) e do primeiro Present (linha 5.743) |
| Regressão | Nenhuma: 2.935 `IB EXECUTADO`, 1.646 `TRIPE_PROVADO`, 279 Presents, 5 `BYTE_PROOF PASS`; zero `INCONCLUSIVO`, `Usable=0`, `[PT_READ]`, timeout, fault, TDR e `DEVICE_REMOVED` |

**Veredito pela letra do critério: D**, porque houve um `privada=0`. A corrida não julga a hipótese inteira.

**O que ela julga, sem torcer a assinatura (P0):** no intervalo em que a trava acontece (depois do desktop), **nenhum mapeamento UC vivo se sobrepunha à janela WC**, e a máquina travou igual (61,8 s contra 45,9 s na 165 e 49,6 s na 166). O aliasing WC/UC permanente **não é suficiente** para a trava (n=1).

O aliasing transitório do PFP no boot continua existindo. Fica como correção pendente de baixa prioridade: mapear a faixa do PFP só por fora da janela WC, ou mapeá-la WC.

**Recuperação:** Código 43 (`CM_PROB_FAILED_POST_START`), fusível TRIPPED (Boots=2, `0xC0000001`). Antes de outro bind: `BC250_RECOVERY.cmd`, `-Reset` e boot frio.

**Regularidade nova (H, estatística fraca):** nas **9 travas** de 28 e 29/09 com log recuperado, o último `[DX91_PAGING_MOVE]` antes do fim do log é **`op=FILL`** (tamanhos de 64 KB a 12 MB), seguido de 5 a 13 linhas de FLUSH_TLB e IB até o corte. A proporção de FILL entre os movimentos de cada corrida vai de 55% a 95%, e a chance de 9 em 9 por acaso é de ~4%.
- **Ressalva:** o fim do log não é o fim da máquina (o anel em RAM perde a cauda).
- **Pista para a próxima bissecção:** o FILL de paginação (`Bc250McdmBuildFillVirtual`, CONST_FILL do SDMA), que é da própria partida, quando a VRAM enche pela primeira vez.

## 8. Exame do FILL (29/09, offline)

**Código, sem defeito achado (P1):**
- `Bc250SdmaEmitConstFill` tem o mesmo formato de `sdma_v5_0_emit_fill_buffer`: cabeçalho CONST_FILL, endereço de 64 bits, valor e contagem em bytes menos 1. Usa `fill_size=2` (dword), como o Mesa.
- O teto por pacote é `0x3FFFFC` (Mesa SDMA<6), abaixo do `fill_max_bytes=0x400000` do Linux.
- `Bc250McdmBuildFillVirtual` fatia no teto, confere o alinhamento, faz multipasse legal e acrescenta NOPs até 8 dwords.
- O codificador de PTE: VRAM leva o endereço de DRAM do segmento; sistema leva `SYSTEM|SNOOPED`.
- Assimetria registrada, sem prova de efeito: a TRANSFER é fatiada em 256 KiB, e o FILL em ~4 MiB. O doc 501 já refutou o tamanho do FILL como causa das paradas de SDMA de 151 e 153 (a causa era a troca do VMID8, corrigida na DX.95).

**Evidência das 9 travas:**
- O último movimento é sempre FILL, e não há `IB EXECUTADO` depois dele. Em 5 das 9, o último IB submetido tem exatamente o tamanho do IB desse FILL (32, 64 ou 96 bytes); nas outras 4, o log acaba antes.
- **Ressalva forte:** na 167, o log termina ~1,2 ms depois do FILL, que é da ordem do tempo de execução de um FILL de 11 MB. A falta de `IB EXECUTADO` pode ser só a cauda perdida do log.

**Rastro durável DX.56 da 167:** `paging-trace-this-bind.bin`, extraído do offset 195.125.552. O último registro da corrida (tick 566) é ~0,19 s anterior ao fim do log de texto e mostra a paginação **ociosa** (`active_count=0`, FLUSH_TLB concluído).

**Migalhas da 167 (P0, fato novo):**
- **Todos** os slots vivos estão em fase DEPOIS: nenhuma CPU estava dentro de acesso MMIO, DDI de paginação ou submissão.
- A última operação da máquina é uma leitura concluída de `0xFC4` (invalidação do HDP, no laço que espera a conclusão do SDMA).
- A leitura "uma CPU presa num acesso que não termina" (plano §2.2) **não vale para a 167**. O quadro é o de uma parada da plataforma **sem** um acesso da CPU pendente, enquanto a CPU só esperava o SDMA.
- Isso casa com a VRAM decaindo durante a trava (doc 506): o controlador de memória parou.

**Hipótese que sobra (H):** a parada é do subsistema de memória (DRAM compartilhada entre CPU e GPU), com a GPU escrevendo em rajada. Candidatos: transição de energia do fabric ou da memória durante tráfego da GPU (T6), ou uma escrita da GPU para destino inválido. Nenhum dos dois é provado pelo log.

**Próximo passo proposto:**
1. Instrumentação passiva: uma migalha "IB em voo" por nó (tipo FILL/TRANSFER/outro, bytes, fence, destino, QPC do submit), zerada na aposentadoria. Assim a próxima trava diz se havia FILL executando.
2. Na mesma corrida, a T6 (C-states da CPU desligados, feita pelo Daniel), que não muda código.
