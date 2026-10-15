# 609 — P3: interrupções pelo IH. O IH é cliente do MMHUB; P3.0 (MSI + foto do MMHUB) e critério da Corrida 229

**Origem:** doc 608, desfecho B: a P1b não basta. A P3, conclusão por interrupção em vez de polling, é a maior diferença que sobra no inventário de paridade (doc 593 §4).
**Fontes:** `inc/linux_ref/PROVENANCE_P3_IH.txt` (16 arquivos do Linux v6.6, com SHA-256) e a coleta Linux da BC-250 de 01/10 (`docs/evidence/2026-10-01_15-01-LINUX-REFERENCIA-UMA256/`).

## 1. Como o amdgpu faz na BC-250

**Coleta Linux (P0 do amdgpu):**
- `detected ip block <ih_v5_0_0> (navi10_ih)`;
- GART de 512 MB em VA `0x0..0x1FFFFFFF`, tabela na VRAM em `0xF40FE00000`;
- **`PCI-MSIX-0000:01:00.0 0-edge amdgpu`** com ~35 mil interrupções.

**`navi10_ih.c` (P1):**
- `navi10_ih_sw_init`: com `AMD_IS_APU`, `use_bus_addr=false`, então o anel 0 do IH usa **endereço virtual da GPU** (`MC_SPACE=4`, VMID 0); 256 KB, com writeback do WPTR e doorbell para o RPTR.
- `navi10_ih_irq_init`:
  1. desliga o anel;
  2. `nbio_v2_3_ih_control`: `INTERRUPT_CNTL2` = página dummy >> 8, `IH_DUMMY_RD_OVERRIDE=0`, `IH_REQ_NONSNOOP_EN=0`;
  3. `navi10_ih_enable_ring`: BASE, `RB_CNTL`, WPTR/RPTR=0, endereço do writeback;
  4. liga `RB_ENABLE`, `RB_GPU_TS_ENABLE`, `ENABLE_INTR`, com `RPTR_REARM=1` se MSI.

**Origem das interrupções que interessam:**
- **SDMA0:** `TRAP` depois da fence (`sdma_v5_0_ring_emit_fence` com `FLAG_INT`) e `SDMA0_CNTL.TRAP_ENABLE`. Vetor cliente `0x08`, fonte `0xE0`.
- **GFX:** EOP, cliente `0x14`, fonte `0xB5`.

## 2. O achado que mudou o plano (P1)

O IH fica no bloco **OSS**, e o OSS é **cliente do MMHUB**, não do GFXHUB (`mmhub_v2_0.c`, tabela de clientes `[15] = "OSS"`). O `gmc_v10_0_gart_enable` liga a GART **nos dois hubs**. O nosso driver só programa o GFXHUB: a D2 e a GART da VMID 0 são do GFXHUB, e nenhum registrador do MMHUB é escrito em lugar nenhum.

**Consequência:**
- Com o anel do IH por VA, o IH escreveria por uma VMID 0 do MMHUB que ninguém configurou.
- O destino depende do estado em que o firmware deixou o MMHUB. Pode ser a página padrão, um fault ou, no pior caso, a RAM física baixa.

**Por isso o arme do IH ficou escrito, mas DESLIGADO** (`BC250_P3_IH_ENABLED 0`), e o portão impede ligá-lo sem a GART do MMHUB.

## 3. O que já está no código (candidato 3.376.1.0)

**P3.0, ativa nesta corrida:**
- **INF:** `MSISupported=1`, `MessageNumberLimit=1`, como o amdgpu (MSI/MSI-X). O hardware anuncia linha, MSI e MSI-X (`DEVPKEY_PciDevice_InterruptSupport=7`); sem a chave, o Windows não usa MSI.
- **Recurso de interrupção no log:** `[P3_IH] recurso de interrupcao #N: MENSAGEM (MSI)` ou `LINHA`, ou `NENHUMA`, a partir da lista traduzida do `DxgkCbGetDeviceInformation`.
- **Foto só de leitura de 17 registradores do MMHUB:**
  - `VM_L2_CNTL`, `PROTECTION_FAULT_STATUS`/`ADDR`;
  - `CONTEXT0_CNTL`, `PAGE_TABLE_BASE`/`START`/`END`;
  - `FB_OFFSET`, `FB_LOCATION_BASE`/`TOP`, `AGP_TOP`/`BOT`;
  - `SYSTEM_APERTURE_LOW`/`HIGH`/`DEFAULT`, `MX_L1_TLB_CNTL`.

  Os offsets do MMHUB entram **só na lista de leitura** do transporte; a de escrita não muda.

