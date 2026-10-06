# 622 — P4.2: escritas de tabela de página no SDMA1, com troca de sinais entre os anéis, e critério da Corrida 236

**Origem:** a Corrida 235 (doc 621) provou o SDMA1 vivo e ocioso e tirou o estado dele das causas suficientes da trava. Daniel pediu a P4.2: colocar trabalho no SDMA1 como o `amdgpu`.

## 1. Como o `amdgpu` divide o trabalho (P1, `inc/linux_ref/sdma_v5_0.c`)

- **Mover e limpar memória → só o SDMA0.** `sdma_v5_0.c:1889`: `buffer_funcs_ring = &adev->sdma.instance[0].ring`. Isso cobre despejo, volta e preenchimento.
- **Atualizar tabelas de página → as duas instâncias.** `sdma_v5_0.c:1907-1910`: `vm_pte_scheds` recebe os dois anéis, e o escalonador escolhe o menos ocupado. Esse critério de carga está no `drm_sched`, fora do `inc/linux_ref`: **P1 de memória, não conferido aqui**.
- **Na placa (doc 552, P0):** os dois trabalharam (`sdma0 0x7A6`, `sdma1 0x656`), sem divisão meio a meio.

**Portanto a P4.2 fiel:**
- as escritas de PTE vão para o SDMA1;
- paginação e invalidação de TLB ficam no SDMA0.

## 2. O que muda (candidato 3.384.1.0)

**Build (`Bc250McdmEmitGpuPageTableUpdate`):**
- o pedaço do paging buffer com os `WRITE_LINEAR` de PTE de cada UpdatePageTable (4c.4) vira uma **faixa** [início, fim) na fila de pontos de divisão;
- faixas contíguas se fundem;
- só registra com a rota armada (SDMA1 anexado) e alinhado a 8 dwords; desalinhado fica no SDMA0 e é contado.

**Envio (`Bc250SdmaPresentSubmitSplitChunked`):** para cada faixa do pedaço:
- **SDMA0:** roda o IB até o início da faixa, escreve `FENCE(Go = token)` (MTYPE_UC, `sdma_v5_0_ring_emit_fence`), faz `POLL_REGMEM(Done == token)` em memória (FUNC 3, RETRY 0xFFF, INTERVAL 4, `sdma_v5_0_ring_emit_pipeline_sync`) e só então retoma o IB, **depois** da faixa.
- **SDMA1:** faz `POLL_REGMEM(Go == token)`, roda o INDIRECT da faixa (mesma VMID 8, mesmo IB), GCR e `FENCE(Done = token)`.
- Go e Done ficam na página Work do SDMA1 (VA `0xE1080`/`0xE1088`, GART da VMID 0, MTYPE_UC).
- **Garantias:**
  - a faixa nunca roda nos dois motores;
  - o SDMA0 nunca passa da faixa sem o Done, e o SDMA1 nunca começa sem o Go, então a ordem do paging buffer fica igual à de hoje;
  - o anel do SDMA1 só é publicado junto com o pedaço do SDMA0 já montado (os dois ou nenhum);
  - sem SDMA1 anexado, ou sem espaço no anel dele, a faixa sai da lista e roda dentro do IB do SDMA0, como antes, e é contada.
- **Tokens:** numerados sob o `RingLock`, nunca 0.

**TDR do SDMA0:** o quiesce desanexa o SDMA1 e para o F32 e o anel dele, porque uma espera por Go poderia ficar sem par. A partir daí as faixas voltam ao SDMA0. O snapshot guarda Go e Done.

**Desligamento:** `Bc250ActiveP4Sdma1Stop` desanexa o envio antes de desligar o SDMA1.

**Chave:** `BC250_P42_SDMA1_PTE_ROUTE=1`. Com 0, volta à P4.1 (SDMA1 vivo e ocioso).

**Telemetria:**
- `[P4_SDMA1] ANEXADO` e `[P4_SDMA1] DESANEXADO (motivo, halt, Go, Done, RPTR, WPTR, contadores)`;
- a cada 10 s, junto da linha do IH: `[P4_SDMA1] regime: build: registradas/desalinhadas/descartadas | envio: rotas/inline/sem_espaco token Go Done RPTR1 cursor1`.

**O que não muda:**
- paginação (transferência, preenchimento) e invalidação no SDMA0;
- composição, flip e blit no SDMA0;
- UMD e RADV iguais às da 235.

## 3. Provas offline

- **Suíte:** `build-contract` EXIT=0. Testes novos:
  - `sdma_packet_builder_test`:
    - sequência do SDMA0 (IB até a faixa, FENCE Go, POLL Done com os campos do Linux, IB retomado depois da faixa, nenhum INDIRECT cobrindo a faixa);
    - sequência do SDMA1 (POLL Go, pré-pad, INDIRECT só da faixa, FENCE Done);
    - recusas: sem troca, com invalidação, token 0, faixa vazia, além do IB, ponto dentro da faixa;
    - aceite de invalidação no fim da faixa;
    - pior caso;
  - `mcdm_callbacks_test`: sem rota nada é registrado; faixa desalinhada fica de fora; faixas contíguas fundem; FlushTlb no fim não funde; a faixa só sai no envio do buffer dela, com offsets exatos; a faixa parcial é descartada.
  - Na primeira compilação, três testes antigos montavam a lista sem zerar os campos novos (lixo da pilha virou faixa). Corrigi zerando as listas.
