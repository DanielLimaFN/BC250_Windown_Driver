# HANDOFF — Etapa B.3.3: Draw Initiator e Disparo de Desenho PM4 (`DRAW_INDEX_AUTO`)

> **Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64
> **Data:** 2026-09-14
> **Fase:** Parede B — Sub-etapa B.3.3 (Draw Initiator e Disparo de Desenho PM4 Não-Indexado)

---

## 1. Visão Geral e Contexto Arquitetural

A etapa **B.3.3** formaliza o disparo canônico do pipeline gráfico 3D da GPU através do pacote PM4 Type-3 `IT_DRAW_INDEX_AUTO` (`0x2D`). Este comando instrui o Command Processor (CP) a acionar o hardware do Primitive Assembler / VGT (Vector Geometry / Tessellator) para montar e rasterizar os vértices fornecidos sem requerer um buffer de índices em memória física.

### Mapeamento de Registrador e Procedência (P1)
No GFX10, o registrador associado ao controle de disparo reside no bloco `BASE_IDX = 1` com offset base de dwords `0xA000`:

| Registro | mmREG | BASE_IDX | BAR5 byte | PM4 Aperture | Classe | Função / Modos |
|---|---:|---:|---:|---:|:---:|---|
| `VGT_DRAW_INITIATOR` | `0x01FC` | 1 | `0x287F0` | CONTEXT `0x01FC` | P1 | Controle de disparo, seleção de fonte e modo de rasterização |

Procedência comprovada contra os arquivos oficiais de referência:
- `inc/linux_ref/gc_10_1_0_offset.h` (L6403-L6404)
- `inc/linux_ref/gc_10_1_0_sh_mask.h` (L24267-L24283)

---

## 2. Estrutura do Pacote e Stream PM4 Type-3

O stream de comando de desenho sem índice consiste em um pacote Type-3 `DRAW_INDEX_AUTO` seguido de um preenchimento com `PACKET2_NOP` para garantir o alinhamento arquitetural estrito exigido pelo microcódigo do CP no GC 10.1:

1. **Pacote 1 (`DRAW_INDEX_AUTO`):** Pacote Tipo 3
   - Opcode: `0x2Du`
   - Payload: 2 dwords (`VertexCount` e `DrawInitiator`)
   - Contagem PM4: $\text{Count} = 2 - 1 = 1$
   - Header: `0xC0000000 | (1 << 16) | (0x2D << 8) = 0xC0012D00u` (1 DW)
   - Dword 1: `VertexCount` (e.g. `3` para um triângulo canônico)
   - Dword 2: `DrawInitiator` (e.g. `0x00000002u` para `AUTO_INDEX` ou `0x00000004u` para compatibilidade D3D UMD)
   - Tamanho: **3 DW**

2. **Pacote 2 (Padding NOP Tipo 2):**
   - Header / Payload: `BC250_PM4_PACKET2_NOP = 0x80000000u`
   - Tamanho: **1 DW**

### Alinhamento Estrito
$$\text{Total do Stream} = 3\text{ DW} + 1\text{ DW} = 4\text{ dwords} \quad (16\text{ bytes})$$
$$4 \pmod 4 = 0 \implies \text{Alinhamento estrito a 16 bytes (1 linha de cache / bloco de fetch do CP)}$$

Combinado com as etapas anteriores da Geometria (B.3):
- B.3.1 (Viewport & Scissor): 24 DW (96 bytes)
- B.3.2 (Primitive Assembler): 8 DW (32 bytes)
- B.3.3 (Draw Initiator): 4 DW (16 bytes)
$$\text{Total B.3} = 24 + 8 + 4 = 36\text{ DW} = 144\text{ bytes} \quad (36 \pmod 4 = 0)$$

---

## 3. Arquivos Criados e Modificados

1. **`driver/core/bc250_pm4_packet_builder.h`**:
   - Definição de `BC250_PM4_REG_VGT_DRAW_INITIATOR` (`0xA1FCu`).
   - Declaração de `Bc250Pm4EmitPacket2Nop`.
