# 701 — MEC.2: poll do MEC sem seletor GRBM. Pacote 3.413.1.0 e critério da Corrida 266

**Data:** 10/10/2026. **Natureza:** preparação offline; nada rodou na placa. **Base:** [doc 694](694_T5_T6_MINERACAO_DO_PROCESSO_NOVO_NA_263_E_INVENTARIO_DE_MMIO_EM_REGIME.md) §2.3 item 4 e [doc 700](700_CORRIDA265_GFXWAVEPROBE0_41_PROCESSOS_TRAVA_ENTRE_CICLOS_DESFECHO_C.md) §6.

---

## 1. Por que

**O que a CPU fazia (código, P0 por leitura):** `Bc250SubmissionEnginePoll` roda a cada tique do timer de aposentadoria (1 ms) com o motor do MEC online, mesmo com o anel de compute ocioso. Em **todo** poll:
1. escrevia `GRBM_GFX_INDEX` = broadcast;
2. escrevia `GRBM_GFX_CNTL` = seletor da fila do MEC;
3. lia `CP_HQD_PQ_RPTR`;
4. escrevia `GRBM_GFX_INDEX` e `GRBM_GFX_CNTL` de volta.

São cerca de 4 escritas e 1 leitura de MMIO por milissegundo, durante a corrida inteira.

**O RPTR lido não servia para aposentar:** `Bc250PersistentRingRetire` usa a fence e o writeback do RPTR em memória. O valor por MMIO só ia para log, diagnóstico e telemetria.

**O amdgpu (P1):** a conclusão sai da fence em memória (`amdgpu_fence_process`). Ele não seleciona fila nem lê registrador de HQD em regime. A seleção por `GRBM_GFX_CNTL` (`nv_grbm_select`) só aparece sob `srbm_mutex`, na carga e na descarga de fila.

## 2. O que mudou

**`driver/wdk/bc250_submission_engine.c`**, chave `BC250_MEC2_POLL_SEM_SELETOR 1`:
- O seletor e a leitura de `PQ_RPTR` só acontecem quando **há fence do MEC pendente que ainda não apareceu na memoria** (`selectorNeeded`). É o caso em que o diagnóstico `[MEC_PENDING]` precisa ler a fila.
- Fora disso, nenhum MMIO: o RPTR registrado fica com a última leitura feita por MMIO.
- A aposentadoria não mudou: continua pela fence e pelo writeback em memória.
- **Chave 0:** volta ao seletor em todo tique.

**Instrumentação, só contagem:**
- Contadores cumulativos dos polls: o total e os que escreveram o seletor.
- Eles vão ao rastro durável num registro novo, **BMP1** (`0x31504D42`), a cada 1 s, ao lado do BFT1: `polls_mec`, `polls_com_seletor`, `polls_sem_mmio`.
- O decodificador (`tools/decode-bc250-paging-trace.py`) passa a mostrar `mec_poll`.
- **Linha de boot:** `[MEC2_POLL_SEM_SELETOR] chave=1 …`.

**Portão:** `audit_mec2_poll_sem_seletor` em `tools/audit-bc250mcdm-safety.py`. Exige:
- a chave em 1;
- toda escrita de `GRBM_GFX_INDEX` e leitura de `PQ_RPTR` dentro do ramo `selectorNeeded`;
- `selectorNeeded` condicionado a fence pendente e não vista;
- a aposentadoria pela fence;
- os contadores instalados.

Duas mutações reprovaram, como deviam: uma escrita de GRBM fora do ramo e a chave em 0.

**Sem mudança:** FT.1, `GfxWaveProbe=0`, T2, 4C, C1, UMD `B73F9BDA` e RADV `B4AE2E14`.

## 3. Identidade do candidato

| Item | Valor |
|---|---|
| DriverVer | `10/10/2026,3.413.1.0` (fonte 3.413.0.0) |
| SYS assinado | `89E1C6ADF9471D310879EEB8446D4DAE2B26DD85ED559968B424C2E573809570` |
| SYS fonte (lab) | `A8A72C817820C2FEB674C97540C096F30D705E41C890986FF2990A3263ECFD69` |
| INF | `CEB69D3918F2CC85D2D436776600FEA5CF768FDFA41CB7AB5545AF6E5F430C3A` (`GfxWaveProbe=0`) |
| CAT | `F98765BF9C98DD0D0F1D813AF08CCB8E0262D398BB7D4F76D9E8FF737D3FE6B7` |

