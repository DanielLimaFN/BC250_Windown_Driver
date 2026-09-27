# Critério da Corrida 65 — CPU-Visible Resource & Bypass do Descritor Faultante

> **Escrito ANTES de qualquer alteração de código ou ensaio físico** (Lei 9).
> **Candidato:** lab **3.226.1.0** (faixa DX.18).
> **Perfil:** `DISPLAY_RENDER_LAB`, WDDM 2.7, P1 PERSISTENT SUBMISSION.
> **Estado:** NÃO VALIDADO EM HARDWARE.

---

## 0. O que a Corrida 64 respondeu

Na Corrida 64 (lab `3.225.1.0`):
1. **A escrita da CPU no índice 4096 é 100% íntegra:**
   - 99 de 99 escritas conferiram com readback imediato (`arenaReadback == arenaEncoded`).
   - `[PDE_WRITE] DIVERGENCIA = 0`.
   - A hipótese de ponteiro corrompido ou mapeamento falso na CPU está **REFUTADA**.
2. **O fault `Cid=10` foi reproduzido com a mesma assinatura:**
   - `FaultAddrPagina = 0x2431` $\longrightarrow$ `GpuVa = 0x2431000`.
   - Caminhada no endereço faltado (`0x2431000`) parou em `PDE=0` (não mapeado).
   - Caminhada em `0x243100000` (`VA << 8`) resolveu perfeitamente para `PA = 0x451CCC000`.
3. **Causa-raiz exata isolada no cruzamento dos logs KMD e UMD:**
   - `Allocate ENTER: Token=0x5245530004B00016 Size=11059200 (2560x1080 bpp=4) Primary=0 hRTResource=...`
   - `MapAllocation LEAVE: hr=0x8000000A VA=0x242F00000`
   - `Upload ENTER: hAlloc=0xc0001000 Bytes=11059200`
   - `Upload LOCK falhou: hr=0x80070057` (`E_INVALIDARG` retornado por `pfnLockCb`)
   - `ResourceUpdateSubresourceUP: WDDM allocation NAO sincronizada (status=CALLBACK_FAILED hr=0x80070057)`
   - UMD acionou fallback Vulkan: `Bc250UmdSyncTextureToVkImage` $\longrightarrow$ `Bc250UmdVkUploadImageData` $\longrightarrow$ `vkCmdCopyBufferToImage`
   - O compute shader interno do RADV (`radv_meta_buffer_to_image_cs`) utilizou descritor de imagem `STORAGE_IMAGE` com `desc[0] = va >> 8` (`0x2431000`).
   - A GPU dereferenciou `desc[0]` (`0x2431000`), causando fault de leitura `Cid=10` sob VMID 2.
   - Stall de 4,11 s $\longrightarrow$ TDR $\longrightarrow$ reset do hardware $\longrightarrow$ próxima alocação falhou com `0x88760870` (`D3DDDIERR_DEVICEREMOVED`) $\longrightarrow$ colapso do DWM.

---

## 1. As Perguntas Desta Corrida

1. **Pergunta 1 (pfnLockCb / CpuVisible):**
   Ao incluir `BC250_ALLOCATION_TOKEN_CLASS_RESOURCE` (`0x52455300u`) na classe de alocações que exigem acesso de CPU (`BC250_ALLOCATION_FLAG_CPU_VISIBLE`, `Bc250AllocationRequiresCpuAccess` e `info->FlagsWddm2.CpuVisible = 1u`), o VidMm passa a aceitar `pfnLockCb` em todas as texturas e buffers do DWM, eliminando o erro `0x80070057`?

2. **Pergunta 2 (Bypass do Descritor Faultante):**
   Com o upload de alocação WDDM bem-sucedido via CPU-visible VRAM, a sincronização de textura na UMD marca o recurso como sincronizado (`WddmAllocationUploaded = TRUE`), definindo o layout ótimo para SRV (`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`) e dispensando a cópia redundante e faultante via `radv_meta_buffer_to_image_cs`. Isso elimina o stall de 4 s e o fault `Cid=10` durante a atualização de texturas do DWM?

