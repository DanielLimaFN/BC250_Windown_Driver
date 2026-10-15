# Doc 739 — Pacote 3.437.1.0: Modelo de Memória Unificada APU (Segmentos 1 e 2) e Critério da Corrida 288

> **Data:** 13/10/2026 18:38  
> **Autor:** Antigravity / DeepMind & Daniel  
> **Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Pacote:** Lab **3.437.1.0** (SYS `942774ECDA44C4FE`, INF `4E3C0BC2`, CAT `75C75ED1`, UMD `B73F9BDA`, RADV `B4AE2E14`)  
> **Script de execução:** [`tools\EXECUTAR_CORRIDA288_APU_UNIFIED_MEM_300S.cmd`](tools/EXECUTAR_CORRIDA288_APU_UNIFIED_MEM_300S.cmd)  
> **Preflight:** [`tools\prepare-corrida288-apu-unified-memory.ps1`](tools/prepare-corrida288-apu-unified-memory.ps1)  
> **Linha de base anterior:** Corrida 287 ([doc 738](docs/738_PACOTE_3436_PERF_SUBMIT_SMU4_ZEROSTALL_E_CRITERIO_CORRIDA287.md)), watchdog 300 s, recuperação limpa.

---

## 1. Motivação e Diagnóstico da Corrida 287

Na Corrida 287, Daniel observou:
> *"às 17:55 deu uma piscada a tela de preto mas de resto tudo funcionou... queria melhorar mais a performance ainda, tipo a tela ainda pisca em preto, o menu ainda demora uns segundos para abrir... dúvida: não dá para funcionar igual uma APU normal? Tipo encheu a VRAM usa a RAM e pronto, já que é tudo o mesmo chip?"*

### A Mineração Forense dos Logs da Corrida 287
A auditoria minuciosa de `display-log-this-bind-FINAL.txt` na Corrida 287 revelou a causa exata das piscadas pretas e do atraso no menu:
1. **Asfixia Artificial de VRAM:** Foram criadas 4.236 alocações pelo Windows/DWM/Explorer/T3.
   - Alocações exigindo estritamente `SegSet=0x1` (apenas VRAM dedicada): **1.010,97 MB**!
   - Quebra das alocações presas na VRAM:
     - `Classe=0x52455300` (Render Targets / Texturas DX11): **750,68 MB**
     - `Classe=0x00000000` (Alocações internas D3D/WDDM): **81,86 MB**
     - `Classe=0xA1100004` (Superfícies GDI / Explorer): **60,50 MB**
     - `Classe=0xA1100003` (Superfícies Staging): **30,75 MB**
     - `Classe=0xA1100001` (Primárias de Display/DWM): **73,94 MB**
2. **Capacidade Física do Segmento 1:** O carveout da VRAM configurado na CMOS é de apenas **256 MB** (~233 MB úteis para alocações após o frame buffer do GOP).
3. **Tempestade de Evicções:** Forçar 1.010 MB de dados dentro de 233 MB fez com que o orçamento livre de VRAM caísse para **7 MB** às 17:55:36 (`orcamento=7 MB`).
4. **Colapso Transitório do VidMm:** Para acomodar novas superfícies do Menu Iniciar e texturas do teste 3D, o VidMm disparou **421 evicções violentas** e **18.186 flushes de TLB** em poucos segundos! O DWM foi forçado a recriar e recarregar superfícies, gerando a "piscada preta" e o atraso de resposta da interface.

---

## 2. A Solução: Arquitetura APU Real (APU.1) com 12 GB de GART

A placa BC-250 é um chip de APU com **16 GB de GDDR6 unificada a 448 GB/s** em barramento de 256 bits.
Tanto o Segmento 1 (VRAM carveout de 256 MB) quanto o Segmento 2 (Abertura / GART de sistema expandida para **12 GB / 12.288 MB**) residem nos mesmos chips físicos de GDDR6. O controlador de memória e o MMHUB acessam o Segmento 2 com a flag de PTE de sistema (`BC250_PTE_SYSTEM`) em velocidade total de hardware, sem nenhum afunilamento de PCIe.

