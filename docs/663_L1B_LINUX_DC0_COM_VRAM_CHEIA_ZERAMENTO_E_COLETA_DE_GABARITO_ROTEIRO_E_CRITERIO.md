# 663 — L1b: Linux com `amdgpu.dc=0` e a VRAM cheia, mais a coleta de gabarito (roteiro e critério)

**Data:** 08/10/2026. **Natureza:** referência fora do nosso driver; não conta como corrida. **NÃO EXECUTADO.**
**Antecedente:** doc 662 (L1, desfecho A, com a ressalva de que a VRAM ficou em 22 MB de mediana).

## 1. A pergunta e o que mais se aproveita

**Pergunta principal:** com o DCN herdado do GOP **e** a VRAM quase cheia, com alocações novas de tela cheia zeradas pelo SDMA (a assinatura das nossas travas), o Linux trava?

**Aproveitando a ida ao Linux (itens do radar ainda não priorizados), tudo só leitura:**

| Item do radar | O que a L1b coleta | Onde fica |
|---|---|---|
| EDID real (item 77, pendência do doc 552) | binário do EDID de cada conector com o `amdgpu` completo (dc=1), mais `edid-decode` | `referencia-dc1-*/edid/` |
| Como o `amdgpu` programa o DCN (P9) | `amdgpu_dm_dtn_log` (estado de HUBP/DPP/MPC/OTG/clocks), `state` do DRM (modo com o pixel clock real), `link_settings`, `output_bpc`, `vrr_range`, `psr_*` do DP | `referencia-dc1-*/debugfs/` |
| Refresh real (achado lateral do doc 662: 60 contra 56,13 Hz) | contador de quadros do OTG0 contra o relógio monotônico, 20 s, em dc=1 e em dc=0 (antes e depois da carga) | `otg-refresh-*.json` |
| Paridade de registradores com o `amdgpu` (doc 593 §4) | **435 registradores** com proveniência (`tools/linux/bc250-l1b-regs.txt`, gerado de `inc/linux_ref`): GRBM/CP/RLC, SDMA0/1, GCVM (L2, faults, os 16 contextos), GCMC/MMMC (aberturas FB/AGP/sistema), MMHUB, IH, doorbells do BIF, os 34 golden settings e o DCN | `regs-*.tsv` / `regs-periodico.jsonl` |
| Estado do MGCG por bloco (657 H1) | todos os `CGTT_*`, `RLC_CGTT_MGCG_OVERRIDE`, `RLC_CGCG_CGLS_CTRL`, `SDMA*_CLK_CTRL` e `MMEA0_*CGTT*` | idem |
| SMU (657 H2) | `gpu_metrics` (binário do SMU) e hwmon (sclk, VDDGFX, VDDNB, potência) a cada 2 s, só na sessão 2; uma foto em dc=1 | `gpu-metrics.txt`, `smu-hwmon.txt` |
| Interrupções (comparar com o nosso IH) | linha `amdgpu` de `/proc/interrupts` a cada 2 s | `interrupcoes.txt` |
| Ocupação de memória | `amdgpu_vram_mm` e `amdgpu_gem_info` a cada 60 s | `memoria-periodica.txt` |

**Separação de variáveis:**
- A **sessão 1** é "limpa": não lê registrador nem fala com o SMU em regime, só no começo e no fim. Ela responde a pergunta principal sem ator novo.
- A **sessão 2** liga a varredura a cada 10 s e o `gpu_metrics` a cada 2 s. Se só a sessão 2 travar, o suspeito é a leitura, não o DCN.

## 2. Carga

- Igual à L1 (weston headless GL 2560×1080, vkcube, glmark2, agora em 2560×1080, e `amdgpu_evict_vram` a cada 2 s a partir de 120 s).
- **Novo: `bc250_vram_stress`** (`tools/linux/bc250_vram_stress.c`, Vulkan, compilado no `preparar`):
  - enche a VRAM com buffers de tela cheia (11.059.200 bytes) até **210 MB**;
  - em laço, libera e realoca 2 por ciclo;
  - com `RADV_DEBUG=zerovram`, o `amdgpu` zera cada alocação nova pelo SDMA (`VRAM_CLEARED`), o análogo do nosso FILL de zeramento;
  - faz `vkCmdFillBuffer` + cópia de tela cheia na fila gráfica e cópia na fila de transferência (SDMA), se a RADV a expuser (`RADV_PERFTEST=transfer_queue`);
  - espera cada fence com teto de 5 s e registra `TIMEOUT` se estourar;
  - escreve uma linha por segundo.
- Checagem offline: `cl /Zs /W4` limpo contra os headers do Vulkan da árvore Mesa. **Não compilado nem rodado no Linux ainda.**

## 3. Roteiro (Daniel)

No Fedora normal, com rede e com o monitor ligado ao DP da placa (para o EDID):

```bash
sudo bash /caminho/do/pendrive/linux/bc250-l1-auto.sh preparar --perfil l1b
```

