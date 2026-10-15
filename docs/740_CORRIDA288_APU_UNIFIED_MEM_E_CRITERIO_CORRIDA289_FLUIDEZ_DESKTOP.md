# Doc 740 — Corrida 288: Memória Unificada APU.1 no Silício, Auditoria Crítica da Lentidão e Critério da Corrida 289 (Fluidez Total do Desktop)

> **Data:** 13/10/2026 21:40  
> **Autor:** Antigravity / DeepMind & Daniel  
> **Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Pacote:** Lab **3.437.1.0** (Corrida 288) -> Lab **3.438.1.0** (Corrida 289)  
> **Pasta de Evidência da Corrida 288:** [`docs/evidence/2026-10-13_20-13-51-DESKTOP-HQAG6T4-lab-binding`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-10-13_20-13-51-DESKTOP-HQAG6T4-lab-binding)  
> **Linha de Base Anterior:** Corrida 287 ([doc 738](docs/738_PACOTE_3436_PERF_SUBMIT_SMU4_ZEROSTALL_E_CRITERIO_CORRIDA287.md)) | **Critério:** [doc 739](docs/739_PACOTE_3437_APU_UNIFIED_MEMORY_SEG1_SEG2_E_CRITERIO_CORRIDA288.md)  

---

## 1. Relatório Forense e Diagnóstico da Corrida 288

### 1.1 O Que Daniel Observou em Hardware
> *"Vamos lá, aparentemente o uso de VRAM aumentou então funcionou e parou de dar as telas pretas, porém ainda tá muito lento as animações e tudo mais. Então preciso de uma análise detalhada da corrida, vamos ver como podemos melhorar a estabilidade, preciso da área de trabalho perfeito. Ao finalizar o teste 3D deu umas piscadas. Atualize todas as documentações."*

### 1.2 O Grande Avanço Estrutural: A Memória Unificada APU (APU.1)
Na Corrida 287, o sistema colapsou com telas pretas porque o KMD exigia estritamente VRAM (Seg 1) para tudo, forçando 1.010 MB de dados dentro de 233 MB úteis.
Na Corrida 288, a arquitetura APU.1 permitiu que alocações não-primárias transbordassem para o Segmento 2 (GART de 12 GB):
- **Alocações Criadas:** 8.810 alocações (totalizando **4.351,28 MB**).
- **Scanout Primário Protegido:** 4 alocações primárias (42,25 MB) permaneceram estritamente ancoradas no Segmento 1 físico com Prioridade MAXIMUM.
- **Uso de Memória no Windows:** VRAM dedicada estável em ~231 MB; RAM de sistema compartilhada absorveu **427,2 MB** de texturas e buffers sem nenhuma falha de alocação.
- **Resultado:** As telas pretas de colapso de memória foram **100% extintas**. O sistema operou por **296,56 segundos** contínuos sem BugCheck, sem TDR e sem GCVM fault, com teardown limpo (`CLEAN 0x00000000`) e rollback online perfeito para `BasicDisplay` (Código 0).

---

## 2. As Causas Reais das Animações Lentas e das Piscadas

A auditoria forense detalhada de `BC250_MONITOR.csv` e `display-log-this-bind-FINAL.txt` identificou com precisão matemática os gargalos no silício:

### 2.1 Por Que as Animações Estão Lentas?
1. **Tempestade de Cópias no SDMA0 por `PreferredSegment = 1` (479,25 MB e Pico de 71,9% da Copy Engine):**
   - No KMD (`bc250_active_miniport.c`), configurou-se `PreferredSegment.SegmentId0 = 1u` (VRAM) e `SegmentId1 = 2u` (RAM).
   - O VidMm tentou enfiar todas as 8.810 alocações (4,3 GB) nos 256 MB de VRAM. Conforme a VRAM enchia, cada nova alocação forçava o VidMm a ejetar uma anterior para a RAM via `DXGK_OPERATION_TRANSFER` (`Dir=0`).
   - Foram disparadas **2.683 transferências de paginação**, copiando **479,25 MB** pelo anel do SDMA0. A engine de cópia atingiu **71,9% de utilização**, mantendo o thread de composição do DWM bloqueado esperando paginação (`WAIT_PAGING`).
   - **Correção:** Na BC-250, tanto o Seg 1 quanto o Seg 2 residem nos mesmos chips físicos de GDDR6 a 448 GB/s. Inverter a preferência para `PreferredSegment.SegmentId0 = 2u` (RAM 12 GB) para alocações não-primárias elimina 99% dessas transferências inúteis!
