# Handoff para a próxima IA — candidato 3.60 aguardando ensaio físico

**Data do corte:** 31/08/2026, após 08:33 (UTC-3)  
**Workspace:** `C:\Users\Daniel\Desktop\NamixWDDM`  
**Pacote preparado:** `3.60.0.0` / Schema de telemetria `127`  
**Estado físico do 3.60:** **NÃO VALIDADO EM HARDWARE**

## 1. Resumo executivo

A Fase P1 possui uma vitória histórica real no candidato 3.54: a fila
persistente MEC2/P0/Q0 drenou `RPTR==WPTR==12`, a GPU gravou as duas fences e o
shader produziu `0xBC250C01`, com zero divergências. Isso não prova a Fase P2.

Os candidatos seguintes localizaram duas regressões:

1. o 3.57 escreveu o lote de shader diretamente no anel persistente em vez de
   publicar um `PACKET3_INDIRECT_BUFFER`;
2. o 3.58 restaurou o IB, mas estava com `INDIRECT_BUFFER_VALID` zerado;
3. o 3.59 restaurou o bit 23 (`0x0080002D`) e o MEC consumiu os quatro DWORDs
   do descritor, porém parou dentro do IB em `SET_SH_REG`;
4. a comparação 3.59 × 3.54 mostrou que o fluxo persistente havia omitido a
   transação one-shot de shader que prepara e prova fisicamente o caminho.

O 3.60 corrige apenas essa ordem: exige primeiro o tripé físico one-shot,
depois liga o motor persistente, exige a P1 física e só então permite as 16
submissões da P2. Se qualquer gate falhar, a P2 não roda.

O pacote 3.60 foi compilado, testado offline, assinado e auditado, mas **nunca
foi vinculado ao dispositivo**. A última leitura antes do encerramento mostrou:

- `LastBoot = 2026-08-31 08:10:52 -03:00`;
- leitura feita às `08:33:35`, uptime 22,7 minutos;
- BC-250 em `BasicDisplay`, `CM_PROB_NONE`;
- foi solicitado `shutdown.exe /r /t 60`, mas não houve comprovação posterior
  de que o reboot ocorreu.

Logo, a próxima IA deve começar confirmando um `LastBoot` posterior a 08:33 e
nunca presumir que a mensagem humana “voltou” significa boot limpo.

## 2. Artefato exato a ensaiar

Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Artefato | SHA-256 |
|---|---|
| `output_lab/bc250mcdm.sys` | `E81521B65343B56EB46BE99486DC70F009961B3522FF76D5C6F95807E4050BA1` |
| `output_lab/bc250mcdm.inf` | `5ABA47899280D21EA61D7EFFCE02720432EA03931583EDB4040137F9F28EEA34` |
| `output_lab/bc250mcdm.cat` | `6AF943900FECCB57DA11DA270B693DCF86593859F1D9CF489DBD09E1108293AB` |

O manifesto deve continuar declarando:

- `BuildKind=MCDM_LAB_BRINGUP`;
- `HardwareValidated=false`;
- `TelemetrySchemaVersion=127`;
- `SkipHwBringup=0`;
- `ShaderDispatchEnabled=true`;
- `PersistentSubmissionEnabled=true`;
- `Verdict=NAO VALIDADO EM HARDWARE`.

Não reempacotar antes do primeiro ensaio: isso mudaria o objeto sob teste e
inutilizaria os hashes acima.

## 3. Evidências físicas que governam o diagnóstico

### Vitória histórica P1 — candidato 3.54

- Documento: `docs/148_VITORIA_FASE_P1_VALIDADA_EM_HARDWARE.md`.
- Evidência principal:
  `docs/evidence/2026-08-30_22-31-14-DESKTOP-HQAG6T4-p1-submission/p1-submission-acceptance.json`.
- Descritor correto: `C0023F00 00A96000 000000F4 0080002D`.
- Resultado: `RPTR==WPTR==12`, fences físicas corretas, resultado
  `0xBC250C01`, zero mismatch.

### Último ensaio físico — candidato 3.59

