# docs/279_HANDOFF_B4_PIPELINE_3D_DRAW_PM4.md — Handoff da Etapa B.4 (Pipeline 3D de Triângulo no Anel Gráfico)

> **Alvo:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 Pro 22H2 x64 build 19045
> **Data:** 2026-09-14
> **Status:** IMPLEMENTADO OFFLINE E VALIDADO POR TESTES DE CONTRATO + AUDITORIA (EXIT=0). PRONTO PARA ENSAIO NO SILÍCIO (Bringup Level 4).

---

## 🎯 1. O que foi feito na Etapa B.4

A Etapa B.4 unificou todos os componentes da **Parede B** em um stream PM4 atômico e determinístico de renderização de triângulo 3D executado diretamente no anel gráfico (`CP_RB0`):

1. **Stream Atômico de 64 DWORDs (256 Bytes, Alinhamento Estrito a 16 Bytes):**
   - **Bloco 1 (Shaders VS + PS):** `SET_SH_REG` (opcode 0x76, 12 DW) carregando `SPI_SHADER_PGM_LO_VS` (0x048) e `SPI_SHADER_PGM_LO_PS` (0x008), com bit de compute proibido por Lei 7.
   - **Bloco 2 (Color Target 0):** `SET_CONTEXT_REG` (opcode 0x69, 8 DW) configurando `CB_COLOR0_BASE..VIEW` (0x0318) com endereço GPUVA em VRAM alinhado a 256 bytes e formato RGBA8_UNORM.
   - **Bloco 3 (Target Mask):** `SET_CONTEXT_REG` (opcode 0x69, 4 DW: 1 header + offset 0x008E + valor 0x0F + PACKET2 NOP) habilitando a escrita RGBA no RT0.
   - **Bloco 4 (Viewport & Scissors):** `SET_CONTEXT_REG` (opcode 0x69, 20 DW) com matemática IEEE-754 calculando transformações de viewport e retângulos de scissor.
   - **Bloco 5 (Primitive Assembler):** `SET_UCONFIG_REG` (opcode 0x79, 8 DW) configurando topologia `DI_PT_TRILIST` (0x04) e contagem de instâncias = 1.
   - **Bloco 6 (Draw Initiator):** `IT_DRAW_INDEX_AUTO` (opcode 0x2D, 4 DW) disparando o desenho indexado de 3 vértices no modo canônico `AUTO_INDEX` (0x02), finalizado com PACKET2 NOP.
   - **Bloco 7 (Fence Física em VRAM):** `IT_WRITE_DATA` (opcode 0x37, 8 DW: 1 header + control 0x00100500 + GPUVA low/high + fence + 3x PACKET2 NOP) confirmando a conclusão física de toda a pipeline gráfica na memória VRAM.
   - **Total:** $12 + 8 + 4 + 20 + 8 + 4 + 8 = 64\text{ DW}$ ($256\text{ bytes}$).

2. **Verificação Estrita Pré-Disparo (`Bc250GfxVerify3DDrawStream`):**
   - Antes de tocar nos registradores MMIO de hardware (`CP_RB0_WPTR`), o anel passa por validação integral de cada uma das 64 dwords emitidas, comparando headers, opcodes, offsets de catálogo e registros planejados.
   - Em caso de qualquer discrepância, a transação aborta com `BC250_GFX_RING_TXN_READBACK_MISMATCH` fail-closed sem acionar a GPU.

3. **Tripé da Lei 2 no Silício:**
   - Injeção de poison na fence antes da emissão (`*FenceCpuAddress = 0x43DAFFFEu`) com prova de readback pela CPU.
   - Lançamento do stream avançando `CP_RB0_WPTR`.
   - Polling de término via `Bc250GfxRingClassifyTripod` aplicando a redução de volta do anel da Faixa A15.
   - Sucesso condicionado estritamente a `BC250_TRIPOD_PROVEN`.

4. **Integração no Driver WDDM (`bc250_active_miniport.c`):**
   - Suporte ao nível `-GfxRingBringupLevel 4`.
   - Alocação estrita de buffer de Render Target (256 KB) na memória VRAM no segmento 1.
   - Submissão atômica de desenho do triângulo seguida de **Rollback incondicional (Lei 5)** restaurando o estado limpo de boot do anel gráfico.

5. **Ferramenta de Laboratório (`tools/bind-bc250mcdm-lab.ps1`):**
   - Atualizado parâmetro `[ValidateRange(0,4)][int]$GfxRingBringupLevel = 0` com suporte ao nível 4.

---

## 🛡️ 2. Gates e Testes Executados

1. **Portão de Segurança (`tools/audit-bc250mcdm-safety.py`):**
   - Adicionada função de auditoria estrita `audit_gfx_3d_draw_is_honest`.
   - Verificou dimensões de 64 DW, reuso dos módulos B.1..B.3, poison de fence, verificação de stream e integração ao miniport.
   - Resultado: **PASS**.

2. **Suíte de Mutações (`tools/test-b4-gfx-3d-draw-mutations.py`):**
   - 18 mutações testadas cobrindo adulteração de stream size, headers, offsets, omissão de blocos, supressão de poison e testes.
   - Resultado: **18/18 mutações detectadas (100% de taxa de detecção)**.

3. **Suíte de Testes de Contrato (`build-contract.bat`):**
   - 47/47 testes de contrato executados e aprovados com EXIT=0.
   - Binários `bc250mcdm.sys`, `bc250readonly.sys` e `bc250compute.sys` compilados e linkados com sucesso.
   - Zero BOM verificado em todos os arquivos modificados.

---

## 🚀 3. Instruções para o Ensaio em Hardware

Para validar a Etapa B.4 no silício real da BC-250:

```powershell
# Executar no PowerShell elevado:
.\tools\bind-bc250mcdm-lab.ps1 -GfxRingBringupLevel 4 -TimeoutSeconds 60
```

Este ensaio irá:
1. Ativar o anel gráfico `CP_RB0` sob transação segura.
2. Desbloquear as micro-engines PFP, ME e CE.
3. Alocar buffer de VRAM para o Render Target 0.
4. Emitir o stream completo de 64 DW (shaders + RT0 + mask + viewport/scissor + primitive + draw + fence).
5. Disparar o anel e atestar a execução física através do tripé da Lei 2 (RPTR == WPTR + Fence em VRAM sobrescrevendo poison).
6. Executar o Rollback incondicional (Lei 5) devolvendo o anel ao estado seguro.
