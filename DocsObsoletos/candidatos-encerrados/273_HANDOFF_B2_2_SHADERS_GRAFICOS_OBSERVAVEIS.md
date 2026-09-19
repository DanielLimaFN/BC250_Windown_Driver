# Handoff — 14/09/2026 · B.2.2 Fechada (Blobs e Manifestos de Shader Gráfico Observável)

> **Para a próxima IA e auditoria de engenharia.** Este documento formaliza a conclusão da etapa
> **B.2.2**, documentando a montagem artesanal dos shaders mínimos observáveis (VS e PS), o rigor
> epistemológico exigido pelas Leis 1, 4 e 8, e a resolução exaustiva dos 6 apontamentos de
> auditoria (D1 a D6).
>
> **Alvo:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) · Windows 10 Pro 22H2 x64 build 19045
> **Estado:** OFFLINE (Contrato + Testes Unitários + Portão de Segurança + Metatestes de Mutação).
> **Hardware:** Zero driver instalado, zero hardware tocado nesta sessão. **NÃO VALIDADO EM HARDWARE.**

---

## 1. O que é a Etapa B.2.2

A etapa B.2.2 fornece os primeiros programas de shader gráfico (Vertex Shader e Pixel Shader) em
código de máquina nativo da arquitetura AMD RDNA 1.0 (GFX10.1 / GC 10.1.3), acompanhados de seus
manifestos estáticos e funções de validação.

Esses shaders têm como objetivo permitir o teste de binding de pipeline e configuração de registradores
`SPI_SHADER_PGM_*` na etapa B.2.4 (e posterior submissão PM4), sem disparar travamento (hang) nas waves
da GPU.

---

## 2. Resolução Exaustiva dos Apontamentos de Auditoria (D1 a D6)

| Item | Defeito Identificado | Resolução Implementada | Evidência / Portão |
|---|---|---|---|
| **D1** | **SHA-256 decorativo / tautológico:** o teste comparava strings constantes e o portão não calculava o hash sobre os dwords reais. | `audit-bc250mcdm-safety.py` passou a analisar (`parse`) os arrays C `g_Bc250GfxObservableVsShader` e `g_Bc250GfxObservablePsShader`, empacotar os dwords em little-endian e recalcular dinamicamente o SHA-256 com `hashlib.sha256()`. | Mutações **M8** e **M9** em `test-b2-2-gfx-observable-shader-mutations.py` alteram dwords nos arrays C e são detectadas imediatamente pela divergência do SHA-256 recalculado. |
| **D2** | **Conferência circular entre manifesto e Regra H:** ambos foram escritos artesanalmente sem compilador independente no host. | Ressalva epistemológica documentada explicitamente em comentários e cabeçalho (`BC250_GFX_OBSERVABLE_SHADER_H`), admitindo a limitação. | Portão exige a string `RESSALVA EPISTEMOLOGICA (D2)`; mutação **M10** detecta remoção. |
| **D3** | **Posição constante no VS (triângulos de área zero):** o VS emite `(0, 0, 0, 1.0)`, inviabilizando rasterização na B.5. | Declarada dívida formal `DIVIDA DECLARADA B.2.2/D3` no header e no handoff: suficiente para binding B.2.4 sem hang, mas deve ser expandido na B.3 (geometria). | Portão exige a declaração de dívida; mutação **M11** detecta remoção. |
| **D4** | **Assinatura e falha silenciosa em `Bc250PopulateGfxShaderPlanFromObservable`:** função retornava `void` e não sinalizava falha. | Assinatura alterada para `int Bc250PopulateGfxShaderPlanFromObservable(...)`. Em caso de erro (argumentos nulos ou manifesto inválido), zera o plano (`Plan`) e retorna `0` (falha fechada). Retorna `1` em sucesso. | Teste unitário e portão cobram o retorno `int` e teste com `NULL`; mutações **M12** e **M13** cobram assinatura e teste. |
| **D5** | **Passagem de bastão fora do repositório:** `walkthrough.md` estava apenas no diretório scratch do agente. | Criação deste documento oficial em `docs/273_HANDOFF_B2_2_SHADERS_GRAFICOS_OBSERVAVEIS.md`. | Arquivo versionado no repositório com codificação UTF-8 sem BOM. |
| **D6** | **`#pragma function(memset)` vazando para modo usuário:** diretiva estava fora do bloco kernel no teste unitário. | Movido para dentro do `#ifdef BC250_KERNEL_MODE` em `gfx_observable_shader_test.c`. | Build de contrato compila limpo sem conflitos de intrínsecos. |

---

## 3. Bytecode ISA RDNA 1.0 (GFX10.1) e Análise de Opcodes

### 3.1 Vertex Shader (VS) — 8 dwords (32 bytes)
* **SHA-256:** `219387cf4964de41ac24153b4669b8d1313ef23ecb63ce07a886db69434b9539`
* **Classe Epistêmica:** `H` (Montagem artesanal, cruzada com `aco_assembler.cpp` do Mesa e manual AMD RDNA 1.0)
* **Objetivo:** Emitir `pos0 = (0.0f, 0.0f, 0.0f, 1.0f)` com flag `done=1` e encerrar a wave.

