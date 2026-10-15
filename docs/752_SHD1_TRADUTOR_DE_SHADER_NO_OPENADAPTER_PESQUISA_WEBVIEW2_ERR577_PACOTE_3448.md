# Doc 752 — SHD.1: tradutor de shader aberto no OpenAdapter (Pesquisa/WebView2, err=577) e pacote 3.448.1.0

> **Data:** 14/10/2026, offline. Fonte: Corrida 293b ([doc 751](751_CORRIDA293B_DESKTOP_INTEIRO_SEM_COPIA_PSO1_CACHE_DE_PIPELINES_PACOTE_3447_E_CRITERIO_CORRIDA294.md)).
> **Pedido do Daniel:** passo 1 (PSO.1) e passo 2 (Pesquisa).

## 1. O que o log do WebView2 da Pesquisa mostra (P0)

Log `Microsoft.Windows.Search_…_BC250_UMD_LOG_2244.txt` (processo `msedgewebview2.exe`):
- o processo abre o adaptador, cria o device (a RADV carrega e o `vkCreateDevice` dá `res=0`), consulta formatos e cria buffers;
- no primeiro shader aparece `SHADER_TRANSLATOR: ARMADO porem a DLL 'C:\BC250_LIB\libvkd3d-shader-1.dll' nao abriu (err=577) -- fail-closed`;
- em seguida `Bc250UmdVkCreateShaderModule RECUSADO` e `VkModule=0` no vertex e no pixel shader;
- a falha fica gravada para o processo inteiro (`s_Bc250ShaderTranslatorState=0`), então nenhum shader dele traduz mais.

O erro 577 é `ERROR_INVALID_IMAGE_HASH`. A `libvkd3d-shader-1.dll` está assinada com o certificado de teste. As dependências dela (`libgcc_s_seh-1.dll` e `libwinpthread-1.dll`) e a `vulkan_radeon.dll` **não** estão. A RADV, mesmo sem assinatura, carregou nesse mesmo processo. O que muda é o **momento**:
- a RADV entra no `CreateDevice`;
- o tradutor só entra no primeiro shader.

**H:** o Chromium aplica a mitigação de integridade de código no processo de GPU depois da inicialização, e as DLLs que chegam depois disso são recusadas. Sem shaders, o ANGLE não compila, e a Pesquisa desenha por software. Isso explicaria por que ela é **sempre** mais lenta. Que o Chromium caia para software é **NÃO VALIDADO**: falta o log dele.

## 2. Correção e prova

- **SHD.1 (UMD):** `Bc250UmdShaderTranslator()` passa a ser chamado no fim do `OpenAdapter10_2`. A DLL e as dependências dela carregam junto com a RADV, antes do travamento. A linha `SHADER_TRANSLATOR: SHD1 pre-carga no OpenAdapter -> ARMADO|INDISPONIVEL` entrou no LOGMIN. Uma falha continua não fatal e com o motivo nomeado.
- **Prova pelo Chromium:**
  - `tools/armar-log-webview2.ps1`, rodado **uma vez, elevado, antes de desligar**, põe `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--enable-logging --v=0 --log-level=0` no ambiente da máquina;
  - o EXECUTAR da 294 roda `tools/coletar-log-webview2.ps1` no fim. Ele copia os `chrome_debug.log` da última hora para `webview2-logs/` da evidência e resume as linhas de GPU (ANGLE, D3D11, SwiftShader, blocklist);
  - `-Desarmar` remove a variável depois.
- **Auditoria:** portão novo para a pré-carga.

**Build e pacote:**
- `build-umd` EXIT=0; build de laboratório OK; auditoria de segurança e auditoria do pacote PASS; strings `SHD1 pre-carga` e `[PSO1]` conferidas na UMD.
- **Lab 3.448.1.0** = 3.446.1.0 + PSO.1 + SHD.1: SYS `C3184ECA…`, UMD `0A0500A2…`, INF `9D556A16…`, CAT `CE89D783…`.
- O 3.447 nunca rodou e foi substituído.
- **Roteiro:** `tools/EXECUTAR_CORRIDA294_PSO1_CACHE_DE_PIPELINES_300S.cmd`.

## 3. Critério da Pesquisa na Corrida 294 (soma ao do doc 751)

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | no log da UMD do `msedgewebview2`, `SHD1 pre-carga … ARMADO` e shaders com `VkModule` ≠ 0; no `chrome_debug.log`, nenhuma queda para software; a Pesquisa mais rápida (Daniel) | causa confirmada e corrigida |
| **B** | `SHD1 … INDISPONIVEL` com err=577 já no OpenAdapter | a recusa vale desde o início do processo: assinar `libgcc_s_seh-1.dll`/`libwinpthread-1.dll` ou compilar o vkd3d sem dependências MinGW |
| **C** | `ARMADO`, mas o `chrome_debug.log` mostra GPU desligada por outro motivo (blocklist, nível 10_1, recurso compartilhado) | o motivo aparece no log do Chromium; ele vira o próximo item |
| **D** | nenhum `chrome_debug.log` | o log não foi armado, ou a pasta é outra; procurar `EBWebView` |
| **E** | algo fora do mapa | descrever antes de julgar |

**NÃO VALIDADO EM HARDWARE.**