**P3.1, escrita e desligada:**
- anel 0 do IH na GART (entradas 128..191, VA `0x80000`) e writeback do WPTR (entrada 192);
- sequência do `navi10_ih_irq_init`;
- `TRAP` do SDMA0 depois da fence nos três caminhos assíncronos e no `SyncNop`, com `TRAP_ENABLE` reaplicado depois do soft-reset;
- ISR que consome o anel, conta por fonte e atualiza o RPTR;
- relatório a cada 10 s;
- prova no arme (um NOP com `TRAP` precisa gerar vetor) e falha fechada;
- desarme no teardown e no desligamento da GART.

**Diferença declarada:** o RPTR vai por MMIO, enquanto o amdgpu usa doorbell.

**Portão** `audit_p3_ih_waits_for_mmhub_gart`, com 5 mutações reprovadas:
- IH ligado sem a GART do MMHUB;
- MMHUB na lista de escrita;
- `TRAP` sem conferir o arme;
- ISR sem conferir o arme;
- INF sem MSI.

**Provas offline:** `build-contract` EXIT=0, laboratório 0 falhas, auditorias PASS, `[P3_MMHUB] FOTO` e `[P3_IH] NAO ARMADO` no SYS. O catálogo `docs/03` ganhou a seção da P3.

| Arquivo | SHA-256 |
|---|---|
| SYS | `C25E9857BD00A2E2ED022EB16996C330CE231B5376DCC2DCD0EC97628B2B70FA` |
| INF | `CE53D724583A742CB28FE200C24BB424864C02DCF86EBE308E749A2A461B0EC2` |
| CAT | `1D435D3CFC4C97B7A01FCEA7B75F0E25825B5120C13CD12D6A0D15066217598F` |

**Scripts:** `tools/prepare-corrida229-p30-msi-e-foto-do-mmhub.ps1` e `tools/EXECUTAR_CORRIDA229_P30_MSI_MMHUB_600S.cmd`. O preflight a seco parou só na CMOS.

## 4. Critério da Corrida 229

É uma **corrida de medição**. O comportamento da GPU não muda: o IH fica desligado e o `TRAP` não é emitido. A única mudança ativa é o MSI no INF.

**Pré-condição:**
- StartDevice `status=0`, P1b ativa;
- linhas `[P3_IH] recurso...` e `[P3_MMHUB] FOTO: 17/17 ... nenhuma escrita no MMHUB`.

**Desfechos:**

- **A — MSI entregue e MMHUB fotografado**
  - *Assinatura:* `recurso de interrupcao #1: MENSAGEM (MSI)` e 17/17 lidos.
  - *Implicação:* com o `CONTEXT0_CNTL` e as aberturas medidos, desenhar a **P3.0b**, a GART da VMID 0 no MMHUB como `mmhub_v2_0_gart_enable`, mexendo só no que a foto mostrar necessário e preservando a abertura que o display usa. Depois disso, ligar a P3.1.
- **B — interrupção por LINHA**
  - *Assinatura:* `LINHA` em vez de `MENSAGEM`.
  - *Implicação:* o Windows ignorou o MSI do INF. Investigar o INF e a instalação antes da P3.1, porque o `RPTR_REARM` depende de MSI.
- **C — nenhuma interrupção**
  - *Assinatura:* `NENHUMA interrupcao entregue pelo PnP`.
  - *Implicação:* sem recurso não há P3 no Windows. Entender por que o PnP não atribuiu nenhum.
- **D — regressão pelo MSI**
  - *Assinatura:* StartDevice cai, Código 43, ou comportamento diferente da 228 antes de qualquer trava.
  - *Implicação:* o MSI mudou algo no PnP. Comparar recursos e reverter o INF.
- **E — fora do mapa**
  - *Assinatura:* descrever o observado antes de julgar.
  - *Implicação:* a definir.

**A trava dura continua possível** (nada da P3 está ativo). Uma trava nesta corrida é só mais uma amostra da configuração da 228.

## 5. Pendências

**NÃO VALIDADO EM HARDWARE:**
- MSI no Windows para esta placa;
- o estado do MMHUB;
- toda a P3.1.

**Próximas etapas:**
1. P3.0b: GART no MMHUB.
2. P3.1: IH e `TRAP` com prova.
3. P3.2: conclusão pela interrupção (DPC), com o polling como rede.
4. P3.3: RPTR do IH por doorbell.
