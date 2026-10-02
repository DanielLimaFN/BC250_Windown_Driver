# PLANO — DESKTOP ACELERADO E ESTÁVEL

> **Reescrito em 30/09/2026**, depois da Corrida 186. A versão anterior (parada em 21/09, Corrida 58) está no git: `git show b125b1a:PLANO_DESKTOP_ACELERADO.md`.
> **Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3) · Windows 10 22H2 x64 · display herdado do GOP.
> **Escopo:** o Windows compõe o desktop (DWM, Explorer, menu Iniciar) **pela GPU da BC-250**, de forma estável, sobrevivendo a TDR. **Fora do escopo:** jogos, D3D12 e DCN (modo, refresh, hotplug), que entram depois da fase D5.
> **Governança:** [`CLAUDE.md`](CLAUDE.md) (Leis 1–10) e [`DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md`](DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md). Fonte de verdade por corrida: [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md).

---

## 1. Onde estamos (30/09, depois da Corrida 186)

| Fase | O que é | Estado | Evidência |
|---|---|---|---|
| **D0** | O anel gráfico executa | ✅ na prática · 🟡 formal | O que o DWM desenha chega à tela (grade 86.400/86.400 na 181, BYTE_PROOF PASS). A prova formal do D0.7 (canários byte a byte no destino do IB gráfico) nunca rodou; a X4 retirou os canários do caminho de execução |
| **DX** | Tradução de memória nos VMIDs de contexto | ✅ | Zero `GFXHUB_FAULT` e zero TDR nas Corridas 182–186, com milhares de draws |
| **D1** | Primeiro pixel pela GPU | ✅ | Desktop visível desenhado pela GPU desde a Corrida 134, com imagem anterior ao rollback |
| **D2** | Compilador de shader | 🟡 | Os shaders do DWM e do menu traduzem (184). O tradutor ainda é carregado de fora (`C:\BC250_LIB\libvkd3d-shader-1.dll`); D2.2/D2.5 abertos |
| **D3** | Cobertura D3D11 do DWM | 🟡 avançado | dwm.exe sem crash por 600 s nas Corridas 183–185 (~3.700 draws, 1–2 recusas). O Explorer ainda tem draws recusados (`draw PM4 ausente`). O inventário formal do que o DWM usa (D3.1) nunca foi feito |
| **D4** | Desktop acelerado | 🟡 | Present por GPU, CPU fora do caminho. Menu Iniciar instável: ícones na 184, ausentes na 185, T4.6 não testada (a 186 travou antes; na 188 o menu não foi aberto). Texturas perdidas por tabela de objetos do KMD cheia (P0, 188). ~12–16 FPS com a tela animando |
| **D5** | Desktop estável | 🟡 | 600 s sem trava nas Corridas 182–185; TDR real recuperado com o desktop vivo na 153. **Trava dura** na 181 e na 186 |

**Candidato atual em disco:** **Frente B**, pacote **3.335.1.0** (SYS `8000EE83…`, UMD `8D2FD05B…`, RADV `5447A17D…`), para a **Corrida 190 abrindo o menu**. Critério no [doc 547](docs/547_FRENTE_B_VKIMAGE_SOBRE_A_PROPRIA_ALOCACAO_E_CRITERIO_CORRIDA190.md) §4. A 189 travou depois de o DWM recriar o device por falta de memória ([doc 546 §6.3](docs/546_CORRIDA188_TABELA_DE_OBJETOS_CHEIA_E_CANDIDATO189_T47_I1C.md)).

---

## 2. Os três problemas que sobram

### P-1. Trava dura intermitente, abaixo do Windows

