# HANDOFF — Etapa B.3.2: Primitive Assembler e Topologia de Triângulos (`VGT_PRIMITIVE_TYPE`)

> **Sub-etapa:** B.3.2 (Geometria e Rasterização / Parede B)  
> **Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) · Windows 10 Pro 22H2 x64 build 19045  
> **Data:** 2026-09-14  
> **Status:** CONCLUÍDO (OFFLINE / 100% COBERTO POR GATES E MUTAÇÕES)

---

## 1. Proveniência e Mapeamento de Registradores GFX10 (P1)

No GFX10, os registradores de geometria do montador de primitivas (`VGT_PRIMITIVE_TYPE` e `VGT_NUM_INSTANCES`) residem no bloco `BASE_IDX = 1` com offset de dword `0xA000` (aperture de 64 KB a partir do BAR5 byte `0x28000`).
Na taxonomia PM4 da AMD, esses registradores pertencem ao espaço **UCONFIG** (`BC250_PM4_UCONFIG_REG_BASE = 0xC000u`), sendo programados através do pacote Type-3 `SET_UCONFIG_REG` (`IT_OPCODE = 0x79`).

| Registrador | mmREG (`gc_10_1_0_offset.h`) | BASE_IDX | BAR5 Byte | SOC15 DW | UCONFIG Offset PM4 | Classe | Campos Relevantes |
|---|---:|---:|---:|---:|---:|:---:|---|
| `VGT_PRIMITIVE_TYPE` | `0x2242` | 1 | `0x30908` | `0xC242` | `0x0242` | P1 | `PRIM_TYPE[5:0]` (`0x04` = `DI_PT_TRILIST`) |
| `VGT_NUM_INSTANCES`  | `0x224D` | 1 | `0x30934` | `0xC24D` | `0x024D` | P1 | `NUM_INSTANCES[31:0]` (mínimo 1) |

---

## 2. Estrutura do Stream PM4 Emitido

A função `Bc250GfxEmitPrimitiveAssemblerStream` emite 3 pacotes Type-3:
1. **Pacote 1 (`VGT_PRIMITIVE_TYPE`):** 1 registrador UCONFIG
   - Header: `0xC0017900u` (`TYPE=3`, `COUNT=1`: 2 dwords de payload - 1, `IT=0x79`)
   - Offset: `0x0242u`
   - Valor: `Topology` (e.g. `0x00000004u` para `DI_PT_TRILIST`) (3 DW)
2. **Pacote 2 (`VGT_NUM_INSTANCES`):** 1 registrador UCONFIG
   - Header: `0xC0017900u` (`TYPE=3`, `COUNT=1`: 2 dwords de payload - 1, `IT=0x79`)
   - Offset: `0x024Du`
   - Valor: `NumInstances` (e.g. `0x00000001u`) (3 DW)
3. **Pacote 3 (Padding NOP):**
   - Header: `0xC0001000u` (`TYPE=3`, `COUNT=0`: 1 dword de payload - 1, `IT=0x10`)
   - Valor: `0x00000000u` (2 DW)

**Total emitido:** exatamente **8 dwords** ($3 + 3 + 2$), mantendo alinhamento estrito a 4 dwords / 16 bytes.

---

## 3. Resultados de Verificação

| Verificação | Comando | Resultado |
|---|---|---|
| **Build de Contrato Offline** | `cmd.exe /c "driver\wdk\build-contract.bat"` | **EXIT=0** (`45 testes unitários passaram`) |
| **Auditoria Geral de Segurança** | `python tools/audit-bc250mcdm-safety.py` | **PASS** (`verdict: NAO VALIDADO EM HARDWARE`) |
| **Suíte de Mutações B.3.2** | `python tools/test-b3-2-gfx-primitive-assembler-mutations.py` | **PASS (16/16 detectadas · 100%)** |
| **Total Mutações Parede B** | Mutações B.1 + B.2 + B.2.2 + B.2.3 + B.2.4 + B.3.1 + B.3.2 | **PASS (152/152 detectadas · 100%)** |
| **Integridade de Codificação** | Inspeção de BOM UTF-8 | **Sem BOM (`BOM: False` em todos os arquivos)** |

---

## 4. Próximos Passos (Parede B)

Avançar para a sub-etapa **B.3.3**:
* **Etapa B.3.3 — Draw Initiator (`DRAW_INDEX_AUTO` / `3D_DRAW`):**
  - Emissão de comando de desenho sem índice (`IT_DRAW_INDEX_AUTO = 0x2D`) no anel gráfico.
  - Parametrização para 3 vértices (`IndexCount = 3`) com `DrawInitiator` apropriado para draw não-indexado.
