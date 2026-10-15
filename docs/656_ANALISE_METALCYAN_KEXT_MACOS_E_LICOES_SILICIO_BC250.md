# 656 — Análise do Kext MetalCyan (macOS) e Lições de Silício para o Driver WDDM da AMD BC-250

> **Governança Técnica, Descobertas Arquiteturais e Cruzamento de Falhas**  
> *Target: AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2, DCN 2.0.1, SMU 11.0.8, Zen 2 8C/16T, 16 GB GDDR6 UMA, Windows 10 22H2 x64)*  
> *Fonte Analisada: `referencias/MetalCyan` (Lilu Kext para macOS Tahoe 26.7.1, autor amethyst8118, baseado no NootedRed/ChefKiss)*  
> *Data: 2026-10-08*

---

## 1. O que é o MetalCyan e por que ele é crucial

O repositório `referencias/MetalCyan` contém a implementação de um plugin de kernel (kext Lilu) que habilita aceleração gráfica completa (**Metal 3**) para a APU AMD BC-250 no macOS.

O macOS possui drivers nativos para RDNA 1 (`AMDRadeonX6000`, originalmente construídos para Navi 10 / RX 5700). Como a GPU da BC-250 (**Cyan Skillfish: GC 10.1.3**) compartilha a microarquitetura gráfica base com Navi 10, o autor do MetalCyan adaptou as chamadas e corrigiu cirurgicamente as discrepâncias entre a GPU dedicada (dGPU PCIe) e a APU com memória unificada (UMA) e firmware customizado.

### Estado funcional comprovado no silício pelo MetalCyan
* **Metal 3 acelerado:** WindowServer, Safari, Firefox e aplicações Metal rodando com aceleração total sem fallback de software.
* **Resolução nativa:** 4K a 60 Hz via DisplayPort.
* **VRAM total:** 4 GB / 16 GB completamente utilizáveis e visíveis pela CPU.
* **Telemetria de SMU ativa:** Clocks da GPU, temperatura Tctl, tensões e carga em tempo real.
* **Desbloqueio de 40 CUs:** Suporte a 40 CUs funcionais (stock é 24 CUs).

O valor do MetalCyan para o nosso driver WDDM (`NamixWDDM`) está nos **diagnósticos de falhas de hardware e firmware que o autor mapeou diretamente no silício da BC-250**, resolvendo vários dos mesmos gargalos e congelamentos que enfrentamos em nossas corridas no Windows.

---

## 2. Achado 1: O Bug Fatal de Invalidação de TLB no SDMA 5.0 (Causa do Hang no `FLUSH_TLB`)

### O problema no silício
Nas Corridas 128 e 129 do NamixWDDM, o driver travou com a thread de paginação do VidMm bloqueada dentro de `FLUSH_TLB`. O autor do MetalCyan documentou com precisão cirúrgica a causa exata dessa falha (`referencias/MetalCyan/MetalCyan/BC250HWL.cpp:2501-2548`):

> *"With flush type 0 the packet still never completed: SDMA 5.0's firmware on the BC-250 does not finish GPUVM_INV at all (Linux's sdma_v5_0 never emits it; it invalidates with SRBM_WRITE of the REQ register and a POLL_REGMEM of its ACK). The live dump of the hung GPU had SDMA1's page queue parked on that opcode (STATUS2.CMD_OP 0x10, firmware looping) with the GC VM L2 busy: VMID 1's page-table program queued behind it never landed (CTX1 PDB 0), and WindowServer's first GFX work waited on its fence until the userspace watchdog panicked. Flush type 0, as the requests the hub acknowledges (GVM's 0x02F80000 form)."*

1. O microcódigo do SDMA 5.0 da Cyan Skillfish 2 **possui um defeito de fábrica:** ele não conclui o pacote de anel nativo `GPUVM_INV` (opcode `0x10`). O processador de comandos do SDMA entra em loop infinito (`CMD_OP 0x10`).
2. Requisições com *flush type 1* (utilizadas por drivers de dGPU da AMD) nunca são confirmadas pela MMU/GFXHUB e travam o L2.