---

## 2. O que Muda no Candidato 3.226.1.0

1. **Winsys (`driver/umd/bc250_wddm_winsys.c`):**
   - Na criação de alocação, `tokenClass == BC250_ALLOCATION_TOKEN_CLASS_RESOURCE` recebe `flags |= BC250_ALLOCATION_FLAG_CPU_VISIBLE`.
2. **Contrato de ABI de Alocação (`driver/core/bc250_allocation_abi_contract.c`):**
   - `Bc250AllocationRequiresCpuAccess` inclui `tokenClass == BC250_ALLOCATION_TOKEN_CLASS_RESOURCE`.
3. **Miniport (`driver/wdk/bc250_active_miniport.c`):**
   - `CreateAllocation` assegura que alocações do tipo `RESOURCE` e alocações onde o runtime solicitou `CpuVisible` recebam `info->FlagsWddm2.CpuVisible = 1u`.
4. **UMD D3D10/11 (`driver/umd/bc250_d3d_umd.c`):**
   - Adicionado rastreamento de sincronia WDDM (`res->WddmAllocationUploaded`).
   - Quando `Bc250WinsysUploadAllocation` tem êxito (`BC250_WINSYS_OK`), `WddmAllocationUploaded` é marcado como `TRUE`.
   - Em `Bc250UmdSyncTextureToVkImage`: preservado o incremento obrigatório `++res->TextureWriteDdiCount` e, caso o recurso já esteja sincronizado via upload WDDM, atualiza o layout da `VkImage` e realiza o bypass seguro de `Bc250UmdVkUploadImageData`, evitando o dispatch do compute shader problemático.
   - Em `Bc250UmdVkCreateResource`: se os dados iniciais (`pInitialDataUP`) já foram sincronizados pelo upload WDDM, realiza o bypass do upload Vulkan.
5. **Suítes de Teste Offline:**
   - Adicionados testes de contrato em `driver/tests/allocation_abi_contract_test.c` e `driver/tests/active_miniport_test.c` validando que tokens de classe `RESOURCE` recebem `CpuVisible = 1u`.

---

## 3. Desfechos Previstos (Lei 10)

### Desfecho A — Sucesso Total na Atualização de Texturas (Meta Principal)
- **Assinatura:**
  - `Upload ENTER` em recursos `RESOURCE` seguido de `Upload LEAVE: hr=0x00000000` (zero `Upload LOCK falhou: hr=0x80070057`).
  - Zero stalls de 4 s no DWM.
  - Zero faults `Cid=10` no log do KMD.
  - DWM prossegue além de `ResourceUpdateSubresourceUP` sem `D3DDDIERR_DEVICEREMOVED`.
- **Implicação:** A cadeia de upload de texturas via WDDM e o bypass do descritor de meta-copy estão provados no silício. Avança para a composição e apresentação de quadros.

### Desfecho B — pfnLockCb Passa, mas Outro Fault Ocorre no Render
- **Assinatura:**
  - `Upload LEAVE: hr=0x00000000` em todos os recursos.
  - TDR ocorre posteriormente durante submissões de desenho ou apresentação, com log reportando fault em outro cliente (`Cid != 10`) ou no estágio de rasterização/pixel shader.
- **Implicação:** O subsistema de upload de recursos está sanado; o bloqueador passa a ser a emissão gráfica ou o pipeline de shaders do DWM.

### Desfecho C — pfnLockCb Continua Falhando
- **Assinatura:**
  - Presença de `Upload LOCK falhou: hr=0x80070057` no log da UMD.
- **Implicação:** O VidMm recusa CPU-visibility por restrição adicional na abertura/compartilhamento de recursos ou política de segmento. Hipótese de flags refutada.

---

## 4. Governança e Regras de Conclusão

- Conformidade com `AGENTS.md` e `GEMINI.md`:
  - Zero WARP, zero fallback de cópia por CPU.
  - Telemetria baseada em registradores físicos e medições reais.
  - Relato estruturado obrigatório após a execução física do candidato.