- **Frequência:** 2 em 13 corridas desde a T3.3 (Corrida 173). Antes dela, quase todas travavam (docs 526/528).
- **Assinatura:** um FILL de paginação submetido ao SDMA e **sem conclusão**; nenhum TDR, BugCheck ou timeout. Nas duas últimas, o gatilho foi o pico de alocações ao abrir o menu (186) ou ao recriar as primárias (181).
- **O que já se sabe (docs 505–507):**
  - em 48 min travada, a máquina não gerou `0x133`: nenhum processador atendia o relógio;
  - a VRAM perde conteúdo durante a trava;
  - conclusão: **o controlador de memória para** (H forte).
- **Suspeito natural:** troca de relógio ou energia da memória e do fabric, comandada pelo SMU/PSP, sob a nossa carga. O nosso driver **não manda mensagem ao SMU**.
- **Nenhuma faixa de driver D3D resolve isso.** Sem separar "placa" de "nosso driver", cada corrida pode morrer por essa trava e não dizer nada sobre a faixa em teste.

### P-2. Superfícies compartilhadas copiadas, não compartilhadas

- O dwm.exe cria uma imagem Vulkan própria e **copia** a superfície que outro processo (o menu) desenhou. No Linux, quem importa um buffer lê a **mesma memória**.
- Essa arquitetura gerou em sequência os defeitos das T4.0–T4.6: VA lida da struct errada, use-after-free no staging e no binding, cópia congelada antes do conteúdo, recopia cara por quadro.
- A T4.6 (recopia a cada quadro) é **paliativo declarado**, não correção.

### P-3. Custo por quadro

- Flushes síncronos na destruição de recursos, shaders e RTVs e no `ResourceMap DISCARD` (~3 por quadro no DWM).
- Log gravado direto no disco a cada linha (`FILE_FLAG_WRITE_THROUGH`), escolhido para sobreviver às travas.
- Pipelines e despejo de código de pipeline repetidos a cada abertura do menu (o menu recarrega a DLL e o device).
- BYTE_PROOF de ~6,5 s nos quadros 1, 5 e 10 de cada device (constante, não é causa de queda).

---

## 3. Mudança de método: menos corridas, mais bancada

**Diagnóstico da última semana:** 6 dos 7 defeitos das faixas T4.0–T4.6 eram **lógica da nossa UMD** e teriam aparecido sem hardware. Só a trava dura exige a placa. A partir daqui, **corrida é confirmação, não descoberta.**

Frentes, em ordem:

### Frente W — A trava pelo Windows, sem Linux (PRIMEIRA desde 30/09)

> O Linux não vai estar disponível tão cedo (Daniel, 30/09). A Frente L fica **adiada**, com o script pronto. A pergunta dela ("placa ou nosso driver?") passa a ser atacada por três caminhos que só usam o Windows.

**O que já foi descartado com medição** (não repetir): C-states desligados pelo `powercfg` (T6, a 168 travou em 54,9 s); FILL em pedaços de 256 KiB (X2A); leitura de tabelas pela CPU; toggle de `HDP_MISC`. Fato que manda: **10 de 10 travas terminam num FILL de paginação sem conclusão**, e a taxa de travas acompanha a taxa de paginação (doc 505 §3).

