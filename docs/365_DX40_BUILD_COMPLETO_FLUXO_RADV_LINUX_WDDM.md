# DX.40 — build completo e rastreamento do fluxo RADV ↔ KMD

> Critério da próxima medição: [Corrida 84](366_CRITERIO_CORRIDA84_DX40_TELEMETRIA_FLUXO.md). No preflight de 23/09, o marcador de rollback ainda exigia boot frio; a corrida não começou.

> 23/09/2026. Build e empacotamento **offline**, sem instalar ou vincular o driver e sem corrida nova. Continuação de [DX.39](364_DX39_TELEMETRIA_FILAS_FENCES_E_INDICE_OFFLINE.md) e da [revisão Linux/Mesa](361_REVISAO_CYAN_SKILLFISH_LINUX_MESA_WDDM_POS_CORRIDA83.md). **NÃO VALIDADO EM HARDWARE.**

## O que foi construído

| Componente | Resultado | Artefato / SHA-256 |
|---|---|---|
| KMD contratual | `driver/wdk/build-contract.bat` EXIT 0, incluindo testes e link da imagem de produção | `driver/wdk/output/bc250mcdm.sys`; essa imagem não contém o bring-up lab |
| KMD laboratório | `build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup` EXIT 0; recompilou a base e a variante lab | `driver/wdk/output_lab/bc250mcdm.sys`, fonte `336D46E3D3B778CF836C166F01562F97173D77540AAD46D562EFECA370257723` |
| UMD D3D11 e harnesses | `driver/umd/build-umd.bat` EXIT 0 | `driver/umd/output/bc250umd.dll`, fonte `F1AC592F860E8E648A5680CC74101AECDA3A31FD04D3F145D6E8B3BFE1B7AE89` |
| Mesa RADV WDDM2 | `build_radv.bat` EXIT 0, recompilou `radv_wddm2_cs.c` e vinculou a DLL | `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll`, `bf6a1edae5c6f64e5a0c5795878e5d187814eb91a1e5c0f3447074b6f7cee14a` |
| Pacote KMD/UMD lab | `package-bc250mcdm-lab.ps1` com perfil da Corrida 83 e `audit-bc250mcdm-lab-package.ps1` PASS | `driver/wdk/package/output_lab`, `DriverVer=09/23/2026,3.246.1.0`; SYS assinado `F1EE106C78517506F6625E08CAE5C9717D298B9CEBEF1E8198A3F8A0FE403796`, UMD assinada `45BB28C3613F8E7D654B373E3E3D76669A58BD13C45876A301781AB5821396B9`, INF `5D0ED26EA3978873E428E5654135ABED4715CFC9A5DADE81D2FFC6CE5790C323`, CAT `2904A5B3FB980F5F004BC0CA1FA5CDA168C8272640E7B18C960E3F4A9DB7B8CB` |

O pacote mantém `DISPLAY_RENDER_LAB`, WDDM 2.7, persistent submission, shader dispatch e `GfxWaveProbe=1`. Esse último campo foi materializado no INF com os mesmos switches de aceitação do pacote da 83, **somente para build**. A RADV é um artefato externo ao pacote INF; o wrapper de ensaio copia a DLL construída para `C:\BC250_LIB` antes do bind e agora **cancela** se a cópia falhar ou seu SHA-256 divergir. A DLL atualmente implantada em `C:\BC250_LIB` ainda é a da Corrida 83 (`5988b38e86febb514c4c5f1d5fb5ce5a4b6fdd2b57668f2a1b6baca3b1a0461f`). Nenhuma cópia ou bind foi executado aqui.

## O que os novos logs permitem seguir

O RADV usa `submit_id` crescente por processo. Quando `GfxIbDumpDir` ou `BC250_RADV_IB_DUMP` está armado, cada evento `BC250_DIAG` também vai para `bc250_flow_pid<PID>_start<tempo de criação>.log` no mesmo staging dos IBs. O arquivo é limitado a aproximadamente 4 MiB por processo; ao atingir o limite, um aviso sai em `stderr` e o índice classifica o arquivo como truncado pelo tamanho. A resolução do diretório ocorre uma vez por processo. Sem opt-in, os eventos continuam no `stderr`, sem arquivo de fluxo. O binder já copia todos os arquivos do staging para `gfx-ib-dump/` e executa o indexador offline ao final.

| Etapa de uma submissão | Evento e campos relevantes | O que comprova |
|---|---|---|
| Fila/contexto | `RADV_CONTEXT_CREATE`, `RADV_QUEUE_READY`, `RADV_QUEUE_DESTROY`; PID, IP, node, contexto, handle e ID da HW queue | Criação/vida da fila do lado RADV. BC-250 usa contexto virtual e `hwqueue=0`, sem inventar uma HW queue. |
| Entrada | `RADV_QUEUE_SUBMIT_BEGIN`; PID, `submit_id`, IP, CS/preambulo/postambulo, espera e sinal | Pedido recebido pelo winsys; não é aceite do KMD. |
| Espera | `RADV_QUEUE_WAIT`; `submit_id`, contagem, NTSTATUS | Resultado da chamada WDDM de espera. Falha interrompe antes do submit. |
| Montagem | `RADV_CS_LAYOUT`; `submit_id`, `SINGLE`/`CONCAT`/`CHAIN`/`EMPTY`, CSs, VA/DW finais, tamanho PDD, dump feito | Qual buffer foi oferecido a D3DKMT. `RADV_CONCAT_REJECT` identifica recusa antes da chamada. |
| Chamada D3DKMT | `RADV_QUEUE_SUBMIT_END` e `.bin.meta`; `submit_id`, status, `accepted`, VA/DW/hash, layout | Aceite do submit pelo WDDM; **não** leitura nem execução pelo CP. O `.meta` fica junto do dump mesmo se o stderr desaparecer. |
| Sinalização | `RADV_QUEUE_SIGNAL`; `submit_id`, primeiro valor e NTSTATUS | Resultado da solicitação de sinalização após o submit. Valor solicitado não comprova fence aposentada. |
| KMD | `KMD_CONTEXT_CREATE/DESTROY`, `KMD_GFX_SWITCH`, `KMD_GFX_SUBMIT`, `KMD_SDMA_SUBMIT`, `KMD_GFX_REJECT`, `KMD_TDR_WAVE` | Mapeia contexto/node, decisão da transição por VMID, fence WDDM/anel, RPTR/WPTR/SQ e onda no TDR. `KMD_GFX_SWITCH` registra `prev_vmid`, `vmid`, `switch`, `wait_fence`, `root`, `pm4_dw`, `context_control`, header/controle reais do IB no anel (`ib_header`, `ib_control`, `ib_valid`, `ib_inherit_vmid`, `ib_vmid`) e novo WPTR. |

