# 615 — Corrida 232: IH vivo em regime (TRAPs 1 para 1) e trava dura aos ~77 s

**Corrida:** 06/10/2026, bind às 10:58:55. Candidato lab 3.379.1.0 (SYS `F847DF2E…`), boot frio, CMOS em 256 MB.
**Evidência:** `docs/evidence/2026-10-06_10-58-55-DESKTOP-HQAG6T4-lab-binding/`; o log é o `C:\BC250_DISPLAY_LOG.txt` a partir do offset do baseline.
**Critério:** doc 614 §2.
**Relato do Daniel:** "travou, não deu tela azul nem preta, só travou". Ele reiniciou pelo botão.

## 1. Medições (P0)

**Pré-condição OK:** MSI (`Vector=82`), `[P3B_MMHUB] resultado: LIGADA ... PROTECTION_FAULT_STATUS=0`, StartDevice `status=0`.

**O IH funcionou na primeira vez:**
- `[P3_IH] ATIVO: anel 0 do IH na GART VA=0x80000 (262144 bytes, MC_SPACE=4, VMID 0) ... RB_CNTL=0x403301A1 ... MSI (RPTR_REARM=1)`.
- Prova: `NOP com TRAP -> IH escreveu (WPTR 0x0 -> 0x20, ISR=1, traps vistos=1)`.
- PTE da RAM do anel com `MTYPE_UC` (`PTE[128]=0x300045337F077`).

**Regime:** linhas `[P3_IH] regime` até `ISR=2341 reclamadas=2341 vetores=2341 traps_sdma=2341 eop_cp=0 outros=0 overflows=0 traps_emitidos=2341`, com `RPTR=WPTR_wb=0x124A0`.
- **Todo TRAP emitido virou exatamente um vetor no anel e uma chamada do nosso ISR.**
- Zero vetor de outra origem, zero overflow.
- O caminho completo funciona: SDMA0 → IH → anel pela GART do MMHUB → writeback do WPTR → MSI → `DxgkDdiInterruptRoutine` → nosso ISR.

**Trava dura:**
- ~77 s depois do StartDevice, com 590 Presents e 3.077 IBs.
- O log termina numa submissão gráfica normal (`SUBMIT SUCESSO`, `GfxRetire`, `FENCE COMMIT`), sem fault, timeout nem TDR.
- Mesma assinatura das anteriores.

**Pós-trava:** a coleta automática rodou atrasada (`coleta-automatica.json` às 11:03:49, depois da minha primeira leitura da pasta).
- Teste de RAM sem erros (8.036 MB × 2).
- O `RESET_REASON` lido pela coleta manual das 11:04 veio `0x40000400`, sem o bit 19. A automática já tinha lido e limpado o registrador (W1C), então essa segunda leitura não informa o motivo.
- Recuperação e reset do fusível feitos pelo Daniel: BasicDisplay em Código 0 e fusível CLEAN.

## 2. Veredito frente ao critério

| Desfecho | Ocorreu? | Por quê |
|---|---|---|
| A | **não** (Lei 10, regra 3) | IH ativo, traps crescendo junto com os emitidos e overflow zero estão todos presentes. Faltam o desktop até o fim e o teardown, porque a máquina travou |
| B | não | o ISR roda |
| C | não | o IH escreve |
| D | não | sem tempestade (só traps do SDMA, 1 para 1); sem BugCheck nem trava logo depois do arme (a trava veio ~77 s depois, em regime) |

**O resultado é a trava dura prevista**, a primeira amostra com o IH vivo.

**O que fica:**
- **A P3.1 está validada no silício como mecanismo** (P0): 2.341 de 2.341 traps entregues e atendidos.
- **Ter o IH ligado não tira a trava** (P0 nesta amostra). Mas a conclusão das submissões ainda é por polling, e a diferença com o amdgpu que sobrou é exatamente essa.
- **O tempo curto (~77 s) não é atribuível ao IH:** a 202 travou aos ~95 s sem IH. A diferença é **H**.

## 3. Próximo

**P3.2:** a conclusão de submissão passa a ser disparada pela interrupção. O ISR enfileira o DPC e o DPC aposenta e notifica, como `amdgpu_fence_process` a partir do `sdma_v5_0_process_trap_irq`. O polling por timer fica como rede, com período maior.

**NÃO VALIDADO EM HARDWARE:**
- P3.2;
- teardown do IH em regime;
- teardown com transporte remapeado (doc 612).
