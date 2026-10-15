# Doc 753 — Corrida 294: o cache de pipelines funciona, o menu recria o device a cada abertura e a Pesquisa nasceu antes do bind

> **Data:** 14/10/2026, bind às 10:53:05, rollback online (`RECOVERY_OK_ONLINE`).
> **Candidato:** lab **3.448.1.0** (PSO.1 + SHD.1), docs [751](751_CORRIDA293B_DESKTOP_INTEIRO_SEM_COPIA_PSO1_CACHE_DE_PIPELINES_PACOTE_3447_E_CRITERIO_CORRIDA294.md) e [752](752_SHD1_TRADUTOR_DE_SHADER_NO_OPENADAPTER_PESQUISA_WEBVIEW2_ERR577_PACOTE_3448.md).
> **Evidência:** `docs/evidence/2026-10-14_10-53-05-DESKTOP-HQAG6T4-lab-binding/`.
> **Observado pelo Daniel:** as primeiras aberturas do menu lentas; reaberto alguns segundos depois, ainda "meio lento"; a Pesquisa lenta e com pequenos bugs; uma piscada às 10:55, que ele atribui ao teste 3D.

## 1. Estabilidade (P0)

- Zero TDR, fault ou BugCheck; rollback online.
- `COMPOSITE_BLT OK VIA_VA` = 0: o zero cópia continua em efeito.
- PISC.1: 224 erros de contagem antigos e **1** latch tardio real (o 1º flip, 49 ms).
- OTG a 60,001 Hz, 739 VSYNC perdidos (4,2%).
- Journal com 278 `NO_SPACE` (eram 56 na 293b).

## 2. PSO.1: funciona, e o ganho no menu é pequeno (P0)

- **DWM:** `[PSO1] cache de pipelines ATIVO` (arquivo novo, 0 bytes na 1ª vez).
- **Menu:** carrega e grava o próprio arquivo na pasta temporária do pacote. Da 2ª abertura em diante, `6868 bytes carregados`.
- **Pesquisa (SearchApp):** chega a ~43 KB.
- **Defeito meu:** o arquivo é regravado a cada 8 pipelines mesmo sem nada novo (39.664 bytes repetidos várias vezes). Gasta escrita em disco à toa. Correção: gravar só quando o tamanho do cache mudar.

## 3. Menu: recria o device inteiro a cada abertura (P0) e o custo não tem dono único

- O StartMenuExperienceHost faz `DestroyDevice` ao fechar, de forma limpa e sem erro antes, e cria o device de novo ao abrir. Foram 5 ciclos na corrida.
- Cada ciclo recria a instância e o device Vulkan, ~63 alocações (`Allocate` + `MapAllocation` + `MakeResident`), ~57 uploads de textura e ~136 draws.
- **Do `CreateDevice` ao 1º Present:**

| Abertura | Tempo | Cache |
|---|---|---|
| 1ª | 1,49 s | vazio |
| 2ª | 1,39 s | 6,8 KB |

- Com cache, o ganho foi de ~0,1 s: a compilação de shader **não** é o custo principal.
- O tempo está espalhado entre draws, uploads e alocações, sem um bloco dominante. O carimbo de tempo do log tem resolução de ~15 ms, então não dá para atribuir com precisão.
- **Próximo:** MENU.1, medição por fase com QPC (Vulkan, alocação+residência, upload, espera de paginação, até o 1º Present) numa linha por device. Depois disso, atacar a maior fase.
- Se é normal o menu soltar o device ao fechar no Windows 10 com outros drivers é **H**.

## 4. Pesquisa: o WebView2 nasceu antes do bind (P0 pelos horários)

- Pasta `EBWebView` da Pesquisa: `lockfile`, `Last Version` e `chrome_debug.log` às **10:36:56**. Isso é antes do bind (10:53:05), com o driver básico da Microsoft.
- **Nenhum `msedgewebview2` carregou a nossa UMD nesta corrida.** Na 293b eram dois. O Chromium inicializa o processo de GPU no adaptador que existe quando nasce e não troca depois.
- A Pesquisa rodou a corrida inteira **fora da nossa GPU** (no adaptador básico, H: por software). A lentidão e os "pequenos bugs" desta corrida não medem o nosso driver.
- É um artefato do roteiro de laboratório: o driver entra depois do boot. Com o driver instalado no boot isso não ocorre.
- A SHD.1 está provada só no SearchApp e no DWM (`SHD1 pre-carga … ARMADO`). No WebView2 ela ainda é **NÃO VALIDADA**.
- **Log do Chromium:** foi armado (variável presente), mas o `chrome_debug.log` ficou com **0 bytes**; dentro do AppContainer ele não grava. Desarmar com `tools\armar-log-webview2.ps1 -Desarmar`.
- **Próximo:** depois do bind, reiniciar a Pesquisa (encerrar `SearchApp.exe` e os `msedgewebview2` filhos) para o WebView2 nascer na nossa GPU. Só então avaliar a SHD.1.

## 5. Piscada das 10:55 (H)

A T3 começou às 10:55:25 e fez 12 ciclos. O único latch tardio foi o 1º flip, e não houve TDR. A piscada coincide com o início da T3, cujos processos abrem e fecham janelas. Isso é compatível com o que o Daniel supôs, mas não está provado.

## 6. Veredito

**Parcial:**
- PSO.1 funciona (P0), mas não resolve a abertura do menu;
- SHD.1 não pôde ser avaliada na Pesquisa por causa do roteiro;
- desktop e estabilidade mantidos.

**NÃO VALIDADO EM HARDWARE:** SHD.1 no WebView2 e a causa da lentidão do menu.
