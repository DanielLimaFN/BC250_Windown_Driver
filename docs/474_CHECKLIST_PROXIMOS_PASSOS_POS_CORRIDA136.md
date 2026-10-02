# 474 — Checklist dos próximos passos (após a Corrida 136)

> 26/09/2026. Legenda: ✅ feito, com prova no hardware · 🟡 parcial · ❌ falta · 🔜 próximo passo. Só é ✅ o que tem prova P0 (Lei 1). Cada item diz como fecha (Lei 3).

## 1. O que já está de pé
| status | item | prova |
|---|---|---|
| ✅ | Driver WDDM carrega e o Windows comita o modo de vídeo | Corridas 131–136 |
| ✅ | Paginação rápida e sem deadlock | `FLUSH_TLB` p50 ~1,4 ms (136) |
| ✅ | DWM compõe e apresenta pela GPU | 1.544 Presents `hr=0` (136) |
| ✅ | Desktop visível na tela da placa | relato do Daniel (136) + `BYTE_PROOF PASS` no scanout |
| ✅ | 10 min sem travar | 134, 136, 137 e 138 (quatro seguidas com boot frio) |
| ✅ | App D3D11 de teste | 60/60 `PRESENT_OK` |
| ✅ | Migalhas MMIO gravadas em disco | 5.431 registros `BCR1` (134) |

## 2. Estabilidade
| status | item | como fecha |
|---|---|---|
| ✅ | Log do KMD sem linhas intercaladas | 137: 294.648 linhas, zero corrompidas (doc 476) |
| 🟡 | Acumular janelas de 10 min (teto do watchdog mantido em 600 s, decisão do Daniel em 26/09) | contagem de corridas de 600 s seguidas sem trava, com boot frio; 5 (134, 136, 137, 138, 141); 139 e 140 travaram com o harness rodando |
| 🟡 | SDMA parado em fill (0x10E_4 na 135 e 151, TDR na 153) | DX.95 na 154: 1 troca do VMID8 (contra 760) e zero parada em 130 s (doc 502); fecha com 600 s |
| 🔜 | Trava com o harness D3D11 sobre journal grande (139/140) | BGH1 validado na 142 (linha de base ociosa medida); a 142 não reproduziu a trava (harness sobre Explorer recém-nascido). 143 reproduziu a trava com a GPU ociosa e MMIO vivo: o problema está do lado da CPU/SO (doc 484); 149/150 sem trava; **152 travou ~1 s depois de o harness abrir**, mesmo quadro (doc 499); 153 (DX.94) sem trava em 600 s com o harness (doc 501); **154 travou aos ~130 s SEM o harness** (doc 502); 155 travou aos ~77 s; **157: 48 min sem BugCheck, a trava está abaixo do Windows** (doc 505) |
| ❌ | Trava dura intermitente (115/128/129/133) | migalhas `BCR1` nomeiam o acesso na próxima trava |
| 🟡 | TDR recuperando sem perder a tela | 153: TDR real recuperado, DWM reiniciou, e depois 1.907 Presents; Daniel não viu a tela piscar, mas o TDR foi ~1 s depois do início do bind, durante a transição de adaptador: não conclusivo. Fecha com TDR no meio do desktop sem perder a imagem |

## 3. Desktop utilizável
| status | item | como fecha |
|---|---|---|
| 🟡 | Explorer estável: textura compartilhada recusada | 137: 6 quedas com uso real; recusa entre o nosso CreateAllocation e o VidMm (doc 476); 139: textura compartilhada criada pela 1ª vez (3/3, hr=0) e Explorer sem quedas, mas trava dura aos ~36 s; 141 sem o harness: 600 s, Explorer sem quedas e textura compartilhada 4/4; a trava das 139/140 exige o harness sobre o estado grande (doc 481) |
| ❌ | Janelas, menus e apps comuns funcionando | uso do desktop durante as janelas de 10 min, sem queda |
| ❌ | **Memória dinâmica: segmento de memória do sistema real** (hoje 2 GB fixos, uso não comprovado), dimensionado pela RAM | alocações de GPU vistas em memória do sistema, com pixels conferidos |
| ❌ | Opcional: VRAM além da janela de 256 MB como segundo segmento não visível pela CPU (só então `UMA_SIZE` maior na BIOS traz ganho) | segmento 2 declarado e usado; BIOS mantém 512 MB até lá |
| 🔜 | (DX.94 pronta, 3.303.1.0: espaço 4.096, journal 1.024; testada na 153) Espaço de GPUVA sem `NO_SPACE` e sem crescer sem parar (141: N preso no teto de 1.024 desde ~5 min, 446 `NO_SPACE` e 460 `vkAllocateMemory` do Explorer com -2) | N estável e zero `NO_SPACE` numa janela de 10 min com a textura compartilhada ligada |

## 4. Desempenho
| status | item | como fecha |
|---|---|---|
| 🟡 | (FPS.3b validada na latência: conclusão do SDMA 5,2 → 1,5 ms, 155; FPS do harness ainda não medido) Velocidade do desktop | FPS.1: FLUSH_TLB 3,0 → 0,6 ms; Presents do DWM 3,1 → 6,5/s (152); FPS do harness ainda não medido com ela |
| ❌ | 60 FPS no app de teste | harness a 60 FPS medido |

## 5. Gráficos de verdade (rumo a jogos)
| status | item | como fecha |
|---|---|---|
| ❌ | Marco 3: tela colorida por pipeline gráfico Vulkan | `vkCmdDraw` com shaders e pixels conferidos |
| ❌ | Marco 4: cubo 3D | cubo na tela, pixels conferidos |
| ❌ | Apps D3D11 reais sem modificação | app comum rodando sem erro |
| ❌ | D3D12 | app de teste D3D12 rodando |
| ❌ | Jogo real na tela da placa, sem WARP | jogo jogável, com TDR recuperando |

## 6. Final
| status | item | como fecha |
|---|---|---|
| ❌ | Display próprio (DCN, P9): outros modos e 60 Hz reais | trocar resolução/frequência sem depender do GOP |

## Nota sobre `UMA_SIZE` da BIOS (26/09)
O KMD lê o total de VRAM do hardware (`RCC_CONFIG_MEMSIZE`), não da CMOS. O segmento do Windows fica limitado à janela de 256 MB visível pela CPU (BAR); o excedente vira reserva opaca (`papel=7`). Com 512 MB: segmento `[0xA8C000, 0xFA00000)`, ~245 MB, e `[0x10000000, +256 MB)` sem uso. Subir `UMA_SIZE` hoje só tiraria RAM do Windows. Todas as corridas validadas usaram 512 MB. `bc250_cmos_vram_contract.c` não é chamado e só aceitaria a assinatura da ferramenta do Linux (`0x42435041`), não a de fábrica (`$ABL`).
