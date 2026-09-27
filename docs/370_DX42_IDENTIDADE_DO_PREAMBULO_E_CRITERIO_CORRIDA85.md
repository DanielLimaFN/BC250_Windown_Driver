# DX.42 — identidade do preâmbulo e critério da Corrida 85

> 23/09/2026. Faixa **offline**, sem bind. **Substitui o candidato RADV** do
> [docs/369](369_CRITERIO_CORRIDA85_DX41_PREAMBULO.md), que nunca rodou; o resto do
> critério do 369 continua valendo, salvo onde este doc diz o contrário.
> Operador físico: Daniel. Teto: **uma** execução, só depois de boot frio.

## 1. Por que a DX.41 sozinha não bastava

Conferi os 65 dumps da [Corrida 84](367_CORRIDA84_DX40_RESULTADO.md).

- **P0 (dumps):** nos IBs bons, o preâmbulo tem **exatamente 180 DW**. Ele é
  idêntico entre processos diferentes, bem formado e termina num `NOP` nos DW
  173–179. O CS principal começa no DW 180 (`C0043700…`). Nos dois IBs ruins
  (fence 107 e harness `submit_id=10`), **só** esses 180 DW estão zerados; o
  CS principal está presente.
- **Inferência forte (H):** o preâmbulo e o começo do CS principal caem na
  **mesma página de 4 KB** do slot de destino. O CS principal chegou íntegro,
  então o slot e o seu mapeamento funcionaram; quem leu zero foi a **origem**,
  `cs->buf` do preâmbulo, um BO mapeado pela CPU.
- **Consequência para o critério:** a pergunta da DX.41 (origem ou cópia)
  tinha um desfecho quase previsível, `source_zero_prefix=180`. A pergunta que
  falta é **por que a origem zerou**:
  - (a) a RADV regenerou o preâmbulo (`radv_update_preambles`) e o CS novo já
    nasceu zero, ou a escrita nele não ficou;
  - (b) o **mesmo** BO perdeu o conteúdo depois de escrito: device removido,
    ou a alocação foi movida ou descartada pelo VidMm/KMD.
- **Fato de apoio (P0):** o zero do harness veio depois do TDR 3, com o device
  já removido. O do PID 12816 veio sem TDR anterior, logo depois de dois
  submits `cs=0`.

## 2. A mudança (uma só, de medição)

Arquivo `src/amd/vulkan/winsys/wddm2/radv_wddm2_cs.c` e `.h` do fork, espelhados
em [`external/radv-wddm/bc250-radv-wddm.patch`](../external/radv-wddm/bc250-radv-wddm.patch).

- **Serial por CS:** `radv_wddm2_cs_create` dá a cada CS um número único,
  crescente por processo. Um preâmbulo regenerado ganha serial novo, mesmo que o
  ponteiro seja reutilizado.
- **Hash no finalize:** o winsys passou a usar `radv_wddm2_cs_finalize`, que
  chama o finalize comum e depois grava o FNV64 (o mesmo dos dumps), o `cdw` e o
  prefixo zero dos bytes que a CPU acabou de escrever. Isso vale só para CS de
  até 512 DW (o preâmbulo tem 180) e exclui os 4 DW de encadeamento, igual ao
  `copy_base`.
- **Evento `RADV_PREAMBLE_IDENTITY`,** a cada concatenação, antes do veto da
  DX.41:
  - `cs_serial`;
  - `changed`: serial diferente da submissão anterior no mesmo contexto/IP;
  - `changes`, `uses`, `resets`;
  - `cs_ptr`, `buf`, `ib_va`, `bo_handle`, `num_ib`;
  - `fin_count`, `fin_hashed`, `fin_cdw`, `fin_zero_prefix`, `fin_fnv`;
  - `src_fnv`, `copy_fnv`, `fin_match`, `src_zero_prefix`;
  - `level=ERROR` quando `fin_match=0` ou a origem tem prefixo zero.
- **Nenhum byte submetido muda.** A recusa DX.41 (`REJECT_ZERO_PREAMBLE`,
  `VK_ERROR_DEVICE_LOST` antes de D3DKMT) continua **igual**: ela é a única
  mudança de comportamento desta corrida frente à 84, herdada do 369.

## 3. Verificação offline

| Item | Resultado |
|---|---|
| `build_radv.bat` | EXIT 0; `vulkan_radeon.dll` SHA-256 `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`, string `RADV_PREAMBLE_IDENTITY` conferida no binário |
| `audit-radv-wddm-safety.py` | PASS, com o portão DX.42 novo |
| `tools/test-dx42-preamble-identity-mutations.py` | **6/6 mutações reprovam**: sem serial, finalize sem hash, winsys de volta ao finalize comum, evento removido, sem `ib_va`, sem `fin_fnv` |
| Patch espelhado | SHA-256 `AB836A8BDA0109BF8D4A6AA4BF109976D6067DB751045DCB2D04D3BB1BF88AE7`; `PROVENANCE.md` atualizado |
| `prepare-corrida85-dx42.ps1` | PASS: KMD/UMD 3.246.1.0 inalterados, RADV DX.42 por hash, auditoria do pacote PASS |
| `audit-bc250mcdm-safety.py`, `audit-mcdm-ddi-matrix.py`, `audit-ddi-noop-callbacks.py` | PASS |
| `sync-radv-fork-delta.py` (verify) | REPROVA **só** porque a DLL implantada em `C:\BC250_LIB` ainda é a DX.40 (`BF6A1EDA…`). Esperado: o wrapper copia a DX.42 e confere o hash antes do bind. |

