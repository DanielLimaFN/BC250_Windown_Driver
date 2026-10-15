# 602 — P1b: CPU na VRAM pela DRAM, sem HDP, como o amdgpu em APU; critério da Corrida 224

**Data:** 05/10/2026 · **Candidato:** lab **3.372.1.0** · **Base:** [doc 601](601_CORRIDA223_DOORBELL_ATIVA_PELA_PRIMEIRA_VEZ_TRAVA_AOS_338S_E_ARTEFATOS_DO_CLIMA.md) (doorbell ativa, trava aos ~338 s)
Trabalho offline. **NÃO VALIDADO EM HARDWARE.**

## 1. O que o amdgpu faz nesta placa (P1)

Fonte: linux v6.6, com cópias em `inc/linux_ref/PROVENANCE_P1B_HDP_APU.txt`.

- **A placa é APU:** `amdgpu_drv.c` cadastra `0x1002:0x13FE = CHIP_CYAN_SKILLFISH | AMD_IS_APU`.
- **A CPU acessa a VRAM pela DRAM:** em `gmc_v10_0_mc_init`, numa APU x86_64 sem passthrough, `aper_base = get_mc_fb_offset() = GCMC_VM_FB_OFFSET << 24`. É o endereço físico da VRAM na própria DRAM (`0x460000000` nesta placa), **não a BAR PCI**, mapeado com `ioremap_wc`.
- **Por isso não há HDP:** `amdgpu_device_flush_hdp` e `amdgpu_device_invalidate_hdp` retornam logo na primeira linha em `AMD_IS_APU`. Sem a BAR no caminho, não há HDP para esvaziar.

**Nós fazíamos o contrário:**
- toda escrita e leitura da CPU na VRAM passava pela BAR0 (`0xC0000000`): janela do display, segmento do VidMm (os `Lock` do DWM e da UMD), anéis, tabela da GART, CSB e bloco do MEC;
- cada submissão do SDMA0 fazia HDP flush e invalidate por MMIO (2 escritas e 2 leituras de registrador);
- mais os `HDP_PT_FLUSH` dos caminhos de tabela de página.

Tirar só o HDP e continuar pela BAR seria **perder** a coerência que o flush deu em 10/09. As duas coisas andam juntas, e esta faixa faz as duas.

## 2. O que muda

**`bc250_vram_cpu_path.h` (novo), fonte única:**
- `Bc250VramCpuTranslate(barPhys)` devolve `base_DRAM + (barPhys − BAR0)` quando a P1b está armada, e identidade quando não está;
- `Bc250VramCpuHdpNeeded()` é FALSE com a P1b ativa e conta as omissões.

**Arme `Bc250ActiveArmVramCpuViaDram`**, no StartDevice, logo antes do `DisplayInit`, que é o primeiro mapeamento de VRAM do regime:
- **Preflight:**
  - a base vem da placa (`TopologyEvidence.AddressSpace.VramBaseOffset`, ou seja `GCMC_VM_FB_OFFSET<<24`) e é alinhada a 1 MB;
  - a faixa `[base, +256 MB)` **não cruza nenhuma** faixa de RAM do Windows (`MmGetPhysicalMemoryRanges`).
- **Prova de alias:** a 1ª página (scanout do GOP) é lida pela BAR e pela DRAM. Os 1.024 dwords têm de ser iguais e não-zero: é a mesma memória pelos dois caminhos.
- **Qualquer falha:** `NAO ATIVA`, e o caminho fica inteiro pela BAR com HDP, declarado.

**Sítios traduzidos (todos os mapeamentos de VRAM do regime):**
- janela WC do display (`FrameBufferVirtual`, de que dependem o present, a primária e as leituras de PTE);
- `CpuPhysicalStart` do segmento do VidMm;
- anel legado do SDMA0 (`SdmaPresentInit`);
- anel e Work de boot do SDMA (`sdma_ring_work`);
- bloco do MEC (`submission_engine`);
- GFX (bring-up e online);
- tabela da GART da D2;
- `gcvm_backing`;
- `vram_probe`;
- firmware PFP;
- leitura de PTE.

**HDP omitido com a P1b ativa:**
- `Bc250SdmaPresentHdp` (cada submissão);
- `Bc250ActiveHdpWriteFlush` (`HDP_PT_FLUSH`, P7, D2, flush de TLB);
- `Bc250SdmaHdp` (anel de boot).

**Teardown:** desarma, com log de quantos HDP foram omitidos.

