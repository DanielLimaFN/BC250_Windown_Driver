# Corrida 105 (DX.61–63): resultado; DX.63b e critério da Corrida 106

> **Corrida 105:** 24/09/2026, bind às 16:25:54.
> - Guardião armado às 16:26:00.
> - Rollback iniciado às 16:28:55.
> - `RECOVERY_OK_ONLINE` às 16:29:08, **no mesmo boot**.
>
> **Candidato:** lab `3.266.1.0`, SYS `3C7ACDEF…`, UMD `3B70D7F2…`, RADV `FCAA11FE…`.
> **Evidência:** `docs/evidence/2026-09-24_16-25-54-DESKTOP-HQAG6T4-lab-binding/`.
> - `display-log-this-bind-FINAL.txt`: 17.093.092 bytes, `complete_at_capture=true`.
> - `umd-logs-pos-travamento/`: 8 logs.
> - `safe-bind-pnp-samples.jsonl`, `safe-bind-recovery-attempt1.txt`, `bc250-recovery-log-attempt1.txt`.
>
> **Critério:** [doc 413 §5](413_DX61_63_RECOVERY_BLT_DESTINO_SEGUIMENTO_ALOCACAO_E_CRITERIO_CORRIDA105.md).
> **Relato do Daniel:** "igual o de sempre, tela bem travada tipo 1 FPS, depois começou a bugar, porém dessa vez o watchdog agiu".

## 1. Resultado por eixo

### Controle

- 275 presents no log do KMD.
- Os 60 presents `Blt=1` produziram `BLT_DESTINO RECUSADO`, e **zero** `Blt 2D … -> scanout 0x0 DstXY=`.
- O jsonl de amostras está presente.

**Controle OK.**

### Eixo 1 (DX.63): desfecho B

