# AGENTS.md — Diretrizes Universais para Agentes de IA e Modelos Autônomos

> **Instruções de Contexto e Governança para Agentes (OpenAI Codex, ChatGPT, Copilot Workspace, Auto-Agents)**  
> *Target: AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 11 x64 Kernel Driver*

---

## 🤖 1. Contexto Geral do Sistema e Arquitetura

Você está atuando no repositório **BC250-WinDv**, cujo objetivo é o desenvolvimento de um driver gráfico de computação de alta performance e baixo nível para o chip **AMD BC-250**:
- **Plataforma**: APU baseada em console, 16 GB GDDR6 unificada, CPU Zen 2 (6 ou 8 núcleos), GPU GC 10.1.3 (Cyan Skillfish 2, 24 CUs ativas).
- **Modelo de Driver Alvo**: Microsoft MCDM (Compute-Only WDDM Miniport, `Class=ComputeAccelerator`), desacoplado do desktop para evitar telas pretas.
- **Camada de Laboratório**: Driver KMDF de diagnóstico (`bc250dbg.sys`) operando em paralelo para medições diretas de MMIO e prova no silício.

---

## 🔒 2. Leis Inegociáveis de Execução para Agentes

Ao propor qualquer código, script ou diagnóstico:

### A. Exibição Obrigatória da Barra de Status
Inicie suas respostas apresentando a barra de progresso atualizada:
```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ██████████████████▋░  93%   Estavel e Isolado
SDMA (DMA Engine & Robustez)     ███████████████████░  95%   Provado em Silicio
GCVM & Traducao de Memoria       ██████████████████░░  90%   PTE 0x10 provado; sem GPUVA dinamica
RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
Telemetria & Observabilidade     ███████████████████░  95%   127 campos P1 (Schema 127)
Compute & Async Queues (MEC/MQD) ████████████████████ 100%   Fases P1 e P2 provadas no silicio (16/16 submissoes)
Miniport WDDM / MCDM Producao    ███████████████░░░░░  75%   Fase P2 Concluida; transicao para P3 (UMD/Vulkan)
=============================================================================================
Veredito Global: FASE P2 MULTI-SUBMISSAO PERSISTENTE VALIDADA EM HARDWARE
=============================================================================================
```

### B. O Ciclo de Vida Transacional (State Machine)
Toda interação de baixo nível com hardware deve obrigatoriamente seguir a máquina de estados:
```text
[PREFLIGHT] -> [SNAPSHOT] -> [APPLY] -> [READBACK] -> [PROVA NO SILÍCIO] -> [ROLLBACK/TEARDOWN]
     |                                                       |                      |
     +---(abortar se inválido)                               +---(falhar fechado)---+
```

1. **Preflight**: Validar permissões, verificar se a sessão de boot está limpa, checar se a engine está responsiva.
2. **Snapshot**: Capturar o estado original dos registradores ou páginas de memória.
3. **Apply**: Escrever no hardware respeitando as máscaras SOC15 `(lido & ~mask) | (or_val & mask)`.
4. **Readback**: Ler de volta o registrador/memória para auditar a escrita.
5. **Prova no Silício**: Submeter trabalho ao ring físico e validar o tripé (`RPTR == WPTR`, fence física da GPU com poison check, integridade de dados).
6. **Rollback**: Restaurar o snapshot e liberar todos os recursos (handles, buffers, mapeamentos).

---

## 🚫 3. Anti-Alucinação e Prevenção de Falsos Positivos

Como agente de IA, você **NÃO DEVE**:
- Alucinar que uma funcionalidade está "pronta", "acelerada" ou "funcionando" porque o código compilou com sucesso ou retornou `STATUS_SUCCESS`.
- Sugerir o uso de fences ou sinalizações simuladas por CPU (`KeSetEvent`, incrementos manuais de ponteiro, etc.).
- Propor fallbacks de software (WARP, loops em C) disfarçados de execução de GPU.
- Utilizar endereços de MMIO não catalogados em [`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md) ou presentes em quarentena (`Q`).
- Omitir a cláusula **`NÃO VALIDADO EM HARDWARE`** se qualquer evidência física faltar.

---

## 📂 4. Mapa de Referência Rápida do Repositório

- **ABI e Estruturas de Kernel**: [`inc/bc250dbg_abi.h`](inc/bc250dbg_abi.h)
- **Contratos de Hardware do Miniport**: [`driver/core/`](driver/core/)
- **Implementação do Miniport WDDM MCDM**: [`driver/wdk/`](driver/wdk/)
- **Driver de Laboratório (KMDF)**: [`dbg/`](dbg/)
- **Scripts de Teste e Harness**: [`tools/`](tools/)
- **Norma Suprema de Validação**: [`BC250_HARDWARE_RULES.md`](BC250_HARDWARE_RULES.md)
- **Documento de Retomada Técnica**: [`docs/09_PONTO_DE_RETOMADA.md`](docs/09_PONTO_DE_RETOMADA.md)
- **Roadmap para Vulkan e LLM**: [`docs/20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md`](docs/20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md)