2. **`driver/core/bc250_pm4_packet_builder.c`**:
   - Implementação de `Bc250Pm4EmitPacket2Nop`.
3. **`driver/tests/pm4_packet_builder_test.c`**:
   - Verificação unitária de emissão do `PACKET2_NOP`.
4. **`driver/core/bc250_gfx_draw_initiator.h`**:
   - Contrato formal de disparo não-indexado (`DRAW_INDEX_AUTO`).
   - Constantes de opcode `0x2D`, header `0xC0012D00u`, NOP `0x80000000u`.
   - Modos canônicos: `AUTO_INDEX = 2`, `TRIANGLELIST = 4`, `POINTLIST = 1`, `RECTLIST = 0x11`.
5. **`driver/core/bc250_gfx_draw_initiator.c`**:
   - Validação fail-closed (`VertexCount == 0`, modos de `DrawInitiator` ilegais).
   - Reúso estrito dos emissores `Bc250Pm4EmitDrawIndexAuto` e `Bc250Pm4EmitPacket2Nop`.
   - Verificação estrita com suporte a buffer circular (`RingDwords - 1`).
6. **`driver/tests/gfx_draw_initiator_test.c`**:
   - Testes unitários com casos canônicos, modos legados, limites fail-closed, wrap circular e detecção de corrupção.
7. **`driver/wdk/build-contract.bat`**:
   - Compilação e execução automatizada de `gfx_draw_initiator_test.exe`.
8. **`tools/audit-bc250mcdm-safety.py`**:
   - Adição do portão `audit_gfx_draw_initiator_is_honest`.
9. **`tools/test-b3-3-gfx-draw-initiator-mutations.py`**:
   - 16 mutações estruturais (todas 100% detectadas).
10. **`docs/03_CATALOGO_DE_REGISTRADORES.md`**:
    - Catalogação formal de `VGT_DRAW_INITIATOR` (`0x01FC`, P1).

---

## 4. Evidências de Validação Offline

* **Compilação e Suíte de Contratos:**
  - `build-contract.bat`: **EXIT=0 (46/46 testes unitários aprovados)**.
* **Auditoria de Segurança:**
  - `audit-bc250mcdm-safety.py`: **PASS**.
* **Auditoria por Mutação (Parede B):**
  - B.1 (Anel Gráfico): 30/30
  - B.2 (Contrato de Shaders): 15/15
  - B.2.2 (Shaders Observáveis): 16/16
  - B.2.3 (Staging de Shaders): 27/27
  - B.2.4 (Pacotes SH REG): 26/26
  - B.3.1 (Viewport & Scissor): 22/22
  - B.3.2 (Primitive Assembler): 16/16
  - B.3.3 (Draw Initiator): 16/16
  - **Total Parede B: 168/168 mutações detectadas (100% de cobertura contra falsos sucessos)**.
* **Higiene de Arquivos:**
  - Zero BOM em todos os arquivos modificados e criados.

---

## 5. Próximos Passos (Parede B)

Com a conclusão das etapas de Geometria e Disparo (B.3.1, B.3.2 e B.3.3), todo o conjunto de comandos PM4 necessários para um triângulo completo 3D está implementado e coberto por testes canônicos:
1. Shaders VS + PS em VRAM real com `SET_SH_REG` (B.2).
2. Viewport + Scissors com `SET_CONTEXT_REG` (B.3.1).
3. Primitive Assembler + Topologia com `SET_UCONFIG_REG` (B.3.2).
4. Draw Initiator com `DRAW_INDEX_AUTO` (B.3.3).
5. Color Target 0 (`CB_COLOR0_*`) no Framebuffer (B.0).

Próxima sub-etapa recomendada: **Etapa B.4 — Pipeline Completo de Renderização de Triângulo 3D (Montagem do Command Stream Integrado e Emissão no Anel Gráfico)**.
