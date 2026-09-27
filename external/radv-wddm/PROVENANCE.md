# Fork Mesa/RADV WDDM2 da BC-250 — proveniência e cópia do delta

> **Criado em 19/09/2026.** Até esta data o driver Vulkan da placa existia em **um único
> diretório sem cópia**, fora deste repositório, e nada aqui registrava onde ele estava.
> A DX.40 foi usada na Corrida 84 com falha funcional; a DX.41 atual permanece
> **NÃO VALIDADA EM HARDWARE**. Este arquivo registra proveniência, não prova de funcionamento.

---

## 1. Identidade

- **Commit base:** `801c9763c6043f0de8408e905a5324eea06d81d7`
- **Assunto do commit base:** `HACK: vulkan/wddm2: always check status`
- **Origin:** `https://gitlab.freedesktop.org/lfrb/mesa.git`
- **Árvore de trabalho:** `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm` (~1,6 GB)
- **SHA-256 do patch:** `ab836a8bda0109bf8d4a6aa4bf109976d6067db751045dcb2d04d3bb1bf88ae7` (DX.42 offline, 23/09/2026: identidade do preambulo -- serial do CS, hash no finalize, VA/handle do BO; inclui DX.41 e faixas anteriores). Anterior: `b608dcb6...` (DX.41)
- **SHA-256 do DLL:** `bf6a1edae5c6f64e5a0c5795878e5d187814eb91a1e5c0f3447074b6f7cee14a` (DX.40 usado na Corrida 84; implantado em `C:\BC250_LIB`). Anterior: `5988b38e...` (Corrida 83)

**DX.42 apenas offline:** DLL reconstruída em `build/src/amd/vulkan/vulkan_radeon.dll`, SHA-256 `fcaa11fe9dfef68225a12d3fc455d9f777651e2ad3f31b2e32525a889dd1008c`, com a string `RADV_PREAMBLE_IDENTITY` conferida no binário. É a RADV fixada para a Corrida 85 ([critério](../../docs/370_DX42_IDENTIDADE_DO_PREAMBULO_E_CRITERIO_CORRIDA85.md)). **Não validada em hardware.**

**DX.41 apenas offline (histórico, substituída pela DX.42 antes de rodar):** DLL reconstruída em `build/src/amd/vulkan/vulkan_radeon.dll`, SHA-256 `5c4e770140db3a0c2df36a3f7cf11b2448789285cfff54f2e0e677bce0891dee`. A DLL implantada em `C:\BC250_LIB` é a DX.40 da Corrida 84 (`bf6a1eda...`); build e implantação divergem até outro ensaio. O espelho reflete a árvore externa atual. O wrapper verifica a cópia antes do bind. **DX.41 não foi validada em hardware.** [Análise e correção offline](../../docs/368_DX41_ANALISE_IB_ZERO_POS_CORRIDA84.md).

> **DX.26 (22/09/2026):** o winsys passa a declarar `ip_discovery_version = 10.1.3`; sem isso o Mesa aplicava a correcao de versao do kernel e tratava o GFX1013 como GFX10_3 (medido no IB da Corrida 71). NAO VALIDADO EM HARDWARE.

> **DX.22 (22/09/2026):** a concatenacao BC-250 do IB passou a usar BO proprio por submissao (pool por contexto/IP, reciclagem so apos fence), em vez de escrever dentro do CS do preambulo. Corrida 67 mediu o defeito: IBs cumulativos na VA do preambulo, +392 DW por submit. NAO VALIDADO EM HARDWARE.

**A base NÃO é o Mesa main.** É o ramo de desenvolvimento WDDM2 de `lfrb`, e o próprio
commit base é um `HACK:`. O winsys `src/amd/vulkan/winsys/wddm2/` (9 arquivos) **já existe
nessa base** — o nosso delta o modifica, não o cria.

### Cadeia conferida (P0, por hash)

**Rechecagem em 23/09/2026, após a Corrida 83 e antes da edição DX.38:** `binding-provenance.json`, o DLL implantado em `C:\BC250_LIB` e o DLL então existente em `build/src/amd/vulkan` tinham SHA-256 `5988b38e86febb514c4c5f1d5fb5ce5a4b6fdd2b57668f2a1b6baca3b1a0461f`. `python tools/sync-radv-fork-delta.py` conferiu base e hash do delta então espelhado (`74304831...`) e a igualdade dos dois DLLs. Essa verificação não recompila e não prova que cada arquivo da árvore está no binário. O bloco abaixo registra a medição **histórica de 19/09**, não o DLL atual.

```
lfrb/mesa @ 801c976  →  bc250-radv-wddm.patch (23 arquivos, ~1.122 linhas)
                     →  build/src/amd/vulkan/vulkan_radeon.dll   7837c96027e5e562…
                     ≡  C:\BC250_LIB\vulkan_radeon.dll           7837c96027e5e562…
```

Os dois DLLs eram **byte-idênticos nessa medição de 19/09**. O DLL da Corrida 34 era `bb857af7e337e8ae…` e está
guardado em `C:\BC250_LIB\vulkan_radeon.dll.pre-2026-09-19-ibdump`; o DLL de 19/09
(`7837c96027e5e562…`) tinha o despejo de IB e sem o fail-open de fence. Aquele binário publicava `Mesa 26.2.0-devel (git-801c9763c6)`,
o que bate com o commit base — dá para reconfirmar sem git:

