# 500 — DX.94: teto do espaço de GPUVA de volta a 4.096 (journal fica em 1.024); critério da Corrida 153

> 28/09/2026, offline. Aprovado pelo Daniel depois do [doc 499](499_CORRIDA152_SDMA4M_SEM_0x10E_TRAVA_DURA_AO_ABRIR_O_HARNESS_E_DWM_2X.md).

## 1. Por que agora

**Na Corrida 152 (P0):**
- o espaço de GPUVA ficou em `Cap=1024 Mapped=1024 Livres=0` desde os 7,5 s;
- houve 26.195 `PUBLICACAO_FALHA` e 309 `NO_SPACE` do journal.

**O que isso faz (P1, por leitura de código):**
- Cada falha de publicação **aborta o journal com as PTEs de hardware já escritas**.
- A vista de software, que resolve Present, escape e composição, passa a não conter mapeamentos que a GPU tem.

**Histórico do teto:**
- A DX.85 baixou o teto de 4.096 para 1.024 porque as 128/129 travaram no FLUSH_TLB. A hipótese era o custo do commit.
- A condição para reabrir (medir e cortar esse custo) foi cumprida pela FPS.1.

## 2. O que mudou
| Item | Antes | DX.94 |
|---|---|---|
| `BC250_GPUVA_SPACE_MAX_RANGES` | 1024 | **4096** |
| `BC250_GPUVA_JOURNAL_MAX_CHANGES` | 1024 | 1024 (de propósito) |

O journal limita o **lote** do commit. O commit ainda reconstrói o espaço uma vez por mudança, então:

| Configuração | Pior caso do commit |
|---|---|
| Espaço 4.096, journal 1.024 | 1.024 × ~123 µs ≈ **126 ms** |
| Espaço e journal em 4.096 | ~500 ms, o padrão das 128/129 |

Subir o journal exige antes aplicar o lote numa reconstrução só.

**Memória:** ~440 KiB por espaço e quatro espaços no contexto ativo, ~1,8 MiB. A DX.81 já alocava isso e fez bind.

## 3. Provas offline
**Benchmark** (harness da FPS.1, código atual):

| Teto | N | µs por substituição |
|---|---|---|
| 1024 | 1000 | 18,8 |
| 4096 | 1000 | 85,5 |
| 4096 | 4000 | 122,8 |

**Suítes:** `build-contract` EXIT 0; `gpuva_space`, `gpuva_snapshot`, `gpuva_journal` e `active_miniport` EXIT 0.

**Portão novo** dentro da auditoria de capacidade de GPUVA:
- espaço ≥ 4.096 e journal = 1.024;
- **duas mutações, duas reprovações:** espaço de volta a 1.024; journal a 4.096.

A auditoria de segurança deu PASS, o build de laboratório EXIT 0 e a auditoria do pacote PASS.

| Artefato (3.303.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `9C8390A1786190A4D5E7B51D7A31946971597BF009EC60EC44C8C50B64E0E7D5` |
| `bc250mcdm.sys` fonte lab | `E5B01B984E917F78212BF64F92C0A513CB73BDA699DC7F4C3F27E0FF0C554FE1` |
| `bc250umd.dll` (igual à 149–152) | `FAE9F89EB95B33C2A4E2A39D1688609EA4A3FF802B43492C72D45964A3406911` |
| `bc250mcdm.inf` | `FEE66323E9B2A746BDD8141D03A897918D6339608FA18AB79FAED8A654ABA7AE` |
| `bc250mcdm.cat` | `6F07182FBD9695FA58632AC9B0257197EC427E9AB1E5CA0E64DBA31D821B90E8` |

## 4. Critério da Corrida 153 (escrito antes)

- **Runner:** `tools\EXECUTAR_CORRIDA153_DX94_600S.cmd` (preparo `tools\prepare-corrida153-dx94.ps1`), sem KD, harness atrasado 150 s, 600 s.
- Antes: `inspect-boot-fuse.ps1 -Reset` e boot frio com corte de energia.
- **Linha de base:** a Corrida 152.
- **Sinal que distingue o novo teto:** `OCUPACAO ... Cap=4096` nas linhas de ocupação, que só saem numa falha de publicação. Se não houver nenhuma falha, a ausência de `PUBLICACAO_FALHA` já prova que o teto não foi atingido.

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A** | Zero `PUBLICACAO_FALHA` em 600 s; FLUSH_TLB p99 < 5 ms; zero trava; harness 60/60 com FPS medido | O esgotamento era real e acabou; o custo continua baixo. Próximo: FPS do harness e o `NO_SPACE` do journal. |
| **B** | Zero `PUBLICACAO_FALHA`, mas trava dura como na 152 | O esgotamento não é o gatilho da trava; ela segue sem nome. Próximo passo sobre a trava: rever o que o harness faz no primeiro segundo. |
| **C** | `PUBLICACAO_FALHA` com `Cap=4096` e `Mapped=4096` | O uso cresce sem teto: um vazamento de faixas, não falta de capacidade. Contar faixas por dono (`OwnerProcess`) no próximo candidato. |
| **D** | FLUSH_TLB p99 ≥ 5 ms, ou trava com o último registro do rastro **dentro** do FLUSH_TLB | O custo do commit voltou a pesar. Reverter a DX.94 e aplicar o lote numa reconstrução só antes de reabrir. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Observações que não entram nos desfechos:
- os `vkAllocateMemory -2` do Explorer (origem não identificada, doc 499);
- os `NO_SPACE` do journal;
- os Presents do DWM por segundo (152: 6,5/s).

**NADA DA DX.94 FOI VALIDADO EM HARDWARE.**