### Solução implementada no MetalCyan e Linux
Em vez de emitir o opcode `0x10`, a invalidação de TLB no anel SDMA deve ser emitida como uma sequência de **21 dwords** composta por:
1. `SRBM_WRITE` configurando a faixa de endereços (Low/High).
2. `SRBM_WRITE` no registrador `VM_INVALIDATE_ENG_REQ` com o valor `0x02F80000 | vmids` (**flush type 0 obrigatório**).
3. Leitura fictícia opcional de `REQ`.
4. `POLL_REGMEM` aguardando o registrador `VM_INVALIDATE_ENG_ACK` casar com a máscara de `vmids`.

```cpp
// Sequência de invalidação de TLB via SDMA comprovada na BC-250:
buf[8]  = 0x02F80000 | (vmids & 0xFFFF); // VM_INVALIDATE_ENG_REQ (GVM format, flush type 0)
buf[18] = vmids & 0xFFFF;                // Target value
buf[19] = vmids & 0xFFFF;                // Mask
```

### Impacto no NamixWDDM
No nosso KMD (`bc250_active_miniport.c` / rotina `DxgkDdiBuildPagingBuffer` com `DXGK_OPERATION_FLUSH_TLB`), qualquer emissão direta de pacote SDMA com opcode `0x10` congelará a fila de paginação. Devemos adotar estritamente a sequência `SRBM_WRITE` + `POLL_REGMEM` ou realizar o flush síncrono via MMIO com `0x02F80000`.

---

## 3. Achado 2: O MEC do GC 10.1.3 é Quebrado — Compute Deve Rodar no Anel GFX

### O problema no silício
Durante o desenvolvimento do Marco 4 e integração do RADV/MEC no NamixWDDM, observamos recusas de submissão, paradas no `vkCreateDevice` e congelamentos rápidos.

O MetalCyan esclarece a realidade física do bloco MEC na BC-250 (`referencias/MetalCyan/MetalCyan/BC250HWL.cpp:2583-2598`):

> *"Compute stays off the MEC. GFX1013's compute engines are broken (Mesa and Linux run the BC-250 with no compute queues: dispatches mis-execute, queue teardown wedges); Firefox's first Metal compute work on a MEC pipe (HIQ-mapped queue, EOP ring 7, VMID 13) froze the machine within 0.2 s. Every accelerator channel takes its hardware channel from AMDGFX10Hardware::getHWChannel(channel type, priority, index) ... a compute request (type 1) gets the GFX channel, so Metal's compute command buffers run on the GFX ring, which executes dispatches correctly."*

### O que acontece no hardware
1. As filas assíncronas do MEC (Compute Queues) no silício GFX1013 possuem defeito de microcódigo/hardware: dispatches computacionais executam com corrupção de dados ou entram em deadlock no teardown da fila.
2. O driver oficial do Linux (`amdgpu`) e o Mesa (`RADV`) **não ativam filas assíncronas de MEC no Cyan Skillfish**; eles submetem comandos de computação exclusivamente no **anel gráfico principal (GFX Ring, Node 0)**.
3. O MetalCyan intercepta a criação de canais de hardware (`getHWChannel`) e força requisições de computação (`type 1`) a utilizarem o canal de GFX (`type 0`).

### Impacto no NamixWDDM
No nosso driver WDDM:
* Não devemos tentar expor o Node 1 (MEC) como fila de computação assíncrona isolada para o DirectX 12 / Vulkan.
* Os contextos de Compute devem ser despachados no **Node 0 (GFX)**, que possui suporte nativo a pipelines de compute shader no hardware RDNA 1 e executa de forma estável.

---

## 4. Achado 3: Regras Rígidas de Energia — Relação Direta com a Trava Dura (Doc 653)

No [Doc 653](653_AUDITORIA_EXTERNA_TRAVA_DURA_E_COMPARACAO_AMDGPU.md), diagnosticamos que a ativação do firmware do RLC (`RLC.1`) acelerava a trava dura em 5x devido a tentativas de power gating sem suporte do SMU.

