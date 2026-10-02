# 496 — M1b tem três bloqueios; a rota mais limpa é Resizable BAR + M2 com segmento visível à CPU

> 28/09/2026, offline. Pedido do Daniel: M1b (memória da RADV na RAM do sistema) e M2 (VRAM além do BAR). A leitura de código e de probes mudou o plano.

## 1. Por que a M1b não é uma flag (P1, leitura)
Na winsys da RADV (`radv_wddm2_bo.c`), o blob BCA1 v1 **não carrega o domínio** (VRAM/GTT). O KMD põe tudo no segmento 1, então os BOs que a RADV pediu como **GTT (memória do sistema)** ocupam a VRAM. Honrar o GTT esbarra em três caminhos do KMD que endereçam memória por endereço físico de VRAM:
1. **Gate de envio gráfico** (`GATE FISICO`/`GfxIbInspect`): caminha e lê o IB pela VRAM. Os command buffers da RADV são GTT; na RAM do sistema, o gate reprovaria **todo** envio gráfico.
2. **`COMPOSITE_BLT`:** na 149 houve fontes que eram BOs pequenos da RADV (`SrcVa=0x240080000`, 4 KB). Na RAM do sistema, as páginas não são contíguas e a cópia por MC não as alcança.
3. **Buffer linear do Present do harness:** pede `HOST_VISIBLE|HOST_COHERENT` (`bc250_umd_vulkan_bridge.c`), que pode ser GTT.

Resolver os três é possível (inspeção por mapeamento de sistema, cópia por GPUVA, tipo de memória explícito), mas é trabalho grande e com risco sobre os caminhos que hoje funcionam.

## 2. Por que a M2 "sem CPU" rende pouco
A atribuição da 150 mostrou que **2.346 MB dos ~2.600 MB despejados são BOs visíveis à CPU** (`CpuVisible=1`). Um segmento além do BAR sem acesso da CPU só pega os ~248 MB de BOs `CpuVisible=0`.

## 3. O dado que muda o plano (P0, probe do leitor ReBAR, 150)
`RebarSupportedMask=0x700`, `RebarSelectedEncoding=8`, `RebarSelectedMiB=256`, `RebarAssignedMiB=256`.

Pela codificação do leitor (encoding 8 = 256 MB), a placa **suporta BAR de 256 MB, 512 MB e 1 GB**, e a BIOS atribuiu 256 MB.

## 4. Rota proposta: ReBAR na BIOS + M2 visível à CPU
1. **BIOS (Daniel):** habilitar *Above 4G Decoding* e *Resizable BAR*, se a BIOS da BC-250 oferecer. Com BAR de 512 MB, os 512 MB de `UMA_SIZE` ficam **inteiros** visíveis à CPU. Com 1 GB de BAR, dá para subir `UMA_SIZE` para 1 GB.
2. **Corrida de controle sem código novo:** o driver atual lê `CpuVisibleBytes` da topologia e já trata "opaco" como `Total − CpuVisible`. Essa corrida só mede se o BAR maior é atribuído (probes ReBAR/topologia) e se o boot e a tela do GOP seguem normais. O segmento continua limitado pelo corte de `0x0FE00000`, então o comportamento do VidMm não muda.
3. **M2 visível à CPU (código):** declarar `[0x10000000, Total − reserva)` como **segmento 3, local e visível à CPU**. Aí ele serve para os BOs `CpuVisible=1` da RADV, que são o grosso do despejo, com a mesma aritmética de MC do segmento 1. Sem os bloqueios da M1b, porque é VRAM contígua, e com a CPU alcançando pelo BAR.

**Riscos:**
- a BIOS pode não oferecer ReBAR, ou o Windows 10 pode não mapear o BAR maior;
- o GOP pode se comportar diferente com o BAR maior;
- partes do código que presumem uma janela de 256 MB (FrameBufferVirtual, provas por amostra).

Tudo precisa ser auditado antes da corrida de M2.

**NADA DISTO FOI IMPLEMENTADO NEM VALIDADO EM HARDWARE.**

## 5. Resultado da verificação da BIOS (Daniel, 28/09)
- A BIOS da BC-250 **não oferece** *Above 4G Decoding* nem *Resizable BAR* (verificação do operador). A rota da seção 4 fica **fechada** enquanto não houver BIOS que atribua um BAR maior. O dispositivo suporta 512 MB/1 GB (P0 do leitor ReBAR), mas a atribuição é da BIOS.
- **Consequência para a memória:** a VRAM visível à CPU fica em 256 MB. Mais memória para a carga real passa pela **M1b**, com os três bloqueios da seção 1 resolvidos nesta ordem:
  1. gate e inspeção do IB gráfico aceitando páginas de sistema;
  2. composição a partir de BO de sistema (cópia por GPUVA em vez de MC);
  3. tipo de memória explícito para o buffer linear do Present.
- **Decisão do Daniel:** antes da memória, atacar o **desempenho (FPS)**. A M1b fica registrada como próxima frente de memória.
