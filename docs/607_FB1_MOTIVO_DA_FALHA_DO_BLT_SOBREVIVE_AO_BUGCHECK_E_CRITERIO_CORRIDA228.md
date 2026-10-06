# 607 — FB.1: o motivo da falha do blt sobrevive à tela azul; critério da Corrida 228

**Origem:** Corrida 227 ([doc 606](606_CORRIDA227_REPETICAO_P1B_TELA_AZUL_0x119_9000_NO_FLIP_MPO3.md)).
- `0x119/0x9000` no flip MPO3 depois de `Bc250SdmaPresentBlt` = FALSE.
- A linha do motivo ficou no anel em RAM e não chegou ao disco.

## 1. Leitura de código (P1)

`Bc250SdmaPresentBltCore` (`bc250_sdma_present.c`) tem 4 saídas FALSE:

| Motivo | Condição |
|---|---|
| `NOT_READY` (1) | `Initialized` ou `HardwareEnabled` = 0 |
| `BAD_ARGS` (2) | endereço 0, 0 bytes ou mais de 64 MB |
| `EMIT` (3) | o montador de `COPY_LINEAR` recusou |
| `TIMEOUT` (4) | 10.000 polls × 5 µs (≈ 50 ms, mais o custo das leituras) sem `RPTR`/fence. O blt é síncrono dentro da DDI de flip e espera atrás do que já está no anel do SDMA0 (`InFlightCount`) |

**Antes:**
- só `TIMEOUT` logava, e só na 1ª e a cada 60ª ocorrência;
- `NOT_READY`, `BAD_ARGS` e `EMIT` eram mudas (Lei 4).

**O log passa por `g_Bc250PagingLogRing`** (128 KB, `bc250_display_ddi.c`). A drenagem só avança o `Tail`, então os bytes ficam no anel até serem sobrescritos. **O fim do log que o bugcheck cortou está dentro do `MEMORY.DMP`.**

## 2. FB.1 (candidato 3.375.1.0)

1. **`g_Bc250SdmaLastBltFailure`** (global, `BC250_SDMA_BLT_FAILURE`), preenchido em toda saída FALSE antes do retorno, sem I/O. Campos:
   - motivo, origem, destino, bytes e polls;
   - WPTR alvo e WPTR, RPTR registrador e writeback;
   - `STATUS`, `STATUS1`, `ERR_LOG`, `IB_CNTL`, `IB_RPTR`, `CTX_STATUS`;
   - fence esperada e observada, `InFlight`;
   - doorbell ativa, P1b ativa, submetidos e concluídos, QPC;
   - `Magic=0x464C5442` quando completo.
2. As três saídas mudas passaram a logar `[SdmaPresent] Blt RECUSADO: <motivo>`.
3. **`tools/extrair-anel-do-dump.ps1`** (elevado, só leitura), com o PDB arquivado na pasta da corrida:
   - `.writemem` do anel;
   - `Head`/`Tail`;
   - `dt bc250mcdm!g_Bc250SdmaLastBltFailure`;
   - grava `anel-log-do-dump.txt` em ordem cronológica.
4. A coleta pós-trava (`coletar-pos-trava.ps1`) chama o extrator sempre que há `MEMORY.DMP` posterior ao bind.

**FB.1b (mesmo dia):** a 1ª extração do dump da 227 achou o PDB privado certo, mas **nenhum símbolo `static`** (o PDB só traz globais). O anel e o Head/Tail passaram a globais (`dbh` confirma os 4 símbolos no PDB novo), e o extrator ganhou um plano B para dumps antigos: copia a imagem do `bc250mcdm.sys` e pega o maior trecho ASCII (o anel). Ele também deixou de travar: comandos por arquivo (`-cf`) e `q` pela entrada padrão.

**Portão:** `audit_fb1_blt_failure_survives_bugcheck`, mais a exigência de anel global (mutação `static` reprovada). 5 mutações reprovadas:
- `TIMEOUT` sem registro;
- `NOT_READY` sem registro;
- global trocado;
- coletor sem extração;
- extrator sem o anel.

**Provas offline:** `build-contract` EXIT=0, laboratório 0 falhas, auditoria de segurança e do pacote PASS, `Blt RECUSADO: NOT_READY` e `alias provado por escrita` no SYS.

| Arquivo | SHA-256 |
|---|---|
| SYS | `48281B18A93873CA0BD5BCFCAD0E33995F7FB8BBB22630B559D2704D77999DDF` |
| INF | `29F7933A2D4B5EDEF0318A33049B6BB8D4010E352376F477318BF19E581A3CFA` |
| CAT | `812BC5C564BF18D9F0E2AE87A3AA79567F1CF78A40603A34E770D51338C0CEAE` |

**Scripts:** `tools/prepare-corrida228-fb1-motivo-do-blt-no-dump.ps1` e `tools/EXECUTAR_CORRIDA228_FB1_P1B_600S.cmd`. O preflight a seco parou só na CMOS.

**Comportamento inalterado:**
- a P1b continua ativa;
- o MPO3 continua devolvendo `INVALID_PARAMETER` quando a cópia falha.

## 3. Ação imediata, sem corrida

O `MEMORY.DMP` da 227 ainda está em disco e é sobrescrito na próxima tela azul. **Rodar o extrator antes da 228, elevado:**

```
powershell -ExecutionPolicy Bypass -File tools\extrair-anel-do-dump.ps1 -EvidenceDir docs\evidence\2026-10-05_16-57-47-DESKTOP-HQAG6T4-lab-binding
```

O build da 227 não tem `g_Bc250SdmaLastBltFailure` (o `dt` falha), mas o anel tem. Se a linha `[PRIMARY_COPY #1024] FAIL` ou `Blt Timeout` estiver lá, o motivo da 227 fica nomeado sem gastar corrida.

