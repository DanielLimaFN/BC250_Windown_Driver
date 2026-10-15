# 580 — Corrida 211: M1b.1 ativa, tela azul por recusa de IB dividido (defeito meu), e a correção por pedaços de anel

**Data:** 04/10/2026, bind às 15:35:26 · **Candidato:** lab **3.358.1.0** · **Critério:** [doc 579](579_M1B1_LIGADA_RADV_NO_SEGMENTO2_E_CRITERIO_CORRIDA211.md) §3
**Evidência:** `docs/evidence/2026-10-04_15-35-26-DESKTOP-HQAG6T4-lab-binding/`

| Item | Situação |
|---|---|
| Log do KMD | Extraído por mim do log cumulativo (28,8 MB): `C:\BC250_DISPLAY_LOG.txt` a partir do offset do baseline |
| Coletor | **Ainda não rodado** quando escrevi |
| Logs da UMD/RADV do fim da corrida | Pendentes (`C:\Windows\Temp` exige elevação) |
| `MEMORY.DMP` | 15:42, 848 MB; o `cdb` exige elevação, então o código do BugCheck ainda não foi lido |

---

## 1. Medido (P0, log do KMD)

| Sinal | 211 | 210 |
|---|---|---|
| `[M1B_RADV_SISTEMA]` | 20 linhas (log amostrado) | — |
| Alocações da RADV (classe 0) por segmento | **144 só no 1**, **981 com transbordo** (`0x3`, preferido 1), **211 preferindo o 2** | 2.550 só no 1 |
| Memória compartilhada no Gerenciador | **0,1 GB** (Daniel, por volta de 15:37) | 0,0 |
| IBs de paginação / submissões gráficas / Present | 11.655 / 2.851 / 3.236 | 20.071 / 972 / 1.433 |
| `COMPOSITE_BLT OK` / recusados | 2.538 / 0 | 753 / 0 |
| `Present RECUSADO 0xC01E0200` | **0** | 109 |
| `IB_TRIPOD TIMEOUT` / `TDR_QUIESCE` | 0 / 0 | 0 / 0 |
| Duração até o fim | **~380 s** | ~228 s |

- **A M1b.1 está ativa pelos três sinais do critério:**
  - o `M1B_MEMORIA` da RADV ainda depende do log da UMD;
  - mas `[M1B_RADV_SISTEMA]` aparece e as alocações de classe 0 têm `SegSet 0x3`;
  - e a compartilhada saiu do zero.
- **Sobre a falta de memória:** a contagem de `res=-2` e `MakeResident NO_MEMORY` depende dos logs da RADV, que ainda faltam. No log do KMD, nenhum Present foi recusado, o que na 210 era consequência da cascata.

## 2. O fim (P0): o driver recusou um IB de paginação, defeito meu

```
[PT_GPU_SPLIT] RECUSADO: entrada de 1018 dw (12 pontos) maior que o teto 960 do anel; IB NAO enviado
... KMD_SDMA_SUBMIT ... accepted=0 status=0xC0000185
```

- O `UpdatePageTable` limitava cada paging buffer a 12 pontos de divisão, ou seja, contava **pontos**.
- Só que cada ponto custa 15 dwords **por VMID**, e os pontos de processo invalidam até 10 VMIDs. Doze pontos deram uma entrada de 1.018 dw, maior que o que cabe num anel de 1024.
- O submit recusou, e `STATUS_IO_DEVICE_ERROR` num IB de paginação é o caminho conhecido do BugCheck (`0x119`). O código exato fica para quando o coletor ler o dump.
- **Não é a trava dura** e não é o hardware. Até ali a corrida estava sem timeout, sem TDR, sem fault e sem recusa de Present.

## 3. Veredito pelo critério do doc 579

- **Não há desfecho A nem B:** a corrida terminou por um defeito de host antes de testar 600 s.
- **Pela letra é D**: "a ligação falhou", ainda que por outro motivo que não a M1b.
- **O que a M1b.1 conseguiu medir** (P0, parcial e com a lacuna dos logs da RADV):
  - transbordo ativo em 1.192 de 1.336 alocações da RADV;
  - compartilhada em uso;
  - zero `0xC01E0200`;
  - 380 s, o mais longo desde a 207, e terminado por um defeito, não pela trava.

## 4. Correção (offline)

**IB dividido escrito em pedaços de anel**, em vez de uma entrada única.
- Cada pedaço cobre uma faixa contígua do IB que termina num ponto de divisão e tem no máximo 480 dw.
- Entre os pedaços, o driver espera espaço pelo RPTR do hardware.
- Só o último pedaço leva a fence e o slot. A ordem de execução é a mesma de uma entrada única.
- **Não existe mais tamanho que force a recusa.** Um ponto único com 15 VMIDs ainda cabe (≈490 dw).
- Um último pedaço só com pontos no fim exato do IB emite apenas as invalidações.

**Testes e portão:**
- **Planejador puro** `Bc250SdmaSplitNextChunk` no builder, com teste dos 12 pontos alternando a máscara da VMID 8 e de 10 VMIDs (a entrada única daria mais de 960):
  - cada pedaço fica com até 480 dw;
  - há progresso a cada passo e nenhuma faixa vazia;
  - a cobertura do IB é total;
  - cada pedaço é emitido pelo helper testado.