| # | O que é | Quem faz | Custa corrida? | O que responde |
|---|---|---|---|---|
| **W1** | **Plataforma sozinha.** Com o BasicDisplay (sem o nosso driver), 30–60 min de estresse de CPU e memória com verificação de dados, mais registro da temperatura do die. A BC-250 é APU: a CPU usa **o mesmo controlador de GDDR6** que trava. Ferramenta nossa (`tools/bc250-platform-stress.exe`, a escrever: multithread, padrão verificado, batimento com `FlushFileBuffers` a cada 2 s) e, para temperatura, HWiNFO com **sensores de GPU desligados** (download e decisão do Daniel). Conferir também a fonte (modelo, watts) e se o ventilador sopra no dissipador | Eu escrevo; o Daniel roda | **Não** (BasicDisplay) | Trava ou erro de dados → **plataforma** (fonte, temperatura, memória), equivalente ao L-A. Estável → a trava precisa da GPU paginando |
| **W2** | **Reprodutor rápido da trava.** Harness que, com o nosso driver e **sem DWM**, gira alocação e liberação de VRAM em ritmo controlado para forçar FILLs de paginação. Meta: transformar 2 travas em 13 corridas em uma trava em poucos minutos. Com isso, cada hipótese vira um ensaio curto e decisivo | Eu escrevo; roda dentro de uma corrida (watchdog) | Sim, mas curtas | Reproduz → temos bancada; variar tamanho, ritmo e caminho do FILL uma variável por vez. Não reproduz → a trava precisa de algo além do FILL (concorrência com GFX, Present, DWM) |
| **W3** | **Diferença contra o amdgpu por leitura de fonte (P1).** Tabela de tudo que o amdgpu programa no caminho do SDMA e da paginação na Cyan Skillfish (`sdma_v5_0.c`, `gmc_v10_0.c`, `gfxhub_v2_0.c`, `mmhub_v2_0.c`, `amdgpu_ttm.c` para o fill, `cyan_skillfish_ppt.c`), e o que nós programamos. Mais um retrato **somente leitura** dos registradores catalogados logo depois do nosso init, para comparar com o que o Linux escreveria | Eu | Não (o retrato vai de carona na próxima corrida) | Substitui a comparação de registradores da L-B. Cada diferença vira um candidato nomeado para o W2 testar |
| **W4** | Frente A (tomada inteligente) | Daniel compra; eu escrevo o script | — | Com o W2, deixa rodar vários ensaios curtos sem o Daniel apertar reset |

**Ordem:** W3 e o código do W2 agora, offline, enquanto a Corrida 189 roda como planejada; W1 quando o Daniel tiver 1 hora sem corrida; W2 no hardware depois do W1 (se a plataforma travar sozinha, o W2 perde o sentido).

**Critério da frente:** a trava passa a ter **um reprodutor** (W2) ou **uma causa de plataforma** (W1). Sem um dos dois, nenhuma faixa de driver mexe na trava.

### Frente L — Linux como bancada de referência (ADIADA em 30/09; script pronto)

**Pergunta única:** a trava dura acontece na mesma placa com o driver oficial (amdgpu + Mesa)?

| Desfecho | O que significa | Próximo passo |
|---|---|---|
| **L-A** | Linux trava do mesmo jeito sob carga equivalente | A causa é placa, BIOS, energia ou memória, **não** o nosso código. Paramos de caçá-la no driver; investigamos BIOS e relógios |
| **L-B** | Linux roda estável por 10 min ou mais sob a mesma carga | O amdgpu configura algo que nós não configuramos. Comparar relógios, estados de energia e registradores do Linux com os nossos |
| **L-C** | O Linux não sobe com aceleração (usa `llvmpipe`) ou não reconhece a placa | Teste inválido; trocar a distribuição ou o kernel antes de concluir qualquer coisa |
| **L-E** | Algo fora do mapa | Descrever antes de julgar |

Roteiro de execução no §4. Não mexe no disco do Windows e não gasta corrida do driver.

### Frente R — Inventário e reprodutor (replay) offline

1. **R1 — Inventário do DWM e do menu (é o D3.1 do plano antigo).** ✅ **Feito em 30/09: [doc 545](docs/545_INVENTARIO_UMD_DWM_E_SHELL_CORRIDAS_181_186.md)**, ferramenta `tools/umd_inventory.py`.
   - O DWM pede pouco e fixo (SM 4.0, dois formatos, grades de estado, zero stencil) e está coberto.
   - O problema está em **memória de vídeo**: 1.124 `OUT_OF_DEVICE_MEMORY` no Explorer com pedidos de 9 KB. **Medido na Corrida 188 (P0, [doc 546](docs/546_CORRIDA188_TABELA_DE_OBJETOS_CHEIA_E_CANDIDATO189_T47_I1C.md)):** a etapa é `CreateAllocation2`, e o KMD recusa com `ObjStatus=3` (tabela de objetos de 4.096 cheia; a RADV pede 64 KiB e cada textura ocupa dois objetos). T4.7 subiu a capacidade para 16.384 como paliativo.
   - E em **criação de device**: 528 `vkCreateDevice -3` na Pesquisa.
   - Nos dois casos o motivo estava perdido no stderr da RADV. A **faixa I.1** o leva para `BC250_UMD_LOG_<pid>-RADV.txt` (validado na 188 em processos comuns). Em AppContainer o redirecionamento derrubava o processo e foi desligado (I.1c); o motivo da Pesquisa continua sem canal.