- Documento: `docs/155_RESULTADO_FISICO_3.59_RPTR_4_E_GATE_SHADER_AUSENTE.md`.
- Evidência:
  `docs/evidence/2026-08-31_08-14-41-DESKTOP-HQAG6T4-p2-multi-submission/p2-multi-submission-acceptance.json`.
- Resultado: 16 submissões aceitas, zero aposentadas, `WPTR=204`,
  `RPTR MMIO=4`, `RPTR report=0`, 32 divergências.
- O anel primário estava perfeito e continha o descritor correto.
- `CP_HQD_IB_CONTROL=0x84300000` e header MEC2 `0xC0037602`
  (`SET_SH_REG`): o MEC entrou no IB e travou durante a programação do shader.
- Os campos `Shader*` estavam ausentes. No 3.54 eles existiam e
  `ShaderPhysicalReady=1` antes da P1.

### Candidato 3.60

- Documento de implementação:
  `docs/154_CANDIDATO_3.60_PREFLIGHT_SHADER_ANTES_PERSISTENTE.md`.
- Mudança central em `driver/wdk/bc250_active_miniport.c`: executar
  `Bc250ActiveRunActiveHqdSubmissionTransaction` antes de colocar o motor
  persistente online; exigir `HqdActiveStatus == BC250_HQD_ACTIVE_OK` e
  `HqdShaderDispatchEvidence.PhysicalReady != 0`; depois exigir a P1 física
  antes da P2.

## 4. Invariantes de hardware que não podem ser alterados

- A faixa física de doorbell MEC lida do firmware é `0x110..0x110`.
- A doorbell canônica da HQD MEC2/P0/Q0 é somente `BAR2 + 0x110`, índice
  `0x44`; `CP_HQD_PQ_DOORBELL_CONTROL=0x40000110`.
- Não escrever `BAR2+0`, `BAR2+0x220` nem fazer varredura de offsets. Pode
  acordar filas de PSP, SDMA, RLC, outros MECs ou VCN.
- Não substituir `CP_MEC_DOORBELL_RANGE_UPPER` por `0x003FFFFC`; esse valor era
  hipótese baseada em Navi 10 desktop e não possui prova nesta BC-250.
- Não presumir BAR2 de 4 MiB. Usar apenas o comprimento resolvido pelo PnP em
  `Context->Core.ResolvedBars[BC250_ADAPTER_DOORBELL_BAR_INDEX].Length`.
- Preservar snapshot e usar RMW SOC15: `(lido & ~mask) | (or_val & mask)`.
- Nunca fabricar fence pela CPU, avançar ponteiro manualmente ou aceitar
  `STATUS_SUCCESS` como prova de GPU.

## 5. Procedimento exato de retomada

Seguir obrigatoriamente:

```text
[PREFLIGHT] -> [SNAPSHOT] -> [APPLY] -> [READBACK]
            -> [PROVA NO SILÍCIO] -> [ROLLBACK/TEARDOWN]
```

### 5.1 Preflight read-only

1. Ler completamente `AGENTS.md`, `BC250_HARDWARE_RULES.md`, este documento,
   docs 148, 154 e 155.
2. Executar `git status --short`. A árvore está muito suja e contém mudanças do
   usuário e da sessão anterior. Não usar `reset`, `clean` nem checkout
   destrutivo.
3. Conferir o boot:

```powershell
$os = Get-CimInstance Win32_OperatingSystem
$os.LastBootUpTime
```

O valor precisa ser posterior a `2026-08-31 08:33:35 -03:00`. Se não for,
falhar fechado e executar uma reinicialização completa antes de tocar no
driver.
4. Exigir exatamente um `PCI\VEN_1002&DEV_13FE*`, serviço `BasicDisplay` e
`ConfigManagerErrorCode=0`.
5. Recalcular os três hashes do pacote e comparar com a seção 2.

### 5.2 Quarentena e binding

Em PowerShell elevado, com vídeo ainda visível:

```powershell
powershell -ExecutionPolicy Bypass -File tools\quarantine-bc250mcdm-lab-residue.ps1 `
  -Execute -IAcceptExactResidueQuarantine

powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 `
  -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch `
  -EnablePersistentSubmission -WatchdogTimeoutSeconds 180
```

O bind grava `tools/_last_lab_binding.json`. Imediatamente:

- conferir serviço `bc250mcdm`, Código 0 e hash do SYS instalado;
- conferir `watchdog-state.json` no `evidence_dir` e processo do watchdog;
- se o watchdog filho tiver morrido ao encerrar o processo de bind, rearmá-lo
  diretamente com `tools/watch-bc250mcdm-lab.ps1` usando exatamente
  `evidence_dir`, `instance_id`, `published_inf` e `watchdog_token` do JSON;
- não desarmar até o usuário confirmar explicitamente que há vídeo.

Com vídeo confirmado, executar:

```powershell
$b = Get-Content tools\_last_lab_binding.json -Raw | ConvertFrom-Json
powershell -ExecutionPolicy Bypass -File tools\confirm-bc250mcdm-lab.ps1 `
  -EvidenceDir $b.evidence_dir -InstanceId $b.instance_id `
  -Token $b.watchdog_token -IConfirmDisplayIsVisible
```

Confirmar que o watchdog ficou `DISARMED`. Depois, somente leitura:

```powershell
powershell -ExecutionPolicy Bypass -File tools\test-p2-multi-submission.ps1
```

### 5.3 Critério de leitura do resultado

Sucesso P2 exige no mesmo ciclo:

- one-shot: campos `Shader*` presentes e `ShaderPhysicalReady=1`;
- P1: `P1EngineState=ONLINE`, `P1ActivateStatus=OK` e prova física completa;
- P2: `P2StreamAttempted=1`, 16 aceitas e 16 aposentadas;
- `RPTR` writeback e MMIO acompanhando `WPTR`;
- 16 resultados e 16 fences sem poison ou mismatch;
- `P2TriadPassed=1` e `P2PhysicalReady=1`;
- rollback/teardown íntegro.

Se o one-shot ou a P1 falhar, o comportamento correto do 3.60 é
`P2StreamAttempted=0` e zero submissões extras. Isso valida o gate fail-closed,
mas **não valida a P2**.

Qualquer fence ausente, poison restante, `RPTR!=WPTR`, mismatch, falha de
rollback ou telemetria incompleta mantém o veredito **NÃO VALIDADO EM
HARDWARE**.

## 6. Depois do ensaio

1. Preservar a nova pasta em `docs/evidence/`.
2. Criar um documento `157_RESULTADO_FISICO_3.60_*.md` com hashes, horário,
   boot, vídeo, tripé e rollback.
3. Restaurar BasicDisplay por `tools/rollback-bc250mcdm-lab.ps1` ou pelo
   watchdog; verificar Código 0.
4. Fazer reboot completo antes de outro candidato que toque hardware.
5. Só alterar código após classificar em qual gate ocorreu a primeira falha.

## 7. Texto curto para iniciar a próxima IA

> Continue o desenvolvimento do BC-250 a partir de
> `docs/156_HANDOFF_IA_CANDIDATO_3.60.md`. Leia também `AGENTS.md`,
> `BC250_HARDWARE_RULES.md` e os docs 148, 154 e 155. O pacote 3.60/schema 127
> está pronto e assinado, mas NÃO VALIDADO EM HARDWARE. Comece read-only:
> confirme que `LastBootUpTime` é posterior a 31/08/2026 08:33:35 -03:00,
> que a GPU está em BasicDisplay Código 0 e que os hashes coincidem. Preserve
> doorbell exclusivamente em BAR2+0x110 e a faixa 0x110..0x110. Não toque
> 0x0/0x220 nem use upper 0x003FFFFC. Depois siga o watchdog e a máquina de
> estados do handoff para ensaiar o 3.60; peça confirmação humana de vídeo antes
> de desarmar. P2 só passa com 16/16 aposentadas, RPTR==WPTR, fences/resultados
> físicos sem poison e rollback íntegro. Preserve a árvore Git suja.

