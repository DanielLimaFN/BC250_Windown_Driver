# Doc 754 — PAG.1 (espera de paginação adiada), MENU.1, PSO.1b e Pesquisa reiniciada depois do bind: pacote 3.449.1.0 e critério da Corrida 295

> **Data:** 14/10/2026, offline. Fonte: Corrida 294 ([doc 753](753_CORRIDA294_CACHE_DE_PIPELINES_FUNCIONA_MENU_RECRIA_O_DEVICE_E_PESQUISA_NASCEU_ANTES_DO_BIND.md)).
> **Resultado:** Corrida 295 no [doc 755](755_CORRIDA295_PAG1_NAO_ERA_O_GARGALO_PESQUISA_NA_NOSSA_GPU_E_CHECKLIST_PARA_O_PROXIMO_FOCO.md) (parcial: PAG.1 ok, mas nao era o gargalo).
> **Pedido do Daniel:** passos 1 a 4 da checklist da 294 na mesma corrida (Pesquisa depois do bind, MENU.1, atacar a fase mais cara, PSO.1b).

## 1. Onde o menu gasta o tempo: mineração da 294 (P0)

O log do menu da 294 já trazia QPC (10 MHz) nas linhas `BC250_HANG` (`CREATE_LOCK_SHARED`, `WAIT_PAGING`). Somando por abertura, do `CreateDevice` ao 1º `Present` (`StartMenuExperienceHost`, pid 7624):

| Abertura | Até o 1º Present | Dentro do CreateResource | Espera de paginação na CPU | Recursos |
|---|---|---|---|---|
| 10:53:23 | 1,49 s | 0,87 s | 0,35 s (16) | 15 |
| 10:53:54 | 1,38 s | 0,98 s | 0,43 s (56) | 55 |
| 10:54:02 | 2,96 s | 1,65 s | 0,33 s (57) | 56 |
| 10:54:54 | 1,61 s | 1,09 s | 0,47 s (59) | 58 |
| 10:55:32 | 2,38 s | 1,27 s | 0,47 s (57) | 56 |

- A ponte Vulkan (`vkCreateInstance` até o `CONCLUIDO`) custa ~92 ms: **não** é o problema.
- **60–70% do tempo está dentro do CreateResource.** Cada recurso faz:
  - a nossa alocação WDDM (`Allocate` + `MapAllocation` + `MakeResident`) e **espera a paginação na CPU** (5–8 ms);
  - depois a memória Vulkan da RADV, ~7 ms. A RADV faz outra alocação WDDM e **espera outra vez** (`radv_wddm2_bo.c`, `WaitForSynchronizationObjectFromCpu (paging)`); P1 pelo código, porque o log da RADV não sai em AppContainer.
- O resto (~0,4–1,3 s) está fora do CreateResource: uploads, draws e o próprio app. A resolução de 15 ms do log não separava isso; a MENU.1 separa.

## 2. O que muda (UMD apenas; KMD sem mudança de código)

### PAG.1: espera de paginação adiada (passo 3)

**O que muda no CreateResource:**
- Recurso com memória Vulkan própria (`FRENTE_B NAO`, buffers L1) não espera mais a paginação da alocação WDDM na CPU.
- A decisão vem de uma política única, `Bc250UmdPag1MayDefer`. Primária e recurso compartilhado esperam como antes.

**Quem espera antes de tocar na alocação** (a espera é instantânea se a cerca já passou):
1. alias de memória (ZC.1, `FRENTE_B SIM`), logo depois de criar o recurso Vulkan e antes de qualquer submissão da RADV, que não conhece a nossa cerca de paginação;
2. `Bc250WinsysUploadAllocation` e `Bc250WinsysReadAllocation` (Lock pela CPU);
3. o `Present` e o `Present1`, para a origem e o destino;
4. a composição pelo KMD (`BltViaKmd`), para a origem e o destino.

**O que não muda:** o que a GPU faz. Só a CPU deixa de esperar.

**Ganho esperado (H):** 0,3–0,47 s por abertura do menu. A espera da própria RADV continua: se o ganho for pequeno, ela é o próximo alvo.

**Log:**
- `[PAG1] espera de paginacao feita depois (motivo)` nas 8 primeiras esperas que bloquearam de fato;
- `[PAG1] device=… adiadas=N, feitas depois=M` no teardown.

### MENU.1: tempo por fase (passo 2)

Uma linha `[MENU1]` por device, no 1º Present ou, se ele não vier, no teardown. Os tempos são por QPC:
- total;
- init, com a ponte Vulkan à parte;
- CreateResource (n, ms), com, dentro dele:
  - a espera de paginação (n, ms, adiadas);
  - a criação Vulkan (n, ms);
- uploads de textura (n, ms);
- submissões da ponte (n, ms);
- draws;
- "fora das fases".

