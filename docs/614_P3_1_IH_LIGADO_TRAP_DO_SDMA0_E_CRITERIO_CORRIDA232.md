# 614 — P3.1: IH ligado (TRAP do SDMA0, ISR, prova por vetor) e critério da Corrida 232

**Origem:** a Corrida 231 (doc 613) provou a GART da VMID 0 no MMHUB em regime, armada depois do último uso do PSP. Esse era o pré-requisito do IH (doc 609 §2). Pela ordem da paridade, o próximo passo é a P3.1.

## 1. O que muda (candidato 3.379.1.0)

**Uma diferença:** `BC250_P3_IH_ENABLED 0 → 1`. O código é o do doc 609 §3, revisto nesta etapa.

**Arme** (`Bc250ActiveP3ArmIh` → `Bc250SdmaPresentIhArm`), no fim do StartDevice, depois da GART do MMHUB:
1. Pré-condições, todas exigidas em tempo de execução:
   - recurso MSI;
   - `P3bMmhubProgrammed=1`;
   - D2 ligada.
2. RAM contígua não cacheada de 256 KB + 4 KB, mapeada nas entradas 128..192 da GART com `MTYPE_UC`; readback; invalidação nos **dois** hubs.
3. A sequência do `navi10_ih_irq_init`:

   | Registrador | Valor |
   |---|---|
   | `INTERRUPT_CNTL2` | dummy >> 8 |
   | `RB_BASE` | VA `0x80000` >> 8 |
   | `RB_CNTL` | `MC_SPACE=4`, `RB_SIZE=log2(64K dw)`, writeback do WPTR, `RPTR_REARM=1` (MSI) |
   | `WPTR_ADDR` | VA `0xC0000` |

   Depois liga `RB_ENABLE`, `RB_GPU_TS_ENABLE` e `ENABLE_INTR`, e confere por readback.
4. `SDMA0_CNTL.TRAP_ENABLE`.
5. **Prova:** um NOP com TRAP tem de fazer o IH escrever no anel em ~100 ms. Sem isso, desarma (falha fechada).

**Regime:**
- toda submissão assíncrona do SDMA0 termina com TRAP;
- o ISR (DIRQL, sem log) consome o anel, conta por cliente e fonte e escreve o RPTR por MMIO;
- uma linha `[P3_IH] regime` a cada 10 s;
- se o ISR nunca rodar com traps emitidos, o driver desarma sozinho.

**A conclusão das submissões continua por polling.** A P3.2 é a próxima etapa.

**Revisado nesta etapa:**
- os registradores do IH (`0x4480..`) e do NBIO (`0x3844`) estão dentro da janela MMIO do SDMA (`0x3000..0x8FFF`);
- `DxgkDdiInterruptRoutine` está registrado nos dois caminhos de inicialização e chama o nosso ISR;
- as migalhas no caminho do ISR só escrevem em RAM, o que é seguro em DIRQL.

**Provas offline:**
- `build-contract` EXIT=0, SYS de laboratório confirmado, três auditorias PASS;
- **as 10 mutações** do portão `audit_p3_ih_waits_for_mmhub_gart` seguem reprovadas;
- strings no SYS: `[P3_IH] ATIVO`, `NAO ATIVA: o NOP com TRAP`, `GART do MMHUB nao ligada`, `[P3_IH] DESARMADO`. O ramo `NAO ARMADO` saiu do binário, como devia.

**Pacote 3.379.1.0:** perfil `DISPLAY_RENDER_LAB`, auditoria PASS.

| Arquivo | SHA-256 |
|---|---|
| SYS | `F847DF2E2D3501CA5B47836991A45A2B5C0E4F17B83F493A3785DBED4A0443D9` |
| INF | `124B98968AFEFE93F3AD38319E55A2D099B1BFEC28434ACBB015D3C4E8E1A7D7` |
| CAT | `6F63438DF5F26EC0891924E46E5710621B2501C80E6A3F861A46C2A8156EA50E` |
| UMD | `8782A5E5…` (inalterada) |

**Scripts:** `tools/prepare-corrida232-p31-ih-ligado.ps1` e `tools/EXECUTAR_CORRIDA232_P31_IH_LIGADO_600S.cmd`. O preflight a seco parou só na CMOS.

## 2. Critério da Corrida 232

**Pré-condição:**
- `PspRingStatus=0` e StartDevice `status=0`;
- `[P3B_MMHUB] resultado: LIGADA`;
- `[P3_IH] recurso de interrupcao #1: MENSAGEM (MSI)`.

Uso normal: menu Iniciar e Pesquisa.

**Desfechos:**

- **A — IH vivo em regime**
  - *Assinatura:* todos os sinais abaixo.
    - `[P3_IH] ATIVO`, com a prova `WPTR a -> b`, `ISR>=1` e `traps vistos>=1`;
    - linhas `[P3_IH] regime` com `traps_sdma` crescendo junto com `traps_emitidos` e `overflows=0`;
    - desktop normal;
    - no fim, `[P3_IH] DESARMADO (teardown)` e `[P3B_MMHUB] DESLIGADA ... falhas=0`.
  - *Implicação:* **P3.2**, a conclusão de submissão disparada pela interrupção (DPC), com o polling como rede.
- **B — o IH escreve, mas o ISR não roda**
  - *Assinatura:* `ATIVO` com `ISR=0`, ou `DESARMADO (ISR nunca chamado com traps emitidos)`.
  - *Implicação:* o vetor MSI não chega ao `DxgkDdiInterruptRoutine`. Investigar a conexão da interrupção pelo dxgkrnl e o `INTERRUPT_CNTL`.
- **C — o IH não escreve**
  - *Assinatura:* `NAO ATIVA: o NOP com TRAP nao fez o IH escrever`.
  - *Implicação:* o anel por VA no MMHUB não traduz, ou o TRAP não sai. Ler o `RB_CNTL` e o `SDMA0_CNTL` da linha e o fault do MMHUB. A corrida segue como a 231.
- **D — tempestade ou regressão**
  - *Assinatura:* qualquer um destes:
    - `overflows` crescendo;
    - `outros` dominando os vetores;
    - lentidão;
    - BugCheck ou trava logo depois do arme (segundos).
  - *Implicação:* chave em 0 e mineração da fonte (`ultimo cliente/fonte`).
- **E — fora do mapa**
  - *Assinatura:* descrever o observado antes de julgar.
  - *Implicação:* a definir.

**A trava dura continua possível.** Ainda não há conclusão por interrupção. Se a trava vier, conta como amostra com uso normal e com o IH vivo.

## 3. Pendências

**NÃO VALIDADO EM HARDWARE:**
- toda a P3.1;
- o teardown com transporte remapeado (doc 612).

**Próximas etapas:**
1. P3.2: conclusão de submissão por interrupção.
2. P3.3: RPTR do IH por doorbell.
