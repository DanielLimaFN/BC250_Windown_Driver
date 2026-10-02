# 543 — Corrida 185: o DWM compunha uma foto da superfície do menu, T4.6 e critério da Corrida 186

> 30/09/2026. Corrida 185: bind às 09:45:53, candidato T4.5 (**3.330.1.0**). Evidência: `docs/evidence/2026-09-30_09-45-53-DESKTOP-HQAG6T4-lab-binding/`.
> Faixa T4.6 offline, candidato **3.331.1.0**. **Nada da T4.6 foi validado em hardware.**

---

## 1. Relato da Corrida 185

1. **Houve avanço?** **Em diagnóstico, sim; funcional, regressão.** O Daniel relata o menu muito lento e **sem ícones**, contra ícones em ~15 s na 184. A regressão expôs a causa de fundo.
2. **Sucessos:**
   - **T4.5 funcionou como desenhada:** no menu, 1.486 draws para 183 flushes, só 8 com flush imediato. Na 184 eram ~1 flush por draw.
   - **Sem regressão da 184:** zero `size/alignment mismatch`, zero `NAME_COLLISION`; 3 cargas da DLL no menu.
   - **Estabilidade:** um dwm.exe de 09:46:03 a 09:55:38. KMD: 591,2 s, 3.763 IBs, zero timeout (inclusive `Blt Timeout`), zero TDR, zero BugCheck, zero trava. Rollback online em Código 0.
3. **Falhas:**
   - **Ícones ausentes (causa na §2).**
   - **Explorer caiu às 09:56:01** (`ucrtbase.dll`), 6 s depois de o rollback começar (09:55:55). Mesmo padrão da 183, fora da janela do ensaio (H: remoção do adaptador).
4. **Veredito:** **falha** frente ao objetivo visual. O critério da 185 fechou no desfecho A (flushes caíram), mas o efeito visível regrediu.
5. **Recuperação e pendências:** BasicDisplay em Código 0. Próximo passo: Corrida 186 com a T4.6.

## 2. Causa: o DWM compunha uma foto da superfície compartilhada (P0)

**Instrumento novo:** [tools/cross-shared-surface-timeline.py](../tools/cross-shared-surface-timeline.py). Ele cruza, pelo token, o log do dwm.exe (abertura e cópia de cada superfície) com o do StartMenuExperienceHost (draws naquela superfície).

| Token | Tamanho | DWM abriu | DWM copiou | Menu: 1º draw | Menu: último draw | Draws depois da cópia |
|---|---|---|---|---|---|---|
| `0x524553001B68000E` | 640x640 | 09:47:11.599 | **09:47:12.591** | **09:47:15.408** | 09:48:07.975 | **564/564** |
| `0x524553001B68005C` | 640x640 | 09:48:08.339 | 09:48:08.405 | 09:48:08.096 | 09:48:13.666 | 88/92 |
| `0x524553001B6800B5` | 640x608 | 09:51:10.139 | 09:51:10.443 | 09:51:09.909 | 09:51:49.898 | 376/382 |
| `0x524553001B6800E7` | 640x672 | 09:51:50.105 | 09:51:50.438 | 09:51:50.137 | 09:52:09.031 | 232/238 |

**Mecanismo (leitura de código):**
- Os draws do menu vão para a VkImage do recurso. O conteúdo chega à alocação WDDM compartilhada **no Present do menu**, por `Bc250UmdBridgeLinearToWddm`.
- O dwm.exe criava a VkImage dele a partir da alocação aberta **uma única vez** (camada 2 da T4.0/T4.1, só quando `VkImg` não existia) e nunca mais recopiava. Era a dívida 4 do doc 540.
- Com o flush por draw da 184 o tempo às vezes favorecia a cópia. Com a T4.5 a cópia passou a ser feita antes do conteúdo.
- **Por que os ícones apareciam na 184:** não está fechado. O cruzamento na 184 mistura cargas por causa de tokens repetidos entre aberturas.

**Como o Linux faz:** quem importa um BO compartilhado lê a **mesma memória**; não há cópia a manter em dia. Esse é o destino. A T4.6 é o passo intermediário.

## 3. A mudança (T4.6), uma variável, só na UMD

