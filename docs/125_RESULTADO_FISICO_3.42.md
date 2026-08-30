# Resultado Fisico do Candidato 3.42.0.0 (SchemaVersion 113)

**Data**: 30/08/2026  
**Ambiente**: Windows 11 x64, APU AMD BC-250 (1002:13FE, GC 10.1.3 / Cyan Skillfish 2).  
**Hash do Binario Ensaiado**: B15A72A7D2E66301367B605C8C4E84CAC181D2F0D44ACFA3E0FECAEF158BD6CD  
**Evidencia Canonica**: docs/evidence/2026-08-30_14-58-18-DESKTOP-HQAG6T4-lab-binding/

---

## 1. O Marco Historico no Silicio: Drenagem e Consumo Total do Anel

O ensaio do Candidato 3.42 atingiu um marco divisor de aguas para o driver BC-250: **o hardware do MEC consumiu e executou 100% dos dwords submetidos ao anel fisico!**

`	ext
========================================================================================
EVIDENCIA FISICA NO SILICIO (CANDIDATO 3.42)
========================================================================================
Submitted Dwords         : 40
RPTR Registrador         : 40 (amostrado 40, avanco completo)
RPTR Report Memory       : 40 (writeback fisico em memoria comprovado)
WPTR Registrador         : 40
PQ_CONTROL               : 0xD030890D (PQ_EMPTY = 1) -> ANEL COMPLETAMENTE DRENADO!
HQ_STATUS0               : 0x40008040 (QUEUE_IDLE = 1) -> ENGINE CONCLUIU E ENTROU EM IDLE
MEC FIFO Header Dump     : 0xC0000E00 (PREAMBLE_CNTL)
                           0xC0064900 (SET_SH_REG)
                           0xC0004600 (DISPATCH_DIRECT)
                           0xC0031500 (RELEASE_MEM)
Display Status           : 100% Estavel, Sem Tela Preta, Sem Hang, Sem TDR
========================================================================================
`

---

## 2. Telemetria e Diagnostico Detalhado

1. **Correcao de CP_HQD_PQ_BASE**:
   - A configuracao de CP_HQD_PQ_BASE com o endereco fisico real na VRAM (buffers.RingGpuAddress) eliminou imediatamente o stall do Fetcher (HQD_ROQ_PQ_BUSY).
   - O MEC leu o anel diretamente da VRAM e decodificou os 4 pacotes PM4 na integra.

2. **Rollback de Registradores**:
   - Falha residual unica: offset=0x0000C840 (CP_HQD_QUANTUM), esperado 0x00000000, observado 0x80000000.
   - O bit 31 (QUANTUM_ACTIVE = 0x80000000) e um bit de status dinamico do hardware que deve ser mascarado na funcao Bc250HqdWritableReadbackMask.

3. **Status de Execucao de Shader / Fence**:
   - Os Compute Units (CUs/SQ) e o pipeline de execucao de shaders utilizam a hierarquia de caches e o UTCL1 (MMU virtual da GPU) para enderecar memoria (global_store_dword, COMPUTE_PGM_LO/HI).
   - Com o anel 100% funcional e o MEC decodificando pacotes, o proximo passo imediato e alinhar a ativacao da tabela de paginas VMID 0 (Vmid0PageTable=1) ou o formato de WRITE_DATA direto na memoria para cravar a fence e o resultado de computacao.

---

## 3. Veredito

**NAO VALIDADO EM HARDWARE** (Consumo do anel 100% provado em silicio; transicao para validacao da escrita fisica de shader e fence).