**Verificações:**
- `build-contract.bat`: EXIT 0.
- Build de laboratório: EXIT 0.
- Empacotador: EXIT 0.
- `audit-bc250mcdm-lab-package.ps1`: PASS.
- `audit-bc250mcdm-safety.py`: PASS.
- O SYS assinado contém `[MEC2_POLL_SEM_SELETOR] chave=` e `[FT1_FLUSH_POR_PROCESSO] chave=`.
- O preflight confere hashes e manifesto. Na execução de teste ele seguiu até a checagem da CMOS, que o lançador faz sozinho.

**Backup:** `driver/wdk/package/output_lab_3.413.1.0_266`.

**Ferramentas:**
- Preflight `tools/prepare-corrida266-mec2-poll-sem-seletor.ps1`.
- Lançador `tools/EXECUTAR_CORRIDA266_MEC2_POLL_SEM_SELETOR_600S.cmd` (CRLF).

## 4. Roteiro (Daniel)

Igual ao da 265:
1. `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset`, **desligar** e ligar.
2. Rodar o lançador como administrador.
3. Quando o desktop aparecer: menu (Win, 10 s, Esc).
4. Como administrador: `tools\RODAR_PROCESSO_NOVO.cmd`.
5. Se travar: reset em 10 a 15 s e deixar a coleta automática terminar.

## 5. Critério (escrito antes da corrida)

### Pré-condição

1. `[MEC2_POLL_SEM_SELETOR] chave=1`, `[StartDevice] GfxWaveProbe=0`, `[FT1_FLUSH_POR_PROCESSO] chave=1` e `[T2_PTE_MTYPE] … 0/0 | 0/1 | 3` no log; a base 4C e C1 ativa; Presents.
2. **A MEC.2 pegou, pelo BMP1:**
   - `polls_mec` cresce cerca de 1.000 por segundo (o timer está vivo);
   - `polls_com_seletor` fica **abaixo de 1% de `polls_mec`** em todo intervalo sem submissão de compute. Na base, era 100%.
3. `leituras_sq` = 0 (BFT1) e FT.1 em efeito, como na 265.

Sem o item 2, a mudança não está em efeito e a corrida não mede nada.

### Desfechos

- **A — 600 s sem trava, com a T3 completando ≥ 80 ciclos.**
  - *Assinatura:* rollback `RECOVERY_OK_ONLINE`, T3 com ≥ 80 ciclos de código 0, zero TDR/fault/BugCheck.
  - *Implicação:* sinal a favor do seletor GRBM a cada 1 ms como gatilho. **Repetição obrigatória** (266b) antes de chamar de efeito.
- **B — trava dentro de um filho da T3**, com a GPU ociosa no último tique.
  - *Implicação:* o seletor do MEC não é causa suficiente. A MEC.2 fica por paridade. Próximo: novo inventário do MMIO restante em regime (doc 694 §2: RPTR/WPTR do SDMA por MMIO, RPTR do IH, contador de quadros do OTG a cada 4 ms, Tctl por SMN).
- **C — trava entre ciclos da T3**, sem processo novo nos 3 s anteriores.
  - *Implicação:* igual a B.
- **D — trava antes da T3** (subida do desktop).
  - *Implicação:* igual a B.
- **E — fora do mapa.** Por exemplo: `[MEC_PENDING]` em excesso, aposentadoria do MEC parada, `[SubmissionPoll] FENCE_REGRESSION`, TDR, fault, ou `polls_com_seletor` alto sem compute.
  - *Implicação:* descrever antes de julgar. Se a aposentadoria do MEC parar, voltar a chave para 0.

**Regra (Lei 10):** desfecho parcial não é desfecho. A sem os ≥ 80 ciclos não é A.

### Teto

- 266 e, se der A, 266b.
- Se der B, C ou D, a MEC.2 fica e a próxima corrida muda outra variável.

**NÃO VALIDADO EM HARDWARE.**
