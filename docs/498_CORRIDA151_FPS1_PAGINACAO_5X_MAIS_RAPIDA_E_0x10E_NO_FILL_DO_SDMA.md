# 498 — Corrida 151: FPS.1 reduziu o FLUSH_TLB 5×, e a corrida morreu aos 49 s num 0x10E_4 com o SDMA parado num fill de 4 MiB; critério da Corrida 152

> Corrida de 28/09/2026 09:14:44, candidato 3.301.1.0 (FPS.1), sem KD, harness atrasado 150 s.
>
> Evidência:
> - pasta `docs/evidence/2026-09-28_09-14-44-DESKTOP-HQAG6T4-lab-binding/`, com `MEMORY-151.DMP` copiado pelo Daniel;
> - análise do dump em `docs/evidence/dump-0x10E-2026-09-28.txt`.

## 1. Relato (CLAUDE.md, cinco pontos)

**1. Houve avanço?** Parcial. A paginação ficou bem mais rápida (P0). A estabilidade regrediu nesta corrida: tela azul aos 49 s.

**2. Sucessos (P0, janela de 49 s, comparada à 150):**

| Medida | 150 | 151 |
|---|---|---|
| FLUSH_TLB, mediana | 3,01 ms | **0,61 ms** |
| FLUSH_TLB, p99 | 51,6 ms | **1,4 ms** |
| Parcela do tempo da thread de paginação dentro do FLUSH_TLB | 82% | **33%** |

A reconstrução do journal deixou de ser o custo dominante. Antes da queda houve 319 `Present #` e 5 `BYTE_PROOF PASS`, com zero TDR e zero `GFXHUB_FAULT`.

Uma correção ao meu primeiro rascunho: houve **5.270 `PUBLICACAO_FALHA` e 95 `NO_SPACE`**. Não é novidade da FPS.1:
- a 150 teve 15.797 e 363 nos 600 s;
- é o journal no teto de 1.024 faixas (lotes de 129 mudanças), já conhecido;
- pela contagem de linhas, a taxa é da mesma ordem nas duas.

Cauda do log recuperada depois do BugCheck: `bc250_display_log-tail-recovered.txt`, 16.499.593 bytes, SHA-256 `103E503E…`.

**3. Falhas (P0):**
- `[SdmaPresent] Blt Timeout` no Node 0, e em seguida **BugCheck 0x10E, Arg1=4** ("Rotation failed in a must succeed path"), em `dxgmms2!VIDMM_MEMORY_SEGMENT::RotateCopyCallback`. É a mesma assinatura da Corrida 135.
- O último IB de paginação submetido (fence 4941) era **um único `CONST_FILL` de 4 MiB** (count=0x3FFFFF) e não teve `IB EXECUTADO`.
- Ponteiros do anel:

  | Ponteiro | Valor |
  |---|---|
  | RPTR | 353.952 |
  | WPTR_Reg | 354.108 |
  | Alvo | 354.180 |

  O SDMA parou antes de terminar o anel.
- O harness não chegou a rodar (a queda veio antes dos 150 s de atraso). **O FPS do harness NÃO FOI MEDIDO.**

**4. Veredito contra o doc 497:** **D (regressão: BugCheck)**. Duas ressalvas:
- A falha não é atribuível à semântica da FPS.1. A 135 teve o mesmo 0x10E_4 sem a FPS.1, e o teste de propriedade mostra resultado idêntico.
- A hipótese mais provável é que a paginação mais rápida aumentou a densidade de fills no SDMA e expôs uma trava que já existia (H).
- O desfecho A não pode ser julgado: faltou o FPS do harness.

**5. Recuperação e pendências:** Daniel reiniciou depois da tela azul. O dump foi copiado e analisado. O driver ativo depois do reboot segue a rotina do fusível e do watchdog, e o marcador de boot frio continua em vigor.

**NÃO VALIDADO EM HARDWARE:**
- a causa da trava do SDMA;
- o FPS do harness com a FPS.1;
- a estabilidade da FPS.1 em 600 s.

## 2. O que se sabe sobre a trava do SDMA