2. **R2 — Reprodutor.** Programa de teste que lê a sequência gravada e chama a nossa UMD na mesma ordem, com um KMD simulado. Inclui recarga da DLL e dois processos compartilhando superfície (produtor e DWM).
3. **R3 — Verificador de heap.** O reprodutor roda com *page heap* (Application Verifier); use-after-free estoura com a linha exata.
4. **R4 — Asserções de custo e ordem:** flushes por draw, cópias por quadro, tokens repetidos, cópia antes do conteúdo.
5. **R5 — Pixels offline (depois).** Vulkan de software (`lavapipe`, da árvore Mesa local) **apenas no programa de teste**, para conferir se o que a UMD manda desenhar está certo. Nunca no produto (Lei 4) e nunca como prova de aceleração.

**Critério da frente:** o reprodutor roda as sequências das Corridas 185 e 186 contra a UMD atual (T4.6) e **acusa ou libera** a T4.6 antes de ela voltar ao hardware.

### Frente E — Gabarito do Windows (ETW com o BasicDisplay)

- O DWM roda nesta máquina sem o nosso driver. Um rastreamento curto (WPR/ETW, perfis de GPU e DXGI), abrindo o menu, mostra **como** o menu cria swapchains de composição, compartilha superfícies e apresenta.
- É observação e documentação; **sem** desmontar o `dwm.exe` (licença do Windows). Símbolos públicos da Microsoft só para nomear pilhas.
- O inventário que manda é o do nosso driver (R1); o ETW é o gabarito de como deveria ser.

### Frente B — Compartilhamento de verdade (correção estrutural do P-2) — EM ANDAMENTO desde 30/09 (doc 547, candidato 3.335.1.0)

- A imagem Vulkan do DWM passa a **ler a própria alocação** do processo produtor, sem cópia, como o Linux faz com BO importado.
- Primeiro passo offline: ler como o winsys WDDM2 da nossa RADV (`mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_bo.c`) importa alocação por handle, e se dá para a imagem do DWM apontar para a memória aberta.
- Depende de R2 para ser testada offline antes do hardware.

### Frente A — Automação da corrida

- Hoje o Daniel precisa estar presente para o boot frio e para o reset quando trava.
- **Tomada inteligente** controlada por script, mais a opção da BIOS "ligar ao voltar a energia" (**a confirmar na BIOS P3.00**): desligar e religar a tomada é boot frio de verdade, e o script faz isso sozinho quando o batimento da máquina parar.
- **Captura de vídeo** (placa de captura noutra máquina) substitui o "apareceram os ícones?". Ler o scanout pela CPU **não**: as Corridas 170–172 mostraram que ler VRAM pela CPU no meio da carga é arriscado.

### Plano B registrado, não ativo

- **UMD D3D10/11 do Mesa (`d3d10umd`) + `zink` sobre a nossa RADV.** Existe na árvore local, cobre `D3D11_0_7`, mas **hoje só roda em software** (llvmpipe, apresentação por GDI; `src/gallium/frontends/d3d10umd/README.md`). Exigiria uma winsys WDDM nossa (alocação, Present, compartilhamento), justamente onde estão os nossos problemas, e não resolve a trava. Reavaliar se a Frente R mostrar que o custo de manter a nossa UMD passou do da troca.

### Descartados (proibidos)

