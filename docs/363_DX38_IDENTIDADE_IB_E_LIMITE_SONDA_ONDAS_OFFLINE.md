# DX.38 — identidade de IB e fronteira de ondas, somente offline

> Continuação posterior: [DX.39](364_DX39_TELEMETRIA_FILAS_FENCES_E_INDICE_OFFLINE.md) acrescenta metadados de fila e atualiza os hashes do patch e da DLL construída. Os hashes abaixo são a fotografia de DX.38.

> 23/09/2026. Sem bind, instalação, nova corrida ou execução no silício. Continuação de [docs/362](362_AUDITORIA_OFFLINE_IB_CONTEXTOS_FENCES_CORRIDA83.md). **NÃO VALIDADO EM HARDWARE.** Nenhum pacote novo foi criado.

## 1. A fence 67 usou o mesmo shader?

**Não é possível determinar com a evidência da Corrida 83.** Há 53 `.bin` em `gfx-ib-dump`, mas nenhum do endereço `0x24024C000`, tamanho 560 DW, que o KMD associou à fence 67 no `display-log-this-bind-FINAL.txt:8841–8845`. Os dois dumps arquivados que contêm `COMPUTE_PGM_LO=0x02003C0B` (`bc250_ib_002_va0000000240030000_dw280.bin` e `..._003_...dw288.bin`) constam no console do `bc250-d3d11-conformance.exe`, PID 11324 (`d3d11-native-conformance-console.txt:6507,6523`). Nenhum deles representa por si o IB67 do DWM. A VA de IB do DWM reaparece em outras fences, e shader/código também pode ser reutilizado. Portanto a associação do PC `0x2003C0B00` com a fence 68 continua **forte por VA/tamanho/ordem**, mas não exclusiva: a origem temporal da onda ainda é H.

Defeito concreto do arquivo antigo: `bc250_dump_ib` nomeava por contador **local ao processo** + VA + tamanho, abria com `wb` e limitava a 16 arquivos por processo. Processos distintos podiam sobrescrever o mesmo nome; as submissões posteriores ao teto não eram guardadas. Isso impede transformar ausência do IB67 em prova de que ele não continha o shader.

## 2. Instrumentação feita

- **RADV WDDM, árvore externa e espelho:** o dump agora usa nome com PID, tick e contador, abre com `CREATE_NEW` (nunca sobrescreve), recusa caminho truncado, aceita até 128 arquivos por processo e registra `Ctx`, IP, VA, DW e FNV-1a 64 dos bytes preparados. O log diz `PREPARADO`: arquivo gravado antes da chamada `D3DKMTSubmitCommand`, sem anunciar aceite ou execução. Arquivo: `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\src\amd\vulkan\winsys\wddm2\radv_wddm2_cs.c`. Espelho: `external/radv-wddm/bc250-radv-wddm.patch` SHA-256 `a7da7ac50c055811417d5e0340123a04dddd36844a862189e54b1f2830c2cbf7`.
- **KMD:** quando `GfxWaveProbe` já está armado, lê o IB inteiro pela tradução do contexto e pela janela VRAM existente, calcula FNV-1a 64 em bytes little-endian e registra `Ctx`, fence WDDM, fence do anel, VMID, root, VA, DW, hash e `Complete`. Leitura incompleta ou acima de 4096 DW produz `Complete=0`, hash zero e não pode ser casada como prova. A computação ocorre antes da publicação do IB; a linha `GFX_IB_IDENTITY` sai após o submit aceito pelo anel. Contrato puro e teste em `bc250_gfx_ib_inspect_contract.c/.h` e `gfx_ib_inspect_contract_test.c`.
- **Fronteira de ondas:** na troca de VMID, com a mesma sonda armada, o KMD amostra `SQ_DEBUG_STS_GLOBAL` e a fence de VRAM imediatamente antes e após **a CPU publicar o WPTR** (`GFX_WAVE_BOUNDARY`). São leituras sem seleção de SE/SQ. O log declara que não são barreira de execução: `SQ_before=0` pode excluir onda antiga naquele instante, mas `SQ_after` não identifica VMID nem determina qual IB lançou uma onda depois. A sonda indireta de PC/VMID continua restrita ao quiesce de TDR, onde já foi usada; movê-la para GPU ativa exigiria escrita em `GRBM_GFX_INDEX` e `SQ_IND_INDEX` durante submissões, risco não coberto por esta faixa.
- **Correlação offline:** `python tools/correlate-gfx-ib-identity.py <pasta-da-corrida>` compara VA, DW e hash completos. Emite `MATCH_UNICO`, `AMBIGUO`, `AUSENTE` ou `INDETERMINADO`; nunca força um único dump quando há duplicatas. Exit 0 exige todos os matches únicos; Exit 1 indica lacuna/duplicata; Exit 2 indica nenhuma identidade DX.38. Na evidência antiga da 83, corretamente relata **zero identidades novas** (Exit 2).

O FNV-1a 64 é uma impressão digital de diagnóstico, não prova criptográfica nem prova de busca/execução pela GPU. Um `MATCH_UNICO` apenas vincula os bytes preparados pela RADV aos bytes que o KMD leu antes do submit. Contexto/fence/VMID vêm do KMD; o valor numérico de `Ctx` do RADV é registrado, mas sua igualdade com o handle interno do KMD **não foi assumida**.

## 3. Verificação offline e proveniência

`driver/wdk/build-contract.bat`: EXIT 0 após as alterações, incluindo compilação do KMD e caso positivo/negativo do fingerprint. `build_radv.bat` na árvore Mesa externa: EXIT 0, compilou `radv_wddm2_cs.c` e relinkou a DLL. `audit-bc250mcdm-safety.py` e `audit-radv-wddm-safety.py`: PASS. A ferramenta de correlação passou em fixture sintética de match único (Exit 0) e duplicata ambígua (Exit 1). `sync-radv-fork-delta.py --update` atualizou o espelho.

DLL RADV **construída offline**: SHA-256 `640f4a151d6662a4e8a9d131624f7eb58c23dcd813a741babf5e857d68897714`. DLL **implantada**, inalterada, da Corrida 83: `5988b38e86febb514c4c5f1d5fb5ce5a4b6fdd2b57668f2a1b6baca3b1a0461f`. A verificação padrão `sync-radv-fork-delta.py` reprova por essa diferença **esperada**; não foi mascarada nem resolvida copiando DLL para o sistema. O KMD lab atual também permanece o da Corrida 83; o build-contract não cria pacote para bind.

## 4. Critério de uso futuro, ainda sem corrida autorizada

O primeiro objetivo físico de DX.38 seria **medição**, sem alteração no PM4: confirmar a presença de `GFX_IB_IDENTITY Complete=1`, dumps sem sobrescrita e ao menos uma correlação única entre RADV e KMD para a sequência anterior ao TDR ou ao encerramento limpo. A ausência de dump, hash incompleto, múltiplos dumps iguais ou TDR sem snapshot será **inconclusiva**, não prova de ausência de shader. Na análise, comparar IB anterior e atual, `GFX_WAVE_BOUNDARY`, fences observadas e `TDR_GFX_WAVE_VMID`. Se ambos os IBs usam o mesmo shader e não houver observação temporal individual da onda, a origem continuará indeterminada; nesse caso não se altera o PM4 com base só no hash.

Uma corrida futura precisa de critério completo e rollback próprios antes do bind. Nenhuma validação de desktop ou de 60 quadros pode ser inferida destes builds.
