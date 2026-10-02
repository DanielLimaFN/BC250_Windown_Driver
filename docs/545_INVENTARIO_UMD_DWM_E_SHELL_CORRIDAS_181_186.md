# 545 — Inventário do que o DWM e o shell pedem à nossa UMD (Frente R1, Corridas 181–186)

> 30/09/2026. **Offline, zero corrida.** Frente R1 do [PLANO_DESKTOP_ACELERADO.md](../PLANO_DESKTOP_ACELERADO.md).
> Ferramenta: [`tools/umd_inventory.py`](../tools/umd_inventory.py). Saída completa (Markdown e JSON): [`docs/evidence/umd-inventario-2026-09-30/`](evidence/umd-inventario-2026-09-30/).
> Fonte: 56 logs da UMD, 11 processos, 6 corridas (181–186). Cada número sai de uma linha de log (P0 sobre o que passou pela nossa UMD).

---

## 1. Como ler

- **Logs usados:** os FINAL da raiz da pasta de cada corrida, `final-elevado/` e `umd-logs-appcontainer/`. Ficam de fora o agregado, os snapshots sem FINAL e os `UserTemp__` (programas de teste de outros dias).
- **Amostragem:** linhas com `[DX70_SAMPLE c=K n=N]` são amostradas; o total real é o maior `N`. No dwm.exe, por exemplo, `ResourceMap` aparece em 1.169 linhas, mas o contador chega a 18.432.
- **Limite:** os logs de AppContainer acumulam por PID entre corridas; um PID reaproveitado pode misturar corridas. Os números são por união das 6 corridas, não por corrida.

## 2. Quem usa a UMD

| Processo | Linhas | Cargas da DLL | Draws gravados | Draws recusados |
|---|---:|---:|---:|---:|
| dwm.exe | 1.012.308 | 34 | 29.154 | 14 |
| StartMenuExperienceHost.exe | 499.011 | 187 | 5.582 | 5 |
| SearchApp.exe | 399.575 | 30 | 105 | **560** |
| Explorer.EXE | 335.795 | 5 | 837 | 6 |
| ShellExperienceHost, SystemSettings, TextInputHost, msedge, webview2, Claude, dxgiadaptercache | < 22 mil cada | — | — | — |

Todos negociam a interface `0x000B000B` (D3D11.0.7).

## 3. O que o DWM pede (é pouco, e é fixo)

| Item | Medido | Leitura |
|---|---|---|
| Draws | 28.533 `DrawIndexed` + 621 `Draw`, todos com instância 1 | Sem instancing no DWM |
| Topologias | 4 (lista de triângulos) e 5 (faixa de triângulos) | Nada de pontos ou linhas |
| Shaders | 552 VS + 319 PS, **todos SM 4.0**, 48 tamanhos distintos; **871 traduções, zero falhas** | Tradutor já cobre o DWM |
| Formatos de recurso | `B8G8R8A8_UNORM` quase tudo; `B8G8R8X8_UNORM` e `R8G8B8A8_UNORM` raros; buffers `UNKNOWN` | Um formato dominante |
| SRV / RTV | `B8G8R8A8` e `R8G8B8A8`, sempre 2D | — |
| Rasterizer | Grade fixa: preenchimento sólido ou wireframe × cull nenhum/frente/trás × scissor | O DWM cria a grade inteira a cada device (múltiplos de 28 = 28 devices) |
| Sampler | Grade fixa: filtro ponto ou linear (e 9× linear sem mip) × endereço wrap/mirror/clamp/border | Idem |
| Depth/stencil | Depth ligado ou desligado; **stencil nunca** | — |
| Map | `WRITE_NO_OVERWRITE` (MapType 5) quase sempre, `WRITE_DISCARD` (4) raro | Buffers dinâmicos por anel |
| Present | 6.828, todos `Flags=0x2` (**Flip**) | Os apps usam `0x1` (Blt) |
| Superfícies abertas de outros processos | 640x640, 12x642, 1024x832, 640x672, 32x32, 640x608... todas `B8G8R8A8` | O menu, a Pesquisa e o Explorer |

**Conclusão (P0 dentro das 6 corridas):** o que o DWM pede à UMD é **pequeno e fechado**: dois formatos, duas topologias, grades fixas de estado, SM 4.0 e zero stencil. **O DWM não é o problema.** Suas 14 recusas em 29 mil draws são efeitos de falhas em outros lugares (§4).

## 4. Onde está o problema: memória e criação de device, não a API D3D

### 4.1 `vkAllocateMemory` → `VK_ERROR_OUT_OF_DEVICE_MEMORY` (P0)

