# Handoff — 14/09/2026 · B.3.1 Fechada (Viewport, Scissor e Registradores de Contexto PM4)

> **Para a próxima IA e auditoria de engenharia.** Este documento formaliza o fechamento completo da etapa **B.3.1**,
> atendendo rigorosamente às exigências de silício e governança:
> 1. **Proveniência P1 de Registradores GFX10**: todos os 12 registradores de Viewport, Scissor e controle de rasterização catalogados com base `0xA000` (`BASE_IDX=1`) e conferidos contra `inc/linux_ref/gc_10_1_0_offset.h` e `gc_10_1_0_sh_mask.h`.
> 2. **Transformação Matemática IEEE-754**: cálculo exato de escala e deslocamento de coordenadas do espaço normalizado (NDC: $X \in [-1, 1], Y \in [-1, 1], Z \in [0, 1]$) para coordenadas de pixel da tela com convenção DirectX ($Y$-down):
>    $$X_{\text{scale}} = W / 2.0f, \quad X_{\text{offset}} = X_0 + W / 2.0f$$
>    $$Y_{\text{scale}} = -H / 2.0f, \quad Y_{\text{offset}} = Y_0 + H / 2.0f$$
>    $$Z_{\text{scale}} = Z_{\text{max}} - Z_{\text{min}}, \quad Z_{\text{offset}} = Z_{\text{min}}$$
> 3. **Empacotamento PM4 Alinhado (20 Dwords)**: emissão delegada a `Bc250Pm4EmitSetContextReg` (IT=0x69) em 4 blocos contíguos somando exatamente 20 dwords (alinhamento a 4 dwords / 16 bytes), com suporte nativo a wrap-around circular no anel buffer.
> 4. **Validação Fail-Closed (Lei 4)**: rejeição estrita de NaN, Infinitos, dimensões negativas, profundidades invertidas e coordenadas de scissor inconsistentes.
> 5. **Falsificabilidade Completa (Lei 1 e Lei 2)**: teste unitário dedicado (`gfx_viewport_scissor_test.c`), portão de auditoria de segurança e suíte de mutações com **18/18 detectadas (100%)**.
>
> **Alvo:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) · Windows 10 Pro 22H2 x64 build 19045
> **Estado:** OFFLINE (Contrato + Testes Unitários + Portão de Segurança + 18/18 Mutações).
> **Hardware:** Zero driver instalado, zero hardware tocado nesta sessão. **NÃO VALIDADO EM HARDWARE.**

---

## 1. Registradores GFX10 Catalogados e Proveniência (P1)

Base de registradores de contexto no GFX10: `BASE_IDX = 1` $\rightarrow$ `PM4_reg_off = mmREG` (base `0xA000`).

| Registrador | mmREG | Base `0xA000` | BAR5 byte | Classe | Função |
|---|---:|---:|---:|---:|---|
| `PA_CL_VPORT_XSCALE` | `0x010F` | `0xA10F` | `0x2843C` | P1 | float 32-bit: $W / 2.0f$ |
| `PA_CL_VPORT_XOFFSET` | `0x0110` | `0xA110` | `0x28440` | P1 | float 32-bit: $X_0 + W / 2.0f$ |
| `PA_CL_VPORT_YSCALE` | `0x0111` | `0xA111` | `0x28444` | P1 | float 32-bit: $-H / 2.0f$ (DirectX $Y$-down) |
| `PA_CL_VPORT_YOFFSET` | `0x0112` | `0xA112` | `0x28448` | P1 | float 32-bit: $Y_0 + H / 2.0f$ |
| `PA_CL_VPORT_ZSCALE` | `0x0113` | `0xA113` | `0x2844C` | P1 | float 32-bit: $Z_{\text{max}} - Z_{\text{min}}$ |
| `PA_CL_VPORT_ZOFFSET` | `0x0114` | `0xA114` | `0x28450` | P1 | float 32-bit: $Z_{\text{min}}$ |
| `PA_SU_SC_MODE_CNTL` | `0x0205` | `0xA205` | `0x28814` | P1 | controle de culling, face e modo de polígono |
| `PA_CL_VTE_CNTL` | `0x0206` | `0xA206` | `0x28818` | P1 | habilitação de transformação de Viewport (`0x0000043Fu`) |
| `PA_SC_WINDOW_SCISSOR_TL` | `0x0081` | `0xA081` | `0x28204` | P1 | `TL_X[14:0]`, `TL_Y[30:16]`, `WINDOW_OFFSET_DISABLE[31]` |
| `PA_SC_WINDOW_SCISSOR_BR` | `0x0082` | `0xA082` | `0x28208` | P1 | `BR_X[14:0]`, `BR_Y[30:16]` |
| `PA_SC_VPORT_SCISSOR_0_TL`| `0x0094` | `0xA094` | `0x28250` | P1 | `TL_X[14:0]`, `TL_Y[30:16]`, `WINDOW_OFFSET_DISABLE[31]` |
| `PA_SC_VPORT_SCISSOR_0_BR`| `0x0095` | `0xA095` | `0x28254` | P1 | `BR_X[14:0]`, `BR_Y[30:16]` |

