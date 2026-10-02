# 499 — Corrida 152: sem 0x10E, DWM a 2× Presents/s, e trava dura ~1 s depois de o harness abrir

> Corrida de 28/09/2026. Bind às 09:58:34, candidato 3.302.1.0 (FPS.1 + SDMA.4M), sem KD, harness atrasado 150 s.
>
> Evidência:
> - pasta `docs/evidence/2026-09-28_09-58-34-DESKTOP-HQAG6T4-lab-binding/`;
> - cauda recuperada `bc250_display_log-tail-recovered.txt`, 62.035.874 bytes, SHA-256 `D822AC12…`, que inclui o boot seguinte a partir da linha ~519.250;
> - logs da UMD do `%TEMP%` do usuário em `umd-logs-pos-travamento/`: harness 9464, Explorer 6148 e taskmgr 4212.
>
> O log do `dwm.exe` (pid 1284) foi copiado depois pelo Daniel. A última linha dele é às 10:01:37.322, em desenho normal (`PsSetSamplers`): o DWM seguia vivo depois do último registro do Explorer.

## 1. Relato (cinco pontos)

**1. Houve avanço?** Parcial. Não houve 0x10E e o desktop ficou mais rápido (P0). A trava dura voltou.

**2. Sucessos (P0):**
- **SDMA.4M ativo no hardware:** os 315 fills de 4 MiB saíram em **dois pacotes** (`DW=10`). Na 151 os 97 saíam em um (`DW=5`).
- Zero `Blt Timeout`, zero BugCheck, zero TDR e zero `GFXHUB_FAULT` nos ~173 s. A 151 morreu aos 49 s.
- **FLUSH_TLB:** 83.067 chamadas, mediana **0,64 ms**, p99 **1,37 ms**; o efeito da FPS.1 se repetiu.
- **Presents do DWM** por segundo (contagem de `[Present #` no KMD):

  | Janela | 150 | 152 |
  |---|---|---|
  | 30–90 s | 3,08 | **6,48** |
  | 90–150 s | 3,47 | **6,02** |

  Isso é cerca de **2×**. É taxa de apresentação do desktop, não FPS do harness.

**3. Falhas (P0):**
- **Trava dura**, com reboot por Kernel-Power 41 às 10:02:50 e **sem dump**: o `MEMORY.DMP` ainda é o da 151, das 09:16.
- **O harness carregou a UMD às 10:01:36.133 e a máquina congelou ~1,2 s depois.** O último registro do harness foi às 10:01:37.073 e o do Explorer às 10:01:37.296. Os dois param **logo depois de `Image MemoryRequirements`**, isto é, ao entrar na alocação de memória da RADV.
  - Para o Explorer esse é o evento mais frequente do fim do log (871 vezes), então a coincidência pode ser só frequência (H).
- **Explorer:** 110 `vkAllocateMemory (Image) falhou: res=-2`, a partir das 10:01:17, subindo até a trava. No KMD, o journal de GPUVA está **cheio**: `Cap=1024 Mapped=1024 Livres=0`, com 26.195 `PUBLICACAO_FALHA` e 309 `NO_SPACE`.
- **FPS do harness NÃO MEDIDO:** ele não passou da criação do primeiro recurso.

**4. Veredito contra o doc 498:** **D**, trava sem `IB_ESTADO`. A falha não é o timeout do blt. É a trava dura da classe 139/140/143 ("abrir app de GPU novo com o desktop crescido"). Com isso:
- a trava aparece em **1 de 1** corrida com FPS.1 que chegou ao harness;
- as 149 e 150, com M1 e sem FPS.1, abriram o harness sem trava.

**5. Recuperação e pendências:**
- Depois do reboot o fusível recusou o StartDevice (`State=2`), como previsto, e o dispositivo ficou fora.
- Antes do próximo bind: `inspect-boot-fuse.ps1 -Reset` e boot frio.
- **NÃO VALIDADO EM HARDWARE:**
  - a causa da trava;
  - se o limite da Mesa resolve o 0x10E (uma corrida limpa de 173 s não prova);
  - o FPS do harness com a FPS.1.

## 2. O que a instrumentação disse sobre a trava

- **Batimento do GPU (BGH1), último registro:**

  | Campo | Valor |
  |---|---|
  | `GRBM_STATUS` | 0xA0003028 (ocioso) |
  | `CP_STAT` | 0 |
  | Anel gráfico | drenado |
  | Fence | 0x514 = última submetida |

  **A GPU estava ociosa.**
- **Rastro de paginação:** `active_count=0`, sem DDI em curso. O último registro é ~0,15 s anterior à última linha do Explorer: a máquina parou de uma vez.
- **Migalhas MMIO:** a última só tem um estágio transitório (`SUBMIT_DDI ENTRADA`). 73% dos registros da corrida têm alguma migalha em voo, e nenhuma persiste. **Nenhum acesso nosso ficou pendurado.**

Tudo repete o quadro da 143 (doc 484): a GPU ociosa, o MMIO respondendo e a trava do lado da CPU/SO. A nossa instrumentação não nomeia o ponto.

## 3. Próximo passo proposto: DX.94, teto de GPUVA de volta a 4.096 (decisão do Daniel)

**Correção (depois da primeira versão deste doc).** A origem do -2 do Explorer **não** está identificada:
- `Bc250McdmBuildPagingBuffer` converte a falha do FLUSH_TLB em `STATUS_SUCCESS` antes de devolvê-la ao VidMm (P1, `bc250_mcdm_callbacks.c`, caso `DXGK_OPERATION_FLUSH_TLB`), então a falha de publicação não chega ao VidMm por esse caminho;
- as primeiras `PUBLICACAO_FALHA` aparecem aos 7,5 s e o primeiro -2 aos ~160 s: não há correlação temporal direta.

O efeito comprovado do esgotamento é outro: **a vista de software perde mapeamentos que a GPU tem** (o journal é abortado com as PTEs já escritas).

**Por quê.** O estado em que as travas acontecem inclui o espaço de GPUVA esgotado. O esgotamento, sozinho, não basta para travar: a 141 teve 460 falhas e a 150 também estava no teto, sem trava. Mas o esgotamento é um defeito de produto em si: são texturas do Explorer que não existem.

**Histórico do teto.**
- A DX.85 voltou o teto para 1.024 porque 128/129, as primeiras com 4.096, travaram no FLUSH_TLB. A hipótese era o custo do commit.
- O doc 467 mandava reabrir a capacidade só depois de medir e cortar esse custo. A FPS.1 fez isso.

**Benchmark offline** (mesmo harness da FPS.1, código atual):

| Teto | N | µs por substituição |
|---|---|---|
| 1024 | 1000 | 18,8 |
| 4096 | 1000 | 85,5 |
| 4096 | 4000 | 122,8 |

Mesmo no pior caso isso é **~14× mais barato** que o commit que rodou 600 s estável na 150 (1.770 µs) e ~57× mais barato que o das 128/129. O custo fixo agora é o tamanho do espaço (zerar e copiar), não o N.

**Mudança.** Somente `BC250_GPUVA_SPACE_MAX_RANGES` 1024 → 4096. O `BC250_GPUVA_JOURNAL_MAX_CHANGES` continua em 1024: o lote máximo medido é 129.

**Teste que fecha:** o critério do [doc 500](500_DX94_TETO_DE_GPUVA_4096_E_CRITERIO_CORRIDA153.md). A trava entra como sinal observado, não como critério de sucesso da DX.94.
