# GEMINI.md — Diretrizes de Operação para Google Antigravity e Gemini

> **Manual de Contexto, Regras e Customização para Antigravity e Gemini CLI**  
> *Target: AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 11 x64*

---

## 🌌 1. Integração com Antigravity e Sistema de Customização

Este arquivo atua como o ponto de entrada normativo para o assistente **Antigravity / Gemini** neste repositório.
- **Carregamento Automático**: O Antigravity descobre `GEMINI.md` e `AGENTS.md` na raiz do projeto para aplicar as regras de contexto hierárquico.
- **Skills Integradas**: O projeto conta com as skills `agy-customizations` e `antigravity-guide` para suporte a customizações, planos e workflows.

---

## 📊 2. Apresentação Obrigatória da Barra de Progresso

Em **toda e qualquer** proposta, plano de implementação (`implementation_plan.md`) ou resposta técnica profunda, **o Antigravity deve apresentar a barra de status consolidada**:

```text
================================== STATUS DO DRIVER BC-250 ==================================

Base Geral Pré-WDDM (Lab)        ██████████████████▋░  93%   Estável e Isolado       (+5)
SDMA (DMA Engine & Robustez)     ███████████████████░  95%   Provado em Silício      (+5)
GCVM & Tradução de Memória       █████████████████░░░  85%   Gate 20/20 Aprovado    (+50)
RLC, CP & PM4 Direct Load        █████████░░░░░░░░░░░  45%   RLC Fora da ROM         (+10)
Telemetria & Observabilidade     ████████░░░░░░░░░░░░  40%   GFXOFF Sincronizado     (+5)
Compute & Async Queues (MEC/MQD) ███░░░░░░░░░░░░░░░░░  15%   Bloqueado pelo CP       ( 0)
Miniport WDDM / MCDM Produção    ████░░░░░░░░░░░░░░░░  20%   Estrutura e DDIs        (+2)

Conformidade BC250_HARDWARE_RULES: 72% | Miniport MCDM Funcional: 43% | Memória/GPUVA: 25%
=============================================================================================
Veredito Global: NÃO VALIDADO EM HARDWARE (Aguardando fechamento do Ring PM4)
=============================================================================================
```

---

## ⚖️ 3. Regras Críticas de Engenharia e Validação

1. **Tripé de Validação no Silício**:
   - Todo trabalho acelerado pela GPU deve demonstrar:
     1. `RPTR` alcançando `WPTR` no anel da engine.
     2. Fence física gravada pela GPU na VRAM com verificação de poison.
     3. Dados destino conferidos byte a byte.
   - Na ausência de qualquer evidência física: declare categoricamente **`NÃO VALIDADO EM HARDWARE`**.

2. **Proibição de Falsos Positivos e Mocks**:
   - Nunca incremente fences via CPU.
   - Nunca use fallbacks de software (CPU, WARP, GDI).
   - Nunca trate enumerações de API ou retorno `STATUS_SUCCESS` como prova de funcionamento de hardware.

3. **Fórmula de Aplicação de Máscara SOC15**:
   ```c
   uint32_t val = (lido & ~mask) | (or_value & mask);
   ```

4. **Metodologia Transacional Fail-Closed**:
   `PREFLIGHT -> SNAPSHOT -> APPLY -> READBACK -> PROVA FUNCIONAL -> ROLLBACK / TEARDOWN`

---

## 🔗 4. Navegação no Repositório e Links de Arquivos

Sempre utilize links em formato GitHub Markdown para referenciar arquivos e códigos:
- Norma Suprema de Hardware: [`BC250_HARDWARE_RULES.md`](BC250_HARDWARE_RULES.md)
- Regras Técnicas Gerais: [`rules.md`](rules.md)
- Instruções Mestras: [`INSTRUCTIONS.md`](INSTRUCTIONS.md)
- Ponto de Retomada do Projeto: [`docs/09_PONTO_DE_RETOMADA.md`](docs/09_PONTO_DE_RETOMADA.md)
- Catálogo de Registradores: [`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md)
- Roadmap MCDM -> Vulkan -> LLM: [`docs/20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md`](docs/20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md)
- ABI de Diagnóstico: [`inc/bc250dbg_abi.h`](inc/bc250dbg_abi.h)
- Driver Miniport MCDM: [`driver/wdk/`](driver/wdk/)
- Driver de Laboratório: [`dbg/`](dbg/)
- Script Mestre de Recuperação: [`tools/BC250_RECOVERY.cmd`](tools/BC250_RECOVERY.cmd)