```text
Offset  Dword       Instrução / Decodificação
0000    0x7E000280  v_mov_b32_e32 v0, 0           (pos.x = 0.0f, literal inline 128)
0004    0x7E020280  v_mov_b32_e32 v1, 0           (pos.y = 0.0f, literal inline 128)
0008    0x7E040280  v_mov_b32_e32 v2, 0           (pos.z = 0.0f, literal inline 128)
000C    0x7E0602FF  v_mov_b32_e32 v3, literal     (pos.w = 1.0f)
0010    0x3F800000  literal 1.0f em IEEE-754
0014    0xF80008CF  exp pos0, v0, v1, v2, v3 done [dword 0]
                      Opcode: 0x3E (bits 31:26) -> 0xF8000000
                      Target: 12 (POS0, bits 9:4) -> 0x000000C0
                      Done:   1 (bit 11)          -> 0x00000800
                      En:     0xF (bits 3:0)      -> 0x0000000F
                      Soma:   0xF80008CF
0018    0x03020100  exp [dword 1]: VSRC0=v0 (0), VSRC1=v1 (1), VSRC2=v2 (2), VSRC3=v3 (3)
001C    0xBF810000  s_endpgm (SOPP opcode 1)
```

### 3.2 Pixel Shader (PS) — 9 dwords (36 bytes)
* **SHA-256:** `f5787965e08da58c5bef2b296dee47dce32de9f62f8b7e93ec0c06a9bb2a16c2`
* **Classe Epistêmica:** `H`
* **Objetivo:** Emitir `mrt0 = (1.0f, 0.0f, 0.0f, 1.0f)` (cor vermelha opaca) com flags `done=1` e `vm=1`, encerrando a wave.

```text
Offset  Dword       Instrução / Decodificação
0000    0x7E0002FF  v_mov_b32_e32 v0, literal     (col.r = 1.0f)
0004    0x3F800000  literal 1.0f em IEEE-754
0008    0x7E020280  v_mov_b32_e32 v1, 0           (col.g = 0.0f)
000C    0x7E040280  v_mov_b32_e32 v2, 0           (col.b = 0.0f)
0010    0x7E0602FF  v_mov_b32_e32 v3, literal     (col.a = 1.0f)
0014    0x3F800000  literal 1.0f em IEEE-754
0018    0xF800180F  exp mrt0, v0, v1, v2, v3 done vm [dword 0]
                      Opcode: 0x3E (bits 31:26) -> 0xF8000000
                      VM:     1 (bit 12)          -> 0x00001000
                      Done:   1 (bit 11)          -> 0x00000800
                      Target: 0 (MRT0, bits 9:4)  -> 0x00000000
                      En:     0xF (bits 3:0)      -> 0x0000000F
                      Soma:   0xF800180F
001C    0x03020100  exp [dword 1]: VSRC0=v0 (0), VSRC1=v1 (1), VSRC2=v2 (2), VSRC3=v3 (3)
0020    0xBF810000  s_endpgm (SOPP opcode 1)
```

---

## 4. Integração de Registradores e Propriedades de Execução

Os manifestos exportam as propriedades necessárias para preenchimento dos registradores de contexto via contrato B.2.1 (`Bc250GfxShaderComputeRegs`):

| Parâmetro | Valor VS | Valor PS | Significado / Decodificação |
|---|---|---|---|
| `WaveSize` | 32 | 32 | Wavefront de 32 threads (padrão RDNA 1.0) |
| `NumVgprs` | 4 | 4 | Registradores de vetor (v0..v3) |
| `NumSgprs` | 8 | 8 | Alocação mínima de registradores escalares |
| `FloatMode` | `0xF0` | `0xF0` | Arredondamento `ROUND_TO_ZERO` para float32 e float64 |
| `PgmRsrc1` | `0x00AF0000` | `0x00AF0000` | `FLOAT_MODE=0xF0`, `DX10_CLAMP=1`, `IEEE_MODE=1` |
| `PgmRsrc2` | `0x00000000` | `0x00000000` | Sem scratch, sem streamout, sem traps |

A validação de registradores foi confirmada pelo teste unitário `gfx_observable_shader_test.c`, garantindo que os valores calculados pelo contrato B.2.1 conferem bit a bit com os manifestos estáticos.

---

## 5. Resultados de Teste e Portões de Segurança

1. **Build do Contrato e Testes Unitários:**
   ```powershell
   cmd.exe /c "driver\wdk\build-contract.bat"
   # OBSERVABLE GFX1010 SHADER: OK (allowlisted blob + SHA-256 provenance + negative mutation test, offline)
   # Exit Code: 0
   ```

2. **Auditoria Geral de Segurança (`tools/audit-bc250mcdm-safety.py`):**
   ```text
   BC250 MCDM SAFETY AUDIT: PASS
   verdict: NAO VALIDADO EM HARDWARE
   ```

3. **Metatestes de Mutação (Todas as suítes passam 100%):**
   * **B.2.2:** `tools/test-b2-2-gfx-observable-shader-mutations.py` -> **16/16 detectadas** (PASS)
   * **B.2.1:** `tools/test-b2-gfx-shader-mutations.py` -> **15/15 detectadas** (PASS)
   * **B.1:** `tools/test-b1-gfx-ring-mutations.py` -> **30/30 detectadas** (PASS)

4. **Higiene de Arquivos:**
   Todos os arquivos criados ou modificados possuem codificação UTF-8 estrita **sem BOM** (`BOM: False`).

---

## 6. Próxima Etapa: B.2.3

Com os blobs e manifestos validados offline, o próximo passo do roadmap é:
* **B.2.3 — Alocação de VRAM e Carregamento de Shaders:**
  * Alocar blocos de VRAM para armazenar os microcódigos do VS e do PS.
  * Copiar os dwords validados via SDMA ou CPU direct window para a VRAM física.
  * Associar os endereços GPUVA (alinhados a 256 bytes) às estruturas de plano (`BC250_GFX_SHADER_PLAN`).
  * Preparar a emissão de pacotes PM4 `SET_SH_REG` para os registradores `SPI_SHADER_PGM_LO/HI/RSRC1/RSRC2` na etapa B.2.4.
