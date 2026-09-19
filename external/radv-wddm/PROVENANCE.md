# Fork Mesa/RADV WDDM2 da BC-250 — proveniência e cópia do delta

> **Criado em 19/09/2026.** Até esta data o driver Vulkan da placa existia em **um único
> diretório sem cópia**, fora deste repositório, e nada aqui registrava onde ele estava.
> **NADA NESTE DIRETÓRIO FOI VALIDADO EM HARDWARE** — é arquivo de proveniência, não prova.

---

## 1. Identidade

- **Commit base:** `801c9763c6043f0de8408e905a5324eea06d81d7`
- **Assunto do commit base:** `HACK: vulkan/wddm2: always check status`
- **Origin:** `https://gitlab.freedesktop.org/lfrb/mesa.git`
- **Árvore de trabalho:** `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm` (~1,6 GB)
- **SHA-256 do patch:** `2b21bde03dfd36d9fca05f7310e9a0a1d9472ca58a08f342acb75327486dd575`
- **SHA-256 do DLL:** `7837c96027e5e56233fa16735cbe22a301a62f61d1e0075ad833efddeefe5868`

**A base NÃO é o Mesa main.** É o ramo de desenvolvimento WDDM2 de `lfrb`, e o próprio
commit base é um `HACK:`. O winsys `src/amd/vulkan/winsys/wddm2/` (9 arquivos) **já existe
nessa base** — o nosso delta o modifica, não o cria.

### Cadeia conferida (P0, por hash)

```
lfrb/mesa @ 801c976  →  bc250-radv-wddm.patch (23 arquivos, ~1.122 linhas)
                     →  build/src/amd/vulkan/vulkan_radeon.dll   7837c96027e5e562…
                     ≡  C:\BC250_LIB\vulkan_radeon.dll           7837c96027e5e562…
```

Os dois DLLs são **byte-idênticos**. O DLL da Corrida 34 era `bb857af7e337e8ae…` e está
guardado em `C:\BC250_LIB\vulkan_radeon.dll.pre-2026-09-19-ibdump`; o atual
(`7837c96027e5e562…`) é o de 19/09 com o despejo de IB e sem o fail-open de fence. O binário publica `Mesa 26.2.0-devel (git-801c9763c6)`,
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

23 arquivos modificados, ~1.122 linhas. Núcleo:

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
3. **Despejo de IB opt-in (novo em 19/09)** — `radv_wddm2_cs.c`, `bc250_dump_ib()`.
   Com `BC250_RADV_IB_DUMP=<diretório>` **ou** a chave
   `HKLM\SOFTWARE\BC250\Umd\GfxIbDumpDir` definida, cada IB concatenado é gravado cru em
   `bc250_ib_<n>_va<hex>_dw<n>.bin` (teto de 16 arquivos, e o teto é anunciado no stderr).
   **Fail-closed:** sem a variável nada é aberto e o caminho de submissão fica byte a byte o
   mesmo. Existe porque a Corrida 34 parou dentro de um IB de 1008 DW do qual não guardamos
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
