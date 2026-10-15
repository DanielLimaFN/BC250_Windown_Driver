# 579 — M1b.1 ligada: RADV no segmento 2, e o critério da Corrida 211

**Data:** 03/10/2026 · **Tipo:** offline · **Antecede:** [doc 578](578_MINERACAO_CORRIDA210_FALTA_DE_MEMORIA_NO_SEGMENTO1_CASCATA_DO_DWM_E_O_QUE_SOBRA_DA_TRAVA.md) (mineração da 210) e [doc 554](554_M1B1_RADV_NO_SEGMENTO_2_DORMENTE_E_CORRECAO_DO_HEAP.md) (desenho dormente).
**Decisão:** do Daniel, "VRAM mínima, memória extra pelo segmento 2" (doc 551). Ele autorizou ligar em 03/10.

---

## 1. O que liga (os três interruptores juntos, como o portão exige)

| Interruptor | Onde | Efeito |
|---|---|---|
| `BC250_M1B_RADV_MEMORY=1` | RADV, `radv_wddm2_winsys.h` | Tamanhos reais dos segmentos via `D3DKMTQueryStatistics`. O segmento 2 é abertura (GART de 2 GB, `Aperture=1`) e vira o GTT. `FUSION` passa a valer, com o heap local em 2/3 de (GTT + VRAM visível). O blob da alocação passa a carregar `DOMAIN_GTT` e `COMMAND` |
| `BC250_M1B_RADV_SYSTEM_SEGMENT_ENABLED=1` | KMD, `CreateAllocation` | Política `Bc250AllocationRadvSystemSegmentPolicy`:<br>• RADV com domínio GTT → `SegSet 0x3`, preferido 2 → 1;<br>• RADV na VRAM **sem** CPU → `SegSet 0x3`, preferido 1 → 2 (transbordo);<br>• comando, VRAM com CPU e classe da UMD → ficam no 1.<br>Log `[M1B_RADV_SISTEMA]` |
| `BC250_M1B_PRESENT_DEVICE_LOCAL=1` | UMD, ponte Vulkan | O buffer linear do Present pede primeiro `DEVICE_LOCAL\|HOST_VISIBLE\|HOST_COHERENT` |

**Conferido no fonte da RADV** (`radv_physical_device_init_mem_types`, P1):
- O tipo local e mapeável é domínio **VRAM com `CPU_ACCESS`**. Pela política, fica no segmento 1, e a cópia do Present por MC (`COMPOSITE_BLT`, 753 OK na 210) continua válida.
- O tipo só local é domínio VRAM `NO_CPU_ACCESS` e passa a poder transbordar.

**Impacto esperado, medido sobre a 210 (P0 das alocações; o efeito é H):**
- das 2.550 alocações da RADV, **1.908** (1,5 GB acumulados) são sem CPU e ganham o transbordo;
- 642 (349 MiB) continuam presas ao segmento 1.

## 2. Provas offline

| Item | Resultado |
|---|---|
| RADV recompilada do zero | 108 objetos apagados, pela lição do ninja parcial. EXIT=0. DLL `80EBB248…7043`; strings `M1B_MEMORIA status=` e `M1B_SEGMENTO id=` presentes e `M1B_MEMORIA desligada` ausente. Cópia da anterior guardada em `vulkan_radeon.dll.pre-m1b-5447A17D` |
| UMD | `build-umd` EXIT=0; DLL nova `8782A5E5…` |
| `active_miniport_test` | Reprovou na primeira passada: a linha 2103 congelava o `SegSet=1` da M1b desligada. Reescrito pela política do contrato (fonte única), e agora reprova de propósito se a M1b for desligada |
| `audit_m1b1_radv_system_segment` | PASS. **Mutação:** desligar só o KMD reprova ("interruptores divergentes KMD=0 UMD=1" e "RADV diverge do KMD") |
| Auditorias | segurança, pacote de laboratório, matriz e noop PASS; `build-contract` EXIT=0 |

**Pacote (assinado, auditado, NÃO INSTALADO):** lab **3.358.1.0**.

| Arquivo | Hash |
|---|---|
| SYS | `5EF2D739…40A5` (`[M1B_RADV_SISTEMA]` conferido) |
| INF | `568A3464…F520` |
| CAT | `9580696A…0486` |
| UMD | `8782A5E5…E468` |
| RADV | `80EBB248…7043` |

Preflight `prepare-corrida211-m1b1-radv-segmento2.ps1`: os cinco hashes e o manifesto conferem. Ele só recusou a CMOS antiga, que o roteiro lê antes.

**Riscos declarados:**
- **Caminho nunca exercitado em escala:** página de sistema servindo a RADV inteira. A base P0 vem da M1 (Corrida 149, mais de 5.000 alocações) e dos DMA buffers.
- **Alocações da RADV na VRAM com CPU** continuam presas ao segmento 1, e a RADV agora acredita ter ~1,5 GB "visíveis". Elas ainda podem esgotar o segmento 1.
- **`COMPOSITE_BLT` de origem no segmento 2** é recusado com log (falha fechada).
- **Se a leitura dos segmentos falhar**, a RADV falha fechada e não cria o device: o desktop não sobe.

## 3. Critério da Corrida 211 (escrito antes, Lei 10)

**Roteiro:** recuperação, boot frio, CMOS em 256 MB, `tools\EXECUTAR_CORRIDA211_M1B1_RADV_SEGMENTO2_600S.cmd`, mesmo uso. **Anotar no Gerenciador de Tarefas a memória dedicada e a compartilhada** logo depois do desktop, ao abrir a Pesquisa e antes do fim. Se travar: reset e coletor antes da recuperação.

**Sinais:**
- `radv/wddm2: M1B_MEMORIA status=0x00000000 vis=… gtt=…` nos logs da RADV;
- `[M1B_RADV_SISTEMA]` no KMD;
- distribuição `SegSet` das alocações de classe 0 (`DX93_ALLOC_MAP`);
- `MakeResident: status=0xC0000017` e `vkAllocateMemory res=-2`;
- número de instâncias do `dwm.exe`;
- `COMPOSITE_BLT` recusado;
- memória compartilhada no Gerenciador;
- trava, timeouts e faults.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | M1b ativa (`M1B_MEMORIA status=0`, `[M1B_RADV_SISTEMA]` > 0, alocações de classe 0 com `SegSet 0x3`), compartilhada > 0, **zero** `MakeResident NO_MEMORY`, **um** `dwm.exe` e ~600 s sem trava dura | Falta de memória resolvida; a trava não reapareceu (n=1). Repetir antes de concluir sobre a trava |
| **B** | M1b ativa, zero `NO_MEMORY` (ou muito menos que 211) e um só DWM, mas **trava dura** | O defeito funcional fechou; a trava é independente da falta de memória, como a 207 já indicava. Seguir para fences por interrupção, SMU e SDMA1 |
| **C** | M1b ativa, mas `NO_MEMORY` e a cascata do DWM continuam | O transbordo não basta: as alocações com CPU no segmento 1 ainda esgotam a memória. Rever a política para VRAM com CPU (a cópia por MC exige o 1) |
| **D** | `M1B_MEMORIA` com falha, device da RADV não criado, desktop não sobe, `COMPOSITE_BLT` recusado em massa, ou zero `[M1B_RADV_SISTEMA]` | A ligação falhou. Voltar offline; reverter os três interruptores |
| **E** | Fora do mapa | Descrever antes de julgar |

**Regras:**
- A e B exigem a M1b **ativa** pelos três sinais.
- Desfecho parcial não conta.

**NÃO VALIDADO EM HARDWARE.**