O [índice](../tools/index-bc250-telemetry.py) agora lê também os arquivos de fluxo: agrupa etapas por arquivo e `submit_id`, põe erros, arquivo no limite, etapa ausente e submissão órfã nos alertas e aponta a linha de origem. Também resume quantos controles de IB GFX têm o bit 23 `VALID=1`, diferença concreta frente ao emissor GFX Linux, e marca switch sem os 34 DW/`CONTEXT_CONTROL` esperados. O ID é local ao processo; cruzar RADV↔KMD exige VA/DW/FNV64 e, se único, as fences do KMD. O [correlator](../tools/correlate-gfx-ib-identity.py) exclui `.meta` rejeitado/inválido. Um `MATCH_UNICO` não prova execução do IB.

## Comparação com o fluxo Linux

Segundo a [documentação oficial do Mesa RADV](https://docs.mesa3d.org/drivers/radv.html), a aplicação grava comandos em CSs, o winsys envia IBs ao KMD, e o CP os executa depois da submissão. No [Linux v6.6 `amdgpu_ib_schedule`](https://github.com/torvalds/linux/blob/v6.6/drivers/gpu/drm/amd/amdgpu/amdgpu_ib.c#L183-L229), o scheduler avalia troca de **contexto** e sincronização, faz flush de VM, emite `CONTEXT_CONTROL` quando aplicável, emite um ou mais IBs e depois a fence. O [emissor GFX10](https://github.com/torvalds/linux/blob/v6.6/drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c) define os pacotes físicos. São fontes de referência, não evidência de que nosso código executou a mesma ordem no silício.

| Ponto | Linux | WDDM BC-250 atual / medição que falta |
|---|---|---|
| Fila | DRM `amdgpu` recebe CSs e pode operar filas GFX/ACE separadas | RADV WDDM usa `D3DKMT_CREATECONTEXTVIRTUAL`; a BC-250 não cria HW queue WDDM. Logs identificam IP/node/contexto, inclusive tentativa gang recusada. Ainda falta medir quais filas o DWM usou na próxima corrida. |
| IBs do RADV | Vários CSs podem virar vários IBs na submissão | Caminho DX.22 concatena em BO próprio quando há mais de um CS simples; `CHAIN` cobre CS crescido. `RADV_CS_LAYOUT` e hash mostram a escolha. Equivalência byte a byte com o Linux não foi demonstrada. |
| Troca de contexto | `ring->current_ctx != fence_ctx`, com necessidades de sincronização e flush avaliadas separadamente | Nosso GFX usa `LastSubmitVmid != Vmid` e fence anterior. `KMD_GFX_SWITCH` torna o predicado observável em **todas** as submissões GFX medidas, inclusive `switch=0`. O gatilho não é equivalente; a Corrida 83 já mostrou TDR apesar do pacote DX.37. |
| Resultado | ioctl aceito → ring/CP executa → fence aposentada | `RADV_QUEUE_SUBMIT_END accepted=1` → `KMD_GFX_SUBMIT committed=1` → RPTR/fence física/TDR. São três afirmações distintas. Nem o aceite nem o commit substituem pixels corretos, ausência de TDR ou 60 quadros. |

Os dados atuais **não dão certeza de que o fluxo WDDM segue corretamente o Linux**. Eles criam os pontos de comparação para uma corrida física. Também não resolvem a origem temporal das ondas VMID antigo da Corrida 83: [DX.38](363_DX38_IDENTIDADE_IB_E_LIMITE_SONDA_ONDAS_OFFLINE.md) explica por que o hash do IB não basta.

## Verificação e limites

Auditorias KMD/RADV e pacote: PASS. O gate RADV exige fluxo opt-in limitado, ID, layout e eventos de espera/submit/sinal. O indexador passou em fixtures de fluxo completo, fluxo interrompido e erro de sinalização, além dos casos DX.39 de metadados inválidos. Parser do binder e verificação de hash do wrapper são estáticos/offline; o wrapper não foi executado. O patch espelhado do fork tem SHA-256 `35c37d4759d0f33549b72202bf3df16e5ed234f8725c0637e8e98e55af61c632`; [proveniência](../external/radv-wddm/PROVENANCE.md).

**Limites materiais:** 4 MiB por processo e 128 IBs por processo deixam a coleta finita; após o teto, logs/dumps posteriores são lacuna. O arquivo de fluxo faz I/O na submissão quando armado, e o custo/ordenação sob carga do DWM ainda não foram medidos. Contexto RADV e handle interno KMD não devem ser igualados pelo número. O pacote 3.246.1.0 e a RADV DX.40 são **candidatos offline**, sem qualquer avanço funcional validado frente à Corrida 83.
