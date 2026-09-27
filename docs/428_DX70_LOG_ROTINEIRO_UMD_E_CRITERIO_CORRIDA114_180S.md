# DX.70 offline: amostragem de log rotineiro da UMD e Corrida 114

> **Base:** [Corrida 113](427_CORRIDA113_DX69_GANHO_DE_DWM_E_RECUPERACAO_R1.md). **Data:** 24/09/2026. Nenhum bind com DX.70; ganho de FPS e preservacao visual **NAO VALIDADOS EM HARDWARE**.

## Alteracao isolada

O sink unico `Bc250WinsysTraceRaw` amostra **sete classes exatas de sucesso** da UMD: upload de buffer bem-sucedido, upload WDDM entrada/saida com `hr=0`, inicio de command buffer, map e unmap de recurso concluido. As primeiras 16 linhas de cada classe por processo e uma a cada 64 seguem para `OutputDebugStringA`, `stderr` e os dois arquivos write-through; cada linha preservada traz classe e ordinal `DX70_SAMPLE`. Todas as demais linhas seguem integrais, inclusive falhas, `Present`, `FlushCommands` com `DX54_TEMPO`, espera de fence, `WaitPaging`, diagnosticos de pixel e ciclo de vida. O filtro roda **antes** de timestamp, inicializacao do sink e I/O. Nenhum retorno DDI, comando Vulkan, GPUVA, KMD ou registrador muda.

Aplicado offline ao log da Corrida 113, o filtro teria retirado aproximadamente **51.227 de 145.134 linhas** do DWM (35,3%), reduzindo o mesmo numero de `OutputDebugStringA`, `stderr` e ate duas escritas write-through. Esta e' uma **simulacao sobre texto historico**, nao medicao de FPS. Upload `LEAVE` com erro nao corresponde ao prefixo `hr=0x00000000` e permanece integral. `FlushCommands ENTER` permanece integral para nao perder uma ultima entrada em possivel travamento.

## Portoes e pacote

- `driver/umd/build-umd.bat`: EXIT=0. Auditoria de seguranca com regra DX.70 e auditoria do pacote: PASS. `tools/prepare-corrida114-dx70.ps1`: PASS; somente leitura, nenhum bind.
- Pacote lab assinado **3.274.1.0**. KMD inalterado desde a 113: SYS `A4809EEA5341951A2F6E061AA21F9B3A37B3C0CD5C996FAD42B863E1E1DC3A7A`. UMD `80668E4E91EDD0117B66BFDA2992E15F25168566177AC1BEEA739F6C9541A9A3`; INF `8507683D45F0E6B9DDEA352D5C44C30804910AE03E3977AAA742E57F0BB1E65B`; CAT `03D26BC9B04167511DDC3801FF64E5B29568E09D1628FF2ABD614CFC20535F73`. Mesmo perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, RADV e GfxWaveProbe da 113.
- Runner elevado `tools/EXECUTAR_CORRIDA114_DX70_180S.cmd`; janela do guardiao 180 s. `CLEAN_BOOT_REQUIRED` da 113 ainda ativo; exigir desligamento completo e corte de energia antes de executar.

## Criterio escrito antes da Corrida 114

**Controle:** BasicDisplay Codigo 0 apos boot frio; hashes/manifesto e Radeon Software conferidos pelo preflight; segmento DX.67 abaixo da pagina 0xFA00000; 96 slots DX.68 armados; `DX69_MSAA_SAMPLE` mantido; linhas `[DX70_SAMPLE c=... n=...]` surgem com ordinais iniciais e multiples de 64; falhas, `DXGI Present` e `DX54_TEMPO` continuam no log. Corrida 113 de referencia: DWM **2,60 Presents/s nos primeiros 120 s** e media 3,46/s em 171 s; harness 1,0 FPS; 18,19 MB de log DWM. Mesma janela e criterios de contagem.

| Desfecho | Condicoes | Conclusao permitida |
|---|---|---|
| **A** | Controles completos; desktop legivel sem artefatos; recuperacao BasicDisplay/0 em cinco amostras no **mesmo boot**; DWM **>=3,25 Presents/s** nos primeiros 120 s; classes rotineiras amostradas | Ganho >=25% frente a 113 nesta janela. Nao prova causalidade exclusiva nem 60 FPS. |
| **B** | Controles completos e recuperacao R1, mas DWM <3,25/s ou harness sem ganho | A amostragem adicional nao basta; usar os tempos retidos para atacar o proximo custo. |
| **E** | Travamento, artefatos, TDR/BugCheck, controle incompleto ou rollback somente em outro boot | Registrar desfecho literal; nao declarar A/B. |

Medir tambem harness FPS, p50/p90 entre Presents e dentro de Present, `BridgeLinearToWddm`, `SubmitUs`/`EsperaUs`, linhas/bytes do DWM, Explorer e KMD, `WaitPaging`, recusas e log de pixel. Uma janela curta que passa nao fecha o travamento de 3–8 min. O modo GOP ainda limita atualizacoes visiveis a 56,13 Hz.
