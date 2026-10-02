# 491 — Desenho: memória unificada (segmento de sistema para recursos) e VRAM além da janela do BAR

> 27/09/2026, offline. Pedido do Daniel depois da Corrida 148, em que a trava veio ao abrir o navegador, sem harness, com N=918 e a GPU ociosa.
> **Por que agora:**
> - todas as travas vêm com a memória sob pressão;
> - a 146 mediu 7.340 despejos VRAM→sistema (~2,46 GB) em 600 s;
> - a corrupção da 145 veio com 216/256 MB de memória dedicada em uso.
>
> Mais memória é necessária ao produto e é a hipótese mais forte para a trava (H).

## 0. Resultado da Corrida 148 (registro)
- **Candidato:** DX.92 / 3.298.1.0, sem KD. Evidência: `docs/evidence/2026-09-27_23-20-51-DESKTOP-HQAG6T4-lab-binding`.
- **Trava no tick ~978 (~98 s), antes do harness (~180 s),** ao abrir o navegador depois do Gerenciador de Tarefas.
  - O último BGH1 é DEPOIS completo, com a GPU ociosa.
  - O último estágio é o commit do journal (44), com N=918.
  - BCR1 mostra só `PAGING_DDI` em voo, na CPU 11; **nenhuma submissão em voo**.
  - O Ctrl+Scroll Lock não funcionou.
- **Veredito frente ao doc 490: C.** A submissão em voo da 147 foi coincidência. Qualquer app gráfico novo com o sistema carregado dispara a trava; o harness não é necessário.

## 1. Fatos medidos e lidos
| Fato | Fonte | Confiança |
|---|---|---|
| A BC-250 é APU: a "VRAM" é um pedaço da GDDR6 compartilhada (`UMA_SIZE` na BIOS); a RAM do sistema é a mesma memória física | plataforma, doc 474 | P1 |
| Segmento 1 (local, visível à CPU) = `[0xA8C000, 0xFA00000)` ≈ 245 MB | `[DX67_VRAM_MAPA]` na 146 | P0 |
| `[0x10000000, +256 MB)` papel 7 **não é firmware**: é `TotalBytes − CpuVisibleBytes`, reservado só por estar fora da janela do BAR | `bc250_active_miniport.c:8811` | P1 |
| TMR (papel 5), firmwares e MQD ficam dentro dos 256 MB | `[DX67_VRAM_MAPA]` | P0 |
| Segmento 2 = abertura de 2 GB (`Flags.Aperture`, base lógica 0x100000000), declarado desde o início | `bc250_mcdm_callbacks.c` QUERYSEGMENT4 | P1 |
| **Os DMA buffers do dxgkrnl já moram no segmento 2** (`DmaBufferSegmentSet = 1<<1`) **e o SDMA os executa por PTE de sistema**, com tripé completo (07/09 17:43 e 18:16) | `bc250_active_miniport.c:4483`, docs 182/183 | **P0**: a GPU já lê RAM do sistema pelo nosso caminho |
| O codificador de PTE trata os segmentos 0/2 como endereço físico de sistema com SYSTEM\|SNOOPED | `bc250_mcdm_callbacks.c:645` | P1 |
| As tabelas de página ficam no segmento 1 (`PageTableSegmentId = PagingProcessPageTableSegmentId = 1`) | `bc250_mcdm_callbacks.c:2773` | P1 |
| **Os recursos só podem morar no segmento 1** (`AllocationSegmentSet = 1`) | `bc250_active_miniport.c:8590/10280`; DX.59 `SegSet=0x1` | P0 |
| 28 pontos do KMD assumem segmento 1: resolvedor de Present, `COMPOSITE_BLT` (`Bc250SegmentOffsetToMc`), primária/scanout, provas por amostra de VRAM pela CPU, raiz de página | grep | P1 |

## 2. Item 1 — memória do sistema para recursos (segmento 2)
**Ideia:** recursos que o KMD não precisa tocar por endereço físico passam a aceitar os segmentos 1 **e** 2 (`SupportedRead/WriteSegmentSet = 0x3`, preferência pelo 1). Quando a VRAM enche, o VidMm os coloca na RAM do sistema, que é a mesma GDDR6, em vez de despejar em cadeia.

