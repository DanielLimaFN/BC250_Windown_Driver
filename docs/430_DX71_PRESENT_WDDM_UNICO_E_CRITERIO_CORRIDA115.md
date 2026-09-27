# DX.71 offline: um proprietario do scanout no DXGI Present

> Base: [Corrida 114](429_CORRIDA114_DX70_RESULTADO_DUPLA_APRESENTACAO.md). Data: 24/09/2026. Correcao **NAO VALIDADA EM HARDWARE**.

## Diagnostico e alteracao

A foto da 114 mostra o triangulo pequeno na janela do harness e uma segunda imagem grande sobre o desktop. O log UMD registrou 60 escritas diretas ao scanout pelo harness e 593 pelo DWM; cada uma tambem seguiu para `pfnPresentCb`. O codigo confirmava duas rotas de apresentacao para cada DXGI Present. **Hipotese forte:** o scanout direto contorna a posicao/recorte geridos pelo DWM e disputa o quadro fisico, gerando imagem deslocada e piscar. A foto e os logs sustentam a hipotese, mas a correção ainda precisa de ensaio.

Na DX.71, a UMD ainda destila a imagem Vulkan, faz a ponte GPU Vulkan->alocacao WDDM e chama o callback DXGI. Retira a chamada `radv_bc250_present_scanout` das rotas linear e RAW e o escape `PRESENT_BLT` da rota PM4 do DXGI Present; o KMD recebe a alocacao pelo callback e decide a copia final. Se GPUVA ou ponte falharem, o Present recusa explicitamente. O backend PM4 preserva os bytes da alocacao WDDM sem sobrescrever com a imagem Vulkan. O caminho direto do RADV para seus selftests continua disponivel fora do DXGI Present. KMD, registradores, GPUVA, RADV, perfil e tunables do pacote permanecem como na 114.

Risco mensuravel: o KMD pode passar a fazer mais copias integrais por SDMA (na 114 houve 647 `REDUNDANT_SKIP` apos a escrita direta); a taxa pode cair ou a nova rota revelar erro de residencia/copia. `PRESENT_OK` sozinho nao fecha visual. A auditoria DX.71 recusa reintroducao de escrita direta no preparo do DXGI Present, e a DX.54 ainda exige que a inspecao fora da amostra diga `NAO_AMOSTRADO`.

## Portoes e pacote

- `driver/umd/build-umd.bat`: EXIT=0, testes offline incluidos. `tools/audit-bc250mcdm-safety.py`: PASS. `tools/audit-bc250mcdm-lab-package.ps1`: PASS. `tools/prepare-corrida115-dx71.ps1`: PASS, somente leitura. Nenhum bind da DX.71.
- Pacote lab assinado/auditado **3.275.1.0**: SYS `A4809EEA5341951A2F6E061AA21F9B3A37B3C0CD5C996FAD42B863E1E1DC3A7A` (igual 114); UMD `884A0A60CB5B1AE3C55D849A72491CC961C64EF268285EB248FDE0E2DAB29AC7`; INF `5D6782F9ABFF4075E5FC2C1C0F09F46DE6A53EE635004102BEF0553C136463C2`; CAT `DCFB5B8CD2F068F808DA31813C1D4FAADF8CE0B876B071433DC52C8DEA0D8C14`.
- Runner elevado: `tools/EXECUTAR_CORRIDA115_DX71_180S.cmd`. `CLEAN_BOOT_REQUIRED` ativo apos a 114: boot frio com corte de energia antes do bind. Radeon Software continua impedida de abrir pelo preflight.

## Criterio escrito antes da Corrida 115

Controle: hashes/manifesto/INF assinados conferidos, perfil `DISPLAY_RENDER_LAB`, mesmo RADV `FCAA11FE...`, `DX69_MSAA_SAMPLE` e `DX70_SAMPLE` presentes, nenhum `DIRECT SCANOUT` no log UMD do DWM ou harness, e `[DX71_WDDM_ONLY]` presente. Comparar com 114: DWM 3,09 Presents/s nos primeiros 120 s, harness 1,3 FPS e falha visual; com 113: DWM 2,60/s e harness 1,0 FPS sem artefatos relatados. Janela 180 s; guardiao deve retornar BasicDisplay Codigo 0 em cinco amostras no mesmo boot.

| Desfecho | Condicoes | Veredito permitido |
|---|---|---|
| **A** | Controles completos, 60/60 Presents, desktop legivel, triangulo **somente dentro da janela**, sem imagem grande/piscar, copia KMD de conteudo com `BYTE_PROOF PASS`, R1 no mesmo boot, DWM >=2,60 Presents/s nos primeiros 120 s | Defeito de duplicacao corrigido na janela curta, sem regredir abaixo da referencia 113. Nao fecha 60 FPS nem travamento longo. |
| **B** | Imagem duplicada/piscar ausentes e R1, mas DWM <2,60/s, harness sem 60 Presents ou prova de copia incompleta | Composicao possivelmente corrigida; desempenho/contrato ainda insuficiente. Investigar KMD/ponte. |
| **E** | Imagem grande ou piscar persistem, tela preta, travamento, TDR/BugCheck, falha de controle ou rollback apenas em outro boot | Falha literal; arquivar evidencia e nao declarar correcao. |

Medir contagem e tempo de `PRIMARY_COPY`, `REDUNDANT_SKIP`, `BYTE_PROOF`, `DXGI Present`, `BridgeLinearToWddm`, taxa/latencia de DWM e harness. Solicitar observacao visual do Daniel durante e apos o harness; se possivel, foto. Uma janela de 180 s nao valida o travamento de 3–8 minutos nem jogos.
