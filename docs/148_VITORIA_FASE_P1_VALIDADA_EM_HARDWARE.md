# Vitória Histórica — Fase P1 Validada em Hardware (Silício AMD BC-250)

> **Data**: 30/08/2026 22:31:14 (UTC-3) | **Pacote**: `3.54.0.0` | **Schema**: `125`  
> **Evidência**: [`docs/evidence/2026-08-30_22-31-14-DESKTOP-HQAG6T4-p1-submission/p1-submission-acceptance.json`](docs/evidence/2026-08-30_22-31-14-DESKTOP-HQAG6T4-p1-submission/p1-submission-acceptance.json)  
> **Veredito Oficial**: **`P1 SUBMISSAO PERSISTENTE VALIDADA EM HARDWARE`**

---

## 🏆 1. Resumo Executivo da Conquista

Pela primeira vez na história do projeto **BC250-WinDv**, o motor de submissão persistente do driver WDDM/MCDM (`bc250mcdm.sys`) submeteu trabalho assíncrono à GPU física **AMD BC-250 (`1002:13FE` GC 10.1.3 / Cyan Skillfish 2)** através de um **Anel Persistente de Computação (MEC2)** e obteve a execução completa do tripé de hardware com display ativo e zero tela preta.

---

## 📊 2. O Tripé de Prova Física no Silício (100% Aprovado)

| Pilar de Validação | Valor Observado | Valor Esperado | Veredito |
|---|---|---|---|
| **Consumo do Cursor (`RPTR == WPTR`)** | `RPTR = 12` / `WPTR = 12` | `12 == 12` | **PROVADO NO SILÍCIO** |
| **Writeback de RPTR em Memória** | `12` | `12` | **PROVADO NO SILÍCIO** |
| **Fence Física de Dispatch (VRAM +0x2300)** | `0xBC250D15` | `0xBC250D15` (saiu de `0xA55A0FF0`) | **PROVADO NO SILÍCIO** |
| **Resultado do Shader de Computação (VRAM +0x5000)** | `0xBC250C01` | `0xBC250C01` (saiu de `0x43DAFFFE`) | **PROVADO NO SILÍCIO** |
| **Mismatches de Dados** | `0` erros | `0` erros | **PROVADO NO SILÍCIO** |
| **Fence de Sequência do Anel (VRAM +0x2200)** | `1` | `1` | **PROVADO NO SILÍCIO** |
| **Aposentadoria de Fila (Retired Count)** | `1` submissão | `1` submissão | **PROVADO NO SILÍCIO** |
| **Latência de Conclusão** | `1` poll (~1 ms) | $\le 20000$ polls | **IMEDIATO** |
| **Triad Passed** | `1` | `1` | **PROVADO NO SILÍCIO** |
| **Physical Ready** | `1` | `1` | **PROVADO NO SILÍCIO** |

---

## 🔬 3. O Diagnóstico Decisivo e a Solução Final

1. **A Revelação no Candidato 3.53**:
   - A observabilidade dos 12 DWORDs reais gravados no anel revelou que `PACKET3_INDIRECT_BUFFER` (`0x3F`) continha `0x0010002D` (bit 20 legado) em vez do bit 23 (`INDIRECT_BUFFER_VALID`, `0x00800000`).
   - Sem o bit 23, o Command Packet Fetcher (CPF) do GFX10 descartava o branch indireto e congelava o consumo.
2. **A Correção Estrutural no Candidato 3.54**:
   - `Bc250Pm4EmitIndirectBuffer` foi alinhado com a especificação GFX10 / SOC15:
     `DWORD 3 = (ib_dwords & 0xFFFFF) | (vmid << 24) | (1u << 23)` $\rightarrow$ gerando `0x0080002D`.
3. **O Efeito no Silício**:
   - No momento do kick, o MEC2 leu os 12 DWORDs, saltou perfeitamente para o Indirect Buffer de 45 DWORDs em `+0x6000`, executou as 24 CUs da GPU, gravou as duas fences em endereços VRAM isolados e avançou o `RPTR` para 12 em **1 único ciclo de poll**!

---

## 🗺️ 4. Próximos Passos no Roadmap (Transição P1 $\rightarrow$ P2/P5)

Com a **Fase P1 concluída e validada em hardware**, as portas para a computação real de alta performance estão abertas:

1. **Fase P2 (Multi-Submissão & Pipeline Concorrente)**:
   - Exercitar streams sequenciais de centenas de dispatches concorrentes no anel persistente.
   - Validação de wraparound sob carga contínua sem recriar o contexto.
2. **Fase P3 / P4 (Interrupções de Hardware & DxgkDdiSubmitCommand Real)**:
   - Conectar o pipeline de eventos EOP ao Interrupt Handler (IH) do miniport.
   - Integrar os buffers DMA nativos do Dxgkrnl direto ao anel persistente.
3. **Fase P5 (Vulkan Compute / DirectML / LLM)**:
   - Carregamento de pesos FP16/FP32 na GDDR6 unificada e aceleração de inferência local via Vulkan/DirectML.
