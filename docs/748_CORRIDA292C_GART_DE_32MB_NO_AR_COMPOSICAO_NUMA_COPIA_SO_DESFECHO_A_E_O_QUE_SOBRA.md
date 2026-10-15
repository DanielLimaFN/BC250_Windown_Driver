# Doc 748 — Corrida 292c: GART de 32 MB no ar, composição numa cópia só (desfecho A) e o que sobra

> **Data:** 14/10/2026, bind às 09:17:33, rollback online (`RECOVERY_OK_ONLINE`, sem reboot).
> **Candidato:** lab **3.444.1.0** (SYS `218CFF90`), [doc 747](747_CORRIDA292B_ALOCADOR_DO_BOOTSTRAP_AINDA_NAO_EXISTIA_NA_D2_COMPVA3C_PACOTE_3444_E_CRITERIO_CORRIDA292C.md).
> **Evidência:** `docs/evidence/2026-10-14_09-17-33-DESKTOP-HQAG6T4-lab-binding/`.
> **Observado pelo Daniel:** desktop de volta; ainda há piscadas, e o menu demora um pouco para abrir.

## 1. Critério do doc 747: desfecho A (P0)

| Sinal exigido | Medido |
|---|---|
| `TABELA RESERVADA (… alocador da MMU global, 1 tentativas)` | sim, em `0xF3F0000` |
| `ESPELHO OK` | sim (alocador do bootstrap com `total=0x10000000`) |
| `CONTEXTO0 … END=0x1FFF` | sim, readback=1 e rollback 0/0 |
| `COMPOSITE_BLT OK VIA_VA … pedacos=1` | 5.376 composições, **todas** com `pedacos=1`; zero com 2 ou mais |
| `reuso` > 0 | 4.861 reusos e 537 remapeamentos (90% sem reescrever PTE) |
| zero `VIA_VA RECUSADO` | 0, e zero "janela ocupada" |
| zero TDR | 0 TDR, 0 fault, 0 BugCheck |
| desktop visível | sim (operador) |

A GART de 32 MB também levou de volta a cascata que dependia dela:
- P4 (SDMA1) `ATIVO`, com prova em 1 poll;
- P3 (GART do MMHUB e IH) ligada;
- P7 (anel do GFX em GTT) `ATIVO`.

Outros números:
- DX.43 `EMITIDO` 7.942 vezes, zero `CABECA_ILEGIVEL`;
- T3 com 21 ciclos completos;
- DCN: 5.374 flips por MPO3, 5.364 de 5.373 latches pela interrupção do flip, OTG a 60,00 Hz.

**COMP.VA.2 e COMP.VA.3 estão validados em hardware** (passos 1 e 2 da checklist).

## 2. O que sobra

### Piscadas (H, não medido diretamente)

As recusas foram zero, então a causa da 291 ("janela ocupada") não existe mais. Restam três sinais candidatos, nenhum ainda ligado a uma piscada vista:
- **Latch tardio:** 364 de 5.373 flips (6,8%) latcharam com 3 ou mais quadros de atraso. Na mesma linha só 1 flip passou de 50 ms, então as duas contagens não fecham entre si. Pode ser a contagem de quadros e não o latch; a próxima corrida separa os dois.
- **VSYNC perdidos:** 1.200 de 17.753 quadros (6,8%), porque o VSYNC ainda vem por consulta de 4 ms (D06/D07 abertos).
- **Remapeamentos:** 537 (10% das composições). Cada um reescreve 2.700 PTEs e invalida a VMID 0.

### Menu lento (P2)

- **Cópia vinda da RAM:** toda composição copia 2.700 páginas de memória de sistema (`paginas_sistema=2700`). A superfície do DWM é memória da RADV no segmento 2, então a cópia de 11 MB por quadro vem da RAM pela GART. É o passo 3 (render target da RADV na VRAM).
- **Espera da CPU:** antes de cada composição a UMD drena o lote Vulkan na CPU (`Bc250UmdVkDrainPending`) e só depois chama o escape. A CPU espera o GFX terminar a cada quadro. O passo 4 (zero cópia) tira essa espera.
- **Upload LOCK falhou:** 408 vezes, das quais 368 no StartMenuExperienceHost e 30 na Pesquisa, sempre `hr=0x80070057` logo depois de `UploadImageData SUCESSO` de textura pequena. O upload em si deu certo; o `Lock` falha a seguir. É o passo 5.
- **Journal:** 158 linhas de `NO_SPACE` (79 transações abortadas e PTE escrita assim mesmo; o resto são TAINTs). É o passo 6.

## 3. Veredito

**Sucesso** frente ao critério da 292c. As piscadas e a lentidão do menu continuam e passam para os passos 3 a 7.

**NÃO VALIDADO EM HARDWARE:** a causa das piscadas, e o efeito dos passos 3 a 7.