**O que não pode ir para o segmento 2 nesta fase:**
- primária/scanout (o DCN lê por endereço físico);
- origens e destinos das cópias do KMD (`COMPOSITE_BLT` e Present, que endereçam VRAM por MC);
- a textura compartilhada do DWM.

**Critério de classe (UMD → KMD):** a UMD marca no blob privado os recursos "só GPU por VA", isto é, texturas só de SRV (os 238 gradientes 1024x1 do Explorer), vertex/index/constant buffers e staging. O KMD dá `0x3` só a esses. Os demais ficam com `0x1`. Isso respeita a Lei 4: nenhum caminho do KMD passa a ler memória que não sabe endereçar.

**Riscos:**
1. Algum caminho do KMD resolver um desses recursos. O resolvedor devolve `Seg=2`, e as cópias recusam, com log nomeado, pela DX.91 e por `Bc250SegmentOffsetToMc`.
2. Coerência: as PTEs levam SNOOPED.
3. A RADV (`vkAllocateMemory`) tem caminho próprio de alocação e não é coberta nesta fase.

## 3. Item 2 — VRAM além do BAR (segmento 3, local, não visível à CPU)
**Ideia:** declarar `[0x10000000, TotalBytes − reserva_topo)` como **segmento 3**, local e sem `CpuVisible`. Com 512 MB na BIOS, isso dá ~240 MB a mais e **dobra a VRAM útil**. Aí aumentar `UMA_SIZE` na BIOS passa a trazer ganho.

**Mudanças:**
- QUERYSEGMENT4 com 3 segmentos. O 3 vem depois da abertura, então os IDs 1 e 2 não mudam.
- Codificador de PTE para o segmento 3 (base própria no MC).
- `Bc250SegmentOffsetToMc` e resolvedor aceitam o segmento 3. O SDMA endereça toda a VRAM por MC, então as cópias do KMD funcionam.
- Toda leitura da CPU por BAR (provas por amostra, caminhada) recusa ou pula o segmento 3 com log.
- Contrato de objetos/journal: `SegmentId ∈ {0,1}` passa a aceitar 3.
- Recursos com `CpuVisible=0` passam a aceitar `0x1|0x4`.

**Reserva do topo:** a tabela de IP discovery fica em `TotalBytes − 64 KB` no desenho do amdgpu (P1). Reservar o topo de forma conservadora (1 MB) até medir.

**Riscos:**
1. Firmware (PSP/SMU) usando o topo sem estar no inventário.
2. Algum caminho CPU esquecido lendo o segmento 3, o que dá leitura fora do BAR.
3. A regra dos 256 MB do segmento 1 visível não pode quebrar.

## 4. Ordem proposta e testes
| Fase | Conteúdo | Teste que fecha (Lei 3) | Evidência |
|---|---|---|---|
| **M1** | Item 1 só para recursos "só GPU por VA" (UMD marca, KMD dá `0x3`) | Cenário da 148 (desktop + Gerenciador de Tarefas + navegador) com `DX91_PAGING_MOVE` mostrando alocações residentes no segmento 2, **despejos `Dir=0` muito abaixo de 7.340/600 s**, zero `RANGE_SCAN RECUSADO` de Present/composição, desktop correto | pasta da corrida + contagem de `DX91_PAGING_MOVE` |
| **M2** | Item 2 (segmento 3) | Mesmo cenário, com recursos no segmento 3, zero leitura de CPU no segmento 3 (log), `BYTE_PROOF` do scanout ainda PASS | idem |
| **M3** | RADV (`vkAllocateMemory`) no segmento 2/3, `UMA_SIZE` maior na BIOS | A definir depois de M1/M2 | — |

**Critério de abandono:** se M1 ou M2 produzir corrupção ou falha nova atribuível, reverter a fase e isolar antes de seguir. A trava é medida em todas as fases, pelo mesmo cenário da 148.

**NADA DISTO FOI IMPLEMENTADO NEM VALIDADO EM HARDWARE.**