- **60/60** `BLT_DESTINO RECUSADO: motivo=GEOMETRIA_DO_DESTINO_DESCONHECIDA` (P0). O present devolveu `0xC01E0200`.
- Os cinco `BYTE_PROOF` dos flips (#1, #5, #10, #25, #45) **passaram**, contra dois `FAIL` na 104.
  - Isso é compatível com a tese do segundo escritor.
  - Não é o desfecho A: A exigia `BLT_DESTINO OK`, e desfecho parcial não é desfecho.
- **Causa do B (P0 por log e código):** o destino não foi criado pela nossa UMD.
  - O harness só fez uma `Allocate` com geometria, a do backbuffer em VA `0x200011000`; o destino é a VA `0x2407D0000`.
  - O KMD publica as **alocações padrão** (`GetStandardAllocationDriverData`: primária compartilhada, shadow, staging, GDI) **sem geometria**, embora conheça largura, altura e pitch e até devolva o pitch ao dxgkrnl.
  - Na corrida houve exatamente `StagingSurface: 1280x720, Pitch=5120`. **P2:** o casamento é por tamanho, não por handle.
- **O harness contou 60/60 `PRESENT_OK`** com os 60 presents recusados no KMD. Isso confirma que o `PRESENT_OK` dele não enxerga o KMD.

### Eixo 2 (DX.62): desfecho D3

As 18 amostras `Misc=0x2` deram todas o mesmo resultado:
`AllocateHr=0x80070057 KmdStatus=0 OpenCalls=1 OpenStatus=0 DestroyCalls=1 DestroyStatus=0`.

O controle do DWM (`Bind=0x8`, `Misc=0`) teve `OpenCalls=1`, `OpenStatus=0` e `DestroyCalls=0`.

Zero linhas `[ActiveOpenAlloc] RECUSADO`, `[McdmOpenAllocation] RECUSADO` ou `[ActiveDestroyAlloc] RECUSADO`.

**Leitura (P0):**
- o dxgkrnl criou a alocação, abriu no dispositivo criador **com sucesso** e depois a destruiu;
- o nosso `OpenAllocation` **está descartado** como origem;
- a rejeição é posterior às nossas duas DDIs, dentro do `pfnAllocateCb`.

Consultei a documentação do `DXGKARG_CREATEALLOCATION` (o `hResource` é opcional) e das `DXGK_ALLOCATIONINFOFLAGS_WDDM2_0` (a única restrição sobre compartilhada é o lock só pelo criador). **Nenhuma das duas decide.** Não mudo código por palpite.

### Eixo 3 (DX.61): R1

- `RECOVERY_OK_ONLINE` com `attempt_number=1` e `boot_ticks = armed_boot_ticks`.
- 5/5 amostras BasicDisplay/0/`OK` na **rodada 1**.

**Ressalva:** a primeira rodada já passou, então a janela de assentamento **não foi exercitada**. A causa da reprovação da 104 continua desconhecida, e isto **não prova** que a DX.61 a corrigiu. O que a 105 prova é o novo registro de evidência funcionando.

### E — fora do mapa / secundárias

- **Estabilidade na janela de 180 s:** zero TDR, zero `IB_TRIPOD TIMEOUT`, zero GFXHUB_FAULT, zero BugCheck (o `MEMORY.DMP` ainda é o de 13:56, da 103).
  - `SDMA_RETIRE_ARCHIVE NotifiedTotal=8625 NotifyFailed=0`.
  - SDMA Present `Submitted=815 Completed=815`, `Fallbacks=0`.
- **Preempção:** zero `PREEMPT_DONE` em qualquer nó. A DX.60 continua sem exercício.
- **FPS do harness:** média de 1,6 (104: 1,2); janelas finais de 8–9.
- **Composição:** 177 `COMPOSITE_BLT OK` e **98** `RECUSADO (resolucao)` (104: 10). Edge, WebView2 e Claude.exe abriram device nesta corrida, o mesmo padrão da 100 (241).
- **"Começou a bugar" aconteceu de novo com o harness sem escrever no scanout.**
  - Nenhum Blt chegou ao scanout, e os flips conferiram byte a byte nas cinco amostras.
  - A H do doc 413, de que a janela centralizada explicava o defeito visual, **perde força**: o defeito visual ocorre sem ela.
  - Não se sabe o que "bugar" é, porque falta descrição e hora. Pergunta ao Daniel: onde a janela do harness apareceu (ou se apareceu), e como é o defeito (piscar, faixas, imagem antiga, congelamento)?

## 2. Veredito

**Parcial frente à 104.**

**Avanços funcionais:**
- recuperação no mesmo boot (R1);
- zero `BYTE_PROOF FAIL` nos flips conferidos.

**Avanço diagnóstico:**
- o `OpenAllocation` está descartado no laço do Explorer (D3);
- a lacuna de geometria das alocações padrão foi nomeada.

**Falhas:**
- 60/60 presents do harness recusados (B);
- 1,6 FPS;
- defeito visual sem causa;
- laço do Explorer ativo.

**NÃO VALIDADOS EM HARDWARE:**
- pixels do harness no destino;
- desktop estável por 600 s;
- causa do travamento duro;
- DX.60;
- jogo.

## 3. DX.63b (offline): geometria das alocações padrão

É a mesma variável da DX.63: o Blt alcançar o destino.

**O que muda em `Bc250McdmPublishStandardAllocationPrivateData`:**
- ela passa a aceitar geometria e só a publica se ela for coerente com o tamanho (`pitch ≥ largura`, `pitch × altura ≤ tamanho alinhado`, formato ≠ 0).
- **Staging:** `Width`/`Height`/`Pitch` do dxgkrnl e do KMD, com formato 21.
  - `D3DKMDT_STAGINGSURFACEDATA` não tem formato (`d3dkmdt.h:1361`).
  - O KMD já declarava 4 bytes por pixel no pitch. O 21 registra só isso, sem afirmar a semântica do alfa.
- **GDI:** a geometria usa o `Format` entregue pelo dxgkrnl.
- **Primária compartilhada e shadow continuam SEM geometria**, para o `DescribeAllocation` delas não mudar. Isso evita uma segunda variável no caminho do VidPn e do flip.

**O que muda no blt para o destino:**
- aceita o par 21/22: os dois têm 4 bytes na mesma ordem, e a cópia é byte a byte;
- a recusa passa a imprimir `hSrc`/`hDst`;
- o log `[GetStdAllocData] Filled` passa a imprimir a geometria publicada.

**Verificação:**
- portão estendido em `audit_dx63_blt_present_goes_to_destination`;
- `test-dx62-dx63-mutations.py` **16/16** (quatro mutações novas: staging sem geometria, primária ganhando geometria, geometria sem conferir tamanho, formato GDI inventado);
- `build-contract` EXIT=0, build de laboratório OK, auditoria de segurança PASS.

## 4. Candidato da Corrida 106

| Componente | Valor |
|---|---|
| KMD | lab `3.267.1.0`, SYS `2E76ACEB71C08B7C91717386DB8D041BA851FDB9CB81CD710D397BB6436D7F5A`, INF `8E9DBCBE…2652`, CAT `BB89DAF4…1A44`. Auditoria do pacote PASS. Marcas: `Geometria=%ux%u pitch=%u fmt=%u`, `hSrc=%p hDst=%p` |
| UMD | inalterada, `3B70D7F2…FAE2D` |
| RADV | `FCAA11FE…` |
| Pré-voo | `tools/prepare-corrida106-dx63b.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA106_DX63B.cmd` (CRLF), elevado, 180 s, depois de **boot frio** |

## 5. Critério da Corrida 106, escrito antes

**Controle:**
- linhas `[GetStdAllocData] Filled … Geometria=WxH` com valores não nulos para staging/GDI, e `Geometria=0x0` para primária e shadow;
- zero `Blt 2D … -> scanout 0x0 DstXY=`.

| | Assinatura | Implicação |
|---|---|---|
| **A** | ≥ 1 `BLT_DESTINO OK` nos presents do harness **e** zero `BYTE_PROOF FAIL` nos flips conferidos **e** harness 60/60 | O caminho Blt segue o contrato. Visual: a janela do harness deve aparecer **onde o DWM a compõe**. Se não aparecer, a próxima parede é a composição do DWM a partir desse destino. |
| **B** | `BLT_DESTINO RECUSADO motivo=GEOMETRIA_DO_DESTINO_DESCONHECIDA` persiste | O destino não é alocação padrão staging/GDI. O `hDst` da linha, contra o log de `OpenAllocation`/`CreateAllocation`, diz de onde ele vem. |
| **B'** | `BLT_DESTINO RECUSADO` com **outro** motivo (formato, retângulo, segmento, scanout, SDMA) | A lacuna seguinte fica nomeada pelo motivo. |
| **C** | `BLT_DESTINO OK` **e** `BYTE_PROOF FAIL` em algum flip | Existe outro escritor do scanout. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Regras:** desfecho parcial não é desfecho. A recuperação no mesmo boot (R1) é exigida de novo como controle.

**Secundárias:**
- eixo 2 idêntico ao D3 (controle);
- relato visual, com descrição de "bugar" e se a janela do harness aparece;
- FPS;
- `COMPOSITE_BLT RECUSADO`;
- `PREEMPT_DONE Node=0`.

**Próximo passo do eixo 2 (D3), sem corrida dedicada:** descobrir o que o dxgkrnl valida em recurso compartilhado depois do Open.
- Candidatos a medir: trace ETW do provedor DxgKrnl durante a corrida, ou comparação com um driver WDDM real da máquina.
- Não mexer em `hResource` nem em `CpuVisible` por palpite.
