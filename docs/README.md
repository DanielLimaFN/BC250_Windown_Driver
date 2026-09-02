# Base de referência para o novo driver BC-250

Este diretório consolida a análise das tentativas anteriores sem autorizar o reaproveitamento do código delas. O objetivo é preservar descobertas, medições, erros e endereços úteis para que o novo driver seja escrito do zero com menos tentativa e erro.

## Ordem de leitura

**Retomada operacional vigente:**
[`156_HANDOFF_IA_CANDIDATO_3.60.md`](156_HANDOFF_IA_CANDIDATO_3.60.md). O
candidato 3.60 está assinado e auditado, porém **NÃO VALIDADO EM HARDWARE**.

1. [01_EVIDENCIAS_E_LICOES.md](01_EVIDENCIAS_E_LICOES.md) - o que foi realmente demonstrado, o que falhou e quais conclusões históricas foram revogadas.
2. [02_ARQUITETURA_AMDGPU.md](02_ARQUITETURA_AMDGPU.md) - modelo do Linux que deve orientar a decomposição do novo KMD.
3. [03_CATALOGO_DE_REGISTRADORES.md](03_CATALOGO_DE_REGISTRADORES.md) - catálogo normalizado de MMIO, SMN, PSP, SMU, GCVM, CP e SDMA, incluindo endereços antigos em quarentena.
4. [04_PLANO_DO_DRIVER_NOVO.md](04_PLANO_DO_DRIVER_NOVO.md) - arquitetura proposta, gates de implementação e protocolo de validação.
5. [05_INVENTARIO_BRUTO_DE_ENDERECOS.md](05_INVENTARIO_BRUTO_DE_ENDERECOS.md) - índice mecânico de 951 definições e 73 literais encontrados nos fontes, sempre sem presunção de validade.
6. [06_MARCO_ZERO_DESENVOLVIMENTO.md](06_MARCO_ZERO_DESENVOLVIMENTO.md) - primeira base compilável, histórico da ABI, firmware manifestado e fallbacks fechados.
7. [07_INCIDENTE_REMOCAO_BC250V4.md](07_INCIDENTE_REMOCAO_BC250V4.md) - incidente de teardown, remoção offline e contenção permanente do driver legado.
8. [08_STATUS_PRE_WDDM.md](08_STATUS_PRE_WDDM.md) - estado consolidado, barra de progresso e checklist vivo antes do WDDM.
9. [09_PONTO_DE_RETOMADA.md](09_PONTO_DE_RETOMADA.md) - **comece por aqui ao retomar**: o que a máquina é, o que está provado, o que não funciona, os erros que viraram regra e o próximo passo.
10. [19_MARCO_MCDM_FAIL_CLOSED.md](19_MARCO_MCDM_FAIL_CLOSED.md) - corte mais recente do miniport instalavel: retirada de ownership fabricado, DDIs fail-closed, recovery do `bc250mcdm` e gate antes do lifecycle WDDM funcional.
11. [20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md](20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md) - roadmap fase a fase do miniport MCDM, memoria/GPUVA, PM4/compute, runtime privado, Vulkan ICD e `llama.cpp` acelerado.
12. [34_CAPACIDADES_RUNTIME_SEGMENTO_LOCAL_1.9.0.0.md](34_CAPACIDADES_RUNTIME_SEGMENTO_LOCAL_1.9.0.0.md) - gate fisico da geometria local derivada do inventario, com base e tamanho contiguos fora do scanout/staging.
13. [35_CONFORMIDADE_QUERYADAPTERINFO_MCDM_1.10.0.0.md](35_CONFORMIDADE_QUERYADAPTERINFO_MCDM_1.10.0.0.md) - correcao para `QUERYSEGMENT4` e demais consultas obrigatorias do compute-only KMD.
14. [36_ALLOCATION_SEGMENTO_GPUVA_LATENTE_1.11.0.0.md](36_ALLOCATION_SEGMENTO_GPUVA_LATENTE_1.11.0.0.md) - segmento local conectado a allocations e consultas GPUVA mantidas atras de gates reais.
15. [37_ROOT_PAGE_TABLE_OWNED_1.12.0.0.md](37_ROOT_PAGE_TABLE_OWNED_1.12.0.0.md) - associacao de root page table por processo com endereco VidMm e ownership de contexto.
16. [38_UPDATE_PAGE_TABLE_CPU_VIRTUAL_1.13.0.0.md](38_UPDATE_PAGE_TABLE_CPU_VIRTUAL_1.13.0.0.md) - traducao imediata de `DXGK_PTE` para PTE GC10, ainda atras dos gates GPUVA/paging.
17. [39_GFXHUB_TLB_PLAN_PROVENANCE_1.14.0.0.md](39_GFXHUB_TLB_PLAN_PROVENANCE_1.14.0.0.md) - sela a procedencia do SYS e substitui o falso NOP por um plano GFXHUB engine 17 fail-closed.
18. [40_GFXHUB_TLB_TRANSACAO_FISICA_1.15.0.0.md](40_GFXHUB_TLB_TRANSACAO_FISICA_1.15.0.0.md) - executa request MMIO engine 17 com dummy read, ACK limitado e restauracao do range.
19. [41_GCVM_BACKING_OWNED_VMID8_INERTE_1.16.0.0.md](41_GCVM_BACKING_OWNED_VMID8_INERTE_1.16.0.0.md) - page table e buffer owned, PTE fisica, programacao inerte do VMID8 e rollback integral.
20. [42_PSP_GPCOM_RING_TRANSACAO_1.17.0.0.md](42_PSP_GPCOM_RING_TRANSACAO_1.17.0.0.md) - primeira transicao TOS create/destroy, posteriormente reforcada pelo status estrito.
21. [43_PSP_ACK_ESTRITO_1.18.0.0.md](43_PSP_ACK_ESTRITO_1.18.0.0.md) - reforca o ACK do GPCOM com status baixo zero e registra o gate fisico aprovado.
22. [44_PSP_LOAD_IP_FW_SDMA0_1.19.0.0.md](44_PSP_LOAD_IP_FW_SDMA0_1.19.0.0.md) - envia firmware SDMA0 pelo GPCOM e exige fence PSP, resposta, checksum e teardown.
23. [45_PSP_LOAD_IP_FW_PAYLOAD_1.20.0.0.md](45_PSP_LOAD_IP_FW_PAYLOAD_1.20.0.0.md) - remove o header AMD do payload entregue ao PSP, preservando autenticacao do arquivo completo.
24. [46_PSP_PAYLOAD_ALINHADO_1.21.0.0.md](46_PSP_PAYLOAD_ALINHADO_1.21.0.0.md) - copia o payload autenticado para uma PA alinhada e telemetra todos os buffers PSP.
25. [47_SDMA_F32_ATIVA_1.22.0.0.md](47_SDMA_F32_ATIVA_1.22.0.0.md) - executa reset/run transacional da F32, mede o PC fisico e restaura o snapshot.
26. [48_SDMA_RING_COPY_FENCE_1.23.0.0.md](48_SDMA_RING_COPY_FENCE_1.23.0.0.md) - primeiro COPY real no ring SDMA owned, com RPTR/WPTR, fence, dados e rollback.
27. [49_SDMA_COPY_LAYOUT_1.24.0.0.md](49_SDMA_COPY_LAYOUT_1.24.0.0.md) - corrige a ordem Navi10 do COPY e telemetra a publicacao do WPTR.
28. [50_SDMA_WRITE_LINEAR_1.25.0.0.md](50_SDMA_WRITE_LINEAR_1.25.0.0.md) - reproduz no miniport o WRITE_LINEAR de quatro dwords ja provado no legacy.
29. [51_SDMA_LAYOUT_LEGACY_DIAGNOSTICO_1.26.0.0.md](51_SDMA_LAYOUT_LEGACY_DIAGNOSTICO_1.26.0.0.md) - reproduz o layout fisico legacy e captura falhas SDMA/UTCL.
30. [52_SDMA_LAYOUT_CONTIGUO_OWNED_1.27.0.0.md](52_SDMA_LAYOUT_CONTIGUO_OWNED_1.27.0.0.md) - preserva o layout relativo em um unico bloco owned livre.
31. [53_SDMA_GATE_RPTR_MMIO_DADOS_1.28.0.0.md](53_SDMA_GATE_RPTR_MMIO_DADOS_1.28.0.0.md) - usa RPTR MMIO, fence GPU e comparacao de dados como gate fisico.
32. [54_RLC_PSP_EXEC_ROM_1.29.0.0.md](54_RLC_PSP_EXEC_ROM_1.29.0.0.md) - carrega RLC tipo 8 e exige a saida fisica da ROM.
33. [55_PFP_DIRECT_LOAD_ISOLADO_1.30.0.0.md](55_PFP_DIRECT_LOAD_ISOLADO_1.30.0.0.md) - carrega somente o PFP em VRAM owned e exige movimento real do PC com rollback.
34. [56_RLC_ALREADY_ACTIVE_1.31.0.0.md](56_RLC_ALREADY_ACTIVE_1.31.0.0.md) - evita recarregar o RLC quando o preflight ja o encontra fora da ROM.
35. [57_RLC_F32_EXPLICITO_1.32.0.0.md](57_RLC_F32_EXPLICITO_1.32.0.0.md) - ativa explicitamente o F32 do RLC depois da carga PSP.
36. [58_RLC_ISOLADO_1.33.0.0.md](58_RLC_ISOLADO_1.33.0.0.md) - separa o RLC da primeira tentativa de unhalt do PFP após a tela preta.
37. [59_HANDOFF_IA_2026-08-27.md](59_HANDOFF_IA_2026-08-27.md) - contexto autocontido para outra IA continuar a partir do gate PFP.

