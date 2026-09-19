# Handoff — 14/09/2026 · B.2.3 Fechada (Staging e Alocação de Shaders na VRAM)

> **Para a próxima IA e auditoria de engenharia.** Este documento formaliza a conclusão e auditoria
> da etapa **B.2.3**, documentando o contrato formal de upload e alocação de microcódigo de shader na
> VRAM, as garantias de alinhamento de hardware de 256 bytes, delimitação estrita de janela de VRAM e
> zonas reservadas (scanout GOP, anel CP_RB0, dummy page), detecção de colisão (zero overlap), prova
> de escrita byte a byte por leitura (*readback proof*) com injeção via transporte e barreira de memória,
> resolução dos apontamentos D1 a D4 e o portão de mutação com 27/27 mutações detectadas.
>
> **Alvo:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) · Windows 10 Pro 22H2 x64 build 19045
> **Estado:** OFFLINE (Contrato + Testes Unitários + Portão de Segurança + Metatestes de Mutação).
> **Hardware:** Zero driver instalado, zero hardware tocado nesta sessão. **NÃO VALIDADO EM HARDWARE.**

---

## 1. O que é a Etapa B.2.3

A etapa B.2.3 constrói a ponte determinística entre os manifestos e blobs imutáveis de shader gráfico (B.2.2)
e os registradores de microcódigo do hardware (B.2.4). Ela governa o processo de carregar os dwords de
código para a memória VRAM/GART e provar que a escrita ocorreu sem corrupção, sem colisão e em estrita
conformidade com a geometria de paginação da GPU.

---

## 2. Resolução dos Apontamentos de Auditoria (D1 a D4)

| Item | Defeito Identificado | Resolução Implementada | Evidência / Prova |
|---|---|---|---|
| **D1** | **Prova de readback estática e inexercitável:** o código gravava e relia do mesmo ponteiro em RAM; neutralizar o readback (`if (0 && ...)`) passava nos testes e no portão. | Criada a interface `BC250_GFX_SHADER_STAGING_TRANSPORT` com `Write32`, `Read32` e `MemoryBarrier`. O teste `gfx_shader_staging_test.c` injeta falhas no transporte (`fixture.FailReadbackOffset = 2u`) para exercitar concretamente o retorno `READBACK_FAILED`. | Provado: aplicar `if (0 && ...)` causa falha imediata na compilação/execução do teste em `build-contract.bat` com `TESTE FALHOU (gfx_shader_staging_test) - Linha/Codigo: 1`. Mutação M11/M12/M25 no portão. |
| **D2** | **Ausência de validação de VRAM e zonas reservadas:** qualquer endereço passava, inclusive 0 ou colisões com o scanout GOP. | Criada `BC250_GFX_SHADER_STAGING_WINDOW` com `BC250_GFX_VRAM_WINDOW_DEFAULT_BASE` (`0xA8C000`) e `BC250_GFX_VRAM_SCANOUT_RESERVED_BYTES` (`0xA8C000`). Rejeita scanout com `RESERVED_COLLISION`, fora da janela com `OUT_OF_WINDOW`, e colisão contra anel/dummy page. | 5 novos casos de teste no teste unitário; mutações M21, M22, M26, M27 no portão. |
| **D3** | **Divergência Doc × Código:** header afirmava que em caso de mismatch "zera a memória", mas o código só zerava os planos. | Implementada a função `Bc250StagingWipeMemory` que limpa com zeros toda a área gravada dos shaders na VRAM/RAM em qualquer falha de readback ou contrato, registrando `MemoryClearedOnFailure = 1u`. | Teste unitário verifica que todos os dwords da memória de destino tornaram-se 0 após a falha; mutação M24 no portão. |
| **D4** | **`walkthrough.md` ausente no repositório:** `Test-Path walkthrough.md` retornava `False`. | `walkthrough.md` foi gerado e gravado diretamente na raiz do workspace (`c:\Users\Daniel\Desktop\NamixWDDM\walkthrough.md`). | `Test-Path walkthrough.md` retorna **True**. |

---

## 3. Garantias Formais e Invariantes do Contrato (Leis 1, 2, 4 e 7)

### 3.1 Alinhamento de Hardware Estrito (256 bytes — Lei 7)
* **Constante:** `#define BC250_GFX_SHADER_STAGING_ALIGNMENT 256ull`
* **Justificativa de Silício:** Os registradores `SPI_SHADER_PGM_LO_VS` e `SPI_SHADER_PGM_LO_PS` na arquitetura
  RDNA 1.0 utilizam os bits `[39:8]` do endereço GPUVA (descartam os 8 bits inferiores `[7:0]`).
* **Comportamento:** Qualquer endereço de buffer de shader com `(GpuAddress & 0xFFull) != 0ull` é sumariamente
  recusado com o código nomeado `BC250_GFX_SHADER_STAGING_MISALIGNED`.

