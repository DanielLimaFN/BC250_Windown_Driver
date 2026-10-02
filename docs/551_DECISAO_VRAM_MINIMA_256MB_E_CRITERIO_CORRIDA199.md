# 551 — Decisão: VRAM mínima (256 MB), sem VRAM grande como muleta, e critério da Corrida 199

> **Data:** 01/10/2026, depois da Corrida 198.
> **Decisão do Daniel:** *"o ideal seria deixar em 256mb apenas né e fazer tudo funcionar com o mínimo e não usar GB de VRAM como muleta"*.
> **Antecedentes:** [doc 548](548_M2_SEGMENTO3_VRAM_ALEM_DO_BAR_DINAMICO_E_CRITERIO_CORRIDA194.md) (M2), [doc 549](549_COLETA_LINUX_CMOS_APCB_CONTROLA_A_VRAM_E_AMDGPU_SEM_IP_DISCOVERY.md) (CMOS), [doc 550](550_CONTROLE_DA_VRAM_PELA_CMOS_NO_WINDOWS.md) (ferramenta), [doc 496](496_M1B_BLOQUEIOS_E_ROTA_REBAR_MAIS_M2.md) (M1b e BAR).

---

## 1. Corrida 198 (01/10 14:31, 3.344.1.0, CMOS em 4096 MB) — o M2 completo funcionou, e a trava dura voltou

- **Pasta:** `docs/evidence/2026-10-01_14-31-29-DESKTOP-HQAG6T4-lab-binding/` (cauda do KMD recuperada, 17.458.815 bytes; logs de AppContainer em `umd-logs-appcontainer/`).
- **Relato do Daniel:** tela preta, precisou reiniciar.
- **Event Log (P0):** desligamento inesperado (ID 41), **sem BugCheck**; boot seguinte às 14:35:41.

| Fato (P0) | Fonte |
|---|---|
| `[M2_SEGMENTO3] ... 3824 MiB status=ok` e `alocacoes no segmento 3: LIGADAS` | KMD |
| **2.086 imagens da RADV** com `SegSet=0x5 Preferido=3` (só 24 linhas `[M2_SEGMENTO3_ALOC]`, por amostragem) | `DX93_ALLOC_MAP` |
| Zero `GFXHUB_FAULT`, zero TDR, zero `IB_TRIPOD TIMEOUT` em ~212 s | KMD |
| 1.844 Presents; composições com dados conferidos (`COMPOSITE_DATA_PROOF SAMPLED_PASS 5/5`) até o fim | KMD |
| Zero `OUT_OF_DEVICE_MEMORY` / `vkAllocateMemory -2` e zero `Lock2 failed` nos `-RADV` coletados | UMD |
| DWM sem laço de recriação (8 `CreateDevice`, 0 `DestroyDevice`) | KMD |
| Fim do log no meio de atividade normal (submit gráfico aposentado), sem erro nosso | KMD |
| `TRANSFER Dir=0` (despejo): 995 em ~212 s ≈ 4,7/s, contra ≈ 5,7/s na 197 | KMD |

**Veredito frente ao doc 548 §11:**
- Os sinais de A apareceram: alocações no segmento 3, zero fault, zero falta de memória nos logs coletados, DWM estável.
- **Ressalva (Lei 10.3):** os logs `-RADV` da pasta são só os retratos iniciais, e a coleta final não rodou. A ausência de falta de memória está provada só nesse trecho.
- **Tela preta e reset = a trava dura (P-1 do plano)**, com a mesma assinatura de sempre: log cortado no meio de atividade normal, sem BugCheck.
- **Não sei quando a tela ficou preta.** Se foi no começo, há um defeito visual do M2 que o log não mostra (desfecho B, texturas no segmento 3). Se foi no fim, é só a trava. Lacuna declarada.

---

## 2. A decisão: 256 MB e "funcionar com o mínimo"

### 2.1 Por que faz sentido técnico (não é só preferência)

1. **Na BC-250 a "VRAM" e a RAM do Windows são a mesma GDDR6** (APU, 16 GB num só barramento). A UMA da CMOS só decide quanto fica reservado para a GPU e quanto o Windows vê. Memória "do sistema" usada pela GPU pela abertura (segmento 2) não fica num chip mais lento: é o mesmo chip.
2. **É assim que o Linux faz numa APU:** o `amdgpu` usa uma UMA pequena e põe a maior parte em GTT (memória do sistema mapeada para a GPU). O nosso equivalente é a **M1b** (doc 496).
3. **O BAR é de 256 MB e a BIOS não oferece Resizable BAR** (doc 496 §5, P0). Com UMA de 256, **toda a VRAM fica visível à CPU**:
   - o segmento 3 deixa de existir (o driver percebe sozinho);
   - a regra do `Lock2` para VRAM sem CPU (docs 548 §8–§9) deixa de se aplicar.
4. **VRAM grande esconde defeitos de memória.** Os problemas abertos são justamente de paginação e residência: o `0x34` da 197 é um `Lock` disputando com um TRANSFER, e a M1b nunca foi resolvida. Com 256 MB eles aparecem cedo e sempre, e podem ser corrigidos. Com 4 GB aparecem raramente e parecem sorte.
5. **O produto não pode exigir que o usuário mude a CMOS.** O padrão de fábrica é 512; um driver que só funciona com 4 GB não serve.

### 2.2 O que muda no plano