As regras superiores continuam em [`BC250_HARDWARE_RULES.md`](../BC250_HARDWARE_RULES.md). Se houver divergência, elas prevalecem.

## Ensaios físicos disponíveis

| Ensaio | O que cobre |
|---|---|
| `tools/capture-baseline.ps1` | baseline read-only, com consultas SMU opcionais |
| `tools/test-sdma-rollback.ps1` | init e rollback do anel, sem submissão |
| `tools/test-sdma-minimal.ps1` | uma submissão com fence física e restauração |
| `tools/test-sdma-robustness.ps1` | repetição, wraparound, retomada, três casos negativos, escada de recuperação e dumps de falha |
| `tools/test-sdma-recovery.ps1` | experimento controlado de recuperação: valida o re-bring-up numa engine saudável antes de julgar dano |
| `tools/test-sdma-failclosed.ps1` | gate reescrito: fronteira de entradas inválidas, detecção do estado mentiroso e falha limpa |
| `tools/decode-sdma-fault.py` | decodifica os dumps de falha com nomes de bit do header autoritativo |
| `tools/capture-gcvm-baseline.ps1` | captura read-only do estado de tradução do GC; não exige boot limpo nem contamina a sessão |
| `tools/decode-gcvm.py` | decodifica o dump GCVM e compara com o que `gfxhub_v2_0.c` do upstream exige |
| `tools/test-gcvm-pagetable.ps1` | passo 1 do gate de memória: constrói e confere page table na VRAM sem ligar tradução |
| `tools/test-gcvm-transaction.ps1` | passo 2: prova o caminho de volta dos registradores de tradução, com submissão SDMA antes e depois |

