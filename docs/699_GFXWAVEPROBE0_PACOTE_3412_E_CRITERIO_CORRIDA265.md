# 699 — GfxWaveProbe=0: pacote 3.412.1.0 e critério da Corrida 265

**Data:** 10/10/2026. **Natureza:** preparação offline; nada rodou na placa. **Base:** [doc 694](694_T5_T6_MINERACAO_DO_PROCESSO_NOVO_NA_263_E_INVENTARIO_DE_MMIO_EM_REGIME.md) §2.3 e [doc 698](698_CORRIDA264_FT1_EM_EFEITO_TRAVA_NA_SUBIDA_DO_DESKTOP_DESFECHO_D.md) §5.

---

## 1. Por que

- Na 263, na 263b e na 264, os intervalos finais antes da trava tinham leituras do SQ pela CPU (27 a 46 por intervalo na 264).
- Essas leituras só existem porque o pacote de laboratório arma `GfxWaveProbe=1` (`-ArmGfxWaveProbe`). É instrumentação de diagnóstico de TDR, não algo que o amdgpu faça a cada submissão.
- Com a sonda armada, a CPU faz por submissão do GFX:
  - `SQ_DEBUG_STS_GLOBAL` antes e depois da troca de VMID (`bc250_active_miniport.c:2076` e `:2457`, contados em `leituras_sq`);
  - `CP_RB0_RPTR`, `CP_RB0_WPTR` e SQ no diagnóstico depois do submit (`:16020`, sem contador);
  - linhas `BC250_DIAG`/`[GFX_IB_IDENTITY]` no log (`:2532`, `:15388`, `:15457`, `:15982`).
- No TDR do GFX, a sonda ainda escreve seletores (`GRBM_GFX_INDEX`, `SQ_IND_INDEX`). Com ela desligada, o quiesce registra `[TDR_GFX_WAVEPC] DESARMADO` e não escreve seletor.

**Uma mudança só, sem código novo:** o pacote sai sem `-ArmGfxWaveProbe`, e o INF empacotado fica com `GfxWaveProbe=0`. A FT.1, a T2 e o resto da base são os da 264.

**O que se perde:** a identidade do IB (`[GFX_IB_IDENTITY]`) e a inspeção de onda num eventual TDR do GFX. Na 264 não houve TDR.

## 2. Identidade do candidato

| Item | Valor |
|---|---|
| DriverVer | `10/10/2026,3.412.1.0` (fonte 3.412.0.0) |
| SYS assinado | `A8A6C087733E28C290F8925F5E0C3CCC3A08D3E9652D96FB4AB53F29A639AB9C` |
| SYS fonte (lab) | `7B5485777B1B6BA5DF9662E4EAE7BF4694CD88028A672EB76EC0D8F97CE34A37` (rebuild do mesmo fonte da 264) |
| INF | `F4045E91658732069494CFE8968F99A888CB1B17CD342D9871831600633EACBB` (`GfxWaveProbe,0x00010001,0`) |
| CAT | `7B630D05A0945D9938BA78EE4A6C7DC87EA65713765A63D01B844E7808AD4110` |
| Manifesto | `GfxWaveProbe: 0`, `GfxWaveProbeArmed: false` |
| UMD, RADV, RLC, T3 | iguais à 263/263b/264 (`B73F9BDA`, `B4AE2E14`, `20ACEFDB`, `768C82F7`) |

**Verificações:**
- Build de laboratório: EXIT 0.
- Empacotador: EXIT 0.
- `audit-bc250mcdm-lab-package.ps1`: PASS.
- `audit-bc250mcdm-safety.py`: PASS.
- O SYS assinado contém `[FT1_FLUSH_POR_PROCESSO] chave=` e `[StartDevice] GfxWaveProbe=`.
- O preflight confere hashes e manifesto. No boot atual (sujo, da trava da 264) ele recusou pelo boot frio, como deve.

**Backup:** `driver/wdk/package/output_lab_3.412.1.0_265`.

**Ferramentas:**
- Preflight `tools/prepare-corrida265-gfxwaveprobe0.ps1` (exige `GfxWaveProbe=0` no manifesto).
- Lançador `tools/EXECUTAR_CORRIDA265_GFXWAVEPROBE0_600S.cmd` (CRLF).

## 3. Roteiro (Daniel)

Igual ao da 264:
1. `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset`, **desligar** e ligar.
2. Rodar o lançador como administrador.
3. Quando o desktop aparecer: menu (Win, 10 s, Esc).
4. Como administrador: `tools\RODAR_PROCESSO_NOVO.cmd`.
5. Se travar: reset em 10 a 15 s e deixar a coleta automática terminar.

## 4. Critério (escrito antes da corrida)

### Pré-condição

1. `[StartDevice] GfxWaveProbe=0 (present=1)`, `[FT1_FLUSH_POR_PROCESSO] chave=1` e `[T2_PTE_MTYPE] … 0/0 | 0/1 | 3` no log; a base 4C e C1 ativa; Presents.
2. **A sonda está desligada:** o contador BFT1 `leituras_sq` fica em **0** em todos os registros, e não há linhas `BC250_DIAG … event=KMD_GFX_SWITCH` nem `[GFX_IB_IDENTITY]`.
3. A FT.1 continua em efeito: `vmids_invalidadas − flush_paging ≤ 16 × flush_user_vmid` em todo intervalo.

Sem o item 2, a mudança não está em efeito e a corrida não mede nada.

### Desfechos

- **A — 600 s sem trava, com a T3 completando ≥ 80 ciclos.**
  - *Assinatura:* rollback `RECOVERY_OK_ONLINE`, T3 com ≥ 80 ciclos de código 0, zero TDR/fault/BugCheck.
  - *Implicação:* sinal a favor das leituras do SQ/CP pela CPU como gatilho. **Repetição obrigatória** (265b) antes de chamar de efeito.
- **B — trava dentro de um filho da T3**, com a GPU ociosa no último tique.
  - *Implicação:* as leituras do SQ/CP por submissão não são causa suficiente. Próximo: o poll do MEC sem o seletor GRBM (doc 694 §2.3).
- **C — trava entre ciclos da T3**, sem processo novo nos 3 s anteriores.
  - *Implicação:* igual a B.
- **D — trava antes da T3** (subida do desktop, como 256b, 262, 263b e 264).
  - *Implicação:* igual a B, para a trava da subida.
- **E — fora do mapa.** Por exemplo: TDR do GFX (com a sonda desligada não haverá foto de onda; registrar o que o quiesce mostrar), fault, ou trava com `leituras_sq` diferente de zero.
  - *Implicação:* descrever antes de julgar.

**Regra (Lei 10):** desfecho parcial não é desfecho. A sem os ≥ 80 ciclos não é A.

### Teto

- 265 e, se der A, 265b.
- Se der B, C ou D, a sonda volta a ser decisão separada (fica desligada por padrão, porque é instrumentação) e a próxima corrida muda outra variável.

**NÃO VALIDADO EM HARDWARE.**