- **`OpenedShared`:** marca posta pelo `OpenResource` nos recursos de outro processo.
- **`DxgiPresentCount`:** contador por device, avançado em `Bc250DxgiPresent` e `Bc250DxgiPresent1`; é a fronteira de quadro.
- **Recopia por quadro:** na etapa de carga da T4.3 (antes do batch de descritores), um recurso aberto já com VkImage é **recopiado** no primeiro uso de cada quadro do DWM (`Bc250UmdRefreshOpenedSrvImage`). Usa o mesmo caminho da camada 2: staging, dreno, blt SDMA pelo KMD e `vkCmdCopyBufferToImage`.
- **Log:** `[VK_RES] T4.6 RECOPIA de superficie aberta #n` (16 primeiras, depois 1 em 64, e toda falha).
- **Limpeza cosmética:** o bloco movido na T4.3 tinha ganhado uma linha em branco entre cada linha, por gravação `\r\r\n` do meu script. Removidas, com asserção de que o código sem espaços é idêntico.

**Custo esperado (declarado):** um dreno e duas submissões por superfície aberta por quadro do DWM. É o preço da correção sem aliasing; a fluidez tende a piorar no DWM enquanto o menu estiver na tela.

**Provas offline:**
- `build-umd.bat` e `build-contract.bat` EXIT 0.
- Portão novo `audit_opened_surface_is_refreshed_per_frame`, com **três mutações verificadas**: sem a marca, sem o contador no Present, sem a recopia.
- Pacote 3.331.1.0 assinado; 5 auditorias PASS; a UMD empacotada contém `T4.6 RECOPIA`; `prepare-corrida186-t46.ps1` EXIT 0.

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado (**idêntico ao da 182–185**) | `2BBD88C8429016A5AB86E3D9CEF31AA434F1B1799106D40C6DE864A39C9A12B2` |
| `bc250mcdm.inf` | `E590955B2503A3ED97082E5736097DDEB81535DCB73185101DFCE71FBC63203F` |
| `bc250mcdm.cat` | `41D9677121A9E7CE7033DD740424F27D0F1A7BD01E707C294E1F8C2F56C53630` |
| `bc250umd.dll` empacotada | `C879658CF67D99638C3C731CACD3F02F5C6386C63DD2954DF94B52B9337AB29F` |
| UMD fonte | `A8158CA8365AF457A36075B18005B418E770B626B06A37FB8D69644CE26BAF25` |

## 4. Dívidas declaradas

1. **Aliasing da superfície aberta:** o destino é a VkImage do DWM ler a alocação compartilhada diretamente, como no Linux.
2. **Sincronização entre processos:** a recopia não espera o produtor terminar; pode pegar um quadro no meio da escrita. Depende do item 1.
3. **Dívidas dos docs 540–542:** log com escrita direta, cache de pipeline por carga, drenos na destruição, `Blt Timeout` falso, tokens.

## 5. Critério da Corrida 186 (escrito antes, Lei 10)

**Procedimento:** boot frio; `tools\EXECUTAR_CORRIDA186_T46_600S.cmd`; mesmo roteiro. O Daniel informa se os ícones aparecem e em quanto tempo.

**Controles que não podem regredir:** zero mismatch e zero `NAME_COLLISION`; um dwm.exe; zero BugCheck e zero trava.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | `T4.6 RECOPIA` com `blt=0x00000000 copia=1`; no cruzamento, cada superfície do menu tem **recopia depois do último draw** do menu; o Daniel vê os ícones | Foto congelada fechada; o próximo alvo é o custo (aliasing ou recopia só quando o produtor apresentar) |
| **B** | Recopias com `copia=1`, mas os ícones não aparecem | O conteúdo não está na alocação no momento da cópia (escrita de volta do Present do menu ou sincronização); investigar `Bc250UmdBridgeLinearToWddm` no menu |
| **C** | `T4.6 RECOPIA` com `blt` diferente de 0 ou `copia=0` | Falha no caminho de cópia; o `hr` nomeia a causa |
| **D** | Nenhuma linha `T4.6 RECOPIA` no dwm.exe | A condição não disparou (`OpenedShared`, dimensão ou contador); ler a etapa de carga |
| **E** | Algo fora do mapa | Descrever antes de julgar |