Todos recusam sessão suja no preflight e exigem boot limpo. Todos terminam com
rollback e verificação do estado final.

## Escopo analisado

- `referencias/AMD-BC-250-Windows-Driver/`: driver comunitário, documentação histórica e ferramentas de engenharia reversa.
- `referencias/wddm/`: primeira tentativa KMDOD com bring-up integrado.
- `referencias/BC250-V2/`: evolução do display, VidPN, SDMA e camadas UMD.
- `referencias/BC250-V3/`: auditoria fail-closed, correção de offsets e suíte de evidência.
- `referencias/BC250-windowsDriverTest/docs/upstream/`: cópia offline de fontes e headers do AMDGPU usada como referência autoritativa de estrutura e offsets.
- `referencias/Análise Driver AMDGPU BC-250.pdf`: visão de alto nível do AMDGPU e da transição para Windows, auditada criticamente.
- `dbg/`: driver de diagnóstico atual, consultado porque contém os offsets SOC15 corrigidos e experimentos mais recentes.

Não foram considerados prova física: binários existentes, retorno de sucesso, enumeração de API, carregamento de DLL, texto de README, resultado calculado pela CPU, fence escrita pela CPU, RPTR/WPTR isolados ou telemetria sem procedência.

## Vocabulário de confiança

| Classe | Significado |
|---|---|
| **P0 - provado no silício** | Há medição reproduzível e evidência física adequada ao fenômeno. |
| **P1 - derivado de fonte autoritativa** | Vem de header/código upstream e a aritmética foi verificada, mas o comportamento ainda pode não ter sido exercitado na unidade. |
| **P2 - observado parcialmente** | Há atividade física, porém falta uma ou mais provas obrigatórias. |
| **H - hipótese** | Explicação ou caminho ainda não testado. |
| **Q - quarentena** | Endereço, valor ou conclusão antiga incorreta, conflitante ou sem procedência suficiente. Não usar no driver novo. |

## Regra para futuras atualizações

Cada nova evidência deve registrar:

- data e build instalada;
- configuração detectada da unidade;
- origem do endereço usado (`mmREG`, `BASE_IDX`, IP base ou especificação do mailbox);
- valores antes/depois;
- critério de aprovação definido antes do teste;
- artefato bruto do teste;
- veredito em três estados: `PROVADO`, `NÃO VALIDADO EM HARDWARE` ou `BLOQUEADO`.

Uma medição nova pode revogar uma conclusão antiga. Nesse caso, a conclusão antiga permanece documentada como `Q`, com ligação explícita para a evidência que a substituiu.
