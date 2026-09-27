# Corrida 106 (DX.63b): triângulo do harness composto pelo DWM; DX.64 e critério da Corrida 107

> **Corrida 106:** 24/09/2026, bind às 16:57:40.
> - Rollback às 17:00:42.
> - `RECOVERY_OK_ONLINE` às 17:00:52, **no mesmo boot** (tentativa 1).
>
> **Candidato:** lab `3.267.1.0`, SYS `2E76ACEB…`, UMD `3B70D7F2…`.
> **Evidência:** `docs/evidence/2026-09-24_16-57-40-DESKTOP-HQAG6T4-lab-binding/`.
> - `display-log-this-bind-FINAL.txt`: 17.637.339 bytes, `complete_at_capture=true`.
> - `umd-logs-pos-travamento/`: 8 logs.
> - **7 fotos do operador**, enviadas na conversa. Não estão arquivadas na pasta.
>
> **Critério:** [doc 414 §5](414_CORRIDA105_RESULTADO_E_DX63B_CRITERIO_CORRIDA106.md).
> **Relato do Daniel:** "aparentemente igual à corrida anterior; depois de uns 140 s começa a bugar a tela, fica piscando mostrando pedaços da tela".

## 1. Critério, eixo 1: desfecho A

**Controle:**
- `[GetStdAllocData] Filled` com geometria para staging (`1280x720 pitch=5120 fmt=21`) e para as superfícies GDI;
- `Geometria=0x0` para primária e shadow;
- zero `Blt 2D … -> scanout`.

**Resultado:**
- **Zero** `BLT_DESTINO RECUSADO` nos 60 presents Blt. O `OK` é logado nos primeiros 10 e a cada 60, daí 3 linhas; nenhum `RECUSADO: copia GPU`.
- **5/5** `BYTE_PROOF PASS` nos flips.
- Harness com 60/60 presents aceitos, 0 falhos.

**Imagem (fotos do operador, P2 visual; a execução da cópia é P0 pelo KMD):**
- **A janela "AMD BC-250 Direct3D 11 Nativo (bc250umd.dll)" aparece com o triângulo em degradê vermelho/verde/azul, na posição real da janela, composta pelo DWM**, e se move com ela.
- É a primeira vez que o quadro de uma aplicação D3D11, desenhado pela nossa UMD (D3D11→Vulkan/RADV), chega ao monitor pelo caminho de composição do Windows.
- Uma foto mostra um triângulo fantasma à esquerda durante o arraste: conteúdo antigo na área que a janela deixou. É compatível com a composição a ~1 FPS, sem medição que confirme.
- **A cópia para o destino não teve prova byte a byte** (o destino não é o scanout). Os pixels do destino continuam **NÃO VALIDADOS** por máquina.

**Outros:**
- zero TDR, zero timeout, zero GFXHUB_FAULT;
- zero `PREEMPT_DONE Node=0`;
- `COMPOSITE_BLT` com 200 OK e 101 recusados por resolução.

## 2. O "bugar" depois de ~140 s: causa em duas camadas (P0 por log e código)

### Camada 1: o gatilho

O DWM destruiu e recriou o device D3D duas vezes:
- às 16:58:46 (~66 s após o bind);
- às 16:59:23 (~103 s).

Cerca de 3 s antes de **cada** recriação aparece a mesma sequência no log do DWM:

```
ResourceCopyRegion: dst=…602D6F0 (textura 256x256, SRV|RT, recém-criada)
[VK_BRIDGE] ResourceCopyImage Vk: … Extent=50x26
[VK_DESCRIPTOR] RECUSADO … SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA … TextureWrites=0 VkLayout=UNDEFINED
Draw: RECUSADO -- backend Vulkan motivo=SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA
```

**O defeito (P0 por código), em `Bc250UmdVkCopyImageInternal`:**
- as barreiras supunham `COLOR_ATTACHMENT_OPTIMAL` como layout anterior das duas imagens, qualquer que fosse o real;
- a função nunca gravava `VkLayout`.

**O que isso provoca:** a textura foi escrita pela GPU e continuou marcada como nunca escrita. O Draw recusado chama `pfnSetErrorCb(E_NOTIMPL)`. Que isso leve o DWM a recriar o device é **P2**: correlação 2 de 2, ~3 s depois, e nenhuma outra recusa no intervalo.

### Camada 2: a recriação não sobrevive

Esta camada é separada e fica aberta.

**Tokens e VAs repetidos:** o DWM recria as três primárias com **os mesmos tokens** de alocação (`0xA1100001000000{13,14,15}`) e **as mesmas VAs** (0x202070000, 0x2000D0000, 0x200B60000). O contador de token da UMD recomeça quando a DLL é recarregada.

**Depois disso:**
- O bridge escreve os quadros (`NonZeroGrade=86400`) nessas VAs.
- O KMD **não resolve** as VAs (`SpaceResolveAddress failed: status=9`, 108 vezes cada).
- O `SetVidPnSourceAddress` aponta para as primárias físicas novas (0x4E2C000, 0x7C0C000, 0x28FC000), e o `PRIMARY_COPY` as encontra **zeradas** (`SrcNZ=0/256`, `MATCH_BLANK_INCONCLUSIVE`). Ele copia preto para o scanout.
- O flip (`DdiPresent`), sem resolver a VA, cai em `PRIMARY_FALLBACK`, que **soma a base do segmento de novo**: `SegOff=Addr=0x28FC000 → VramOff=0x3388000`.

