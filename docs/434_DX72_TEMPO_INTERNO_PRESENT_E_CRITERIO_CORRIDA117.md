# DX.72: tempo interno de Present e criterio da Corrida 117

> 25/09/2026. Base: [Corrida 116](433_CORRIDA116_DX71_180S_VISUAL_ESTAVEL_R1_FPS_ABAIXO_DO_PISO.md). Alteracao **OFFLINE; NAO VALIDADA EM HARDWARE**.

## Pergunta e alteracao

O harness fixo de 60 quadros ficou em 1,0–1,3 FPS nas corridas 113–116. Na 116, a ida e volta da UMD pelo `pfnPresentCb` teve p50/p90 de **433/466 ms**. Isso nao mede, por si, tempo gasto no KMD. A DX.72 mede com `KeQueryPerformanceCounter` a duracao do caminho valido de `Bc250DxgkDdiPresent`, desde a entrada ate antes do retorno, e emite `[DX72_PRESENT_KMD_TEMPO]` com numero global, contexto, Blt/Flip, status e microssegundos. O harness usa Blt: todos os Blt sao registrados. Flip do DWM: primeiras 16 chamadas e depois uma a cada 32; falhas sao sempre registradas. A leitura final ocorre antes da nova linha de log. Guardas que retornam cedo continuam identificadas por seus logs existentes e nao recebem a nova duracao.

O contador e' apenas observacao; nao muda recurso, copia, SDMA, GPUVA, WDDM, UMD nem RADV. Ainda assim, logs que ja existem **dentro** da DDI entram na duracao. Uma nova linha por Blt acrescenta I/O depois da leitura final; verificar possivel perturbacao pela taxa/latencia comparada a 116. O teste offline `display_vidpn_test` recebeu um contador monotono simulado para cobrir o novo import do kernel; o primeiro build falhou no link desse teste antes da correcao do stub.

## Candidato e portoes

- Build contratual, teste VidPn, build SYS de laboratorio, auditoria de seguranca, assinatura, auditoria de pacote e preflight somente leitura: **PASS**. Nenhum bind foi executado aqui.
- Pacote lab **3.276.1.0**, `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, mesmas opcoes de empacotamento da 116. SYS assinado SHA-256 `CC55CA619D0E658A62CF893D1205F82EA9ED80723CEC8ACA027C7C3B61ED19EA`; PDB `8220C333687E76FD9979DBA3228389F0346953D40A6F4F0B05F5163C5A970DCB`; UMD **igual a 116**, `884A0A60CB5B1AE3C55D849A72491CC961C64EF268285EB248FDE0E2DAB29AC7`; INF `A8C7F4B7E50DFBCBC7C32ED834060D5F5C902DBDA977BF81CA7E7B388B78CEBC`; CAT `AB12EC282411D4CF069379AC162C903781E53039F9D736185898CD3756C0A67F`. RADV esperado `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`.
- Preflight: `tools/prepare-corrida117-dx72.ps1`; runner elevado apos **boot frio com corte de energia**: `tools/EXECUTAR_CORRIDA117_DX72_180S.cmd`. O marcador `CLEAN_BOOT_REQUIRED` da 116 segue ativo neste boot. O runner fixa watchdog em 180 s; a instalacao fisica continua reservada ao Daniel.

## Criterio escrito antes da Corrida 117

Controle: hashes, manifesto, RADV, perfil, auditorias, configuracao de dump e `CLEAN_BOOT_REQUIRED` verificados; arquivo KMD final integral e logs UMD DWM/harness arquivados. Medir no harness os 60 tempos da UMD `pfnPresentCb` e os registros KMD `Blt=1`/`ElapsedUs`; confirmar correspondencia por contexto e numero, status e quantidade antes de comparar medianas. Medir DWM Presents nos primeiros 120 s, intervalos e latencia; registrar visual, copia de conteudo e rollback.

| Desfecho | Condicao | Veredito |
|---|---|---|
| **A — diagnostico valido** | 60/60 `PRESENT_OK`, pelo menos 60 tempos KMD Blt atribuiveis ao harness, dados UMD correspondentes, imagem normal ate o fim de 180 s e BasicDisplay Codigo 0 em cinco amostras no mesmo boot | Medicao de divisao do tempo obtida; **nao** significa meta de velocidade ou estabilidade de 600 s atingida. |
| **B — travamento com dump** | Congelamento e novo `MEMORY.DMP` associado ao ensaio | Falha funcional; usar pilhas para investigar o hang, sem inferir do ultimo log. |
| **C — travamento sem dump** | Congelamento e sem dump novo, inclusive se teclado nao responder | Falha funcional; registrar momento, resposta do teclado e recuperacao. |
| **E — invalido ou outra falha** | Controle diverge, telemetria nao captura Blt, artefato, device-lost, BugCheck ou rollback falha | Nao usar a divisao de tempo como prova; descrever a falha concreta. |

Interpretacao **somente depois** de conferir os pares: KMD p50 >=300 ms sugere tempo principalmente dentro da DDI; KMD p50 <=100 ms com callback ~433 ms sugere espera principal fora dela; entre esses limiares ou sem pares confiaveis fica inconclusivo. Nao somar medianas independentes nem atribuir a diferenca automaticamente ao scheduler. Comparar 117 com 116 (DWM 2,40/s nos primeiros 120 s, harness 1,2 FPS, callback p50 433 ms) para detectar perturbacao da medicao. Registrar qualquer mudanca de velocidade, mesmo que A passe.

Se travar, tentar **Ctrl direito + Scroll Lock duas vezes** para gerar dump de kernel antes do reset. Se o teclado nao responder, anotar e reiniciar pelo botao, sem cortar energia naquele momento para preservar migalhas. A captura manual continua nao provada sob hang. Depois do rollback, obedecer o marcador de boot frio para novo bind.
