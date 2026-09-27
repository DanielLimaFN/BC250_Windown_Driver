# DX.48 — ciclo de vida do adaptador falante; critério da Corrida 93

> 23/09/2026. Faixa **offline**, sem bind. Base:
> [Corrida 92](385_CORRIDA92_DX47_RESULTADO_60_DE_60_E_TRAVAMENTO.md): 60/60,
> zero TDR, e travamento duro depois do harness. Teto: **uma** corrida, depois
> de desligamento completo.

## 1. Pergunta

**O que se sabe (P0):**
- os dois travamentos (88 e 92) têm a mesma última linha gravada:
  `[McdmQAI] Type=29 CapsStatus=0xC00000A3`;
- é o `WDDMDEVICECAPS`, que o dxgkrnl consulta logo depois de um
  `DxgkDdiAddDevice`, servido por um contexto que **não** passou pelo
  StartDevice, no meio da corrida;
- nas corridas sem travamento, essa consulta aparece só uma vez, no início do
  bind.

**H:** o Windows cria uma segunda instância do adaptador no meio da corrida, e o
que vem depois trava a máquina.

**Não dá para medir hoje:** `AddDevice` não escreve nada e o QAI não diz qual
contexto consultou.

## 2. A mudança (só medição; nenhum retorno muda)

**`bc250_active_miniport.c`:** os nomes públicos das DDIs viram envoltórios, e
as implementações passam a `…Impl`, então os testes que comparam ponteiros
continuam valendo. Cada envoltório escreve `ENTER`/`LEAVE` síncronos (PASSIVE):
- `AddDevice ENTER #n PDO=` / `LEAVE #n PDO= Contexto= status=`. O número de
  instância conta quantas vezes o `AddDevice` foi chamado desde que o driver
  carregou;
- `StartDevice ENTER/LEAVE Contexto= status=`;
- `StopDevice ENTER/LEAVE`;
- `RemoveDevice ENTER/LEAVE`.

**`bc250_mcdm_callbacks.c`:** a linha `[McdmQAI]` passa a imprimir o
`hAdapter`.

**`bc250_display_ddi.c`:** `DX48_LIFECYCLE` e o QAI `Type=29` ficam isentos da
amostragem.

**Portões antigos** que liam o corpo das funções pelo nome público (TLB lock no
AddDevice, remoção, StartDevice V5) foram apontados para as `…Impl`, sem
afrouxar nada.

**Mantidas:** DX.43, DX.44/47 (preempção concluída), DX.45b e DX.46. Janela de
300 s.

## 3. Verificação offline

- `build-contract` EXIT=0 (inclui `active_miniport_test`, que compara os
  ponteiros públicos). Build lab EXIT=0.
- Portão novo `audit_dx48_adapter_lifecycle_speaks` e
  `tools/test-dx48-lifecycle-mutations.py`: **6/6 reprovam**:
  - AddDevice mudo;
  - StartDevice sem LEAVE;
  - StopDevice mudo;
  - RemoveDevice mudo;
  - QAI sem `hAdapter`;
  - ciclo de vida amostrado.
- Demais suítes: DX.43 7/7, DX.44 6/6, DX.45 11/11, DX.46 6/6, DX.47 9/9, B.1
  30/30, DX.31 PASS; auditorias de segurança, matriz DDI, callbacks e RADV:
  PASS.
- **Incidente de host, sem efeito no candidato:**
  - a primeira passada da B.1 falhou com `OSError 22` ao gravar
    `bc250_active_miniport.c` (bloqueio momentâneo);
  - conferido: o conteúdo ficou idêntico ao compilado (mesmo `git diff`
    ignorando finais de linha), só os finais de linha foram normalizados para
    CRLF pela gravação da B.1;
  - `bc250_gfx_ring_transaction.c` e os scripts de pacote mudaram **só** em
    finais de linha;
  - a segunda passada da B.1 deu 30/30.

## 4. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.253.1.0`. SYS `48CFDEFD936202E020944E5CB982C61B41422CFD26557CBF4782C60FDE161732`, INF `1D46B077…8296`, CAT `6E9B6076…384A`. Auditoria do pacote PASS; `DX48_LIFECYCLE`, `hAdapter`, `PREEMPT_DONE` e DX.43 no SYS empacotado. |
| UMD / RADV | inalteradas: `45BB28C3…`, DX.42 `FCAA11FE…` |
| Pré-voo | `tools/prepare-corrida93-dx48.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA93_DX48.cmd`, elevado, janela de 300 s, depois de **desligamento completo** (marcador armado no boot das 19:25:19) |

## 5. Critério, escrito antes

**Controle:**
- `[DX48_LIFECYCLE] AddDevice ENTER #1` e `StartDevice LEAVE … status=0x00000000`
  no começo do bind;
- `CntlPlanned=0x50000B0D`.

| | Assinatura | Implicação |
|---|---|---|
| **A** | Travamento **e**, antes da última linha, um `AddDevice ENTER #2` (outro PDO ou o mesmo) seguido do QAI `Type=29` com `hAdapter` = o contexto novo | A segunda instância existe. A próxima faixa trata dela: recusar a segunda instância, ou impedir que o StartDevice dela toque o hardware em uso. |
| **B** | Travamento, e o QAI `Type=29` final tem o `hAdapter` **igual** ao contexto já iniciado, sem `AddDevice #2` | Não é segunda instância: o contexto vivo passou a responder como não iniciado (estado corrompido ou teardown em curso). Olhar `StopDevice`/`RemoveDevice` antes. |
| **C** | Travamento sem QAI `Type=29` no fim | Não é a mesma assinatura das 88/92. A última etapa do ciclo de vida (se houver) nomeia o suspeito. |
| **D** | Sem travamento, 60/60, zero TDR | Estável nesta corrida (os travamentos foram 2 em 6 corridas desde a 87). Registrar qualquer `AddDevice #2` que apareça mesmo sem travar. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Secundárias:**
- preempção: todo `REGISTRADO` com `PREEMPT_DONE`;
- DX.43 (8ª amostra);
- WPTR (voltas do anel);
- guardião: se travar, deve retomar no boot;
- relato visual do Daniel.

**Depois da corrida:** relatório com as 5 seções e a tabela-checklist
atualizada.
