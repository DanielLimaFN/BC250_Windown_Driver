# 540 — Corrida 182: ícones do menu Iniciar visíveis, crash do dwm.exe no binding de descritores, T4.3 e critério da Corrida 183

> 30/09/2026. Corrida 182: bind às 07:52:49, candidato T4.2 (**3.327.1.0**). Evidência: `docs/evidence/2026-09-30_07-52-49-DESKTOP-HQAG6T4-lab-binding/`.
> Faixa T4.3 offline, candidato **3.328.1.0**. **Nada da T4.3 foi validado em hardware.**

---

## 1. Relato da Corrida 182

1. **Houve avanço?** **Sim, funcional.** Pela primeira vez o menu Iniciar mostrou os ícones (relato do Daniel), contra blocos cinza nas Corridas 179 a 181.
2. **Sucessos:**
   - **Critério da 182 fechou no desfecho A.** Nos logs FINAL dos dois dwm.exe:
     - 57 de 57 `OpenResource T4.2: Map=0 Resident=0 WaitPaging=0`;
     - 30 `Superficie compartilhada carregada via SDMA GPU`;
     - **zero** `Fallback ... (0xFF202020)`;
     - 515 `BRIDGE GPU BLT LEAVE: hr=0x00000000` e zero com erro.
   - **Corrida inteira sem trava:** 588,5 s pelo QPC, 5.780 IBs executados, 3.371 FILLs e 2.595 TRANSFERs de paginação, zero TDR, zero BugCheck, zero GFXHUB_FAULT, zero DEVICE_REMOVED.
   - **Rollback online limpo:** `RECOVERY_OK_ONLINE` às 08:03:01, BasicDisplay em Código 0 em 5 amostras.
   - **Coleta FINAL funcionou** (a máquina não travou).
