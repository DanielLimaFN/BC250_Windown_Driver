# Corrida 94 — DX.49: travamento duro, tela preta e log da UMD perdido

> **CORREÇÃO (23/09, após a Corrida 95):** a afirmação "nenhum log da UMD
> sobreviveu" é **falsa**.
> - A sessão que analisou não tinha permissão para ler `C:\Windows\Temp`
>   (acesso negado), e a listagem voltou vazia sem erro visível. Não era medição
>   de ausência.
> - No `%TEMP%` do usuário sobreviveram os logs de Explorer, harness e Edge,
>   copiados para `umd-logs-pos-reboot/`.
> - O harness (pid 6220) termina às 22:08:05.867 em `FlushCommands ENTER`, no
>   8º quadro, sem LEAVE. O travamento foi ~35 s depois do bind, o que explica a
>   tela preta.
> - O log do dwm.exe está em `C:\Windows\Temp` e ainda precisa ser copiado com
>   elevação.
> - Consequência: a premissa da DX.49b ("sem write-through o travamento apaga o
>   log") não foi medida. A mudança é inofensiva, mas não tinha a base que o doc
>   390 afirmou.

> 23/09/2026, bind às 22:07:30, janela de 300 s.
> **Candidato:** lab **3.254.1.0**, SYS `EA3F02B3…`, UMD `B786E360…`.
> **Evidência:** `docs/evidence/2026-09-23_22-07-30-DESKTOP-HQAG6T4-lab-binding/`.
> **Critério:** [doc 388](388_DX49_VAZAO_DO_DESKTOP_E_CRITERIO_CORRIDA94.md).

## 1. O que aconteceu

**Relato do Daniel:** só tela preta. Não apareceu o triângulo do harness nem o
desktop.

**Máquina (P0):**
- Kernel-Power 41 (reinício sem desligamento correto) no boot seguinte;
- no novo boot o fusível recusou o StartDevice (`State=2, Boots=2`) e o guardião
  fez o rollback às 22:17:27 (`RECOVERY_OK_ONLINE`);
- BasicDisplay em Código 0.

**Evidência que faltou:**
- **Lado do binder:** o travamento matou o binder, então a pasta não tem o log
  final do KMD, nem os logs da UMD, nem a saída do harness (os `.tmp` estão com
  0 bytes).
- **Log do KMD:** resgatado do arquivo cumulativo a partir do offset do
  baseline (`display-log-this-bind-RECUPERADO.txt`, 29.735 linhas, sha256
  `3E8E78CE…15F9`).
- **Logs da UMD:** **não sobrou nenhum** `BC250_UMD_LOG_*` em
  `C:\Windows\Temp`. É o custo declarado da parte 3, que tirou o write-through,
  e custou a evidência do lado da UMD justamente numa corrida que travou. Erro
  meu de avaliação: troquei durabilidade por velocidade no único log que
  descreve o DWM.

## 2. O que o log do KMD mostra (P0)

**Controle presente:** `AddDevice #1` → `StartDevice LEAVE status=0`,
`CntlPlanned=0x50000B0D`.

**Parte 2 (lote) chegou ao KMD:**

| | Corrida 93 | Corrida 94 |
|---|---|---|
| Submissões GFX | 2022 em ~5 min | 228 antes de travar |
| IB máximo | 824 DW | **5824 DW** |

Na 94 houve 30 IBs acima de 1000 DW, todos concluídos: tripé GFX com
RPTR==WPTR e fence escrita pela GPU. Vários draws por IB, como pretendido.

**Parte 4 (dirty rects) funcionou:**
- 30 `DIRTY_RECTS n=` aceitos e zero recusados;
- inclui retângulos parciais reais, por exemplo `(2193,1040)-(2393,1080)`
  com 32.000 bytes;
- todos com `DIRTY_RECT_PROOF Match=5/5 SrcNZ=5`.

**Outros sinais:**
- 41 Presents (32 flip, 9 blt) e 41 `COMPOSITE_BLT OK`;
- 68 warm-ups DX.43, zero TDR, zero `CP_RESET_DETECTED`, zero `GFXHUB_FAULT`,
  zero timeout.

**GPU sadia até o fim do log:**
- o último tripé GFX (linha 28758) tem WPTR=RPTR=6024 no registrador e no
  writeback;
- a última submissão GFX (Fence=229, 288 DW) teve `SUBMIT SUCESSO`;
- o log termina no meio do registro de uma submissão SDMA (`PT_WALK`).

Nada indica engine travada antes do corte.

**DX.48:** sem `AddDevice #2` e sem QAI `Type=29` no meio da corrida. A
assinatura difere das 88 e 92 (lá a última linha era o QAI `Type=29`).

## 3. Leitura

- **Desfecho D** (regressão: travamento) na letra do critério.
- **Não dá para atribuir o travamento à DX.49 (H):**
  - a GPU estava ociosa e sadia no último ponto medido;
  - travamentos duros já aconteceram sem a DX.49 (88 e 92, 2 em 7 corridas);
  - o que muda de fato com a DX.49 é o tamanho dos IBs (até 7× maior) e o
    ritmo das submissões.
- **Para o próximo passo:** sem os logs da UMD não se sabe se o DWM ou o
  harness estavam vivos, nem o que faziam no momento do corte.
- **Tela preta, P2:** o scanout recebeu dados não nulos (`DIRTY_RECT_PROOF
  SrcNZ=5`), mas o Daniel não viu nada. O relato não informa quanto tempo
  depois do bind ficou preto nem quando travou, e o log do KMD não tem
  horário. Se o travamento veio cedo, pode não ter havido quadro visível.

## 4. Próximo passo proposto (DX.49b, uma variável)

- **Mudança:** devolver a durabilidade ao log da UMD sem voltar ao custo antigo.
  Handle aberto uma vez **com** `FILE_FLAG_WRITE_THROUGH`; cada linha é um
  `WriteFile` durável, sem `CreateFile`/`FlushFileBuffers`/`CloseHandle` por
  linha.
- **Mantido:** o resto da DX.49 igual (lote, staging, dirty rects).
- **Leitura da próxima corrida:**
  - se travar de novo, o log da UMD diz onde o DWM e o harness estavam;
  - se não travar, mede-se a vazão, que esta corrida não mediu.