- **Mutação:** um teto quatro vezes maior reprova a suíte. A primeira mutação reprovou só na compilação (`/WX`), não valia, e foi refeita.
- **Portão:** exige o caminho de pedaços, o teto próprio e a espera entre pedaços, e **proíbe** a recusa por tamanho ("maior que o teto").

**Pacote (assinado, auditado, NÃO INSTALADO):** lab **3.359.1.0**.

| Arquivo | Hash |
|---|---|
| SYS | `BFE5D315…F918` (`pedacos de anel` presente, `maior que o teto` ausente) |
| INF | `DA5BD00A…FCE0` |
| CAT | `B80E05CC…DF4E` |
| UMD | `8782A5E5…` igual à da 211 |
| RADV | `80EBB248…` igual à da 211 |

Preflight `prepare-corrida212-m1b1-pedacos-de-anel.ps1`: hashes e manifesto conferem.

## 5. Critério da Corrida 212

É o do doc 579 §3, sem mudança: mesma variável (M1b.1), com a correção de defeito. Há um sinal a mais: `[PT_GPU_SPLIT] … escrito em N pedacos de anel` com zero `RECUSADO`. O que o Daniel anota no Gerenciador continua igual.

## 6. Pendências

- **Rodar `tools\coletar-pos-trava.ps1` elevado agora**, antes da recuperação. Ele traz os logs da UMD e da RADV (`res=-2`, `NO_MEMORY`, `M1B_MEMORIA`, quantidade de DWMs) e lê o dump.
- Recuperação e boot frio antes da 212.
- **NÃO VALIDADO EM HARDWARE:**
  - a M1b.1 resolvendo a falta de memória (falta o log da RADV);
  - 600 s sem trava;
  - os pedaços de anel.

## 7. Complemento (coletor rodado, 04/10)

- **Dump:** `VIDEO_SCHEDULER_INTERNAL_ERROR (0x119)`, Arg1=2 ("o driver falhou na submissão de um comando"), Arg2=`0xC0000185`. O bucket é `0x119_2_DRIVER_FAILED_SUBMIT_COMMAND_dxgmms2!VidSchiSendToExecutionQueue`. É exatamente a recusa do IB dividido (§2), confirmada.
- **A M1b.1 nos três processos (P0):** `M1B_MEMORIA status=0x00000000 vis=251084800 inv=0 gtt=2147483647` no DWM, no Explorer e num cliente. A RADV leu os tamanhos reais e passou a ver o GTT de 2 GB.
- **A falta de memória da 210 não voltou (P0):**
  - **zero** `MakeResident STATUS_NO_MEMORY` em todos os logs da RADV (210: 211);
  - **um único `dwm.exe`** (pid 1288), vivo das 15:35:36 às 15:41:56, a corrida inteira (210: quatro instâncias em cascata);
  - o Explorer também ficou vivo do início ao fim.
- **Resíduo:** 3 `vkAllocateMemory res=-2` na Pesquisa, todos para uma imagem de 800×672 (2,7 MB, tipo 0), **sem** `NO_MEMORY` do VidMm por baixo e sem perda de device. A falha vem do lado da RADV (orçamento do heap). Isso é **H**; investigar se repetir.
- **Veredito revisto:** o defeito funcional da 210 (cascata do DWM por falta de memória no segmento 1) **não apareceu com a M1b.1** (P0, n=1). A trava dura não foi testada: a corrida terminou pelo `0x119` do §2 aos ~380 s.

## 8. Revisão antes da Corrida 212 (pedido do Daniel: ter certeza de que a corrida ajuda)

- **Furo latente fechado:** o consumo de pontos aceitava 16 por envio. Um excedente dentro do IB ficaria pendente e seria emitido 64 envios depois, o que seria invalidação atrasada, da mesma classe da 209.
  - O limite subiu para **32**. Contra o teto de 12 do `UpdatePageTable`, o excedente é inalcançável.
  - Se ocorrer mesmo assim, é contado e logado (`[PT_GPU_SPLIT] EXCEDENTE`).
  - Primeiro tentei fundir o excedente no ponto vizinho; era **errado**, porque mover a invalidação deixa operações com a TLB velha. Revertido antes do build.
- **`[ActiveOpenAlloc] RECUSADO 0xC0000035`:** 194 na 211 e 0 na 210. É uma alocação compartilhada aberta duas vezes pelo mesmo device, e o registro recusa a duplicata. **Não é fatal** (um só DWM a corrida toda). Apareceu porque, pela primeira vez, um DWM viveu o bastante para reabrir recursos. Fica como defeito funcional a tratar, com contagem na 212.
- **Pacote final:** lab **3.360.1.0** (a 3.359.1.0 nunca foi instalada).

| Arquivo | Hash |
|---|---|
| SYS | `61EB27C7…1463` |
| INF | `A32D8AD1…7427` |
| CAT | `35B1E80F…5036` |
| UMD | `8782A5E5…` igual |
| RADV | `80EBB248…` igual |

- **Provas offline:** auditorias PASS. Preflight completo **aprovado**, já com a leitura da CMOS em 256 MB das 15:52. Uma falha de CRLF que eu introduzi no `.cmd` foi pega pela auditoria e corrigida.