**Fora do escopo, declarado:** `bc250_compute_service.c`, o serviço de compute do CLI de laboratório. Ele tem base própria e HDP próprio, é coerente consigo e não roda no regime do desktop. O `FrameBufferPhysical` reportado ao Windows (`CrtcVsync.PhysicalAddress`) continua o da BAR, porque é o endereço que o próprio sistema entregou.

## 3. Provas offline

- `build-contract` EXIT=0;
- laboratório "TODOS OS TESTES APROVADOS (0 falhas)";
- auditoria de segurança PASS;
- auditoria do pacote PASS;
- string `[P1B_VRAM_DRAM] ATIVO` no SYS assinado.
- **Portão novo `audit_p1b_vram_cpu_pela_dram`, com 14 mutações reprovadas:**
  - chave em 0;
  - tradutor sem conferir a faixa;
  - MEC, anel de boot, segmento do VidMm, tabela da GART, SDMA legado e janela WC voltando à BAR;
  - arme depois do `DisplayInit`;
  - arme sem checar a RAM do Windows;
  - arme sem prova de alias;
  - HDP do SDMA e HDP de PT incondicionais;
  - teardown sem desarme.

| Arquivo | SHA-256 |
|---|---|
| SYS | `C02E845EAD8E0E7564EB1F387903E45CAADA6D1FB5AF4C065E512E3291F50A21` |
| INF | `8F48FDCD14B4E9900B76F0952EC2143C9FF9551F2370553F3FAA31A53415E1E3` |
| CAT | `E7D8DFBC48C1888A899F8949564769C8E79BE3887C5C4141055595AF3A60073C` |

**Scripts:** `tools/prepare-corrida224-p1b-vram-pela-dram.ps1` e `tools/EXECUTAR_CORRIDA224_P1B_VRAM_PELA_DRAM_600S.cmd`. O dry-run passou em hashes e manifesto e parou na CMOS, como esperado.

**Riscos declarados (H):**
- Não sei se o `DxgkCbMapMemory` aceita endereço fora dos recursos da placa (a DRAM). Se recusar, o anel de boot ou o MEC falham fechado no StartDevice (desfecho C). A prova de alias usa `MmMapIoSpaceEx`, então ela não testa isso.
- A janela WC e os mapeamentos UC continuam separados por faixa, como na T3.1, porque a tradução preserva os offsets.

## 4. Critério da Corrida 224 (Lei 10)

**Pré-condição de validade:**
- `[P1B_VRAM_DRAM] ATIVO: ... BAR 0xC0000000 -> DRAM 0x460000000 ... nenhuma sobreposta; alias provado 1024/1024`;
- depois disso, a base mantida: `[DisplayInit] FrameBuffer mapping` sem falha, D2, `[P2_DOORBELL] ATIVA`, P7 e `DrenoOk=1`;
- **nenhuma** linha `[HDP1_FLUSH]` nem `[HDP_PT_FLUSH]` depois do arme.

Desfechos:

- **A — 600 s sem trava dura**, com a P1b ativa até o fim (`TEARDOWN: desarmada (HDP omitido N vezes)`).
  - *Implicação:* o acesso da CPU pela BAR/HDP era causa, ou parte dela. Só conta com repetição (≥2 seguidas).
- **B — trava dura igual**, com a P1b ativa.
  - *Implicação:* a BAR/HDP sai das causas suficientes. Próximo: P3 (conclusão por interrupção).
- **C — a P1b não arma** (`NAO ATIVA`) ou o StartDevice falha num mapeamento traduzido.
  - *Implicação:* o log diz qual checagem: RAM sobreposta, alias que não confere ou `DxgkCbMapMemory` recusado. Se não armar, a corrida repete a 223 pela BAR.
- **D — arma, mas aparece corrupção** (artefatos novos, primária errada, fence invisível, timeout do SDMA).
  - *Implicação:* algum caminho ainda depende do HDP, ou algum mapeamento da CPU ficou fora do tradutor.
- **E — fora do mapa:** descrever antes de julgar.

## 5. Pendências

- **Daniel:** recuperação, fusível, boot frio, CMOS `-Ler` a menos de 15 min e `tools\EXECUTAR_CORRIDA224_P1B_VRAM_PELA_DRAM_600S.cmd`.
- **NÃO VALIDADO EM HARDWARE:**
  - a P1b;
  - 600 s sem trava de forma repetível;
  - composição de superfície entre processos (doc 601 §4).