O MetalCyan corrobora e resolve essa exata questão (`referencias/MetalCyan/MetalCyan/BC250HWL.cpp:1892-1904` e `1997-2012`):

### Configuração obrigatória no Cyan Skillfish
* O Linux roda o Cyan Skillfish com:
  $$\text{cg\_flags} = 0, \quad \text{pg\_flags} = 0$$
* **Clock Gating proibido:** Todas as máscaras de MGCG, CGCG e CGLS devem permanecer desativadas (`RLC_CGCG_CGLS_CTRL = 0`).
* **Handshake com SMU proibido:** Ao iniciar o RLC, o bit 23 de `RLC_PG_CNTL` (`RLC_PG_NO_SMU_HANDSHAKE`) **deve obrigatoriamente ser setado**:

```cpp
constexpr UInt32 kRlcPgNoSmuHandshake = 1U << 23;

// Sequência comprovada no MetalCyan:
gcWriteReg(gc, kRlcCgcgCglsCtrl, 0);
gcWriteReg(gc, kRlcPgCntl, 0);
gcWriteReg(gc, kRlcPgCntl, gcReadReg(gc, kRlcPgCntl) | kRlcPgNoSmuHandshake);
```

Se o bit 23 não for setado, o RLC tenta negociar estados de energia com a SMU. Como a SMU da BC-250 (firmware 11.0.8) opera em conjunto de mensagens customizado, a transação não recebe resposta e o barramento do Data Fabric entra em stall, causando a trava dura imediata do sistema.

---

## 5. Achado 4: Tradução de Memória do Carve-Out (`GCMC_VM_FB_OFFSET`)

O MetalCyan detalha a matemática de endereçamento necessária para operar a memória GDDR6 compartilhada da APU:

1. **Registrador de Base Física:**  
   `GCMC_VM_FB_OFFSET = GC_BASE_0 + 0x170B` (`0x1260 + 0x170B = 0x296B`).  
   Armazena o endereço físico do carve-out no barramento do sistema ($\text{PA} \gg 24$).
2. **Page Directory Base (PDB / Root Page Table):**  
   Os registradores `GCVM_CONTEXTn_PAGE_TABLE_BASE_ADDR_LO32/HI32` exigem o endereço físico real:
   $$\text{PA}_{\text{root}} = \text{Offset}_{\text{VRAM}} + \text{gvmFbOffset}$$
   Se o driver gravar apenas o offset relativo da VRAM (começando em 0), a MMU da GPU buscará os blocos de tabelas na RAM física do sistema entre 0 e 512 MB, gerando corrupção e pânico imediato.
3. **PTEs e PDEs de VRAM:**  
   Qualquer entrada de tabela que aponte para alocação em VRAM (sem a flag `SYSTEM` no bit 1) deve conter o endereço físico do carve-out.
4. **Pacote PM4 `MAP_PROCESS`:**  
   Nas filas agendadas pelo CP, as dwords 2 e 3 do cabeçalho `MAP_PROCESS` devem ser ajustadas com `vramPhys(base)` antes da submissão.
5. **Acesso da CPU além dos 256 MB da BAR0:**  
   Como a BAR0 na BIOS cobre apenas 256 MB mas o carve-out possui de 4 GB a 16 GB, blocos de tabelas de página alocados além de 256 MB são acessados diretamente pela CPU mapeando o endereço físico correspondente (`Offset + gvmFbOffset`) com cache desativado (`Uncached` / `Write-Combine`).

---

## 6. Achado 5: Display e DCN 2.0.1 (Arquitetura P9)

Para a implementação futura do Display Nativo (Prioridade P9 do roadmap), o MetalCyan fornece o mapa completo do hardware DCN 2.0.1 na BC-250 (`referencias/MetalCyan/MetalCyan/BC250DCN.cpp`):

