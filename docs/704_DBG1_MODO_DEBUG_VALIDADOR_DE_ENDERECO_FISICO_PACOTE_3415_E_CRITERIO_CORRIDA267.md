# 704 — DBG.1: modo debug, validador de endereço físico e alvo da paginação. Pacote 3.415.1.0 e critério da Corrida 267

**Data:** 12/10/2026. **Natureza:** preparação offline; nada rodou na placa. **Base:** [doc 703](703_TAXAS_MEDIDAS_DO_MMIO_EM_REGIME_E_NOVAS_FORMAS_DE_IDENTIFICAR_A_TRAVA.md) §4, §5.1 e §5.3.

---

## 1. Chave de modo debug (pedido do Daniel)

**`DebugTrace`** é um tunable do INF, lido no StartDevice.

| Valor | Efeito |
|---|---|
| **0** (padrão do INF fonte) | Nada da coleta pesada nova roda. |
| **1** | Liga a coleta pesada nova. O empacotador de laboratório escreve 1 com `-DebugTrace`, e o manifesto registra `DebugTrace`. |

**Linha de boot:** `[DBG1_DEBUG_TRACE] chave=… MODO DEBUG …` ou `modo normal`.

**O que o modo debug liga:**
1. **Validador de endereço físico.** Toda folha de PTE de memória de sistema (segmentos 0 e 2) tem o endereço físico classificado contra:
   - as faixas de RAM do Windows (`MmGetPhysicalMemoryRanges`, guardadas no StartDevice);
   - a faixa proibida, o carve-out da VRAM `[base DRAM, +256 MB)`.

   Classes: 0 = na RAM, 1 = fora da RAM, 2 = atravessa o fim de uma faixa, 3 = no carve-out, 4 = abaixo de 1 MiB, 5 = sem tabela.

   **Só conta. Nada é recusado**, para não mudar o caminho da GPU.
2. **Amostras**, gravadas no rastro e no log a partir da thread do rastro, nunca da thread de paginação:
   - as 8 primeiras violações, com segmento, classe e endereço;
   - os 8 primeiros endereços crus de folhas do segmento 2. Isso fecha se o VidMm entrega endereço físico ou deslocamento na abertura.
3. **Evento `PAGING_PTE`** no anel de eventos, um por `UPDATE_PAGE_TABLE`, com:
   - a VA da 1ª PTE;
   - o número de entradas;
   - o segmento e o nível;
   - o endereço da 1ª folha.
5. **DBG.1b — rastro drenado a cada paginação.** A mineração do [doc 705](705_MINERACAO_DE_46_TRAVAS_PAGINACAO_ATIVA_E_A_JANELA_CEGA_DE_UM_TIQUE.md) mostrou que a máquina morre dentro do tique seguinte ao último registro (~110 ms), justamente na janela que o rastro não grava. No modo debug, cada FILL, TRANSFER ou UPDATE_PAGE_TABLE montado e cada submit do SDMA0 dão um "chute" (`KeSetEvent`, sem espera nem arquivo) na thread do rastro. A thread acorda na hora e grava os eventos pendentes, sem esperar o tique de 100 ms. O tique continua igual. Fora do modo debug o chute não faz nada.
4. **Anel de eventos maior.** O espelho em RAM passa de 48 para 512 posições sempre, mas só o modo debug drena 96 eventos por tique, em vez de 16. Na 266 o anel perdia ~60% dos eventos.

**Registros novos no rastro:**

| Registro | Conteúdo |
|---|---|
| **BPA1** (a cada 1 s) | folhas conferidas, seg0, seg2, fora da RAM, carve-out, abaixo de 1 MiB, sem tabela |
| **BPV1** | as amostras do item 2 |

O decodificador mostra `pa_validator`, `pa_samples` e o tipo `PAGING_PTE`.

**O que NÃO muda:**
- os logs que já existiam;
- o caminho da GPU;
- os registradores: nenhuma leitura ou escrita nova.

O custo é de CPU (uma classificação por folha, até 64 faixas) e de disco (rastro maior).

**Correção de enquadramento:** o "alvo" de um FILL/TRANSFER de paginação é uma VA do processo de paginação. O endereço físico real sai das PTEs que o VidMm escreve, e é exatamente o que o validador confere. Por isso o item 5.3 do doc 703 virou o evento `PAGING_PTE`, e não uma tradução de cada cópia.

## 2. Arquivos

| Arquivo | Mudança |
|---|---|
| `driver/core/bc250_pa_validator.h` | classificador puro, usado no driver e nos testes |
| `driver/wdk/bc250_mcdm_callbacks.c/.h` | chave, faixas, validador no encoder de PTE, `PAGING_PTE`, BPA1/BPV1, dreno de 96 |
| `driver/wdk/bc250_mmio_breadcrumb.h` | `BC250_CRUMB_EVT_PAGING_PTE` e `BC250_CRUMB_EVT_MIRROR_COUNT` |
| `driver/wdk/bc250_active_miniport.c` | lê `DebugTrace` e instala as faixas (`Bc250ActiveArmPaValidatorRanges`) |
| `driver/wdk/package/bc250mcdm.inf` | `DebugTrace=0` |
| `tools/package-bc250mcdm-lab.ps1` | `-DebugTrace` e o manifesto |
| `tools/decode-bc250-paging-trace.py` | decodifica os registros novos |
| `driver/tests/mcdm_callbacks_test.c` | 8 casos do classificador, incluindo a faixa proibida dentro da RAM e o estouro de 64 bits |
| `tools/audit-bc250mcdm-safety.py` | portão `audit_dbg1_debug_trace` |

O portão `audit_dbg1_debug_trace` exige:
- `DebugTrace=0` no INF fonte;
- o validador sai antes de classificar quando a chave está em 0;
- o validador não loga na thread de paginação;
- toda folha de segmento 0/2 passa pelo validador;
- o StartDevice lê a chave.

