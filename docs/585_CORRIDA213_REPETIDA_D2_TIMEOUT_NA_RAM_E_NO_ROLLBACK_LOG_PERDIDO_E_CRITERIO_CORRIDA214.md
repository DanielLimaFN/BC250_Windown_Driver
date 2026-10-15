# 585 — Corrida 213 repetida: a D2 estourou o tempo na RAM e no rollback; log perdido por desenho; critério da Corrida 214

**Data:** 04/10/2026, binds às 20:53:34 e 21:09:10 · **Candidato:** lab 3.361.1.0 · **Critério:** [doc 583](583_D2_ANEL_E_WORK_DO_SDMA0_NA_RAM_PELA_ABERTURA_AGP_E_CRITERIO_CORRIDA213.md) §4
**Evidência:** as duas pastas `docs/evidence/2026-10-04_20-53-34-…` e `…_21-09-10-…`; rastro durável `C:\BC250_PAGING_TRACE.bin` a partir dos offsets dos baselines.

## 1. Medido (P0)

- **As duas tentativas deram o mesmo resultado:**
  - `V5StartDeviceStatus=0xC0000483`, Código 43 em ~3 s, sem tela azul;
  - zero linhas de log do KMD; na segunda, o arquivo novo **nem foi criado**;
  - as migalhas param na mesma sequência (274).
- **Migalhas de MMIO, tique a tique:**

| Tique (100 ms) | 212 (sem D2) | 213 (D2) |
|---|---|---|
| 4 | seq 111, última: leitura de `SDMA0_ERROR_LOG` (fim do init) | seq 179, última: escrita de `SDMA0_MINOR_PTR` (publicação do WPTR) |
| 5–12 | sem MMIO, depois MMU e paginação (milhares) | **sem MMIO por ~0,9 s** |
| 13 | — | seq 269, última: outra escrita de `MINOR_PTR` |
| 14–23 | — | **sem MMIO por ~1,1 s** |
| 24 | — | seq 272: leitura de `STATUS` (depois de WPTR e RPTR); seq 274: escrita de `RB_CNTL` |

## 2. Leitura (P2, reconstruída das migalhas)

- A sequência "publica WPTR → ~1 s lendo só a fence na memória → lê WPTR, RPTR, STATUS" é o `SYNC_NOP TIMEOUT`. O teto é `BC250_SDMA_TRIPOD_MAX_POLLS = 200000 × 5 µs = 1,0 s`, igual aos intervalos medidos.
- **Ela aconteceu duas vezes:**
  1. o NOP de prova no **anel na RAM** (AGP) não devolveu a fence;
  2. o **rollback para a VRAM** também não.
- Depois veio o retorno `STATUS_DEVICE_HARDWARE_ERROR` da D2 e o teardown (`RB_CNTL`). A liberação da RAM foi pulada, porque `RingInSystemMemory` ficou FALSE. A memória ficou vazada de propósito.
- **O que isso diz (H):** o acesso do SDMA0 pela abertura AGP não completou, e o motor, ou o caminho de memória dele, ficou num estado que o soft reset do SDMA0 não limpa. Ou seja: a AGP, como a programamos, não funciona neste chip, ou trava o caminho. O Linux não liga a AGP no BC-250 (P0, coleta L-B).
- **O que falta saber:** se o RPTR andou (o SDMA leu o anel na RAM e falhou só na escrita da fence) ou ficou parado (não conseguiu ler). Isso está nas linhas `SYNC_NOP TIMEOUT` que o log perdeu.

## 3. Por que o log sumiu (P1, leitura de código; corrige o doc 584)

- `Bc250DisplayLog` só escreve no arquivo em PASSIVE, **com APCs ligadas**. DriverEntry, AddDevice e StartDevice rodam com APCs desligadas, então tudo vai para o anel em RAM, que só é drenado pela próxima linha escrita num contexto normal.
- Na 212 o StartDevice passou e o anel foi drenado depois. Na 213 ele falhou, o dispositivo foi removido, e o anel se perdeu.
- **A hipótese da fragmentação do doc 584 está refutada:** o append elevado no arquivo velho funcionou.
- **LOG.1 (correção):** `Bc250DisplayLogDrainFromWorker`. A thread do rastro durável, que roda em PASSIVE com APCs ligadas a cada 100 ms e já faz I/O de arquivo durante a paginação, drena o anel a cada tique e uma última vez antes de sair. Usa o mesmo dono único do arquivo (DX.88) e o mesmo caminho de escrita. A D2 não muda.

## 4. Candidato da Corrida 214 (assinado, auditado, não instalado)

**Lab 3.362.1.0** = 3.361.1.0 + LOG.1. Build de laboratório "TODOS OS TESTES APROVADOS", auditoria de segurança PASS, auditoria do pacote PASS.

| Arquivo | SHA-256 |
|---|---|
| SYS | `87B090BCD0C06550082D1BD71FD60FDB8F49F9B8F4DD40AD582C86FD831C9F35` |
| INF | `77CEABA37845CFF5378D7E2CF2415D2B0153C11DFC971A19333629606E874DF0` |
| CAT | `D3A75559D4D539F1D677E07D95843849E672CEA247A0CC37DB0548BC9925B2DF` |
| UMD e RADV | iguais |

Scripts: `tools/prepare-corrida214-d2-com-log-duravel.ps1` e `tools/EXECUTAR_CORRIDA214_D2_COM_LOG_DURAVEL.cmd`.

## 5. Critério da Corrida 214 (Lei 10)

A corrida é curta e barata: o caminho de falha conhecido dá Código 43 em segundos, sem trava.

- **A — o log chega e a D2 falha como na 213.**
  - *Assinatura:* `[D2_ANEL_SISTEMA] APLICANDO`, um `SYNC_NOP TIMEOUT` com WPTR/RPTR/FenceObs, `FALHOU a prova na RAM`, um segundo timeout e `ROLLBACK FALHOU`.
  - *Implicação:* a AGP sai da mesa. **RPTR == WPTR** no primeiro timeout = o SDMA leu o anel na RAM e a fence não voltou (escrita ou visibilidade). **RPTR parado** = nem a leitura completou. Nos dois casos, o próximo passo é GART como o Linux, ou desistir da D2.
- **B — o log chega e a D2 liga.**
  - *Assinatura:* `[D2_ANEL_SISTEMA] ATIVO` e a sonda `PROVADO`.
  - *Implicação:* a 213 teria falhado por outra coisa, que o log nomeia; o critério A–E do doc 583 volta a valer para a janela de 600 s.
- **C — o log chega e a falha é outra** (MMU global recusada, `TDR_REPROGRAM_READBACK FALHOU`, etc.).
  - *Implicação:* a linha nomeia o ponto; corrijo antes de qualquer nova corrida.
- **D — o log continua vazio.**
  - *Implicação:* a LOG.1 não funcionou (por exemplo, a thread do rastro não chega a drenar antes da remoção). É defeito meu; a corrida não informa sobre a D2.
- **E — fora do mapa.** Descrever antes de julgar.

## 6. Pendências

- Boot frio antes do bind. A placa está no BasicDisplay; a RAM vazada da tentativa morreu com o boot.
- **NÃO VALIDADO EM HARDWARE:** a D2, a AGP neste chip, a LOG.1, e 600 s sem trava com 256 MB.
