# Base de referência para o novo driver BC-250

Este diretório consolida a análise das tentativas anteriores sem autorizar o reaproveitamento do código delas. O objetivo é preservar descobertas, medições, erros e endereços úteis para que o novo driver seja escrito do zero com menos tentativa e erro.

## Ordem de leitura

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
