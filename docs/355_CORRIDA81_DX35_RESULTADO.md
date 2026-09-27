# Corrida 81 — DX.35 (par de SWITCH_BUFFER, linha do tempo de faltas): resultado

> **23/09/2026, 09:40:07–09:42:37.** Candidato lab **3.242.1.0**, SYS `91767BD0...` (conferido em
> `binding-provenance.json`), `GfxWaveProbe=1`, ICD `5988B38E...`.
> Pasta: `docs/evidence/2026-09-23_09-40-07-DESKTOP-HQAG6T4-lab-binding/`.
> - O BugCheck matou o watchdog antes da coleta final.
> - Cauda resgatada em `bc250_display_log-tail-recovered.txt` (1.341.686 bytes, SHA-256 `A19DBCAA...`), contígua a
>   `display-log-this-bind.txt` pelo offset (12.106 linhas somadas).
>
> Critério prévio: [docs/354](354_DX35_CRITERIO_CORRIDA81_SWITCH_BUFFER_E_LINHA_DO_TEMPO_DE_FALTAS.md).
> **Veredito: D — o `SWITCH_BUFFER` trava o anel.** Seguiu-se **BugCheck 0x116**. Reverter V1.

## 1. Houve avanço?

**Não funcional; regressão causada pela V1.** Houve aprendizado de diagnóstico com resposta limpa: o par de
`SWITCH_BUFFER`, como emitido, **trava o CP neste anel**. A hipótese da DX.35 está refutada **nesta forma**.

## 2. Sucessos medidos

- **SB-ATIVO:** as 7 trocas saíram com `PM4DW=35` e `Eng=0`.
- **Linha do tempo de faltas funcionou:** 2 amostras, cada uma com 1 fault drenado (`ClearAcked=1`, CNTL restaurado).
  As duas trouxeram o **mesmo** fault: `Status=0x001C0041` → `Vmid=1`, `Cid=0`, escrita, `PermissionFaults=0x4`
  (escrita sem permissão), GpuVa `0x2401A1000`.
  - É o mesmo endereço do fault latchado no TDR 2 da Corrida 80 (lá sob VMID 2).
  - É uma escrita recorrente numa página sem permissão de escrita. Pista nova, ainda sem atribuição.
- **Recuperação por TDR:** `TDR_RESTART PROVADO` 5 de 5.
- **Autoteste:** `nonggc` 5/5.
- **Host:**
  - bind em boot novo;
  - o watchdog terminou em `RECOVERY_OK_ONLINE` depois do reboot do BugCheck;
  - o marcador foi gravado com o boot corrente (09:41:55).

## 3. Falhas

- **5 TDRs, todos com `ME_HEADER_DUMP = PFP_HEADER_DUMP = 0xC0008B00` (SWITCH_BUFFER)**, zero ondas
  (`SQ_DEBUG_STS_GLOBAL=0`, a sonda não rodou por não haver onda), anel com RPTR parado antes do WPTR.
  - Cada TDR veio logo depois de uma `VMID_SWITCH_VM_FLUSH`: a primeira troca já travou.
  - O CP não passa do `SWITCH_BUFFER`.
  - H: sem o Constant Engine em uso, o `SWITCH_BUFFER` espera uma contrapartida do CE que nunca vem. O upstream usa
    CE/preâmbulo; nós não.
- **BugCheck 0x116** às 09:41:
  - `(0xffffe683b6772460, 0xfffff8026438f160, 0, 0xd)`, dump `C:\Windows\MEMORY.DMP` de 09:42:17;
  - Arg3=0 e Arg4=0xD são iguais aos dumps de 12–13/09, cuja pilha era `TdrBugcheckOnTimeout` (o Windows desiste
    depois de TDRs repetidos, sem falha de uma operação nossa);
  - **Pilha conferida (P0)**, em `docs/evidence/dump-0x116-2026-09-23.txt`, gerado pelo Daniel com elevação:
    `VidSchiWorkerThread → VidSchiCheckHwProgress → VidSchiReportHwHang → TdrIsRecoveryRequired →
    TdrBugcheckOnTimeout → KeBugCheckEx`, **idêntica** às de 12–13/09;
  - zero quadros do `bc250mcdm` na pilha, e Arg3=0 (nenhuma operação nossa falhou);
  - `SYMBOL_NAME bc250mcdm!Bc250McdmCollectDbgInfo+0` é o owner tag que o dxgkrnl publica em Arg2 para nomear o
    adaptador, não endereço de falha;
  - leitura: o Windows desistiu depois dos TDRs repetidos, que foram causados pela V1.
- Harness 1/60. Autoteste: `nodcc` não fechou; `nongg` com S1 `MAP_FALHOU res=-5` (NÃO MEDIDO).
- A tela preta depois do quadro do harness é coerente com o anel gráfico parado a cada troca.
- `[TDR_GFX_WAVE_VMID]` não saiu: não havia ondas. A pergunta do VMID ficou sem resposta nesta corrida.

## 4. Veredito frente ao critério (docs/354)

- **D:** TDR com ME/PFP em `0xC0008B00` logo depois da troca, em 5 de 5.
- Implicação escrita antes: **reverter V1; nada sobre o VMID é concluível.**
- A, B e C não ocorreram. E: não havia onda, mas isso é consequência de D.
- F: a regressão existe, mas é **atribuída** à V1 pela assinatura, então D a descreve. H: não ocorreu.

## 5. Recuperação e pendências

- O BugCheck reiniciou a máquina. O watchdog, na tentativa 1, restaurou o BasicDisplay (Código 0 em 5 amostras,
  09:42:37).
- Leitura posterior: serviço `BasicDisplay`, ProblemCode 0.
- Marcador de boot frio armado com o boot atual.
- **NÃO VALIDADO EM HARDWARE:** eliminação da classe de TDR e o efeito de qualquer sequência de troca que não trave.

**Próximos passos propostos (não implementados):**
1. **Reverter o par de `SWITCH_BUFFER`**, voltando a troca a 31 DW. Manter a linha do tempo de faltas, que funcionou.
2. **Pista nova:** a escrita sem permissão em `0x2401A1000`, recorrente na 80 e na 81, sob VMIDs diferentes. Descobrir
   qual alocação e qual mapeamento (RADV com `RADEON_FLAG_READ_ONLY` → `Write=0`?) e quem escreve (`Cid=0`).
3. **Próximo candidato da troca de VMID (desfecho C do docs/354):** `CONTEXT_CONTROL` com `load_cs_sh_regs`. Entra só
   depois do item 1 e de uma corrida que confirme a volta ao comportamento da 80.
4. ~~**Dump**~~ — **feito**: `docs/evidence/dump-0x116-2026-09-23.txt`, pilha conferida no §3.