### 3.2 Delimitação de Janela de VRAM e Proteção de Reservas (D2)
* **Scanout GOP:** Faixa `[0, 0x00A8C000)` (11.059.200 bytes herdados do UEFI). Qualquer endereço dentro desta
  faixa é recusado com `BC250_GFX_SHADER_STAGING_RESERVED_COLLISION`.
* **Janela VRAM:** Faixa `[0x00A8C000, 0x00A8C000 + 0x0F531000)`. Qualquer endereço fora da janela é recusado
  com `BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW`.
* **Anel Gráfico e Dummy Page:** Se informados na estrutura de janela, qualquer interseção de faixa com os shaders
  é recusada com `BC250_GFX_SHADER_STAGING_RESERVED_COLLISION`.

### 3.3 Ausência Total de Sobreposição (Zero Overlap)
* Validação de que as faixas de VS e PS sejam estritamente disjuntas em ambos os espaços:
  1. **GPUVA:** `vsGpuStart < psGpuEnd && psGpuStart < vsGpuEnd`
  2. **Virtual CPU:** `vsVirtStart < psVirtEnd && psVirtStart < vsVirtEnd`
* Colisões retornam `BC250_GFX_SHADER_STAGING_OVERLAP`.

### 3.4 Prova de Escrita com Barreira e Ponto de Injeção (Lei 2 / D1)
* Gravação e leitura utilizam `BC250_GFX_SHADER_STAGING_TRANSPORT` quando fornecido, permitindo simulação
  determinística de corrupção ou bit-flip.
* Barreira de memória `MemoryBarrier` é chamada obrigatoriamente antes do laço de readback.
* **Ressalva de Write-Combining (WC):** O readback da CPU comprova que os dados alcançaram a abertura da CPU.
  Em mapeamentos BAR WC reais, um flush de HDP (`HDP_MEM_COHERENCY_FLUSH_CNTL`) na GPU é indispensável antes
  da busca de microcódigo pela SPI (requisito da etapa B.2.4).

### 3.5 Limpeza de Memória e Falha Fechada Estrita (Lei 4 / D3)
* Logo na entrada da função, `VsPlanOut`, `PsPlanOut` e `EvidenceOut` são limpos com `Bc250StagingZeroMemory`.
* Em caso de falha após gravação (ex: mismatch de readback ou contrato), toda a memória de destino é limpa
  com zeros (`Bc250StagingWipeMemory`) e registra-se `EvidenceOut->MemoryClearedOnFailure = 1u`.

---

## 4. Tabela de Interfaces e Estruturas

```c
typedef enum BC250_GFX_SHADER_STAGING_STATUS {
    BC250_GFX_SHADER_STAGING_OK                   = 0,
    BC250_GFX_SHADER_STAGING_BAD_ARGUMENT         = 1,
    BC250_GFX_SHADER_STAGING_MISALIGNED           = 2,
    BC250_GFX_SHADER_STAGING_BUFFER_TOO_SMALL     = 3,
    BC250_GFX_SHADER_STAGING_OVERLAP              = 4,
    BC250_GFX_SHADER_STAGING_READBACK_FAILED      = 5,
    BC250_GFX_SHADER_STAGING_MANIFEST_INVALID     = 6,
    BC250_GFX_SHADER_STAGING_CONTRACT_FAILED      = 7,
    BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW        = 8,
    BC250_GFX_SHADER_STAGING_RESERVED_COLLISION   = 9
} BC250_GFX_SHADER_STAGING_STATUS;

typedef struct BC250_GFX_SHADER_STAGING_WINDOW {
    uint64_t VramWindowBase;
    uint64_t VramWindowBytes;
    uint64_t ReservedScanoutBytes;
    uint64_t ReservedRingGpuVa;
    uint32_t ReservedRingBytes;
    uint64_t ReservedDummyPageGpuVa;
    uint32_t ReservedDummyPageBytes;
} BC250_GFX_SHADER_STAGING_WINDOW;

typedef struct BC250_GFX_SHADER_STAGING_TRANSPORT {
    void    *Context;
    uint32_t (*Write32)(void *Context, void *VirtualAddress, uint32_t DwordIndex, uint32_t Value);
    uint32_t (*Read32)(void *Context, void *VirtualAddress, uint32_t DwordIndex, uint32_t *ValueOut);
    void     (*MemoryBarrier)(void *Context);
} BC250_GFX_SHADER_STAGING_TRANSPORT;

typedef struct BC250_GFX_SHADER_STAGING_BUFFER {
    void     *VirtualAddress;
    uint64_t  GpuAddress;
    uint32_t  BufferSizeBytes;
} BC250_GFX_SHADER_STAGING_BUFFER;

typedef struct BC250_GFX_SHADER_STAGING_EVIDENCE {
    uint32_t              VsDwordsWritten;
    uint32_t              PsDwordsWritten;
    uint32_t              VsReadbackMatches;
    uint32_t              PsReadbackMatches;
    uint32_t              VsReadbackMismatches;
    uint32_t              PsReadbackMismatches;
    BC250_GFX_SHADER_REGS VsRegs;
    BC250_GFX_SHADER_REGS PsRegs;
    uint32_t              MemoryClearedOnFailure;
    uint32_t              StagingCompleted;
} BC250_GFX_SHADER_STAGING_EVIDENCE;
```