### Regras do Modelo de Memória Unificada no KMD:
1. **Alocações Não-Primárias (Render Targets, Texturas, Buffers, GDI, Staging, DComp/XAML):**
   - `SupportedReadSegmentSet = 0x1u | 0x2u;` (Segmentos 1 e 2 suportados)
   - `SupportedWriteSegmentSet = 0x1u | 0x2u;`
   - `PreferredSegment.SegmentId0 = 1u;` (Prioridade 1: VRAM dedicada rápida)
   - `PreferredSegment.SegmentId1 = 2u;` (Prioridade 2: RAM de sistema / GART de **12 GB**)
   - `AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;`
   - **Resultado:** O VidMm preenche o Segmento 1 normalmente. Ao atingir o limite ou pressão de memória, em vez de expulsar texturas do desktop e congelar o DWM, o VidMm transborda as alocações suavemente para o Segmento 2! Além disso, alocações maiores que 233 MB agora são aceitas perfeitamente na RAM unificada de 12 GB.
2. **Primária de Scanout do DCN (Display):**
   - `SupportedReadSegmentSet = 1u;`
   - `SupportedWriteSegmentSet = 1u;`
   - `PreferredSegment.SegmentId0 = 1u;`
   - `PreferredSegment.SegmentId1 = 0u;`
   - `AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_MAXIMUM;`
   - **Resultado:** O scanout do DCN HUBP0 fica 100% ancorado no Segmento 1 físico linear, mantendo o mecanismo comprovado da Corrida 274/287 intacto. O VidMm é terminantemente proibido de evictar a primária de exibição.
3. **M1 (GPU_VA_ONLY):**
   - Mantém `PreferredSegment.SegmentId0 = 2u` e `SegmentId1 = 1u` para recursos que não necessitam de scanout nem MC direto.

---

## 3. O Que Vem no Pacote 3.437.1.0

1. **APU.1 (Memória Unificada):**
   - Transbordo automático de alocações não-primárias de VRAM (Seg 1) para Sistema (Seg 2).
   - Eliminação de engasgos e piscadas pretas por evicção do VidMm.
   - Proteção estrita de scanout no Segmento 1 com prioridade máxima.
2. **Zero-Stall DCN Flip (MPO3):**
   - Mantido: notificação VSYNC imediata quando o hardware confirma latch (`latchedNow == TRUE`), eliminando a latência de 32 ms.
3. **Telemetria SMU.4:**
   - Mantida: GFXCLK, VDDGFX mV, SMU_VER, IF_VER, FEAT Mask, P-State (Msg 0x0C) e Target GFXCLK (Msg 0x0F).
4. **Perf Submit Node 1 GFX:**
   - Mantido: amortização de logs de DISPATCH_LEVEL com preservação de telemetria de entrada.
5. **Carga T3 Animada 3D:**
   - Mantida: 64 MB VRAM, janela visível 480x320, 60 FPS, 180 frames por ciclo (3.0s de tela ativa).
6. **Watchdog:** 300 s (5 minutos).

---

## 4. Critério de Aceite da Corrida 288

| Item | Critério de Sucesso | Métrica Esperada |
| :--- | :--- | :--- |
| **C1** | Alocações Não-Primárias em Seg 1 e Seg 2 | `SegSet=0x3` com `Preferido=1,2` em `[APU_ALLOC]` e `[DX93_ALLOC_MAP]` |
| **C2** | Zero Piscadas Pretas do DWM | Orçamento de VRAM estável; evicções do VidMm caem drasticamente (<< 100) |
| **C3** | Resposta Rápida do Menu Iniciar | Abertura ágil de menus e janelas sem travamento ou atraso |
| **C4** | Primária de Scanout no Segmento 1 | Primária com `SegSet=0x1`, `Preferido=1` e `Prioridade=MAXIMUM` |
| **C5** | DCN Zero-Stall | > 99,8% dos flips com latch em < 16,7 ms |
| **C6** | Telemetria SMU.4 | Monitor exibindo GFXCLK 1500 MHz, Target 1500 MHz, VDDGFX 100 mV |
| **C7** | Teste T3 3D Animado | Ciclos rodando em 60 FPS com renderização visível e código de saída 0 |
| **C8** | Recuperação Limpa | Watchdog de 300 s fecha com `RECOVERY_OK_ONLINE` no BasicDisplay |