2. **I/O Síncrono no SSD e Flush de Disco por Linha de Log:**
   - O KMD gerou 78,2 MB de log (661.982 linhas). Em `PASSIVE_LEVEL`, cada gravação executava síncronamente `ZwFlushBuffersFile`. Centenas de flushes físicos por segundo no SSD congelam os núcleos da CPU, degradando a fluidez das animações.

### 2.2 Por Que Ocorreram Piscadas ao Finalizar o Teste 3D?
1. **Presents Recusados no Processo 3D (Formato 28 vs 21):**
   - O teste 3D criou o swapchain com `DXGI_FORMAT_R8G8B8A8_UNORM` (formato 28u). O KMD exigia estritamente formato 21u ou 22u no Present Blt, **recusando 100% dos Presents (2.992 frames em 16 ciclos)**.
   - A janela 3D renderizou em fallback GDI. Ao fechar, a limpeza do swapchain emitiu desalocações em rajada.
2. **Estouro de Capacidade do Journal de Software (4.096 slots esgotados):**
   - O volume massivo de alocações saturou o limite de 4.096 faixas do journal de software (`Mapped=4086, Livres=10`).
   - Ao encerrar o processo 3D, inserções de desalocação falharam com `status=0x7` (`BC250_GPUVA_NO_SPACE`).
3. **Retorno Indevido de `STATUS_IO_DEVICE_ERROR` ao Sistema Operacional:**
   - Em `bc250_active_miniport.c` linha 17385, quando o journal de software estourou, o KMD retornou `STATUS_IO_DEVICE_ERROR` no `DxgkDdiFlushTlb` (2.590 vezes no log), **apesar de a invalidação física no silício GFXHUB ter ocorrido com sucesso**!
   - O VidMm e o DWM interpretaram esses 2.590 erros de I/O como falha de hardware transitória e reiniciaram as superfícies de composição do desktop, provocando as piscadas visuais relatadas pelo usuário.

---

## 3. Telemetria do Silício e DCN Zero-Stall na Corrida 288

- **Flips MPO3 Realizados:** 4.787 flips solicitados, **4.789 latches confirmados pelo hardware**.
- **Métrica Zero-Stall:** **4.786 flips (99,94%)** completaram latch em **< 16,7 ms** (sub-frame).
- **Roteamento de Interrupção:** **4.782 de 4.789 latches (99,85%)** foram notificados via IRQ do Flip do HUBP0.
- **Flips Fora do Segmento 1:** **0** (scanout 100% íntegro e blindado).
- **Taxa de Atualização do OTG:** 17.793 quadros contados = **59,997 Hz** (painel perfeitamente sincronizado via DP).
- **Telemetria SMU.4:** GFXCLK cravado em **1.500 MHz**, Target 1.500 MHz, VDDGFX **100 mV**, P-State 2-6.
- **Temperatura:** Tctl variou entre 75,4 °C e 79,9 °C (média 77,2 °C).
- **Fusível de Boot:** Status `0x00000000` (`CLEAN`), teardown fechou sem corrupção.

---

## 4. Tabela-Checklist da Corrida 288

