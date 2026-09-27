# DWM: falha D3D11 e fundacao do winsys nativo

> **REPRODUÇÃO P0 EM 07/09 16:19:** o pacote de produção chegou a Código 0, MMU global,
> CommitVidPn e 12 Present, mas a tela ficou preta. Quatro falhas do DWM e dois dumps
> completos de ~202 MB repetiram `RaiseFailFastException(e0464645)` com parâmetro
> `0x887A0004`; a UMD registrou `OpenAdapter10/10_2 -> E_NOTIMPL`. Ver
> [`179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md`](179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md).

Data: 2026-09-06

## Veredito medido

A tela preta da corrida `2026-09-05_22-20-17` nao foi causada por falha do
`DxgkDdiPresent` nem do blit SDMA. O dump real de `dwm.exe` registra o fail-fast
`0xe0464645`, com HRESULT `0x887A0004` (`DXGI_ERROR_UNSUPPORTED`), em
`dwmcore!CD3DDevice::CreateD3D11Device`. O log por processo da UMD confirma que
o DWM entrou em `OpenAdapter10_2`; a UMD nao tinha um backend D3D11 nativo.

Na mesma corrida, o caminho de display comitou o VidPn, recebeu a primaria e
completou o blit de 11.059.200 bytes pelo tripé SDMA. A origem em `0xAB0000`,
entretanto, permaneceu `NonZero=0/256`. Portanto o hardware copiou preto para o
scanout. Esta conclusao liga a pilha do dump, a entrada real na UMD e a prova do
blit; ela nao infere falha de render apenas pela aparencia da tela.

Evidencia: `docs/evidence/2026-09-05_22-20-17-DESKTOP-HQAG6T4-mcdm-display-binding/`.

## Correcao iniciada

Foi criada uma camada nativa de winsys em `driver/umd/bc250_wddm_winsys.*`.
Ela usa os callbacks do runtime WDDM para:

- criar e destruir contexto virtual;
- criar e destruir fila de paginacao monitorada;
- criar alocacao com `BC250_ALLOCATION_PRIVATE_V1`;
- mapear e liberar GPUVA;
- esperar a fence de paginacao escrita pelo sistema, sem incrementar fence na CPU;
- submeter um command buffer GPUVA com contexto de broadcast explicito.

O teardown recusa destruir a fila enquanto existir GPUVA mapeada e aguarda a
fence de paginacao antes da liberacao. Falhas de callback sao propagadas.

O gate `BC250_D3D10_11_NATIVE_READY` continua em zero. `OpenAdapter10`,
`OpenAdapter10_2`, `OpenAdapter11` e `OpenAdapter12` falham fechado com
`E_NOTIMPL`. Isso e intencional: callbacks de memoria e submissao nao formam um
renderer D3D11. Nao ha WARP nem fallback de software.

Teste offline nomeado: `WDDM_WINSYS_CONTRACT`, integrado a
`driver/umd/build-umd.bat`. Resultado atual: PASS. Ele valida somente o contrato
de callbacks por mocks; hardware nao e tocado.

## Proximo gate fisico

Nao deve haver nova corrida de display apenas com esta fundacao: o resultado
visual continuaria preto porque ainda nao ha DDIs D3D11 de device/resources/
shaders/draw/present.

O proximo teste de hardware sera `WDDM_WINSYS_COMPUTE_SUBMIT_TRIPOD`, somente
depois de uma entrada diagnostica real da UMD conseguir montar um command buffer
valido. Criterio de aprovacao:

1. contexto virtual e GPUVA criados pelos callbacks WDDM;
2. submissao entra no KMD pelo `pfnSubmitCommandCb`;
3. `RPTR == WPTR`, fence fisica substitui poison e o resultado e conferido byte
   a byte;
4. evidencia arquivada em `docs/evidence/<timestamp>-wddm-winsys-submit/`.

Depois, o gate que fecha a tela preta sera `DWM_PRIMARY_NONZERO`: DWM sem
fail-fast, pixels nao nulos na primaria e blit de apresentacao novamente fechado
pelo tripé.

Backend grafico D3D11: **NAO VALIDADO EM HARDWARE**.

Submissao pelo winsys UMD: **NAO VALIDADO EM HARDWARE**.

Correcao visual da tela preta: **NAO VALIDADO EM HARDWARE**.