---

## 5. Portão de Segurança e Metatestes de Mutação (27/27)

O script `tools/test-b2-3-gfx-shader-staging-mutations.py` submete o portão `audit_gfx_shader_staging_is_honest`
a 27 mutações:

* M1: Alinhamento de hardware afrouxado no header (256 para 4 bytes) -> **DETECTADA**
* M2: Máscara de alinhamento suprimida na implementação -> **DETECTADA**
* M3: Recusa nomeada de desalinhamento suprimida -> **DETECTADA**
* M4: Zeragem de VsPlanOut na entrada suprimida (Lei 4) -> **DETECTADA**
* M5: Zeragem de PsPlanOut na entrada suprimida (Lei 4) -> **DETECTADA**
* M6: Zeragem de EvidenceOut na entrada suprimida (Lei 4) -> **DETECTADA**
* M7: Checagem de sobreposição GPUVA suprimida -> **DETECTADA**
* M8: Checagem de sobreposição virtual CPU suprimida -> **DETECTADA**
* M9: Recusa nomeada de sobreposição suprimida -> **DETECTADA**
* M10: Recusa nomeada de buffer pequeno demais suprimida -> **DETECTADA**
* M11: Conferência de readback do VS suprimida (Lei 2 / D1) -> **DETECTADA**
* M12: Conferência de readback do PS suprimida (Lei 2 / D1) -> **DETECTADA**
* M13: Recusa nomeada de readback failed suprimida -> **DETECTADA**
* M14: Validação de manifesto do VS suprimida -> **DETECTADA**
* M15: Validação de manifesto do PS suprimida -> **DETECTADA**
* M16: Cálculo de registradores SPI suprimido -> **DETECTADA**
* M17: Teste perde verificação de desalinhamento de hardware -> **DETECTADA**
* M18: Teste perde verificação de sobreposição de buffers -> **DETECTADA**
* M19: Teste perde verificação de buffer pequeno demais -> **DETECTADA**
* M20: Teste perde verificação de manifesto inválido -> **DETECTADA**
* M21: Recusa nomeada de colisão com região reservada suprimida (D2) -> **DETECTADA**
* M22: Recusa nomeada de endereço fora da janela de VRAM suprimida (D2) -> **DETECTADA**
* M23: Barreira de memória antes do readback suprimida (D1) -> **DETECTADA**
* M24: Limpeza de memória na falha de readback suprimida (D3) -> **DETECTADA**
* M25: Teste perde injeção de falha de readback (D1) -> **DETECTADA**
* M26: Teste perde verificação de colisão de reserva (D2) -> **DETECTADA**
* M27: Teste perde verificação de janela de VRAM (D2) -> **DETECTADA**

**Resultado:** `B.2.3 MUTATION SUITE: PASS (27/27 detectadas)`

---

## 6. Matriz de Não-Regressão Global

| Suíte / Teste | Escopo | Resultado |
|---|---|---|
| `test-b2-3-gfx-shader-staging-mutations.py` | Staging e Alocação de Shaders na VRAM | **27/27 PASS** |
| `test-b2-2-gfx-observable-shader-mutations.py` | Blobs e Manifestos Observáveis VS/PS | **16/16 PASS** |
| `test-b2-gfx-shader-mutations.py` | Contrato Puro de Shaders Gráficos | **15/15 PASS** |
| `test-b1-gfx-ring-mutations.py` | Contrato do Anel Gráfico e Transação | **30/30 PASS** |
| `audit-bc250mcdm-safety.py` | Auditoria Estrita de Segurança do MCDM | **PASS** |
| `build-contract.bat` | Compilação C `/NODEFAULTLIB` + 42 testes unitários | **EXIT=0** |
| `Test-Path walkthrough.md` | Presença de walkthrough.md no repositório | **True** |
| Codificação UTF-8 | Ausência de BOM em todos os arquivos tocados | **`BOM: False` em todos** |

---

## 7. Próximo Passo: Etapa B.2.4

Com o microcódigo gravado na VRAM, alinhado a 256 bytes, dentro da janela válida de VRAM e protegido contra
colisões com scanout, e com os registradores `SPI_SHADER_PGM_*` calculados e conferidos, a próxima etapa é:

**B.2.4 — Emissão PM4 `SET_SH_REG` e Submissão no Anel Gráfico (Node 1):**
* Montar os pacotes Type-3 `PKT3_SET_SH_REG` para os 4 registradores de VS e os 4 registradores de PS;
* Garantir contagem exata de dwords e *fail-closed*;
* Executar flush de HDP e submissão no anel gráfico real (Node 1).