| Processo | Falhas | Tamanho pedido |
|---|---:|---|
| Explorer.EXE | **1.124** | 1.123 × **9.472 bytes** (textura 1024x1) |
| SearchApp.exe | 56 | variados, até 12 MB |
| dwm.exe | 10 | até 4 MB |
| StartMenuExperienceHost.exe | 7 | variados |
| SystemSettings.exe | 3 | 3 × 17 MB |

- **Consequência visível (P0):** 198 `Textura SEM conteudo ... a imagem NAO sera aceita como SRV` no Explorer, e as recusas `SRV_RECURSO_SEM_VKIMAGE` e `RTV_VULKAN_INVALIDO`. São partes da interface que não se desenham.
- **Mecanismo (P0 pelas contagens + H forte na ligação):**
  - falta memória com pedidos de 9 KB, o que aponta para granularidade e não para volume;
  - na Corrida 183 o KMD registrou **3.193 alocações de exatamente 64 KiB presas ao segmento de VRAM** (`SegSet=0x1`, `[DX93_ALLOC_MAP]`), de um segmento de **~257 MB** (`0xF531000`);
  - a T3.9 voltou `EvictionSegmentSet` para 0 (fim do BugCheck `0x10E`), então **nada pode ser despejado** para a memória do sistema;
  - as ~4.500 texturas pequenas do Explorer a 64 KiB cada dariam ~280 MB, mais que o segmento inteiro.
- **Como os drivers reais evitam isso:** agrupam recursos pequenos numa alocação só (suballocation) e despejam para a memória do sistema sob pressão. Nós fazemos uma alocação WDDM por imagem, sem despejo.

### 4.2 `vkCreateDevice` → `VK_ERROR_INITIALIZATION_FAILED` (P0)

- **528** na Pesquisa, 10 no Explorer, 6 no dwm.exe.
- **Consequência:** sem ponte Vulkan, o device D3D fica sem backend, e os draws caem em `Draw: RECUSADO -- compilador/shader-state/draw PM4 ausente` (**525** na Pesquisa).
- **Causa: NÃO IDENTIFICADA.** O código tem cinco pontos que devolvem esse erro no winsys WDDM2 da RADV (`radv_wddm2_winsys.c:764–816`: abrir o adaptador, ler informações da GPU, ler o nome, criar o device, criar a fila de paginação). Todos só escrevem o motivo no **stderr da RADV, que não chega a nenhum log nosso**.
- Hipótese de esgotamento de VMID **descartada** por medição: o pool inicia com 12 livres de 16 e não há nenhuma linha de esgotamento.

### 4.3 `vkQueueSubmit` → `VK_ERROR_DEVICE_LOST` (P0)

- 21 na Pesquisa, 6 no menu, 5 no dwm.exe, 4 no Explorer, 2 no SystemSettings. Depois disso o device D3D daquele processo para de desenhar.
- Causa não investigada nesta faixa.

### 4.4 O menu e a Pesquisa recarregam a DLL o tempo todo (P0)

- 187 cargas da DLL no menu e 30 na Pesquisa (com 587 aberturas de adaptador).
- Cada recarga recria device, pipelines e todos os estados: ~18 mil `CheckFormatSupport` no menu, e a grade de sampler e rasterizer recriada a cada vez.
- É custo, não defeito, mas multiplica tudo o que está na §4.1.

## 5. Lacunas da instrumentação (o que o inventário NÃO consegue dizer)

| Lacuna | Por que importa | Como fechar |
|---|---|---|
| Motivo do `vkCreateDevice -3` | 528 falhas sem causa | Levar os `fprintf(stderr)` do winsys WDDM2 da RADV para o sink de trace da UMD |
| Tamanho real da alocação WDDM de cada imagem Vulkan | Fecha a §4.1 por medição direta | Registrar `reqSize` e o tamanho alocado lado a lado na UMD |
| Blend: só `AlphaToCoverage`/`IndBlend` | Não dá para listar as equações de blend que o DWM usa | Registrar `SrcBlend`, `DestBlend`, `BlendOp` e `WriteMask` do RT0 |
| Sampler: só `Filter`/`AddressU` | Faltam `AddressV/W`, anisotropia e borda | Registrar o descritor inteiro uma vez por estado |
| `CreateResource` sem `BindFlags`/`MiscFlags` (só aparecem na recusa) | Não distingue recurso compartilhado de comum | Acrescentar os dois campos à linha de sucesso |
| Shader sem hash do bytecode | "48 tamanhos distintos" é aproximação | FNV do DXBC na linha de `Create*Shader` |

## 6. O que isso muda no plano

1. **Prioridade nova, antes da Frente B:** a pressão de memória de vídeo (§4.1). Ela explica ícones e texturas faltando **sem** envolver o DWM. Duas direções, uma variável por vez:
   - **suballocation** de recursos pequenos na UMD (várias imagens numa alocação WDDM);
   - rever o **despejo** (`EvictionSegmentSet`), que a T3.9 desligou por causa do `0x10E` (doc 535). Religar exige entender aquele `0x10E` antes.