- **Portão novo `audit_p42_sdma1_pte_route`. 13 mutações reprovadas**, com os fontes conferidos por hash. Na primeira passada, a do "ponto dentro da faixa" passou (a agulha casava no outro ramo); corrigi e repeti.
- A regra antiga da 4c.4 passou a aceitar `Bc250SdmaEmitSplitIndirectCross`.
- **Pacote 3.384.1.0:** `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, auditoria PASS, strings `[P4_SDMA1] ANEXADO/DESANEXADO/regime: build:` no SYS. O 3.383.1.0 intermediário, sem os contadores do build, **não deve ser usado**.

| Arquivo | SHA-256 |
|---|---|
| SYS | `DA42466AF7A4E16173CEB6E194D41E3EDEA64FCE61FCC1B67B1A5E74675EFB1A` |
| INF | `5C3390144885EC6A8E2FDC1292C12C4154F15A980CF830419826855FE21F8BCD` |
| CAT | `3D4FF2FB8B5B704D74B4EF7A11E42C0BCAA856CC30CC1A58FA9A30F31D6521DD` |
| UMD | `8782A5E5…` (inalterada) |
| RADV | `C6BEC54B…` (a da 234) |

**Scripts:** `tools/prepare-corrida236-p42-pte-no-sdma1.ps1` e `tools/EXECUTAR_CORRIDA236_P42_PTE_NO_SDMA1_600S.cmd`. O preflight a seco passou nos hashes e parou só na CMOS.

## 4. Riscos declarados

1. **`RETRY_COUNT 0xFFF` como espera infinita.** É a forma do Linux para a mesma espera. Se o firmware desistir depois de N tentativas, o SDMA0 seguiria sem o Done e a ordem quebraria em silêncio. Sinal: Done atrás de Go numa linha de regime com o SDMA0 já adiante. Confiança P1 pelo uso no Linux.
2. **O SDMA1 executar o IB sob a VMID 8.** O contexto 8 do GFXHUB é do hub inteiro, mas é a primeira vez que outro motor lê por ele.
3. **O TDR do SDMA0 corta a P4.2 pelo resto da corrida.** É deliberado: falha fechada.

## 5. Critério da Corrida 236

**Pré-condição:**
- StartDevice `status=0`;
- `[P3B_MMHUB] LIGADA`, `[P3_IH] ATIVO`, `[CP_EOP] ARMADO`;
- `[P4_SDMA1] BringUp=OK` e `[P4_SDMA1] ANEXADO`.

**Uso:** boot frio, menu e Pesquisa.

- **A — PTE no SDMA1 em regime**
  - *Assinatura:* todos os sinais abaixo.
    - linhas `[P4_SDMA1] regime` com `rotas` crescendo junto com `registradas`, `inline`/`sem_espaco` pequenos, e `Done` igual ou perto de `Go`/`token`;
    - zero `IB_TRIPOD TIMEOUT`, `TDR_QUIESCE`, `GFXHUB_FAULT`;
    - desktop normal;
    - teardown com `[P4_SDMA1] DESANEXADO` e `[P4_SDMA1] DESLIGADO`.
  - *Implicação:* a P4.2 fecha como mecanismo. Se a trava vier igual, a divisão de trabalho entre os SDMA sai das causas, e o próximo é a P5 ou a P3.3.
- **B — a rota não chega**
  - *Assinatura:* `ANEXADO` presente e `rotas=0`, com `registradas=0` (nada registrado) ou `desalinhadas` alto (faixas fora do alinhamento).
  - *Implicação:* o build não produz faixas aproveitáveis. Ler o alinhamento das operações anteriores ao UpdatePageTable. O resto da corrida é igual à 235.
- **C — a troca emperra**
  - *Assinatura:* `IB_TRIPOD TIMEOUT` ou TDR do Node 0 com `Go > Done` (no `[P4_SDMA1] DESANEXADO` do TDR ou na última linha de regime), ou `RPTR1` parado com `cursor1` adiante.
  - *Implicação:* o SDMA1 não executa a faixa (VMID 8 no SDMA1, UTCL1, poll do Go). O TDR já desanexa; ler os registradores do SDMA1 no quiesce.
- **D — regressão**
  - *Assinatura:* qualquer um destes:
    - `GFXHUB_FAULT` novo ou TDR gráfico (PTE errada ou fora de ordem);
    - imagem corrompida;
    - BugCheck;
    - Código 43.
  - *Implicação:* chave `BC250_P42_SDMA1_PTE_ROUTE=0` e ler o caso.
- **E — fora do mapa**
  - *Assinatura:* descrever o observado antes de julgar.
  - *Implicação:* a definir.

**A trava dura continua possível.** Se vier, conta como a primeira amostra com as PTEs no SDMA1.

## 6. Pendências

**NÃO VALIDADO EM HARDWARE:**
- a P4.2 inteira;
- os teardowns do SDMA1, do EOP, do IH e da GART do MMHUB em regime;
- a semântica de `RETRY_COUNT 0xFFF` no SDMA 5.0.