### 1. Ausência de apertura DCN_VM
O DCN 2.0.1 na APU Cyan Skillfish **não possui apertura de translação de display**.
Portanto, os registradores de scanout de superfície primária e cursor:
* `HUBPREQ_PRIMARY_SURFACE_ADDRESS` (`0x60A`)
* `HUBPREQ_PRIMARY_SURFACE_ADDRESS_HIGH` (`0x60B`)

**Não aceitam endereço MC da GPU.** Devem receber obrigatoriamente o **endereço físico UMA do sistema**:
$$\text{PA}_{\text{scanout}} = \text{Endereço}_{\text{MC}} - \text{fbBase} + \text{fbOffset}$$

### 2. Contagem de Pipes
A Navi 10 possui 6 pipes de exibição; a Cyan Skillfish possui **apenas 4 pipes**. O MetalCyan desativa divisão de planos (MPC splits) para garantir 1 pipe por stream, evitando chamadas direcionadas ao pipe 5 inexistente.

### 3. Clocks do Display (DENTIST VCO)
* O gerador de clock do display (DENTIST) é lido no registrador `CLK4_CLK_PLL_REQ` (`0x16C00 + 0x460E`):
  $$\text{vcoKHz} = \text{fbInt} \times 100000 + \frac{\text{fbFrac} \times 100000}{65536} \approx 2.670.000\text{ kHz}$$
* O divisor de clock do display e do pixel pipe é controlado em `DENTIST_DISPCLK_CNTL` (`0xC0 + 0x64`), e os valores válidos do divisor (`DID`) operam em passos de um quarto (`didToDiv4`), com piso em `0x0A` (divisor 2.5 do GOP).

---

## 7. Achado 6: Driver e Mailbox da SMU 11.0.8

O MetalCyan implementa o controle completo da SMU através das portas PCI do Host Bridge (`00:00.0`, `Vendor 0x1022`):
* Registrador de Índice SMN: `0xB8`
* Registrador de Dados SMN: `0xBC`

### Filas de Mensagens (Mailbox Queues)
| Fila | CMD | RSP | ARG | Finalidade Principal |
| :--- | :--- | :--- | :--- | :--- |
| **Queue 0** | `0x03B10A08` | `0x03B10A68` | `0x03B10A48` | Informações de GPU, clocks e estados de energia |
| **Queue 3** | `0x03B10A20` | `0x03B10A80` | `0x03B10A88` | Telemetria de CPU, tensões e limites térmicos |

### Mensagens Seguras e Testadas
* **Q0 `0x02`:** `GetSmuVersion`
* **Q0 `0x0F`:** `QueryGfxclk` (Retorna clock atual da GPU em MHz)
* **Q0 `0x38`:** `GetGfxVid` (Tensão da GPU: $1550\text{ mV} - \text{VID} \times 6.25\text{ mV}$)
* **Q3 `0x36`:** Tensão atual da CPU (mV)
* **Q3 `0x37`:** Tensão atual da GPU (mV)
* **Q3 `0x40`:** Limite de temperatura da CPU (°C)
* **Q3 `0x43`:** Frequência individual por núcleo (arg: 0 a 7)
* **Temperatura Tctl:** Leitura direta do registrador SMN `0x00059800` (`THM_TCON_CUR_TMP`, bits [31:21] multiplicados por 0,125 °C).

### Procedimento de Desbloqueio das 40 CUs
O silício possui 40 CUs físicas (2 Shader Engines × 2 Shader Arrays × 5 WGPs). A BIOS vem configurada de fábrica com colheita forçada (`0xFFF80000`). O desbloqueio atômico comprovado consiste em gravar:
1. `CC_GC_SHADER_ARRAY_CONFIG` (`0x1260 + 0x100F` = 0)
2. `SPI_PG_ENABLE_STATIC_WGP_MASK` (`0x1260 + 0x1277` = `0x1F`)
3. `RLC_PG_ALWAYS_ON_WGP_MASK` (`0xA000 + 0x4C53` = `0x1F`)
*(Atenção: Deve ser gravado antes do `hw_init` e reaplicado imediatamente após o start do RLC, pois a subida do microcódigo restaura o harvest original).*

