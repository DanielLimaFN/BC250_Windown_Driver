# 590 — LC.1 (rollback sem laço) e V0.1 (uma tabela só para a VMID 0); critério da Corrida 217

**Data:** 05/10/2026 · **Candidato:** lab **3.365.1.0** · **Base:** [doc 589](589_CORRIDA216_TDR_VOLTOU_A_VRAM_MEC_REESCREVE_A_VMID0_600S_SEM_TRAVA_E_LACO_NO_ROLLBACK.md) (Corrida 216)
Trabalho offline. **NÃO VALIDADO EM HARDWARE.**

## 1. Correção do doc 589 (Lei 1)

O doc 589 afirmava na §2.2 que a raiz do MEC estava "fora do formato" e que, por isso, o contexto 0 dele não era usado. **Isso está errado.**

- O motor de submissão do MEC grava a fence com `RELEASE_MEM` em **GPUVA `0x2200` na VMID 0** (`bc250_submission_engine.c`).
- A retirada é lida dessa fence (`ComputeFenceVirtual`), e `MEC_CURSOR_RECONCILE ... Retired=1` aparece no StartDevice de todas as corridas.

Ou seja, a tabela do MEC traduz no silício (P0), mesmo com raiz e PTEs em endereço MC. O mecanismo proposto na §2.3 ("leitura de um endereço físico inexistente") cai junto. Sobra só a correlação da 216, com a VMID 0 desligada e sem trava, que continua **H**, sem mecanismo. Também medido: o MEC só submete durante o StartDevice. Na janela de uso da 216 houve zero publicações do MEC.

Consequência para o desenho: não dá para simplesmente tirar o contexto 0 do MEC. A solução precisa ser **uma tabela só**, a da GART, que é o que o Linux faz.

## 2. LC.1 — a recusa conta só o buffer atual

- **Defeito (doc 589 §2.4):** o `UpdatePageTable` em `GPU_VIRTUAL` recusava o paging buffer quando a fila **global** de pontos de divisão chegava a 12. No teardown do rollback, os pontos de buffers não enviados nunca saíam da fila, e todo buffer novo, mesmo vazio, era recusado para sempre.
- **Correção:** `Bc250PagingSplitMustDefer(BufferVa, WriteOffset, 12)`, em `bc250_mcdm_callbacks.c`.
  - Conta só os pontos em `[DmaBufferGpuVirtualAddress, +DmaBufferWriteOffset]`.
  - Buffer sem nada escrito ou sem GPUVA nunca é recusado, o que garante progresso.
  - O limite continua por IB, porque cada ponto vira um pedaço de IB no anel. Os pontos de outros buffers saem como invalidação única antes do IB, como já acontecia.
- **Log novo:** `[PT_GPU] paging buffer 0x...+0x... com 12 pontos de divisao proprios -> ... (fila global=N)`.
- **Teste:** `mcdm_callbacks_test` reproduz "12 pontos alheios e buffer novo vazio ou já escrito", que não pode recusar, e o caso do próprio buffer cheio, que deve recusar.

## 3. V0.1 — uma tabela só para a VMID 0

- **Contrato do HQD:** campo novo `BC250_HQD_MC_BUFFERS.Context0External`. Com valor 1, o contrato não escreve nem restaura os 5 registradores do contexto 0. A invalidação da VMID 0 continua. A evidência ganhou `GcvmContext0External`.
- **`Bc250ActiveD2GartShareWithMec`** (em `bc250_submission_engine.c`, protótipo em `bc250_active_miniport.h`). Com a GART da D2 ligada:
  - escreve as 32 PTEs da fila do MEC nas entradas **0..31** da tabela da GART (VA `0..0x1FFFF`), no formato que o MEC já provou (`MC | 0xE1`);
  - confere por readback;
  - aponta `Buffers` para a tabela da GART com `Context0External=1`;
  - registra `[D2_GART] MEC (...) na tabela da GART`.

  Com a GART desligada, devolve FALSE e o MEC segue com a tabela própria, como antes.
- **As duas entradas do MEC usam o helper:** a transação HQD (`HQD_ATIVO`) e o motor de submissão (`MOTOR_DE_SUBMISSAO`).
- **D2:** o anel e o Work saem das entradas 1/2 para **32/33** (MC `0x20000`/`0x21000`), para não colidir com o MEC. A foto passa a ler `PTE[32..35]` e mostra `MEC=`.
- **Rollback do TDR com a tabela compartilhada:** o contexto 0 **fica ligado**, porque desligar tiraria a tradução do MEC. Só o anel volta para a VRAM, com o log `[D2_GART] MANTIDA no rollback`. O teardown continua restaurando o contexto 0.

## 4. Testes e provas offline

- **`build-contract`:** EXIT=0.
  - `compute_mqd_contract_test` ganhou o caso V0.1: com o contexto 0 da GART, o contrato não o toca e invalida a VMID 0.
  - `mcdm_callbacks_test` ganhou o caso LC.1.