```bash
grep -a -o -E "26\.2\.0-devel \(git-[0-9a-f]+\)" /c/BC250_LIB/vulkan_radeon.dll
```

## 2. Como usar este diretório

| Quero | Comando |
|---|---|
| Conferir que o espelho ainda bate com a árvore e com o DLL | `python tools\sync-radv-fork-delta.py` |
| Regenerar o espelho depois de mexer no RADV | `python tools\sync-radv-fork-delta.py --update` |
| Auditar o RADV pelas regras do projeto | `python tools\audit-radv-wddm-safety.py --mesa-root "C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm"` |
| Reconstruir a árvore do zero | clonar `origin`, `git checkout 801c9763c6`, aplicar `bc250-radv-wddm.patch`, então `scripts/` |

`scripts/` traz os quatro arquivos de build que também viviam só na árvore externa
(`setup_mesa_env.bat`, `configure_radv.bat`, `build_radv.bat`, `download_tools.py`).
O `tools/` da árvore externa (494 MB de toolchain baixada) **não** é espelhado — é
reconstruível por `download_tools.py`.

## 3. O que o delta contém

Estado DX.41: 24 arquivos modificados frente ao commit base. Núcleo:

- `src/amd/vulkan/winsys/wddm2/` — `bo`, `cs`, `winsys`, `wsi` (residência, GPUVA, submissão)
- `src/vulkan/runtime/vk_wddm2_monitored_fence.c` — fences monitoradas do WDDM
- `src/vulkan/runtime/vk_dxgi.cpp`, `vk_dxcore.cpp`, `wsi_common_win32.cpp` — enumeração e WSI
- `src/amd/common/ac_gpu_info.c`, `radv_physical_device.c` — identificação do Cyan Skillfish 2

### Dívidas conhecidas dentro do delta

1. **Fail-open na espera de fence — REMOVIDO em 19/09.** `vk_wddm2_monitored_fence.c`
   rebaixava device-lost para `VK_SUCCESS` quando todas as fences esperadas já tinham
   atingido o valor-alvo em memória, imprimindo
   `check_device_status warning: ... but all fences satisfied -> continuing!`.
   Era **nosso** (não está na base — confirmado por `git show HEAD:…`) e violava a Lei 4:
   a fence ter chegado ao alvo prova que aquelas escritas aconteceram, não que o dispositivo
   está vivo. Agora o status é propagado. A detecção não mudou; só o rebaixamento saiu.
   Ressalva de escopo mantida no código: na Corrida 34 ele **não** foi a causa provada dos
   51 quadros fantasmas — o `FlushCommands` devolveu `res=-4` logo depois, e a causa provada
   era o harness D3D11 descartar o HRESULT de `Present()`.
   Verificável no binário: `grep -a -c "all fences satisfied"` → 0.
2. **Heaps de 34 bits** — `radv_wddm2_bo.h`: `RADV_WDDM2_32BIT_HEAP_START 0x200000000`,
   `RADV_WDDM2_HEAP_START 0x240000000`, teto `0x3F0000000`. Curou o GCVM Protection Fault
   da Corrida 33 (validado no silício em 19/09).
3. **Despejo de IB opt-in (novo em 19/09, ampliado em DX.38/39/40)** — `radv_wddm2_cs.c`, `bc250_dump_ib()`.
   Com `BC250_RADV_IB_DUMP=<diretório>` **ou** a chave
   `HKLM\SOFTWARE\BC250\Umd\GfxIbDumpDir` definida, o IB final preparado (concatenado, encadeado ou simples) é gravado cru em
   `bc250_ib_pid<PID>_t<tick>_<n>_va<hex>_dw<n>.bin` (teto de 128 por processo, anunciado no stderr). Cada dump completo gera um `.meta` com fila, esperas, sinais e retorno de `D3DKMTSubmitCommand`.
   **Opt-in:** sem variável e sem chave de registro, não se abre arquivo de dump ou fluxo; os eventos diagnósticos ainda podem ir ao `stderr`. O arquivo `bc250_flow_pid*.log` guarda fases da fila com `submit_id` e limite de aproximadamente 4 MiB por processo. Existe porque a Corrida 34 parou dentro de um IB de 1008 DW do qual não guardamos
   um dword. **A chave de registro entrou em 19/09, depois da Corrida 35:** a variável de
   ambiente não alcança o `dwm.exe` — que foi justamente quem submeteu o IB que travou o CP —
   e o despejo saiu vazio por isso. **E em 19/09, depois da Corrida 36, a chamada saiu de dentro
   do ramo de concatenação (`cs_total > 1`) e passou para depois dos dois ramos:** a chave já
   estava criada e apontando certo, mas a submissão que travou o CP não passava por aquele ramo,
   e `gfx-ib-dump/` saiu vazio pela segunda vez. Lê-se com `python tools\decode-gfx-ib.py <arquivo> --stopped-at-opcode 0x..
   --remaining-dw N`, cruzando com a sonda `[TDR_GFX_PACKET]` do KMD.
4. **Concatenação de CS específica do BC-250** — `radv_wddm2_cs.c:783`, que funde preamble e
   main CS num IB linear único (`BC-250 UNIFICADO: %u CS concatenados`). Não existe no
   upstream, e é o caminho que produziu o IB que travou o CP na Corrida 34.
