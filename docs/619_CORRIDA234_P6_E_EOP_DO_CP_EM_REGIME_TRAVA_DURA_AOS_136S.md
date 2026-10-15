# 619 — Corrida 234: P6 e EOP do CP funcionando, e trava dura aos ~136 s

**Corrida:** 06/10/2026, bind às 13:48:51. Candidato lab 3.381.1.0 (SYS `9B9FE1B8…`) com RADV `C6BEC54B…`, boot frio, CMOS em 256 MB.
**Evidência:** `docs/evidence/2026-10-06_13-48-51-DESKTOP-HQAG6T4-lab-binding/`:
- `display-log-this-bind-RECUPERADO.txt` e o log vivo `C:\BC250_DISPLAY_LOG.txt` a partir do offset 345.975.018;
- coleta automática presente: motivo do reset, teste de RAM e logs da UMD.

**Critério:** doc 618 §4.
**Relato do Daniel:** "travou e tive que reiniciar".

## 1. Medições (P0)

**Pré-condição OK:**
- StartDevice `status=0`;
- `[P3B_MMHUB] LIGADA` (34/34);
- `[P3_IH] ATIVO`;
- `binding-provenance.json` com `vulkan_radeon.dll` `C6BEC54B…`: a RADV nova carregou.

**EOP do CP — funcionou 1 para 1:**
- `[CP_EOP] ARMADO`: `CP_INT_CNTL_RING0 0x00000000 -> 0x04000000`, conferido por readback;
- última linha `[P3_IH] regime`: `eop_emitidos=1236 eop_gfx=1236 dpc_gfx_por_irq=1236`, com `eop_cp=1236` e `outros=0`;
- ou seja, todo `RELEASE_MEM` do GFX com `INT_SEL=2` virou um vetor de me 0/pipe 0 e um DPC de aposentadoria;
- o SDMA0 seguiu igual à 233: `traps_sdma=6393`, `dpc_por_irq=6375`, zero overflow.

**P6 — funcionou:**
- `[P6_MTYPE] folha UC` e `UpdatePT ... com MTYPE_UC` presentes. A linha é amostrada (8 primeiras), então o total da corrida não está no log.
- As alocações marcadas foram os BOs de IB concatenado da RADV (`AbiFlags=0x39` = CPU_VISIBLE | DOMAIN_GTT | COMMAND | GL2_BYPASS, 80 KB), mapeados em mais de um processo, 20 folhas UC cada.
- Zero `ABI_PRIVADA_RECUSADA`: KMD e RADV concordam no contrato.

**Regime:**
- ~136 s pela QPC (primeira linha `qpc=2827124490`, última `qpc=4182482078`, a 10 MHz);
- 6.567 IBs executados no SDMA0, 1.326 fences no anel gráfico, 792 Presents;
- composição com `COMPOSITE_DATA_PROOF SAMPLED_PASS` (pixels não nulos);
- zero `TDR_QUIESCE`, `GFXHUB_FAULT`, `Blt Timeout`, `IB_TRIPOD TIMEOUT`, `DEVICE_REMOVED` e `INSIDE_IB`;
- nenhum TDR nem falha de aplicativo nos logs do Windows entre 13:48 e o reboot;
- um `BUSY_YIELD`, como na 233.

**Recusas, classes conhecidas:**
- 80× `[ActiveOpenAlloc] RECUSADO ... 0xC0000035`, um `DUPLICATE_OWNER` do DWM. Começam ~8 s antes da trava. A mesma classe aparece em dez corridas desde 26/09 e 31 vezes na 231: são as superfícies compartilhadas entre processos (Frente B). **Não** é efeito da P6.
- 22× `DIRTY_RECTS_RECUSADOS`.
- 3× `DescribeAlloc RECUSADO`.

**Trava dura aos ~136 s, mesma assinatura:**
- O log termina no meio de atividade normal: um `PAGING_DDI ENTER` sem `LEAVE`, logo depois de um Present e de um `COMPOSITE_BLT OK`.
- Sem timeout e sem fault antes.

**Pós-trava:**
- boot às 13:54:47;
- `RESET_REASON=0x40080400` (bit 19, reset pelo `0xCF9`), como as outras;
- teste de RAM sem erros (7.995 MB × 2);
- CMOS 256 MB;
- no boot seguinte o fusível recusou o StartDevice (`State=2, Boots=2`). A placa está em **Código 43** com o `bc250mcdm`;
- **o rollback do watchdog não rodou** (estado `OBSERVING` às 13:55:12; `C:\BC250_RECOVERY.log` parado às 11:47). Ao contrário da 233, a recuperação é manual.

## 2. Veredito frente ao critério

| Desfecho | Ocorreu? | Por quê |
|---|---|---|
| A | **não** (Lei 10, regra 3) | Presentes: EOP armado e 1 para 1, linhas da P6, zero fault/TDR, desktop por ~136 s. Falta o teardown (`[CP_EOP] DESARMADO`), porque a máquina travou |
| B | não | `eop_gfx` = `eop_emitidos` |
| C | não | linhas `[P6_MTYPE]` presentes |
| D | não | sem recusa de ABI, fault, TDR ou tempestade de interrupção |
| E | não | |

**O que fica (P0):**
- **EOP do CP funciona como mecanismo:** a conclusão do anel gráfico foi disparada pela interrupção 1.236 vezes sem perda.
- **P6 funciona como mecanismo:** a RADV declara `GL2_BYPASS` e o KMD grava MTYPE_UC nessas folhas.
- **Nenhum dos dois tira a trava.** As duas mudanças saem das causas suficientes. Juntas não bastam, e por isso cada uma separada também não basta.
- Esta foi uma das corridas mais curtas (~136 s), mas a duração das travas varia de ~77 a ~584 s sem padrão por candidato. Uma amostra não permite dizer que piorou.

## 3. Recuperação

1. Elevado:
   ```bash
   tools\BC250_RECOVERY.cmd
   ```
2. Depois:
   ```bash
   powershell -ExecutionPolicy Bypass -File tools\inspect-boot-fuse.ps1 -Reset
   ```
3. Boot frio.

**Atenção à RADV:** `C:\BC250_LIB\vulkan_radeon.dll` é a `C6BEC54B…`, que só funciona com KMD 3.381+. Um candidato anterior precisa da cópia `vulkan_radeon.dll.pre-p6-80EBB248`.

## 4. Próximo (paridade, doc 593 §4)

Ordem do inventário que sobra:
1. **P4:** SDMA1 em uso. O Linux usa os dois SDMA; nós só o SDMA0.
2. **P5:** VM de 4 níveis e 48 bits.
3. **P3.3:** RPTR do IH por doorbell.

**NÃO VALIDADO EM HARDWARE:**
- teardown do EOP do CP, do IH e da GART do MMHUB em regime;
- teardown com transporte remapeado.