### PSO.1b: o cache só grava quando muda (passo 4)

O cache não é regravado quando o tamanho é igual ao da última gravação ou ao que veio do disco. A RADV só faz o cache crescer com pipeline novo. Na destruição do device sai uma linha com quantas gravações foram evitadas.

### Pesquisa depois do bind (passo 1)

`tools/reiniciar-pesquisa-apos-bind.ps1` roda numa janela minimizada, aberta pelo EXECUTAR antes do bind:
1. espera a BC-250 em `bc250mcdm`/OK (teto de 240 s);
2. espera mais 20 s;
3. encerra `SearchApp.exe` e os `msedgewebview2.exe` do pacote da Pesquisa;
4. registra quem voltou.

O log fica em `C:\BC250_PESQUISA_REINICIO.log`, copiado para `pesquisa-reinicio.log` na evidência. A coleta do log do Chromium saiu do roteiro (0 bytes na 294). Desarmar com `tools\armar-log-webview2.ps1 -Desarmar`.

## 3. Prova offline

- `build-umd` EXIT=0, com todas as suítes da UMD passando.
- Auditoria de segurança PASS, com 9 portões novos (PAG.1 ×7, PSO.1b, MENU.1).
- Mutações verificadas, cada uma reprovando no portão certo:
  - tirar `ZeroCopyTarget` da espera do alias;
  - deixar a primária adiar;
  - desligar o pulo do PSO.1b.
- Os portões antigos que olham o corpo de `Bc250Umd10CreateResource`, `Bc250UmdVkUploadImageData` e `Bc250UmdVkFlushCommands` passaram a apontar para os corpos (`…Body`). Os wrappers só medem o tempo.
- O F3 continua exigindo a espera no caminho que não adia (`wsStatus = Bc250UmdMenu1WaitPaging(...)`) e proíbe descartar o resultado.
- Pacote e auditoria do pacote PASS. Strings `[MENU1]`, `[PAG1]`, `evitadas` e `SHD1 pre-carga` conferidas na UMD empacotada.

**Lab 3.449.1.0:**
- SYS `D7DE13EA85DB47297A2ABD72A281413CFF3273B87E6EA28ABBE3BF9AAA3A425B`;
- UMD `76DA7661AB75AF620905EB6DFC77C59D166208B522C2F59A8A15D35B1ED40C52`;
- INF `8C4B36D9…`;
- CAT `1B5782AB…`;
- RADV `B4AE2E14` igual.

O SYS muda só pela versão. Não há mudança de código no KMD.

**Roteiro:** `tools/EXECUTAR_CORRIDA295_PAG1_MENU1_PESQUISA_300S.cmd`. Antes, desligar e ligar.

## 4. Critério da Corrida 295

| Desfecho | Assinatura (o log mostra) | Implicação |
|---|---|---|
| **A** | No menu, `[MENU1]` com `adiadas PAG1` ≥ 40 e WaitPaging n ≤ 10 por abertura; CreateResource ≥ 30% menor que na 294 (0,87–1,65 s); Daniel acha o menu mais rápido; zero TDR/fault; nenhuma textura errada | PAG.1 validada; o próximo alvo é a maior fase que sobrar na `[MENU1]` |
| **B** | PAG.1 adiando (`adiadas` alto) e o CreateResource quase igual; `Vk n=… ms` domina | O custo é a alocação da RADV, com a espera dela: adiar também do lado da RADV (`radv_wddm2_bo.c`) |
| **C** | Textura errada, lixo, piscada nova, TDR ou fault de página depois da PAG.1; `[PAG1] espera … feita depois` mostra quem esperou tarde | Algum consumidor tocou a alocação antes da paginação: achar pelo motivo do `[PAG1]` e cobrir, ou voltar a esperar nesse tipo de recurso |
| **D1** | `pesquisa-reinicio.log` com `FEITO`; log da UMD de um `msedgewebview2` com `SHD1 pre-carga … ARMADO` e shaders com `VkModule` ≠ 0 | SHD.1 validada no WebView2; a Pesquisa passa a ser medida no nosso driver |
| **D2** | O mesmo, mas `SHD1 … INDISPONIVEL` com err=577 | A recusa vale desde o início do processo: assinar `libgcc_s_seh-1.dll`/`libwinpthread-1.dll` ou compilar o vkd3d sem MinGW |
| **D3** | `FEITO`, mas nenhum `msedgewebview2` carrega a UMD | O Chromium não escolhe a nossa GPU por outro motivo (blocklist, nível de recurso): ver o `chrome://gpu` |
| **E** | Algo fora do mapa | Descrever antes de julgar |

A e B tratam do menu; D1–D3 tratam da Pesquisa. Os dois grupos são independentes.

**NÃO VALIDADO EM HARDWARE.**