A mutação que tira a guarda do modo debug reprova o portão.

## 3. Identidade do candidato

| Item | Valor |
|---|---|
| DriverVer | `10/12/2026,3.415.1.0` (fonte 3.415.0.0). O 3.414.1.0 foi empacotado sem a DBG.1b, nunca instalado e substituído. |
| SYS assinado | `90700F78C5E404A04540B1B5D024E6C9DAAD4DBCCDB13EBAD40DBD02FEF3EA78` |
| SYS fonte (lab) | `2931B5983658AF1D3C041AD70014DE9C0F72996890C07ED3D5DD437C4AC299F8` |
| INF | `87E09C2E4FBDFE52FE2C920C955EFFF17170F0D761DF1283176D61C69F45BE74` (`DebugTrace=1`, `GfxWaveProbe=0`) |
| CAT | `D5AC6450A599161F5FF3EB41DB4CD71584EDF6C3DEEB108AFF02E80BB5E22275` |
| UMD, RADV, T3 | iguais à 266 (`B73F9BDA`, `B4AE2E14`, `768C82F7`) |

**Verificações:**
- `build-contract.bat`: EXIT 0.
- Build de laboratório: EXIT 0.
- Empacotador: EXIT 0. A 1ª tentativa deu IOException no Copy-Item (armadilha conhecida) e a 2ª passou.
- `audit-bc250mcdm-lab-package.ps1`: PASS.
- `audit-bc250mcdm-safety.py`: PASS.
- No SYS assinado estão as linhas `[DBG1_DEBUG_TRACE]`, `[DBG1_PA_FAIXAS]`, `[DBG1_PA_VIOLACAO]` e `[DBG1_PA_SEG2]`, o nome `PAGING_PTE` e o tunable `DebugTrace`.

**Defeitos meus, pegos antes do pacote:**
- o cabeçalho novo incluía `stdint.h` no modo kernel (a suíte reprovou);
- a data do DriverVer saiu invertida, 12/10 em vez de 10/12 (o Inf2Cat recusou).

**Backup:** `driver/wdk/package/output_lab_3.415.1.0_267`.

**Ferramentas:**
- Preflight `tools/prepare-corrida267-dbg1-modo-debug.ps1` (exige `DebugTrace=1` no manifesto).
- Lançador `tools/EXECUTAR_CORRIDA267_DBG1_MODO_DEBUG_600S.cmd` (CRLF).

## 4. Roteiro (Daniel)

Igual ao da 266:
1. `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset`, **desligar** e ligar.
2. Rodar o lançador como administrador.
3. Quando o desktop aparecer: menu (Win, 10 s, Esc).
4. Como administrador: `tools\RODAR_PROCESSO_NOVO.cmd`.
5. Se travar: reset em 10 a 15 s e deixar a coleta automática terminar.

## 5. Critério (escrito antes da corrida)

### Pré-condição

1. `[DBG1_DEBUG_TRACE] chave=1 (present=1): MODO DEBUG` e `[DBG1_PA_FAIXAS]` com pelo menos 1 faixa de RAM e 1 proibida.
2. BPA1 com `pte_sistema_conferidas` crescendo e `sem_tabela` = 0.
3. Eventos `PAGING_PTE` presentes no BEV1.
4. Pelo menos 90% dos eventos BEV1 gravados (sequência sem buracos grandes), e o último evento a menos de 10 ms do fim do rastro (efeito da DBG.1b).
5. A base da 266: MEC.2, FT.1, `GfxWaveProbe=0`, T2.

Sem os itens 1–3, o modo debug não pegou e a corrida não responde à pergunta.

### Desfechos

- **A — violação de endereço.** BPA1 com `fora_da_ram`, `carve_out_vram` ou `abaixo_1mib` maior que 0, ou alguma amostra BPV1 de violação.
  - *Assinatura:* linhas `[DBG1_PA_VIOLACAO]` com segmento, classe e endereço.
  - *Implicação:* a GPU recebeu PTE para um endereço que não é RAM. É candidata forte a causa, com o endereço na mão. Próximo passo: cruzar o endereço com o mapa físico, achar a origem (VidMm ou encoder) e corrigir; só depois, recusar a PTE.
- **B — zero violações, trava igual.**
  - *Assinatura:* milhares de folhas de sistema conferidas, todas na RAM; os endereços crus do segmento 2 também na RAM (classe 0).
  - *Implicação:* a classe "GPU em endereço inválido por PTE de sistema" sai das causas. O fim da trava passa a ser lido com o anel completo e com os `PAGING_PTE` dos últimos 100 ms.
- **C — 600 s sem trava.**
  - *Implicação:* dispersão. O modo debug muda o tempo de CPU na paginação, então isso não vale como efeito. A medida vale do mesmo jeito: zero ou alguma violação.
- **D — endereços do segmento 2 fora da RAM ou abaixo de 1 MiB, mesmo sem trava.**
  - *Implicação:* o VidMm entrega deslocamento na abertura, e o driver o usa como endereço físico. É defeito de código, independente da trava, e deve ser corrigido antes da próxima mudança.
- **E — fora do mapa.** Por exemplo: a chave em 1 e nada contado, classe 5 (sem tabela), queda de desempenho grande ou TDR.
  - *Implicação:* descrever antes de julgar.

**Regra (Lei 10):** desfecho parcial não é desfecho. B exige as folhas conferidas; sem elas, não há B.

### Teto

Uma corrida responde à pergunta do endereço. Se der B, a próxima é a bisseção pela carga (doc 703 §5.2), com o mesmo pacote em modo debug.

**NÃO VALIDADO EM HARDWARE.**