- **P0:** nas duas quedas 0x10E_4 (135 e 151), o SDMA parou num IB de paginação com um único fill de 4 MiB.
- **P0, e isso enfraquece a ligação:** nas Corridas 146, 149 e 150, que não caíram, **333 a 379 fills de 4 MiB por corrida** foram executados sem problema. O tamanho, sozinho, não trava. **A ligação com o limite de 4 MiB é H fraca.**
- **Descartado por leitura de código (P1):**
  - corrida de lock no WPTR: todos os escritores usam o `RingLock`;
  - sombra de WPTR velha: o mapeamento é uncached, com barreira e flush de HDP.
- **P1:** a Mesa, com o mesmo formato de pacote, limita o fill do SDMA < 6.0 a `0x3FFFFC` e a cópia do SDMA < 5.2 a `0x3FFF00`, e nunca programa 0x3FFFFF. Nós programávamos.

## 3. O que mudou para a 152 (SDMA.4M, pacote 3.302.1.0)

1. **Limites da Mesa** em `bc250_sdma_packet_builder.h`:
   - `BC250_SDMA_FILL_MAX_BYTES = 0x3FFFFC`;
   - `BC250_SDMA_COPY_MAX_BYTES = 0x3FFF00`.

   Todo chamador já fatiava pelo limite; muda só o tamanho das fatias. Fixado em `sdma_packet_builder_test.c`: fill e cópia de 0x400000 recusados, 0x3FFFFC e 0x3FFF00 aceitos. A mutação foi verificada.
2. **Log do timeout:** o `Blt Timeout` agora imprime uma linha `IB_ESTADO` com os registradores e o número de IBs em voo (`InFlight`):
   - `STATUS1`, `ERR_LOG`, `IB_CNTL`, `IB_RPTR`, `IB_BASE_LO`, `IB_SIZE`, `CTX_STATUS`.

   Se a trava voltar, a linha diz se o SDMA está dentro do IB e em que dword. Somente leitura.
3. Continua tudo da FPS.1, DX.90–DX.93 e M1.

Resultados: `build-contract` EXIT 0, auditorias PASS, build de laboratório EXIT 0 e auditoria do pacote PASS. A string `IB_ESTADO` foi conferida no SYS assinado.

| Artefato (3.302.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `7FC7DF20961FDC5CA793E098FABA7DDE0155FE54429B212544FBFFE0660583D3` |
| `bc250mcdm.sys` fonte lab | `832C3E05C355D8C2298ADF2B12368C6EBE413C599E9B7444DE480174C654B9A3` |
| `bc250umd.dll` (igual à 149–151) | `FAE9F89EB95B33C2A4E2A39D1688609EA4A3FF802B43492C72D45964A3406911` |
| `bc250mcdm.inf` | `E0FFE480BAA1D181017A2DBDAA020D67632B4BE69F5F7C30465B369A3824AC24` |
| `bc250mcdm.cat` | `92B208E5E3A6105586EC67A5BA1CA4F73E758D915563EB259A4DF2B822FC42BC` |

## 4. Critério da Corrida 152 (escrito antes)

- **Runner:** `tools\EXECUTAR_CORRIDA152_SDMA4M_600S.cmd` (prepara com `tools\prepare-corrida152-sdma4m.ps1`), sem KD, harness atrasado 150 s, 600 s.
- **Boot frio com corte de energia antes.**
- **Linhas de base:**
  - 150 para o FPS (1,64 FPS no harness);
  - 151 para o FLUSH_TLB.

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A** | 600 s sem `Blt Timeout` e sem BugCheck; zero fill com count > 0x3FFFFB; FLUSH_TLB com mediana ≤ 1 ms; harness 60/60 **e ≥ 3,3 FPS** | A trava sumiu com os limites da Mesa (continua H até repetir) e a FPS.1 dobra o FPS. Próximo: o que sobra no quadro, pela correlação QPC. |
| **B** | 600 s estáveis como em A, mas o harness fica abaixo de 3,3 FPS | Estabilidade ok; o gargalo seguinte do FPS é outro. Medir o quadro de novo pela correlação QPC. |
| **C** | `Blt Timeout` ou 0x10E de novo, **com** a linha `IB_ESTADO` | O limite de 4 MiB não era a causa. `IB_RPTR`/`IB_SIZE`/`ERR_LOG` dizem onde o SDMA parou; a investigação vai para esse pacote. |
| **D** | Trava ou BugCheck **sem** `IB_ESTADO` no log | A falha é outra (não o timeout do blt), ou o log se perdeu. Recuperar a cauda e o dump primeiro. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

**NADA DA SDMA.4M FOI VALIDADO EM HARDWARE.**