- **Build de laboratório:** "TODOS OS TESTES APROVADOS (0 falhas)". Auditorias de segurança e do pacote: PASS.
- **Portão novo** `audit_vmid0_single_table_and_split_progress`, com **8 mutações reprovadas** na auditoria:
  - recusa voltando a ser global;
  - buffer vazio recusável;
  - contrato escrevendo o contexto 0;
  - motor sem a GART;
  - HQD sem a GART;
  - rollback desligando o contexto 0;
  - anel na entrada 1;
  - contexto 0 não marcado como externo.
- **2 mutações de código reprovadas nos testes:** contagem global no `MustDefer` (linha 1322 do `mcdm_callbacks_test`) e contrato ignorando `Context0External` (linha 1072 do `compute_mqd_contract_test`).
- **Achado de teste:** o caso 5 do `active_miniport_test` (submissão dentro do portão durante o Stop) reprovava sempre com `STATUS_DEVICE_BUSY`. Offline, o dreno do Stop não tinha espera, e as 400 voltas passavam em microssegundos. A suíte offline ganhou uma espera curta e real; o build de kernel não muda. Era corrida do teste, não regressão.

| Arquivo | SHA-256 |
|---|---|
| SYS | `6A6A886AD9140A756A1DA95DE1FF559894F2F98DE69A2DE84C3B60CE3AFAF06D` |
| INF | `5F1FA59CD0A06B9D3EE3DC364E50FCB8F4F92F4E9B7223CADA45ABA0FD67EE65` |
| CAT | `67B908CC6D0B3038CBFAB787CCD15E80169AAEE70090941904388F520ECA9D13` |
| UMD | `8782A5E5…` (inalterada); RADV `80EBB248…` (inalterada) |

As strings novas foram conferidas no SYS assinado.
**Scripts:** `tools/prepare-corrida217-d2-vmid0-unica.ps1` e `tools/EXECUTAR_CORRIDA217_D2_VMID0_UNICA_600S.cmd`.

## 5. Critério da Corrida 217 (Lei 10)

**Pré-condição de validade:** o log mostra `[D2_GART] MEC (HQD_ATIVO) na tabela da GART` e `MEC (MOTOR_DE_SUBMISSAO)`, ambos com `divergentes=0`, e `[D2_ANEL_SISTEMA] ATIVO`. Sem isso, a corrida não testou a V0.1.

- **A — a D2 sobrevive à carga.**
  - *Assinatura:* o MEC retira pela tabela da GART (`MEC_CURSOR_RECONCILE PASS ... Retired>=1`), zero `IB_TRIPOD TIMEOUT`, e o anel fica na RAM até o fim. Não aparece `anel de volta na VRAM`.
  - *Implicação:* a falha da 215/216 era a disputa da VMID 0. O critério "trava ou não" do doc 587 passa a valer. Se não travar em 600 s com a VMID 0 **ligada**, a correlação da 216 (VMID 0 desligada) enfraquece como explicação.
- **B — a D2 sobrevive, mas há trava dura.**
  - *Assinatura:* A até a trava, e depois reset manual.
  - *Implicação:* anel e fences na RAM não bastam contra a trava dura. Próximo item: a conclusão por interrupção (doc 582).
- **C — o MEC falha na tabela compartilhada.**
  - *Assinatura:* `MEC_CURSOR_RECONCILE RECUSA`, `Retired=0`, ou o StartDevice falha no HQD com a GART ligada.
  - *Implicação:* as PTEs do MEC na GART precisam de outro formato (PA) ou de outra ordem de invalidação. Comparar com o Linux, que mapeia a fila na GART em PA \| flags.
- **D — a D2 cai de novo no primeiro IB.**
  - *Assinatura:* `IB_TRIPOD TIMEOUT` com a foto.
  - *Implicação:* aplica-se o doc 588 §5. Registradores divergentes significam que existe outro escritor além do MEC. Tudo intacto aponta para o caminho de tradução.
- **E — fora do mapa:** descrever antes de julgar.

**Condição do rollback, em qualquer desfecho:**
- no fim da janela, o rollback do watchdog fecha sem o laço;
- `forca o envio` não cresce depois do `ROLLBACK_STARTED`;
- `C:\BC250_DISPLAY_LOG.txt` para de crescer;
- `RECOVERY_OK_ONLINE` no **mesmo** boot.

Se o rollback travar de novo, é resultado: a LC.1 não bastou.

## 6. Pendências

- **Antes do bind:**
  - decidir o destino de `C:\BC250_DISPLAY_LOG.txt` (25 GB da 216);
  - recuperação;
  - fusível, se estiver disparado;
  - boot frio.
- **NÃO VALIDADO EM HARDWARE:**
  - a LC.1 no teardown;
  - o MEC na tabela da GART;
  - a D2 além do primeiro IB;
  - 600 s sem trava com 256 MB de forma repetível.
