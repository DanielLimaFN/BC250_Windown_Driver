# Corrida 108 (DX.64, 600 s): travamento duro aos ~6 min; DX.65 e critério da Corrida 109

> **Corrida 108:** 24/09/2026, bind às 17:54:57, com o mesmo pacote da 107 (lab `3.268.1.0`, SYS `7C4E6E1B…`, UMD `B6AB6788…`) e janela de 600 s.
> **Evidência:** `docs/evidence/2026-09-24_17-54-57-DESKTOP-HQAG6T4-lab-binding/`.
> - **Log do KMD recuperado** do cumulativo pelo offset do baseline: `display-log-this-bind-RECUPERADO.txt`, 41.117.080 bytes, sha256 `d55d6073…0643`. O boot seguinte começa na linha 307.165.
> - **Rastro DX.56 desta corrida:** `paging-trace-this-bind.bin`, 1.753.152 bytes = 3.176 registros, sha256 `dca56068…18ab`. Decodificação em `paging-trace-decode-last3.json`.
> - **Logs da UMD originais:** `umd-logs-pos-travamento/` (DX.57, copiados pelo guardião no boot novo).
>
> **Critério:** [doc 416 §3](416_CORRIDA107_DX64_RESULTADO_E_CRITERIO_CORRIDA108_600S.md).
> **Relato do Daniel:** "depois de uns 6 minutos travou; esperei, e depois de 10 min travado reiniciei".

## 1. Resultado: desfecho B (travamento duro)

**Linha do tempo (P0):**

| Hora local | Evento |
|---|---|
| 17:53:37,5 | boot (`boot_ticks` da configuração do guardião) |
| 17:54:57 | bind |
| 18:00:11,7 – 18:00:18,0 | `Radeonsoftware.exe` carrega a nossa UMD: OpenAdapter, cria device, 7.650 `CheckMultisampleQualityLevels`, 261 `CheckFormatSupport`, CloseAdapter |
| 18:01:00,12 – 0,13 | últimas linhas da UMD: DWM em `FlushCommands` (1 draw no lote); Explorer em `DestroyResource` |
| 18:01:00,75 | último registro durável DX.56: seq. 1.632.971, estágio 5 / operação 12 (`FLUSH_TLB`), `active_count=1` |
| ~18:06 | reinício manual, depois de ~5 min de tela congelada |
| 18:06:34 – 43 | guardião no **boot novo**: rollback e BasicDisplay Código 0 |

**Tempo até travar:** ~6 min 3 s após o bind, o mesmo da Corrida 100 (~6,0 min).

**Assinatura igual à das 88–102:**
- zero BugCheck, zero TDR, zero timeout, zero GFXHUB_FAULT;
- o log do KMD termina no meio de uma submissão SDMA de paginação (sem valor de localização: o anel de RAM morre com a máquina);
- o último registro durável é `FLUSH_TLB` estágio 5. Doc 405: é uma posição comum de amostragem, **não localiza**.

**Até o travamento, a correção da 107 se manteve:**
- zero `BLT_DESTINO RECUSADO`, zero `PRIMARY_FALLBACK`, zero `status=9`;
- zero `BYTE_PROOF FAIL`, zero `COMPOSITE_BLT RECUSADO`, zero `CP_RESET`.

**Recuperação: não foi R1.** O guardião não roda num kernel travado. Ele recuperou só no boot novo (`boot_ticks` 639258807605000000 ≠ 639258800175000000). O fusível recusou o StartDevice no boot novo e foi limpo pela recovery.

## 2. Correlação com a Radeon Software (P2 como padrão; causalidade H)

Varri as pastas de evidência de 24/09 à procura de logs da UMD do `Radeonsoftware.exe`:

| Corrida | Bind | Radeon Software abre a UMD | Travamento | Intervalo |
|---|---|---|---|---|
| 100 | 10:50:49 | 10:55:59 (+5 min 10 s) | 10:56:51 | ~47 s depois |
| 101 | 11:42:50 | 11:48:00 (+5 min 10 s) | ~11:49:27 | ~81 s |
| 102 | 13:14:53 | 13:20:04 (+5 min 11 s) | ~13:23:43 | ~3,5 min |
| 108 | 17:54:57 | 18:00:11 (+5 min 14 s) | ~18:01:00 | ~42 s |
| 103–107 | janelas de 180 s, ou BugCheck antes de 5 min | **não** aparece | nenhum travamento duro | — |

**Leitura:**
- A Radeon Software sempre abre o adaptador ~5 min 10 s depois do bind.
- **Todo** travamento duro com o candidato atual veio depois disso.
- **Nenhuma** corrida curta chegou a esse ponto.

**Contra a causa única:**
- As 95, 96, 97 e 99 travaram **antes** de 5 min (1,5 / 4,4 / 4,2 / 0,65 min), com outra configuração (lote/anel da DX.49–54).
- Pela nossa UMD a Radeon Software só faz operações benignas.
- Pelo nosso KMD não há escape dela distinguível: só os nossos opcodes e 19 escapes `0x2` de terceiros, espalhados pela corrida.

