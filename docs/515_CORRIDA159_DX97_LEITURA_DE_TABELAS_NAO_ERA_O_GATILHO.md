# 515 — Corrida 159: a DX.97 cortou 97% da leitura de tabelas pela BAR e a trava continuou (B)

> Corrida de 28/09/2026. Bind às 18:05:11, candidato 3.308.1.0 (DX.97 + DX.96-HDP + FPS.3b + DX.95 + DX.94), harness aos 30 s. Trava por volta de 18:06:29 (~1 min 18 s depois do bind, com o harness no quadro 28). Reset rápido; Kernel-Power 41 às 18:07:03, sem dump.
>
> Evidência:
> - pasta `docs/evidence/2026-09-28_18-05-11-DESKTOP-HQAG6T4-lab-binding/`, com a cauda recuperada;
> - logs da UMD em `umd-logs-pos-travamento/`.

## 1. Relato (cinco pontos)

**1. Houve avanço?** De diagnóstico, sim: mais um mecanismo descartado com medição. Funcional, não.

**2. Sucessos (P0):**
- **DX.97 ativa:** 6.829 `PASS_PONTAS`, zero caminhada completa de DEST. A leitura de tabelas caiu de ~7.900 para **273 páginas/s** (18.731 em 69 s).
- **Nenhum fault:** zero `GFX_FAULT_SENTINELA`, `IB_TRIPOD TIMEOUT`, `Blt Timeout` e TDR. **O desfecho C não ocorreu:** não havia página inválida no miolo das faixas.
- A FPS.3b se repetiu: SDMA submit → conclusão com p50 1,51 ms (n=3.699).
- `[DX96_HDP]` presente.

**3. Falhas (P0):**
- Trava dura por volta de 18:06:29, com tela preta.
- O harness chegou a 28 quadros: **1,63 FPS**, 613 ms por quadro, `pfnPresentCb` p50 455,7 ms.
- Divisão do quadro:

  | Dentro de `pfnPresentCb` | Valor |
  |---|---|
  | Duração | 457 ms |
  | IBs de paginação | 22,1 |
  | Esperando conclusão do SDMA | 19,1 ms |
  | FLUSH_TLB | 107,9 ms |
  | Resto (VidMm) | 330 ms |

  Igual à 158: tirar a caminhada não mudou o quadro.

**4. Veredito contra o doc 513:** **B.** A leitura concorrente das tabelas pela CPU **não** é o gatilho. A mudança fica: não houve fault, e ela reduz tráfego sem perda medida.

**5. Recuperação:**
- O fusível vai recusar o StartDevice.
- Recuperação, `-Reset` e boot frio com o monitor ligado.
- **NÃO VALIDADO EM HARDWARE:** a causa da trava.

## 2. Migalhas (reset rápido)
- Última da máquina: CPU00 `SDMA_REG_READ 0xFC4` (leitura de confirmação da invalidação do HDP), concluída (seq 244.513).
- CPU13 `PAGING_DDI` op **0x0C (FLUSH_TLB)**, fase ANTES, seq 244.508.
- Na 158, a mesma posição tinha op 0x0B (UPDATE_PAGE_TABLE).

A thread de paginação estava **dentro de uma DDI de paginação** nas duas travas nítidas. Como ela passa grande parte do tempo lá dentro (o FLUSH_TLB soma ~108 ms por quadro de 613 ms), isso pesa pouco como discriminante (P0 no fato, H na leitura).

## 3. O que já foi descartado com medição

| Hipótese | Ensaio | Resultado |
|---|---|---|
| Toggle de `HDP_MISC` | 158 (DX.96-HDP) | trava igual |
| Leitura de tabelas de página pela CPU/BAR | 159 (DX.97, −97%) | trava igual |
| Mapeamento além da BAR | log | nunca rodou |
| Segmento 2 como endereço cru | log | nenhuma PTE Seg=2 |
| Mensagens ao SMU | código e log | o driver não manda |
| Troca do VMID8 com IB em voo | DX.95 | corrigido; não era a trava dura |

## 4. Próximos passos

**A. Ensaio de plataforma sem código (recomendado primeiro, pelo custo):** desligar os estados de baixo consumo do processador pelo Windows, com o mesmo pacote 3.308.1.0. A BIOS não oferece essa opção. É uma mudança de configuração do sistema, então o Daniel executa e depois reverte:
- `powercfg -setacvalueindex scheme_current sub_processor idledisable 1`
- `powercfg -setactive scheme_current`
- reverter: o mesmo comando com `0`.

Critério:
- **600 s sem trava**, duas vezes: os estados de baixo consumo da plataforma são o gatilho;
- **trava igual:** não são, e resta o padrão de tráfego (DX.98).

**B. DX.98 (tabelas de página escritas pela GPU):** implementar em paralelo, offline. O desenho está no doc 514. **Bloqueio da Lei 7:** o pacote SRBM_WRITE do SDMA e a sequência de invalidação no anel precisam de fonte (upstream `sdma_v5_0.c`, `navi10_sdma_pkt_open.h`) antes de qualquer código.