1. O `preparar`:
   - instala `gcc`, `vulkan-headers`, `vulkan-loader-devel`, `python3` e `edid-decode` se faltarem;
   - compila o estresse e roda um teste de 5 s com 64 MB;
   - move as sessões da L1 para `anteriores-*`;
   - **faz a coleta de referência com o `amdgpu` completo** (este boot, dc=1);
   - cria a entrada do GRUB e arma as 2 sessões;
   - desliga a máquina.
2. **Ligue.** A sessão 1 (limpa) roda sozinha e **desliga sozinha** no fim (~12 min).
3. **Ligue.** Sessão 2, com a varredura, idem.
4. **Ligue.** Boot normal: `~/BC250_L1_RESULTADO.txt` e `~/bc250-l1-resultado-*.tar.gz`.
5. Se ligou e não desligou em ~15 min: travou. Anote a hora e dê Reset; o boot seguinte coleta sozinho.

## 4. Critério (escrito antes, Lei 10)

**Pré-condição de cada sessão** (todas, senão a sessão é inválida):
- `dc_lido=0`;
- weston com GL (radeonsi);
- `CARGA_NA_GPU OK`;
- `VRAM_STRESS vivo` com `CHEIO buffers ≥ 15`;
- **VRAM acima de 200 MB em pelo menos metade das amostras** (é o que a L1 não teve);
- ≥ 150 despejos de VRAM;
- fence do GFX andando até o fim.

| Desfecho | Assinatura (o que o disco vai mostrar) | Implicação |
|---|---|---|
| **A** | As duas sessões com `RESUMO.txt`, sem `TIMEOUT` no estresse, sem timeout de anel, reset ou MCE no dmesg | DCN herdado + VRAM cheia + zeramento de tela cheia **não travam** o Linux: o DCN sai de vez, e a varredura também não é perigosa. Próximo: comparar offline os 435 registradores do Linux com os nossos (as fotos e snapshots que o driver já registra) e levar as diferenças à paridade |
| **B1** | A sessão 1 (limpa) não termina: o batimento para e a máquina não desliga | Com DCN herdado e VRAM cheia **até o Linux trava**: o mecanismo provável é o scanout do GOP sobre memória cheia/reutilizada. Próximo: o controle `controle` com a mesma carga l1b (dc=1); se o controle não travar, a frente passa a ser o DCN básico (P9) ou parar/reservar o scanout |
| **B2** | A sessão 1 termina e a sessão 2 (com varredura) não termina | A leitura de registrador ou o `gpu_metrics` em regime é suspeita, não o DCN. Antes de usar a varredura de novo, repetir a sessão 2 sem o `gpu_metrics` |
| **C** | O estresse não compila ou não roda, ou a VRAM não passa de 200 MB na metade das amostras | Ensaio inválido nessa forma; descrever o motivo (ex.: o `amdgpu` despeja para GTT antes de encher) antes de mudar a carga |
| **D** | Sem trava, mas com `TIMEOUT` no estresse, timeout de anel ou reset recuperado | Anotar o anel e o momento e comparar com os nossos TDR antes de julgar |
| **E** | Fora do mapa | Descrever o observado antes de julgar |

**Desfecho parcial não conta** (regra 3 da Lei 10). **Teto:** 2 sessões, mais 1 de controle só em B1.

## 5. Limites

- A varredura lê por MMIO pelo debugfs, como o `umr`. É leitura, mas é ator novo em regime, por isso fica só na sessão 2.
- Ficaram de fora as portas de dados e índice (`*_DATA`, `*_IND_*`, `*_INDEX`, FIFOs, contadores de desempenho).
- O `gpu_metrics` pede a tabela ao SMU: é mensagem ao SMU, também só na sessão 2.
- O `amdgpu_dm_dtn_log` só existe com dc=1, por isso é lido no boot do `preparar`.
- O gabarito de registradores sai em três estados: desktop KDE (dc=1), dc=0 antes da carga e dc=0 depois da carga. Comparar com o nosso driver exige o mesmo estado; a diferença de estado precisa ser declarada na análise.

## 6. Resultado (08/10, relato do Daniel)

Ao rodar, a tela ficou **verde** no Linux. O ensaio foi **abandonado por decisão do Daniel**: a frente Linux fica encerrada aqui.

- Não há relato de em que passo a tela ficou verde nem pasta de evidência.
- Hipótese (H, sem prova): o mais provável é a coleta de referência do `preparar`, que roda com o display ativo (dc=1) e lê registradores pela BAR (inclusive os do DCN) e o `amdgpu_dm_dtn_log`. Isso mostraria que até leitura no DCN/em blocos vizinhos com o display vivo não é inócua.
- Nenhuma conclusão sobre DCN + VRAM cheia sai daqui. A ressalva do doc 662 continua aberta.

**NÃO EXECUTADO ATÉ O FIM. Nada aqui foi validado em hardware.**