2. **Instrumentação da §5** numa faixa só de log (sem mudar comportamento), para a próxima corrida responder a §4.2.
3. **O DWM em si está coberto:** SM 4.0, dois formatos, grades fixas. A Frente E (ETW) perde prioridade.
4. **O reprodutor (Frente R2)** passa a ter um alvo concreto: reproduzir a sequência de criação de texturas do Explorer e medir quantas alocações de 64 KiB ela gera, sem hardware.

## 7. Correção da §4.1 e faixa I.1 (30/09, mesmo dia)

### 7.1 A granularidade de 64 KiB NÃO é o mecanismo (a §4.1 cai como formulada)

> **Corrigido pela Corrida 188 ([doc 546](546_CORRIDA188_TABELA_DE_OBJETOS_CHEIA_E_CANDIDATO189_T47_I1C.md) §1.1):** a RADV pediu de fato Bytes=65536 para a memória da imagem, e a leitura abaixo sobre 4 KiB não vale para esse caminho. O mecanismo medido é a tabela de objetos do KMD cheia (ObjStatus=3 NO_SPACE →  xC000009A → OOM).

- **Lido no winsys WDDM2 da RADV** (`radv_wddm2_bo.c:483–494`): o alinhamento físico é 4 KiB para BO de VRAM abaixo de 64 KiB. A imagem de 9.472 bytes vira 12 KiB, não 64 KiB.
- **Medido no log do Explorer (Corrida 183):** cada textura D3D gera **duas** alocações, uma WDDM da nossa UMD (`Allocate ... Size=4096 Align=4096`, 1.991 vezes) e a memória Vulkan da RADV. É desperdício, mas de outra natureza.
- **O KMD não recusa essas alocações:** 0 a 5 `CREATE_ALLOC_RECUSADO` por corrida, contra centenas de falhas no Explorer.
- **Onde nasce o erro:** dentro da RADV, que devolve `VK_ERROR_OUT_OF_DEVICE_MEMORY` em quatro etapas depois da criação: `CreateAllocation2`, `MapGpuVirtualAddress`, `MakeResident` (com `MustSucceed=1`, em **toda** memória Vulkan) ou a espera da paginação. O motivo de cada uma só vai para o stderr.
- **Hipótese que sobra (H):** o `MakeResident` falha quando o segmento de ~257 MB enche, e nada pode ser despejado (T3.9). A medição que a separa das outras três é a I.1.
- **Consequência para o plano:** o agrupamento de recursos **não** começa antes de a I.1 dizer qual etapa falha. Fazê-lo agora seria chutar a causa.

### 7.2 Faixa I.1 — só instrumentação, candidato 3.332.1.0 (com a T4.6, ainda sem resposta)

| Mudança | Onde |
|---|---|
| stderr do processo redirecionado para `BC250_UMD_LOG_<pid>-RADV.txt` logo depois de a ponte carregar a RADV (mesmo UCRT). Primeiro `C:\Windows\Temp`, senão o `%TEMP%` do processo | `bc250_umd_vulkan_bridge.c`, `Bc250UmdVkRedirectRadvStderr` |
| `CreateResource` com `Bind`, `Misc`, `Usage`, `Mips` e `Array` | `bc250_d3d_umd.c` |
| `CreateBlendState` com as equações do RT0 (`En`, `Src`, `Dst`, `Op`, `SrcA`, `DstA`, `OpA`, `Mask`) | idem |
| `CreateSampler` com `AddressV/W`, anisotropia, comparação e LOD | idem |
| `Create*Shader` com `Hash=` (FNV-1a do bytecode; o contêiner DXBC é medido pelo tamanho total, não pelo digest) | idem |
| Requisitos de memória de imagem com `alignment` e `typeBits` | `bc250_umd_resource_vk.c` |

- Portão novo `audit_radv_stderr_reaches_evidence`, com mutação verificada (sem a chamada, reprova). UMD: 0 erros, 0 avisos, 49 PASS.
- SYS assinado **idêntico** ao das Corridas 182–186 (`2BBD88C8…`). UMD empacotada `D6F5B5E8…`, INF `C4913C1B…`, CAT `9068183D…`.
- `tools/umd_inventory.py` ignora os arquivos `-RADV` (outro formato).

### 7.3 Critério da Corrida 187 (escrito antes, Lei 10)