## 4. Candidato fixado

| Componente | Valor |
|---|---|
| KMD/UMD | Pacote lab `3.246.1.0` da Corrida 84, sem alteração: SYS `F1EE106C…403796`, UMD `45BB28C3…1396B9`, `GfxWaveProbe=1` |
| RADV | DX.42, `FCAA11FE…DD1008C` |
| Execução | `tools\EXECUTAR_CORRIDA85_DX42.cmd`, elevado, depois de **desligamento completo**. Os scripts DX.41 foram renomeados; o 369 cita os nomes antigos. |

O marcador `C:\BC250_REQUIRE_CLEAN_BOOT.json` está armado para o boot de
13:07:29 (`rollback_boot_ticks=639257764495000000`). Sem boot frio, o wrapper
recusa. Não usar `-Force` nem apagar o marcador.

## 5. Critério, escrito antes da corrida

Grep principal no fluxo RADV e no `telemetry-index.md`:
`RADV_PREAMBLE_IDENTITY`, `RADV_CONCAT_PREAMBLE`, `RADV_CONCAT_REJECT`.

**Pergunta 1 — por que o preâmbulo zera.** Vale só para uma submissão com
`src_zero_prefix>0`. Os desfechos A a D exigem **todos** os sinais listados.

| | Assinatura (o que o log mostra) | Implicação (para onde vai a investigação) |
|---|---|---|
| **A** | `changed=1`, `uses=1`, `fin_hashed=1`, `fin_zero_prefix=0`, `fin_match=0`, `src_zero_prefix=180` | Preâmbulo **regenerado**. A CPU escreveu bytes bons no finalize e eles sumiram antes do primeiro uso. Olhar o mapeamento do BO novo: Lock ou residência entre criação e submit. |
| **B** | `changed=1`, `uses=1`, `fin_zero_prefix=180` (ou `fin_fnv==src_fnv` com zero) | Preâmbulo regenerado cuja escrita **nunca ficou**: a CPU já lia zero no finalize. Olhar o `radv_buffer_map` do BO novo, pois o ponteiro não mapeia a alocação escrita. |
| **C** | `changed=0`, `uses>1`, `fin_zero_prefix=0`, `fin_match=0`, `src_zero_prefix=180` | O **mesmo** BO, já usado com sucesso, perdeu o conteúdo. Cruzar `ib_va`/`bo_handle` com `UpdatePageTable`/`TransferVirtual`/DISCARD do KMD e com TDR/device removed no mesmo intervalo. |
| **D** | Nenhuma submissão com `src_zero_prefix>0`, e `RADV_PREAMBLE_IDENTITY` presente com `fin_match=1` em todas | O defeito não se manifestou. **Não** prova correção; registrar `changes` e `uses` para comparar com a 84. |
| **E — fora do mapa** | Qualquer outra coisa: `fin_match=0` sem prefixo zero, `copy_fnv≠src_fnv`, identidade ausente num `CONCAT`, `fin_hashed=0` no preâmbulo | Descrever o observado **antes** de julgar. Identidade ausente = defeito da instrumentação, não do driver. |

**Pergunta 2 — veto e estabilidade** (herdada do 369, §3 a §5):

- Quando houver prefixo zero, esperar `layout=REJECT_ZERO_PREAMBLE`,
  `RADV_CONCAT_REJECT` e **nenhum** `KMD_GFX_SUBMIT` com aquela VA/hash.
- Um TDR sem ondas com `IB1` no IB zerado **não** deve reaparecer.
- TDRs com ondas no VMID anterior (classe 1–3) **não são testados** por esta
  faixa. Se aparecerem, é a mesma classe aberta, não regressão.

**Veredito:**

- **Diagnóstico com sucesso:** A, B ou C com todos os sinais, e coleta
  pós-rollback completa.
- **Parcial:** evidência útil, mas faltou um sinal ou um dump.
- **Inconclusivo:** candidato divergente, log truncado ou identidade ausente.
- **Funcional:** continua **falha** se houver recusa, TDR, device-lost, tela
  preta ou menos de 60/60. Recusar o IB zerado é contenção, não correção.

Desfecho parcial não é desfecho: A sem `fin_hashed=1`, por exemplo, não é A.

## 6. Depois da corrida

Relatório com as 5 seções obrigatórias. O índice regenerado já destaca eventos
`level=ERROR`. Se houver BugCheck ou reboot, rodar
`tools\recover-bc250mcdm-lost-tail.ps1 -MaxFolders 1` antes de concluir. A
coleta pós-rollback do staging RADV pode não ter rodado nesse caso: conferir
`C:\Windows\Temp\BC250_GFX_IB_DUMP`.

**NÃO VALIDADO EM HARDWARE:** DX.42, DX.41, desktop íntegro, 60/60 e ausência
de TDR.