3. **Falhas:**
   - **dwm.exe 1288 morreu às 07:54:41** com `c0000005` em `bc250umd.dll+0x8376` (evento 1000 do Windows). Um novo dwm.exe (5220) subiu às 07:54:46 e durou até o fim.
   - **Lentidão (relato do Daniel), medida no dwm.exe 5220:**
     - intervalo mediano entre Presents de 61–82 ms nos quadros ativos (~12–16 FPS);
     - 781 flushes síncronos, ~3 por quadro, com espera mediana de 7,8 ms; ocupam ~23% do quadro ativo;
     - principais motivos dos drenos: destruir recurso (545), destruir shader (302), `ResourceMap DISCARD` (267), destruir RTV (165);
     - pfnPresentCb: mediana 2,1 ms; Present no KMD: mediana 1,5 ms.
   - **Bugs visuais (relato do Daniel):** explicação na §2 (H forte).
   - **1 `Blt Timeout` falso no KMD** (2 linhas, Present #376 recusado após 101 ms): ver §4.
   - **BYTE_PROOF de ~6,5 s** continua nos quadros de prova (máximo de 6.509.463 µs). Dívida conhecida (doc 539).
4. **Veredito:** **parcial.** O critério da 182 (desfecho A) foi cumprido; a estabilidade do DWM e a fluidez não.
5. **Recuperação e pendências:**
   - driver ativo ao final: BasicDisplay em Código 0;
   - NÃO VALIDADO EM HARDWARE: DWM sem crash por 600 s, conteúdo do menu sem artefatos, 60 FPS e jogos;
   - próximo passo: Corrida 183 com a T4.3.

## 2. Causa do crash (P0 + leitura de código)

- **Endereço do crash:** `+0x8376` foi resolvido com um build de símbolos das mesmas fontes e flags. Os bytes nesse ponto são **idênticos** aos do binário empacotado (`8b c7 48 8d 0c c0 4d 39 84 cc d0 1d 00 00`).
- **Linha:** o endereço cai em [bc250_umd_descriptors.c:305](../driver/umd/bc250_umd_descriptors.c) (numeração da T4.2), no laço `batch->RestoreImages[prior].image` dentro de `Bc250UmdBindDrawDescriptors`. As entradas de 72 bytes no offset `0x1DD0` são `VkImageMemoryBarrier`.
- **Última linha do dwm.exe 1288:** `Superficie compartilhada carregada via SDMA GPU: 640x640`.
- **Mecanismo (leitura de código):**
  1. Os caminhos de carga de superfície aberta (T4.0/T4.1) rodavam **dentro** de `Bc250UmdBindDrawDescriptors`, depois do `calloc` do `batch`.
  2. Eles chamam `Bc250UmdVkDrainPending` e `Bc250UmdVkCopyStagingBufferToImage`, e ambos terminam em `FlushCommands`.
  3. `FlushCommands` bem-sucedido chama `Bc250UmdVkReleaseDescriptorBatches` ([bc250_umd_vulkan_bridge.c](../driver/umd/bc250_umd_vulkan_bridge.c)): faz `free()` de **todos** os batches, inclusive o que estava em preenchimento, e destrói as image views já criadas nele.
  4. O último flush deixa `RecordingActive=FALSE`. Mesmo assim, o resto do binding e do draw era gravado no mesmo `cmd`, que estava fora de gravação.
- **Consequências:** o item 3 dá use-after-free (crash quando a memória liberada é reaproveitada); os itens 3 e 4 dão descritores com views destruídas e draws gravados num command buffer inválido. Isso é a explicação provável dos **bugs visuais** (H forte, sem prova por pixel).
- **Corrida 181:** a última linha do dwm.exe também foi o fim desse caminho (lá, o fallback cinza, que chama `Bc250UmdVkUploadImageData` e também drena). Mesma classe provável; não há evento de crash porque a máquina travou.

## 3. A mudança (T4.3), uma variável, só na UMD

- **Carga fora do binding:** o bloco de carga saiu **sem alteração de lógica** para `Bc250UmdMaterializeOpenedSrvImage`, chamado por `Bc250UmdMaterializeDrawSrvs`, que percorre só os bindings de imagem da pipeline do draw.
- **Ordem no draw:** `Bc250UmdExecuteVkDraw` chama a carga **antes** de `Bc250UmdBindDrawDescriptors`, com o render pass já fechado pelo DX.75, e depois obtém o command buffer de novo (`Bc250UmdVkGetCommandBuffer`). Falha nessa obtenção recusa o draw com o motivo `COMMAND_BUFFER_AUSENTE_APOS_CARGA_DE_SRV`.
- **Nada drena dentro do binding:** dentro de `Bc250UmdBindDrawDescriptors` sobra só a recusa `SRV_RECURSO_SEM_VKIMAGE`. `Bc250UmdVkUploadBufferData`, que continua lá, não drena (conferido).
- **Mantidos:** T4.2, o cinza da T3.8 e o staging da T4.1.

**Provas offline:**
- `build-umd.bat` EXIT 0 e `build-contract.bat` EXIT 0.
- Portão novo `audit_descriptor_binding_never_drains` em [audit-bc250mcdm-safety.py](../tools/audit-bc250mcdm-safety.py), com **três mutações verificadas**, cada uma reprovando com a própria mensagem:
  1. um dreno dentro do binding;
  2. a nova obtenção do command buffer removida;
  3. a carga removida do draw.
- Pacote 3.328.1.0 assinado; auditorias lab package, safety, DDI matrix e noop PASS; `prepare-corrida183-t43.ps1` EXIT 0.
- A UMD empacotada contém as strings da T4.2 e da T4.3.

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado (**idêntico ao da 182**) | `2BBD88C8429016A5AB86E3D9CEF31AA434F1B1799106D40C6DE864A39C9A12B2` |
| `bc250mcdm.inf` | `D0D419446BF338D51D35FC3315060B0A02E87FB01C53B686FD31A679B218E736` |
| `bc250mcdm.cat` | `9A95FD0CA7F8B5E27E8E1D1C66A8662194153574DA1EC2DBCE15D081064B1873` |
| `bc250umd.dll` empacotada | `4B3FC04D3DA2CD392A707CB2D60D5851E8770621B1E21110D41339C9000505CC` |
| UMD fonte | `A29E97504D6D96AEEBA65F82F9F017B4506E535D991681CD425969111881D639` |
| `vulkan_radeon.dll` | `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C` |

## 4. Dívidas declaradas, não tocadas nesta faixa

1. **Blt Timeout falso no KMD (H forte, leitura de código).**
   - Em [bc250_sdma_present.c:921-930](../driver/wdk/bc250_sdma_present.c) o `RingLock` é solto antes do laço (T3.5), e a espera exige `fenceObserved == nextFence`.
   - Se outro blt entra no meio, ele grava `nextFence+1` no mesmo slot, e a espera nunca vê o valor exato.
   - Medido: `FenceExp=0x274D FenceObs=0x274E`, `RPTR==WPTR`, `STATUS1=0x3FF` (ociosa) → Present #376 recusado.
   - Correção proposta: comparação com volta, `(int32_t)(obs - exp) >= 0`. Mexe no KMD, por isso fica para uma faixa própria.
2. **Log com `FILE_FLAG_WRITE_THROUGH` (H).** ~89 linhas por quadro ativo, ~528 µs de parede por linha em média. Foi escolha deliberada, para o log sobreviver às travas duras; reduzir exige decisão do Daniel.
3. **Drenos síncronos** em destruição de recurso, shader e RTV e em `ResourceMap DISCARD`: ~3 por quadro, ~23% do quadro ativo.
4. **Superfície aberta carregada uma única vez** (só quando `VkImg` não existe): conteúdo que mude depois pode ficar desatualizado (H).
5. BYTE_PROOF de ~6,5 s; `Deallocate` sobre recurso aberto (doc 539).

## 5. Critério da Corrida 183 (escrito antes, Lei 10)

**Procedimento:** boot frio; `tools\EXECUTAR_CORRIDA183_T43_600S.cmd` como Administrador; o mesmo roteiro (~20 s de desktop e depois o menu Iniciar, várias vezes).

**Controles que não podem regredir:** desfecho A da 182 (Map=0, carga via SDMA, zero fallback); zero BugCheck; zero trava.

| Desfecho | Assinatura (o que o log e o Windows mostram) | Implicação |
|---|---|---|
| **A** | Zero eventos 1000 de `dwm.exe` com módulo `bc250umd.dll`; um único dwm.exe do início ao fim; `Superficie compartilhada carregada via SDMA GPU` presente; zero `COMMAND_BUFFER_AUSENTE_APOS_CARGA_DE_SRV` | Crash fechado; o próximo alvo é a fluidez (dívidas 1–3) e os artefatos que restarem |
| **B** | Evento 1000 com `bc250umd.dll` em **outro** deslocamento | Outro defeito; resolver o deslocamento com o build de símbolos, como nesta faixa |
| **C** | Evento 1000 com `bc250umd.dll` no mesmo deslocamento relativo ao laço de `RestoreImages` | A T4.3 não fechou o caminho; existe outro dreno dentro do binding |
| **D** | `COMMAND_BUFFER_AUSENTE_APOS_CARGA_DE_SRV` aparece | A nova obtenção do command buffer falhou (`SubmissionFailed`); investigar o flush anterior |
| **E** | Algo fora do mapa | Descrever antes de julgar |

**Observação separada, em qualquer desfecho:** o Daniel relata se os artefatos visuais diminuíram. Pelo §2 eles devem diminuir; se não diminuírem, a hipótese de que vinham do command buffer fora de gravação cai.