**Procedimento:** boot frio; `tools\EXECUTAR_CORRIDA187_T46I1_600S.cmd`; o mesmo roteiro (menu várias vezes, abrir o Explorer). Se travar, recuperar os logs com o comando de caminho absoluto; ele já copia os `-RADV`.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Existem arquivos `*-RADV*` com linhas `[RADV_WDDM2]`, e em pelo menos uma falha do Explorer aparece a etapa: `CreateAllocation2 failed`, `mapping ... failed`, `MakeResident failed` ou `WaitForSynchronizationObjectFromCpu failed` | A etapa nomeada escolhe a correção (despejo/segmento, espaço de VA ou criação) |
| **B** | Existem `-RADV`, mas as falhas de `vkCreateDevice -3` da Pesquisa nomeiam o ponto (`Can't open adapter`, `Can't fill info`, `Couldn't create device`, `Couldn't create paging queue`) | Idem para a §4.2 |
| **C** | Nenhum arquivo `-RADV`, ou vazio | O redirecionamento não alcançou a RADV (CRT diferente no binário instalado?); conferir a linha `[VK_BRIDGE] I.1 stderr da RADV ->` no log da UMD |
| **D** | Nenhuma falha de alocação nem de `vkCreateDevice` nesta corrida | Sem medida; repetir |
| **E** | Algo fora do mapa | Descrever antes de julgar |

A T4.6 é observada à parte: o Daniel diz se os ícones do menu aparecem, e o cruzamento `tools/cross-shared-surface-timeline.py` mostra se há recopia depois do último draw do menu.

### 7.4 Tentativa 187 abortada sem ensaio, e a correção I.1b

- **O que aconteceu (30/09 15:59, pacote 3.332.1.0, pasta `2026-09-30_15-59-01-...`):**
  - o driver subiu (Código 0, SYS `2BBD88C8…`) e o dwm.exe já criou `C:\Windows\Temp\BC250_UMD_LOG_1268-RADV.txt`, prova de que o redirecionamento alcançou o processo;
  - mas o `Copy-Item` do snapshot do binder (`bind-bc250mcdm-lab.ps1:2043`) falhou com *"arquivo em uso por outro processo"*, erro terminante. O roteiro abortou e fez rollback imediato (`EXIT CODE: 1`). **Não houve ensaio.**
- **Causa (erro meu):** o `freopen_s` abre o arquivo **sem compartilhamento**. Eu tinha afirmado que o binder o coletaria sem verificar.
- **Correção I.1b:**
  1. **UMD:** o `-RADV` é aberto por `CreateFileA` com `FILE_SHARE_READ|WRITE|DELETE` e ligado ao `stderr` por `_dup2` (com `freopen_s` para `NUL` só para dar descritor ao `stderr` de processo sem console).
  2. **Binder:** `Copy-Bc250SharedLog` substitui os quatro `Copy-Item` de logs da UMD. Abre a origem com `ReadWrite|Delete` e, se falhar, **avisa e segue**: um log preso nunca mais aborta uma corrida.
- **Provas offline:**
  - Executável **sem console** (subsistema Windows, como o dwm.exe) com a função copiada literalmente e uma DLL separada com CRT dinâmico falando por `stderr`, como a RADV: `HOST_EXIT=0`, a linha chegou ao arquivo, e o `CopyFileA` com o arquivo aberto funcionou. **A versão antiga com `freopen_s` falha no mesmo teste (`HOST_EXIT=13`, a cópia não abre)**, reproduzindo a 187.
  - Binder: `Copy-Item` contra arquivo sem compartilhamento dá erro terminante; a função nova devolve `False`, avisa, e o roteiro segue.
  - Portão estendido (`audit_radv_stderr_reaches_evidence`): exige compartilhamento e `_dup2` e proíbe `freopen_s` sobre o caminho do log. Mutação sem compartilhamento reprova.
- **Pacote 3.333.1.0** (o 3.332.1.0 chegou a ser instalado, então o DriverVer subiu): SYS idêntico `2BBD88C8…`, UMD `9AF0291D…`, INF `9B9F4C80…`, CAT `6EED8C90…`. Auditorias PASS; `prepare-corrida188-t46i1b.ps1` EXIT 0.
- O critério da §7.3 vale para a **Corrida 188** sem mudança.

## 8. Registro

- `tools/umd_inventory.py`: primeira versão lia também centenas de logs antigos copiados na raiz das pastas, e levava minutos. Restringida a FINAL, `final-elevado/` e AppContainer; roda em ~40 s nas 6 corridas.
- Decodificação dos enums (D3D10 DDI, P1): topologia 4/5 = lista/faixa de triângulos; `FillMode` 3/2 = sólido/wireframe; `CullMode` 1/2/3 = nenhum/frente/trás; `Filter` 0/20/21 = ponto / linear sem mip / linear; `AddressU` 1–4 = wrap/mirror/clamp/border; `MapType` 4/5 = `WRITE_DISCARD`/`WRITE_NOOVERWRITE`; Present `0x1`/`0x2` = Blt/Flip.