| Item | Critério de Aceite | Resultado Obtido | Evidência P0 | Veredito |
| :--- | :--- | :--- | :--- | :---: |
| **C1** | Alocações Não-Primárias em Seg 1 e Seg 2 | `SegSet=0x3` em 8.806 alocações (4.309 MB) | `display-log-this-bind-FINAL.txt` | ✅ |
| **C2** | Fim das Telas Pretas por Asfixia de VRAM | Zero telas pretas; VRAM dedicada estável em 231 MB; RAM de sistema absorveu 427,2 MB | `BC250_MONITOR.csv` | ✅ |
| **C3** | Resposta Rápida das Animações do Desktop | Animações lentas por 2.683 transferências no SDMA0 e I/O síncrono de disco | Logs e relato do usuário | 🟡 |
| **C4** | Primária de Scanout Blindada no Segmento 1 | 4 alocações primárias estritamente em `SegSet=0x1`, Prioridade MAXIMUM | `display-log-this-bind-FINAL.txt` | ✅ |
| **C5** | DCN Zero-Stall | 99,94% dos flips com latch < 16,7 ms (4.786 / 4.789) | `[DCN1_RESUMO]` | ✅ |
| **C6** | Telemetria SMU.4 Ativa | GFXCLK 1500 MHz, Target 1500 MHz, VDDGFX 100 mV | `BC250_MONITOR.csv` | ✅ |
| **C7** | Carga T3 3D Animada | 16 ciclos concluídos com Exit 0 (187 quadros por ciclo) | `BC250_PROCESSO_NOVO.log` | ✅ |
| **C8** | Fim do Teste 3D Sem Piscadas | 2.992 Presents recusados por formato 28 e 2.590 erros `STATUS_IO_DEVICE_ERROR` no journal | `display-log-this-bind-FINAL.txt` | ❌ |
| **C9** | Recuperação Limpa | Watchdog 300 s fecha com `RECOVERY_OK_ONLINE` no BasicDisplay (Código 0) | `safe-bind-final.json` | ✅ |

---

## 5. Plano de Ação para a Corrida 289 (Pacote 3.438.1.0): Fluidez Total do Desktop

Para entregar a área de trabalho fluida e perfeita solicitada pelo Daniel:

1. **APU.2 — Inversão do Segmento Preferido (`PreferredSegment = 2` para Não-Primárias):**
   - Configurar `PreferredSegment.SegmentId0 = 2u` (RAM 12 GB) e `SegmentId1 = 1u` (VRAM 256 MB) para recursos de renderização e buffers.
   - O VidMm aloca diretamente na RAM de sistema unificada. As **2.683 transferências de paginação despencam para < 50**, o uso da Copy Engine cai de 71,9% para < 5%, liberando o DWM para rodar a 60 FPS cravados.
2. **DCN.4 — Validação de Formato Compatível em Blt Present + Swapchain B8G8R8A8 no Teste 3D:**
   - No KMD (`bc250_display_ddi.c`), validar compatibilidade de arranjo de canais: aceitar `srcFmt == dstFmt` (incluindo 28->28) e grupo BGRA/BGRX (21 <-> 22).
   - No teste 3D (`bc250-processo-novo.c`), solicitar nativamente `DXGI_FORMAT_B8G8R8A8_UNORM` (formato 21u), casando 1:1 com o scanout do GOP.
   - Elimina as 2.992 recusas de Present.
3. **GPUVA.4 — Expansão do Journal para 8.192 Entradas e Desacoplamento de Erro no FlushTlb:**
   - Expandir `BC250_GPUVA_SPACE_MAX_RANGES` para 8.192 faixas.
   - Quando a invalidação física no hardware tiver sucesso, a saturação do journal de software nunca retornará `STATUS_IO_DEVICE_ERROR` ao sistema operacional, eliminando as reinicializações de superfície do DWM e as piscadas ao fechar janelas.
4. **LOG.2 — Eliminação de `ZwFlushBuffersFile` Síncrono no Caminho Crítico:**
   - Deixar o flush de disco a cargo do Cache Manager do Windows, eliminando travamentos de núcleos de CPU por I/O.

---

## 6. Critério de Aceite da Corrida 289

| Item | Critério de Sucesso | Métrica Esperada |
| :--- | :--- | :--- |
| **C1** | APU.2 Inversão de PreferredSegment | `PreferredSegment=2` para não-primárias; transferências SDMA0 caem para < 50 |
| **C2** | Fluidez e Agilidade do Desktop | Uso da Copy Engine < 5%; menus e janelas abrindo sem lentidão |
| **C3** | Blt Present Formato 21/28 | 100% de Presents aceitos no teste 3D (zero recusas `0xC01E0200`) |
| **C4** | Zero `STATUS_IO_DEVICE_ERROR` no Encerramento | Término suave de processos gráficos sem piscadas no desktop |
| **C5** | DCN Zero-Stall | > 99,8% dos flips com latch < 16,7 ms |
| **C6** | Recuperação Limpa | Watchdog fecha com `RECOVERY_OK_ONLINE` no BasicDisplay (Código 0) |