**Prova da soma dupla:** no início da corrida, `Addr` do `SetVidPnSourceAddress` = `VramOff` do flip (0x153C000), então `Addr` já é deslocamento de VRAM. Somar 0xA8C000 (um quadro de 2560×1080) lê a região seguinte.

**Resultado na tela:** preto alternando com pedaços de quadros antigos, exatamente o que as fotos mostram.

## 3. Veredito da 106

**Sucesso no eixo do critério (A) e avanço funcional:**
- janela D3D11 com triângulo composta pelo DWM na tela;
- recuperação no mesmo boot pela 2ª vez seguida.

**Falhas:**
- o desktop degrada depois da recriação do device (causa identificada acima);
- ~1,6 FPS;
- o laço do Explorer continua.

**NÃO VALIDADOS EM HARDWARE:**
- pixels do destino por máquina;
- recriação de device do DWM;
- 600 s;
- travamento duro;
- DX.60;
- jogo.

## 4. DX.64 (offline, uma variável): a cópia de imagem respeita e grava o layout rastreado

Mudança em `Bc250UmdVkCopyImageInternal`:
- **Layout anterior:** o rastreado da origem e do destino. `UNDEFINED` é sempre um `oldLayout` válido e descarta só conteúdo que nunca existiu.
- **Barreiras:** cobrem a imagem inteira (todos os mips e camadas), como no caminho de descritores.
- **Origem:** volta ao seu layout.
- **Destino:** passa a declarar conteúdo **só se a origem tinha**.
  - `COLOR_ATTACHMENT` para imagem de 1 mip e 1 camada, ou se já era alvo.
  - `SHADER_READ_ONLY` com mips/camadas, porque a cópia escreve só o subrecurso 0 e o descritor recusa views além dele.
  - Sem conteúdo nos dois lados, o destino fica `UNDEFINED`.
- **Flush com falha:** nada declarado, o destino fica `UNDEFINED`.
- **Mesma imagem como origem e destino:** recusada com log.
- **Log:** `LayoutSrc=a->b LayoutDst=c->d`.

A camada 2 (recriação: tokens/VAs repetidos, `status=9`, soma dupla do `PRIMARY_FALLBACK`) **não foi tocada**. Fica como a próxima variável, se a recriação ainda acontecer.

**Verificação:**
- portão `audit_dx64_copy_tracks_vk_layout`;
- `test-dx62-dx63-mutations.py` **20/20** (4 mutações novas da DX.64);
- `build-umd` com todos os testes de contrato PASS, `build-contract` EXIT=0, auditoria de segurança PASS.
- **Incidente:** a primeira passada da suíte de mutação abortou com `OSError Errno 22` ao gravar `bc250_active_miniport.c`. Conferi o arquivo depois (marcas presentes, nenhuma string de mutação), e a segunda passada deu 20/20 a partir de uma árvore que passou na auditoria inicial.

## 5. Candidato da Corrida 107

| Componente | Valor |
|---|---|
| KMD | lab `3.268.1.0`, SYS `7C4E6E1B406E77B89C6552019DBE549DE280E4B19DC6E18E8BDBAC8C4BCBDB93`, INF `37EC3DF4…884A`, CAT `0F0615BE…AF55`. Auditoria do pacote PASS |
| UMD | `B6AB6788ABBFD77FD6EE66D008E336BF4C4AA8B3B265013CC70B8E9EBD751E0B`; marca `LayoutSrc=%d->%d` |
| RADV | `FCAA11FE…` |
| Pré-voo | `tools/prepare-corrida107-dx64.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA107_DX64.cmd` (CRLF), elevado, 180 s, depois de **boot frio** |

## 6. Critério da Corrida 107, escrito antes

**Controle:**
- linhas `ResourceCopyImage Vk … LayoutDst=0->2` (UNDEFINED→COLOR_ATTACHMENT) ou `…->5` no log do DWM;
- eixo 1 da 106 mantido: `BLT_DESTINO` sem recusa e `BYTE_PROOF` 5/5.

| | Assinatura | Implicação |
|---|---|---|
| **A** | **zero** `SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA` **e** **zero** `D3D10 DestroyDevice` do dwm.exe antes do rollback **e** zero `PRIMARY_FALLBACK` | A recusa pela cópia era o gatilho da recriação. Visual: o desktop **não** deve degradar em 180 s. |
| **B** | zero `SRV_LAYOUT_UNDEFINED…`, mas o DWM **ainda** recria o device | Existe outro gatilho. O log das ~5 s anteriores ao `DestroyDevice` diz qual. A camada 2 fica exposta de novo. |
| **C** | `SRV_LAYOUT_UNDEFINED…` ainda aparece | Existe outro caminho de escrita que não atualiza o layout. O recurso e a chamada anterior a ele o nomeiam. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Regras:** desfecho parcial não é desfecho. R1 (recuperação no mesmo boot) de novo como controle.

**Secundárias:**
- outros `Draw: RECUSADO` e seus motivos;
- FPS;
- relato visual (a janela com o triângulo continua visível; a tela degrada ou não);
- `COMPOSITE_BLT RECUSADO`;
- `PREEMPT_DONE Node=0`.

**Se a recriação ainda acontecer (B), o próximo passo é a camada 2:**
- unicidade de token entre cargas da DLL;
- publicação da VA reutilizada no journal (`status=9`);
- a soma dupla da base no `PRIMARY_FALLBACK`.

Uma variável por vez.