---

## 8. Achado 7: Quirk de Interrupção no SDMA1

No arquivo `referencias/MetalCyan/MetalCyan/BC250HWL.cpp:2840-2849`, há outro detalhe do silício:
* O registrador `SDMA1_CNTL` (offset `0x187C`) nunca tem o bit 0 (`TRAP_ENABLE`) setado automaticamente na subida dos manipuladores padrão de interrupção, enquanto o `SDMA0_CNTL` (`0x127C`) o possui.
* Fences e operações submetidas ao anel do SDMA1 são processadas pela engine, mas nunca completam para o sistema operacional porque o sinal de interrupção física (IV) não é disparado.
* **Solução:** O KMD deve assegurar que o bit 0 de `SDMA1_CNTL` esteja ligado em paridade com o SDMA0.

---

## 9. Tabela de Equivalência: Falhas do NamixWDDM vs Diagnóstico MetalCyan

| Sintoma / Dúvida no NamixWDDM | Causa Raiz Revelada no MetalCyan | Correção Técnica Comprovada |
| :--- | :--- | :--- |
| **Trava no `FLUSH_TLB` (Corridas 128/129)** | SDMA 5.0 da BC-250 trava no opcode `0x10` (`GPUVM_INV`). | Emitir `SRBM_WRITE` em `VM_INVALIDATE_ENG_REQ` (`0x02F80000 \| vmids`) e aguardar via `POLL_REGMEM`. |
| **Stall com Compute no Node 1 (MEC)** | O bloco MEC do GC 10.1.3 tem bug de silício; queues assíncronas travam. | Roteamento de comandos de Compute para o Node 0 (GFX Ring), idêntico a Mesa e macOS. |
| **Trava Dura com RLC ativo (Doc 653)** | RLC tenta negociar power gating com a SMU ausente. | Setar bit 23 de `RLC_PG_CNTL` (`RLC_PG_NO_SMU_HANDSHAKE`) e manter `CGCG_CGLS_CTRL = 0`. |
| **Faults de Memória em PDBs/PTEs** | VRAM tratada como offset 0 em vez de PA do sistema. | Somar `GCMC_VM_FB_OFFSET` a todos os endereços de páginas e raízes em VRAM. |
| **Endereço de Scanout no DCN (P9)** | DCN 2.0.1 não tem translação DCN_VM. | Passar PA físico UMA nos registradores `HUBPREQ_PRIMARY_SURFACE_ADDRESS`. |
| **Timeouts em fences do SDMA1** | `TRAP_ENABLE` (bit 0) desligado em `SDMA1_CNTL`. | Ligar bit 0 em `0x187C` para habilitar interrupções da engine SDMA1. |

---

## 10. Próximos Passos de Engenharia no Repositório

1. **Revisar a rotina de Flush de TLB (`bc250_active_miniport.c`):**  
   Auditar a emissão do `DXGK_OPERATION_FLUSH_TLB` para assegurar que nenhum comando do anel SDMA esteja emitindo opcode `0x10`, substituindo pelo formato síncrono `SRBM_WRITE` com `0x02F80000`.
2. **Desativar permanentemente Clock Gating e SMU Handshake no RLC:**  
   Garantir que as escritas em `RLC_PG_CNTL` incluam incondicionalmente `1U << 23`, alinhando com a H1 do Doc 653.
3. **Mapeamento de Filas de Computação (UMD / KMD):**  
   Garantir que a UMD D3D11/Vulkan direcione pipelines de compute para o Node 0, preservando estabilidade contra o bug de hardware do MEC.
4. **Incorporação de Telemetria SMU no Utilitário de Monitoramento:**  
   Utilizar a descoberta das portas PCI `0xB8/0xBC` e do registrador `SMN 0x59800` para alimentar o monitor de telemetria em tempo real ([Doc 655](655_MONITOR_TELEMETRIA_TEMPO_REAL_CONSOLE_2S.md)) com a temperatura real do chip e frequências da GPU/CPU.