## 4. Critério da Corrida 228

**Pré-condição:** `[P1B_VRAM_DRAM] ATIVO`, StartDevice 0, zero HDP em regime.

**Desfechos:**

- **A — 600 s sem trava e sem tela azul**
  - *Assinatura:* rollback online limpo.
  - *Implicação:* segunda de três com a P1b; repetir.
- **B — trava dura silenciosa**
  - *Assinatura:* GPU ociosa, sem `MEMORY.DMP` novo.
  - *Implicação:* a P1b não basta; próximo, P3 (IH).
- **C — tela azul 0x119 de novo**
  - *Assinatura:* `anel-log-do-dump.txt` com `Blt Timeout`/`Blt RECUSADO` e o registro `g_Bc250SdmaLastBltFailure` com motivo e registradores.
  - *Implicação, por motivo:*
    - `TIMEOUT` com `InFlight>0` e RPTR andando: o blt síncrono espera atrás do trabalho assíncrono, e a correção é no desenho do flip;
    - `TIMEOUT` com RPTR parado: o SDMA0 parou, e isso liga à trava dura;
    - `NOT_READY`: `HardwareEnabled` caiu por outro caminho.
- **D — StartDevice cai ou a P1b não arma**
  - *Assinatura:* Código 43 ou `NAO ATIVA`.
  - *Implicação:* defeito de host.
- **E — fora do mapa**
  - *Assinatura:* descrever o observado antes de julgar.
  - *Implicação:* a definir.

## 5. Pendências

**NÃO VALIDADO EM HARDWARE:**
- o motivo da falha da 227;
- a P1b eliminar a trava;
- a extração do anel (só funciona com dump real).

## 6. Resultado da extração do dump da 227 e FB.2 (mesmo dia)

A extração rodou na terceira tentativa, pelo plano B (`db` em texto, porque nenhum arquivo gravado pelo `cdb` ficou em disco): 57.600 linhas de 16 bytes lidas, 512 ilegíveis. O anel tinha as linhas que o disco perdeu (P0):

```
[SdmaPresent] Blt Timeout #1: TargetWPTR=2755288, WPTR_Reg=2755376, RPTR_Reg=2755376, STATUS=46DEED57, FenceExp=00002CE2, FenceObs=00002CE3
[SdmaPresent] Blt Timeout IB_ESTADO #1: STATUS1=000003FF ERR_LOG=0000000F IB_CNTL=00080101 IB_RPTR=32 IB_BASE_LO=00225420 IB_SIZE=0 CTX_STATUS=0000000D InFlight=0
[PRIMARY_COPY #1024] FAIL Route=MPO3 Sdma=0 ... Status=GPU_EXCEPTION
[PRIMARY_COPY] MPO3 FAIL_CLOSED InternalStatus=0xC01E0200 ReturnStatus=INVALID_PARAMETER
```

**Leitura (P2, números e código concordam):**
- o anel do SDMA0 estava drenado **além** da nossa cópia (`RPTR = WPTR = alvo + 88 bytes`), com a engine ociosa (`STATUS1=0x3FF`) e `InFlight=0`;
- a fence no endereço compartilhado era `0x2CE3`, a da submissão **seguinte**. Logo antes, no anel, há um `COMPOSITE_BLT OK RECT 32x32` do escape do DWM.

A cópia do flip terminou. A composição, numa outra CPU, entrou no anel logo depois e sobrescreveu o endereço da fence. O esperador do flip exigia **igualdade exata** com `0x2CE2`, esgotou os 50 ms, e o fail-closed do MPO3 virou `0x119`.

**Isto é um defeito de host meu, não do silício e não é a trava dura.** A P1b não tem relação com ele: a corrida da fence existia desde que flip e composição passaram a usar o mesmo endereço; só a janela é pequena (4.224 cópias sem falha na 226).

**FB.2 (no mesmo candidato 3.375.1.0, que nunca foi instalado):**
- `Bc250SdmaSyncFenceReached`: aceita fence **igual ou posterior** (diferença com sinal, cobre a volta do contador) e trata o poison como "ainda não". O anel executa em ordem, então uma fence posterior prova que a nossa passou.
- No `Blt` e no `BltRect`, a fence passa a ser **numerada e emitida dentro do `RingLock`**, para que a ordem das fences seja a ordem no anel. Antes ela era sorteada fora do lock, e duas threads podiam entrar no anel na ordem inversa.

**Portão:** FB.2 dentro de `audit_fb1_blt_failure_survives_bugcheck`. 4 mutações reprovadas:
- voltar à igualdade exata;
- helper sem a exclusão do poison;
- helper sem comparação com sinal;
- fence numerada fora do lock.

**Também corrigidos no caminho:**
- o anel e os ponteiros do log viraram globais (FB.1b), e o `dbh` confirma os símbolos no PDB novo;
- o extrator não usa mais `.writemem`, só `db` em texto.

**Resíduo declarado:** os outros caminhos síncronos que usam o mesmo endereço continuam com igualdade exata e fence fora do lock: `SyncNop`, `VmFlushProbe`, `ExecuteIndirect` síncrono e `PagingFill` síncrono. Eles rodam no bring-up e no TDR, não em paralelo com o flip em regime. Não mexi para manter uma diferença por corrida. Se um deles aparecer num `Blt Timeout`, entra na mesma regra.

**Critério da 228:** o mesmo do §4, com uma mudança no desfecho C. Uma nova `0x119` com `FenceObs` posterior a `FenceExp` **refuta a FB.2**; com `RPTR` parado, liga à trava dura.