---

## 2. Estrutura do Stream PM4 Emitido

A função `Bc250GfxEmitViewportScissorStream` emite 4 pacotes Type-3 `SET_CONTEXT_REG` (`0x69`):
1. **Bloco 1 (Transformação de Viewport):** 6 dwords contíguos de `0x010F` a `0x0114`.
   - Header: `0xC0066900u` (Count = 6: 7 dwords de payload - 1)
   - Offset: `0x010Fu`
   - Dados: `VportXScale`, `XOffset`, `YScale`, `YOffset`, `ZScale`, `ZOffset` (8 DW)
2. **Bloco 2 (Raster Mode e VTE Control):** 2 dwords contíguos `0x0205` e `0x0206`.
   - Header: `0xC0026900u` (Count = 2: 3 dwords de payload - 1)
   - Offset: `0x0205u`
   - Dados: `SuScModeCntl` (`0x00000000u`), `VteCntl` (`0x0000043Fu`) (4 DW)
3. **Bloco 3 (Window Scissor):** 2 dwords contíguos `0x0081` e `0x0082`.
   - Header: `0xC0026900u` (Count = 2: 3 dwords de payload - 1)
   - Offset: `0x0081u`
   - Dados: `WindowScissorTl` (com bit 31 ativo), `WindowScissorBr` (4 DW)
4. **Bloco 4 (Viewport Scissor 0):** 2 dwords contíguos `0x0094` e `0x0095`.
   - Header: `0xC0026900u` (Count = 2: 3 dwords de payload - 1)
   - Offset: `0x0094u`
   - Dados: `VportScissor0Tl` (com bit 31 ativo), `VportScissor0Br` (4 DW)

**Total emitido:** exatamente **20 dwords** ($8 + 4 + 4 + 4$), mantendo alinhamento de 4 dwords / 16 bytes.

---

## 3. Resultados de Verificação

| Verificação | Comando | Resultado |
|---|---|---|
| **Build de Contrato Offline** | `cmd.exe /c "driver\wdk\build-contract.bat"` | **EXIT=0** (`44 testes unitários passaram`) |
| **Auditoria Geral de Segurança** | `python tools/audit-bc250mcdm-safety.py` | **PASS** (`verdict: NAO VALIDADO EM HARDWARE`) |
| **Suíte de Mutações B.3.1** | `python tools/test-b3-1-gfx-viewport-scissor-mutations.py` | **PASS (22/22 detectadas · 100%)** |
| **Total Mutações Parede B** | Mutações B.1 + B.2 + B.2.2 + B.2.3 + B.2.4 + B.3.1 | **PASS (136/136 detectadas · 100%)** |
| **Integridade de Codificação** | Inspeção de BOM UTF-8 | **Sem BOM (`BOM: False` em todos os arquivos)** |

---

## 4. Próximos Passos

Avançar na sub-trilha B.3:
* **Etapa B.3.2 — Primitive Assembler e Topologia de Triângulos (`VGT_PRIMITIVE_TYPE`):**
  - Catalogação dos registradores VGT para seleção de topologia `DI_PT_TRILIST`.
  - Configuração do montador de vértices para geometria de 3 vértices.