**Leitura de registro (P1):**
- **Nenhum** driver AMD está na pilha do dispositivo BC-250 nem nos filtros da classe Display.
- Estão rodando `amdfendr`/`amdfendrmgr` (Crash Defender, dispositivo próprio `ROOT\AMDLOG`), `AMDSAFD`, `AMDXE` e `AmdPPM`.
- Serviços: "AMD Crash Defender Service" e "AMD External Events Utility" (automáticos). Tarefa agendada: `StartCN`.
- O que a Radeon Software faz fora da nossa UMD (sensores, SMU, outro caminho de kernel) **não é observável** pela nossa instrumentação.

## 3. DX.65 (offline, só instrumentação): batimento no rastro durável

**Lacuna medida:** o escritor DX.56 gravava só quando a sequência mudava. O último registro não distingue "a paginação parou com o kernel vivo" de "a máquina morreu".

**Mudança em `Bc250PagingTraceThread`:**
- grava a cada 100 ms, mesmo sem mudança;
- `Reserved` passa a ser o contador de batimento do escritor, registro **v2**, com o checksum recalculado antes da escrita.

**Decodificador:**
- aceita v1 (`Reserved=0`) e v2;
- novo resumo `heartbeat_after_last_paging`: registros e milissegundos em que o escritor (PASSIVE_LEVEL) continuou vivo **depois** da última paginação.

**Leitura na próxima corrida:**
- Batimento de ~0 ms depois da última paginação: a máquina parou junto com a paginação.
- Segundos de batimento com a sequência parada: a paginação travou com o kernel vivo, e a última operação passa a **localizar**.

**Verificação:**
- `test-dx56-paging-trace-decoder.py` PASS, com o caso v2 e com v1 de `Reserved≠0` recusado;
- teste do resumo de batimento PASS;
- portão DX.65 dentro de `audit_dx56_paging_trace`, e `test-dx62-dx63-mutations.py` **22/22** (duas mutações novas);
- suíte DX.56 PASS, auditoria de segurança PASS, `build-contract` EXIT=0.

## 4. Candidato da Corrida 109

| Componente | Valor |
|---|---|
| KMD | lab `3.269.1.0`, SYS `7EF9D54D90ABBA2DD30463D698DE253B30E76860EB635532B836BF2A0F5189FC`, INF `6A304FA1…95ED`, CAT `9D72D9DB…4F48`. Auditoria do pacote PASS. Comportamento igual ao 3.268.1.0; só muda o escritor do rastro |
| UMD | inalterada, `B6AB6788…` |
| Pré-voo | `tools/prepare-corrida109-dx65.ps1`: hashes, auditorias **e recusa se a Radeon Software puder abrir** (processo, tarefa `StartCN` habilitada, serviço "AMD External Events Utility" ativo). Testado agora: recusou corretamente, porque os três estavam ativos |
| Execução | `tools\EXECUTAR_CORRIDA109_DX65_600S.cmd` (CRLF), elevado, **600 s**, depois de boot frio |

**Preparação do ambiente, feita pelo operador.** É alteração de configuração do sistema, então quem executa é o Daniel, num PowerShell elevado, **antes** do boot frio:

```
Disable-ScheduledTask -TaskName StartCN
Set-Service -Name "AMD External Events Utility" -StartupType Disabled
Stop-Service -Name "AMD External Events Utility"
Stop-Process -Name RadeonSoftware -Force -ErrorAction SilentlyContinue
```

**Para desfazer depois:**

```
Enable-ScheduledTask -TaskName StartCN
Set-Service -Name "AMD External Events Utility" -StartupType Automatic
Start-Service -Name "AMD External Events Utility"
```

## 5. Critério da Corrida 109, escrito antes

**Variável:** Radeon Software impedida de abrir. O driver tem o mesmo comportamento da 107/108; a DX.65 é só instrumentação.

**Controle:**
- **nenhum** `BC250_UMD_LOG_*` de `Radeonsoftware.exe` na pasta;
- registros v2 no rastro (`version: 2`).

| | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem travamento, guardião reverte sozinho **no mesmo boot** (R1) | A Radeon Software (ou o que ela aciona fora da nossa pilha) é necessária para o travamento neste estado. Próximo: descobrir o que ela toca, antes de qualquer mudança no driver. |
| **B** | Travamento duro **sem** a Radeon Software, com `heartbeat_after_last_paging` ≥ 1 s | A paginação travou com o kernel vivo. A última operação do rastro **localiza**. Próximo: aquela operação. |
| **B'** | Travamento duro sem a Radeon Software, com batimento < 200 ms | A máquina parou junto com a paginação. O rastro não localiza; a correlação com a Radeon cai. Próximo: trilha de outro subsistema (GFX/SDMA/IH). |
| **C** | BugCheck | Seguir o dump. |
| **D** | Controle falhou: a Radeon Software abriu mesmo assim | A corrida não mede a variável. Descrever. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Regras:**
- Desfecho parcial não é desfecho.
- A exige os 600 s inteiros **e** R1.
- Um único A é uma amostra contra quatro; confirmar pede repetir com a Radeon **habilitada** logo em seguida.

**Secundárias:**
- hora do congelamento, se houver;
- FPS;
- presents/min do DWM;
- contextos/min;
- `PREEMPT_DONE Node=0`.