| Item | Antes | Agora |
|---|---|---|
| UMA de trabalho | 4096 MB | **256 MB** (e o driver tem de funcionar com qualquer valor, inclusive 512 de fábrica) |
| M2 (segmento 3 além do BAR) | Frente ativa | **Dormente.** O código fica (é dinâmico e desliga sozinho com 256), com o resultado da 198 registrado. Não é mais o caminho para ter memória |
| M2.1 (abertura no `Lock2`) | Correção ativa | Dormente junto (só age quando há segmento 3) |
| Memória para a carga real | VRAM maior | **M1b: memória do sistema pelo segmento 2**, como o GTT do Linux |
| Ferramenta `bc250-vram` | Faixa 512..8192 | **256..8192** (piso = BAR; nunca medido) |

### 2.3 A frente de memória daqui em diante (M1b), em ordem

Os três bloqueios do doc 496 §1, mais o que as corridas 197/198 mostraram:

1. **A RADV passa o domínio (VRAM/GTT) no blob de alocação** e o KMD o honra: GTT vira `SegSet` com o segmento 2.
2. **Gate e inspeção do IB gráfico aceitando páginas de sistema** (hoje leem o IB pela VRAM).
3. **Composição a partir de memória do sistema:** cópia por GPUVA em vez de endereço MC contíguo.
4. **Tipo de memória explícito** para o buffer linear do Present.
5. **Robustez de paginação**, que com 256 MB vira o caso normal:
   - o `0x34` (Lock da UMD durante um TRANSFER da mesma alocação, 175 e 197);
   - os `Upload LOCK falhou 0x80070057`;
   - `Offer/Reclaim` de verdade.

**Gate da frente:** desktop com Explorer, menu e Pesquisa por 600 s com UMA de 256 MB, sem falta de memória, sem laço de device-lost e sem `0x10E`.

---

## 3. Corrida 199 — critério (escrito antes, Lei 10)

> **Substituído em 01/10 pelo [doc 553 §3](553_INSTRUMENTACAO_PLATAFORMA_TCTL_SDMA1_RESET_E_CRITERIO_CORRIDA199.md):** o candidato passou a ser o 3.345.1.0 (este pacote + instrumentação que só mede). Os valores esperados abaixo continuam valendo.

**Pacote:** o **mesmo 3.344.1.0** da 198 (SYS `FAF7BD3E…`, INF `E31053EE…`, CAT `E76567D6…`, UMD `5CEC87A9…`, RADV `5447A17D…`). Nenhum código de driver mudou: com 256 MB o driver deriva tudo da placa.

**Única variável:** `UMA_SIZE` 4096 → **256**.

**Preparação (Daniel):**
1. `tools\BC250_RECOVERY.cmd` elevado e `tools\inspect-boot-fuse.ps1 -Reset`.
2. `tools\vram-cmos\bc250-vram.ps1 -SetUma 256 -Confirmo`.
3. **Desligar da tomada** e ligar.
4. `tools\vram-cmos\bc250-vram.ps1 -Ler`: precisa mostrar `ABL_SIGNATURE` e `UMA_SIZE=0256`.
5. `tools\EXECUTAR_CORRIDA199_UMA256_600S.cmd` (já relê a CMOS antes do bind).

**Risco declarado:** 256 MB nunca foi medido nesta placa. Se o firmware recusar, a releitura mostra uma assinatura de erro ou outro valor, e a corrida não deve seguir. Se a placa não der imagem no boot, limpar a CMOS (jumper ou bateria) volta ao padrão de fábrica.

**Valores esperados (escritos antes):**

| Linha | Esperado com 256 MB |
|---|---|
| `bc250-vram -Ler` | `ABL_SIGNATURE`, `UMA_SIZE=0256` |
| RAM do Windows | ≈ 16.618.168.320 bytes (12.591.636.480 + 3.840 MiB liberados) |
| `[M2_BASE]` | `0x460000000` (topo `0x470000000` − 256 MiB), `DIFERENTE -- usando a lida` |
| `[M2_SEGMENTO3] VramTotalMiB / CpuVisibleMiB` | 256 / 256, `status=sem_vram_alem_da_janela_de_cpu` |
| `[McdmQAI] QUERYSEGMENT4` | **2** segmentos |
| `[M2_SEGMENTO3_ALOC]` e `[M2_LOCK_ABERTURA]` | **zero** (dormentes por construção) |

**Desfechos:**

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A — o mínimo sobe** | Todos os valores acima **e** desktop visível pela GPU **e** DWM sem laço de recriação | O driver funciona com 256 MB. A memória vira a frente M1b (§2.3), e a falta de memória medida aqui é o ponto de partida dela |
| **B — firmware recusou 256** | `-Ler` com assinatura de erro ou `UMA_SIZE` diferente de 256 | Não rodar. Voltar a 512 (fábrica) como mínimo de trabalho e registrar |
| **C — driver não se adaptou** | Código 43, `RuntimeCapsStatus` ≠ 0 ou segmento 1 recusado no bind | Defeito nosso na derivação com 256 (reservas internas dentro do BAR); a DX.67 deveria cobrir isso |
| **D — falta de memória dominante** | `vkAllocateMemory -2` / `OUT_OF_DEVICE_MEMORY` em massa, ou DWM em laço de recriação | Esperado e útil: é a medida inicial da M1b, não regressão |
| **E — fora do mapa** | Qualquer outra coisa (trava dura inclusive) | Descrever antes de julgar; a trava segue na Fase 1 |

Lei 10.3: A exige todos os sinais.

**NÃO VALIDADO EM HARDWARE.**
