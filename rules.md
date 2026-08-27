# rules.md — Regras Técnicas e Restrições de Engenharia BC250-WinDv

> **Documento Normativo de Diretrizes de Código, Acesso ao Hardware e Validação no Silício**  
> *Este documento é vinculante para todo desenvolvedor e modelo de IA (Claude, Codex, Antigravity, Cursor, etc.).*

---

## 1. 🛑 Regra Inegociável de Aceleração e Validação em Silício

Não considere compilação, enumeração de API, carregamento de DLL, criação de contexto, abertura de janela, retorno `STATUS_SUCCESS` ou strings anunciadas pelo próprio driver como prova de aceleração ou funcionamento.

### O que NÃO é considerado funcionamento da GPU:
- Execução delegada à CPU (emulação, loops manuais).
- WARP, GDI, llvmpipe ou renderizadores de software.
- Fences incrementadas ou sinalizadas pela CPU (ex: escrita de timestamp ou valor via ponteiro de CPU).
- Fallbacks silenciosos ou stubs de DDI.
- Telemetria fixa, estimada ou mockada (ex: retornar 1000 MHz constante).
- `vulkaninfo`, `dxdiag`, `glGetString` ou ferramentas similares usadas isoladamente.

---

## 2. 🔬 As 10 Evidências Físicas Obrigatórias

Toda funcionalidade de compute, cópia DMA ou aceleração deve ser comprovada no silício da BC-250 pelo conjunto integral de evidências:

1. **Firmware Autenticado**: Microcódigo oficial aceito pelo PSP/SOS (`RespStatus == 0`) e engine física inicializada sem erro.
2. **Submissão em Ring Físico**: Pacotes PM4 ou comandos SDMA gravados no buffer do anel mapeado para a engine.
3. **Avanço do WPTR**: O registrador Write Pointer é atualizado via MMIO ou Doorbell.
4. **Consumo pelo RPTR**: O registrador Read Pointer avança autonomamente pela GPU até alcançar o WPTR (`RPTR == WPTR`).
5. **Fence Física Gravada pela GPU**: A GPU grava o valor esperado da fence na memória VRAM/GTT (com verificação de memória não corrompida / poison check).
6. **Integridade de Dados no Destino**: Comparação byte a byte dos dados produzidos pela GPU contra o resultado esperado calculado pela CPU.
7. **Atividade em Registradores de Estado**: `GRBM_STATUS` ou registradores de atividade da engine reportam ocupação transitória durante a execução.
8. **Reação Térmica e de Potência**: Variação coerente de clocks, potência e sensores de temperatura durante a carga contínua.
9. **GFXOFF Desativado Comprovadamente**: `SMUIO_GFX_MISC_CNTL` (ou telemetria direta do SMN) comprova que o bloco gráfico saiu do estado de hibernação profunda durante a execução.
10. **Ausência de Fallback**: Rastreamento garantindo que nenhuma instrução foi desviada para CPU ou renderizador emulado.

> [!CAUTION]
> Se qualquer uma dessas provas estiver ausente, declare estritamente no início e no fim de sua resposta:  
> **`NÃO VALIDADO EM HARDWARE`**

---

## 3. 🏷️ Vocabulário Formal de Confiança

Toda afirmação técnica, endereço de registrador ou valor de campo deve ser classificado em um dos 5 níveis:

| Nível | Identificador | Significado Operacional |
|:---:|:---|:---|
| **P0** | **Provado no Silício** | Medição física reproduzível realizada na BC-250 e arquivada em `docs/evidence/`. |
| **P1** | **Derivado Upstream** | Extraído diretamente de código autoritativo do kernel Linux (`amdgpu`, `soc15.h`, `gc_10_1_0_offset.h`) e conferido matematicamente. |
| **P2** | **Observado Parcialmente** | Atividade física detectada, mas faltam evidências do tripé (ex: WPTR moveu mas RPTR não respondeu). |
| **H** | **Hipótese** | Teoria arquitetural ou proposta de implementação ainda não executada no hardware real. |
| **Q** | **Quarentena** | Endereço legado, conclusão antiga refutada ou tentativa com risco de tela preta / travamento de barramento. **Uso estritamente proibido.** |

---

## 4. ⚙️ Regras de Acesso a Registradores MMIO e SOC15

### 4.1. Fórmula Obrigatória de Modificação de Bits (Bitmask SOC15)
A aplicação de máscaras nas tabelas SOC15 deve substituir **apenas os bits autorizados**:
```c
// CORRETO: preserva bits não cobertos pela máscara
uint32_t novo_valor = (valor_lido & ~mask) | (or_value & mask);

// ERRADO (PROIBIDO): corrompe bits vizinhos fora da máscara
uint32_t errado = (valor_lido & mask) | or_value;
```

### 4.2. Protocolo Transacional de Modificação
1. **Preflight**: Validar alinhamento, allowlist de endereços e estado de prontidão da engine.
2. **Snapshot**: Ler e salvar o valor original do registrador.
3. **Apply**: Escrever o novo valor usando a fórmula correta.
4. **Readback**: Ler imediatamente o registrador para confirmar a escrita física.
5. **Rollback**: Restaurar o valor original do snapshot ao concluir o ensaio ou em caso de erro.

### 4.3. Quarentenas Absolutas de MMIO
- Nunca escrever em registradores de `SETUP_TMR` sem preflight de discovery.
- Nunca emitir comandos de mailbox PSP em `0x58000` fora da estrutura formal de GPCOM e polling de `RespStatus`.
- Nunca sobrescrever áreas de scanout ativas do display (`BasicDisplay` framebuffer em `0xF400..0xF41F`).

---

## 5. 🧠 Gerenciamento de Memória, GCVM e Isolamento

1. **Memória de Produção via VidMm**: No driver MCDM, alocações de GPU pertencem à infraestrutura VidMm do WDDM ou a leases expressamente isoladas do miniport.
2. **Isolamento de VMID**: O driver de produção deve operar com contextos de VMID isolados (ex: VMID 8 para compute, preservando VMID 0 do kernel/firmware).
3. **Invalidação de TLB GFXHub**: Toda atualização de PTE deve emitir a transação física de invalidação de TLB da GFXHub (Engine 17) e aguardar confirmação de ACK antes de submeter comandos que utilizem o endereço virtual correspondente.

---

## 6. 📊 Exigência da Barra de Progresso

Antes de propor qualquer código, script ou plano arquitetural, **toda IA deve exibir** a barra de progresso consolidada:

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