WARP ou Basic Render como caminho de pixel, DXVK por aplicativo, desvio para compute ou LLM, fallback de CPU disfarçado. Leis 1–4 e diretriz de 16/09.

---

## 4. Roteiro da Frente L (Linux, mãos do Daniel) — ADIADO, fica como referência

**Regras:**
- Linux **ao vivo, por pendrive**. Não instalar, não tocar no disco do Windows.
- Boot frio antes (desligar da tomada alguns segundos).
- A BC-250 deve estar com o BasicDisplay no Windows antes de desligar (é o estado normal depois do rollback).

**Passo 1 — Preparar o pendrive.**
- **Fedora 43 Workstation Live** (a comunidade aponta Fedora 43 e Bazzite como melhor suporte pronto; o ISO do Bazzite é instalador, o do Fedora roda ao vivo). Fonte: [elektricM/amd-bc250-docs](https://elektricm.github.io/amd-bc250-docs/linux/distributions/) (P2 externo).
- Requisitos da comunidade: **Mesa 25.1+**; kernel **fora** de 6.15.0–6.15.6 e 6.17.8–6.17.10 (panics e falha de inicialização da GPU na BC-250). Conferir com `uname -r` antes de qualquer conclusão.
- **Bootar no modo normal.** O "Basic Graphics Mode" (nomodeset) desliga o amdgpu, o que dá desfecho L-C.
- **Sem governor.** Sem ele, a GPU fica no estado de fábrica, travada em 1500 MHz, que é o estado em que o **nosso** driver roda no Windows (não falamos com o SMU; equivalência = H). Isso deixa as duas plataformas comparáveis. O governor (`cyan-skillfish-governor-smu`) só num segundo ensaio, se o primeiro for estável.
- Um segundo pendrive (ou uma pasta no mesmo) para gravar os logs, caso a sessão ao vivo não tenha persistência.

**O que a comunidade relata como causa de travamento total na BC-250** ([troubleshooting/stability](https://github.com/elektricM/amd-bc250-docs/blob/main/docs/troubleshooting/stability.md), P2 externo), e o que conferimos:

| Causa relatada | Situação nesta máquina |
|---|---|
| IOMMU ligado na BIOS | Provavelmente **desligado**: nenhum dispositivo IOMMU no Windows e proteção de DMA indisponível (P2, inferência). Conferir na BIOS: Advanced → NB Configuration → IOMMU |
| Temperatura: algumas placas travam acima de 60–65 °C (*"freeze, sometimes white screen"*) | **Nunca medida** nas nossas corridas. O ensaio no Linux registra `sensors`; conferir se o ventilador gira sob carga e o assentamento do dissipador |
| Tensão insuficiente para a frequência | Não controlamos tensão no Windows (firmware de fábrica) |
| Fonte insuficiente (a placa puxa até ~235 W) | A conferir: modelo e potência da fonte |
| Kernel com defeito (Linux) | Só para o ensaio L; ver acima |

**Atalho: o script [`tools/linux/bc250-linux-referencia.sh`](tools/linux/bc250-linux-referencia.sh) faz os passos 2 a 5 sozinho** (inventário, validade do ensaio, monitor a cada 2 s com `sync`, marcação de eventos e resumo; modo `--pos-trava` para depois de uma trava). Só lê o sistema. Os comandos abaixo ficam como referência manual.

**Passo 2 — Conferir que é aceleração de verdade (senão, desfecho L-C):**
```
lspci -k | grep -A3 -i vga              # driver em uso: amdgpu
sudo dmesg | grep -i amdgpu | tail -40  # sem erros de firmware ou de ring
glxinfo -B | grep -i renderer           # AMD, NÃO llvmpipe
vulkaninfo --summary | grep -i devicename
```

**Passo 3 — Gravar o log continuamente no pendrive** (sobrevive à trava):
```
sudo dmesg -w | tee ~/bc250-dmesg.txt
```
Numa segunda janela, registrar relógios e temperatura a cada 2 s (se os arquivos existirem):
```
while true; do date; cat /sys/class/drm/card*/device/pp_dpm_sclk /sys/class/drm/card*/device/pp_dpm_mclk 2>/dev/null; sensors 2>/dev/null | grep -iE 'edge|junction|mem'; sleep 2; done | tee ~/bc250-clocks.txt
```

**Passo 4 — Carga equivalente, 10 minutos:**
- abrir e fechar o menu de aplicativos repetidamente;
- abrir, mover e fechar janelas, um navegador com uma página pesada;
- 2–3 minutos de carga de GPU (`vkcube`, ou `glmark2` se houver).

**Passo 5 — Anotar:**
- travou? que horas? o que estava fazendo?
- se travou: reset, bootar o pendrive de novo e guardar `~/bc250-dmesg.txt`, `~/bc250-clocks.txt` e o `journalctl -b -1 -k` (se a persistência guardou o boot anterior).

**Passo 6 — Trazer os arquivos** para `docs/evidence/<data>_LINUX_REFERENCIA/`. Eu analiso e fecho o desfecho L-A/B/C/E.

---

## 5. Pendências de driver (ficam paradas até as Frentes W e R responderem)

| Item | Estado | Depende de |
|---|---|---|
| T4.6 (recopia por quadro) | Em disco, critério sem resposta | Corrida 189 (menu aberto) |
| Tabela de objetos definitiva (dinâmica, ou um objeto por textura) | T4.7 é paliativo (16.384) | Resultado da 189 (vazamento ou demanda) e Frente B |
| Motivo do kCreateDevice -3 em AppContainer | Sem canal (stderr proibido ali) | Canal próprio, p.ex. OutputDebugString capturado ou log pela UMD |
| `Blt Timeout` falso no KMD (`==` na fence depois do lock solto, T3.5) | Dívida declarada (doc 540) | Faixa própria, pequena |
| Token: volta de 16 bits, PRIMARY sem PID, `hAllocation` do dxgkrnl usado como handle nosso | Dívidas (doc 541) | Faixa de ABI; idealmente o KMD emitir o identificador, como o Linux |
| Tradutor de shader dentro do pacote (D2.2/D2.5) | Aberto | — |
| Custo por quadro (P-3) | Aberto | R4 mede antes de mexer |
| D0.7 formal (canários no destino do IB gráfico) | Aberto | Depois da trava (canários pela BAR eram arriscados, X3/X4) |
| Display sobrevive a TDR (D5.4) | Uma vez na 153 | Repetir depois de D4 |

---

## 6. Regras que continuam valendo

- **Critério escrito antes de cada corrida** (Lei 10): assinatura e implicação separadas, desfecho E sempre presente, desfecho parcial não conta.
- **Uma variável por candidato.** SYS idêntico quando só a UMD muda (182–186 provaram que dá).
- **O empacotador copia, não compila.** Conferir strings novas no binário assinado e o hash da UMD fonte.
- **Boot frio entre binds.**
- **Imagem só conta se anterior ao rollback.** O Explorer cair durante o rollback (183, 185) não é falha do ensaio.
- **Logs:** dwm.exe e Explorer em `C:\Windows\Temp` (coleta FINAL do binder, ou cópia elevada por caminho **absoluto** depois de uma trava); menu, Pesquisa e ShellExperienceHost em `umd-logs-appcontainer/`.
- **Antes de reverter qualquer coisa:** ler o doc da faixa que a criou, o `git diff` não commitado e como o Linux faz.

---

## 7. O que NÃO fecha esta etapa

- Compilar, assinar, carregar DLL, `Código 0` ou `Exit 0` isolados.
- Imagem que coincide com o rollback do BasicDisplay.
- Quadro produzido pela CPU, mesmo que apareça na tela.
- Pixels gerados pelo `lavapipe` na bancada de teste (Frente R5): validam a lógica da UMD, **não** a aceleração.
- Linux estável (Frente L): é referência de hardware, **não** entrega do projeto.
