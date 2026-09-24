# Corrida 97 (DX.51) — resultado; DX.52 e critério da Corrida 98

> 24/09/2026.
> **Corrida 97:** bind às 00:14:10, lab **3.257.1.0** SYS `94DFDF76…`, UMD
> `7226ABEC…`.
> **Evidência:** `docs/evidence/2026-09-24_00-14-10-DESKTOP-HQAG6T4-lab-binding/`.
> **Critério:** [doc 393](393_CORRIDA96_RESULTADO_E_DX51_CRITERIO_CORRIDA97.md).

## 1. Corrida 97: resultado

**Relato do Daniel:** desktop normal de volta, "bem travado" (lento), sem piscar.
O guardião não reverteu e ele reiniciou.

**Máquina (P0):**
- o log do Explorer (sink durável) para às **00:18:21**, antes do prazo do
  guardião (00:19:10);
- **a máquina travou**, e por isso o guardião não pôde agir;
- Kernel-Power 41 às 00:20:52; fusível recusou o boot seguinte;
- rollback `RECOVERY_OK_ONLINE` às 00:21:12.

**Controle (P0):**
- zero `ANEL … criado`;
- 719 draws com `Anel=0 Direto=0 FlushAgora=1`;
- 20 recusas `DX50_BISSECCAO_DESLIGADO`.

A bissecção estava ativa.

**Medido (P0):**
- harness 60/60;
- DWM **123 Present/min**, igual à 95: o ganho de vazão veio do **staging
  persistente**, não do lote;
- KMD (log resgatado, 219.334 linhas, sha256 `BE7E9FBF…B248`): 583 Presents e
  583 `COMPOSITE_BLT OK`;
- zero TDR, zero fault, zero timeout;
- 1202 `KMD_CONTEXT_CREATE` (~290/min);
- laço do Explorer: 156 ciclos.

**Desfecho: C.** Travou sem lote e sem dirty rects.

## 2. O que a mineração cruzada mostrou (P0)

**A ponte Vulkan→WDDM** (escape `COMPOSITE_BLT`, cópia de 11 MB pelo SDMA do KMD
no Present):

| Corrida | OK / falhas | Motivo da falha |
|---|---|---|
| 93 | 149/0 | — |
| 95 | 337/89 | `E_INVALIDARG` |
| 96 | 186/438 | `E_INVALIDARG` |
| 97 | zero falhas no KMD | — |

- **Motivo no KMD:** `COMPOSITE_BLT RECUSADO (resolucao)` com
  `SpaceResolveAddress failed: status=9`. O **destino** (buffer do swapchain do
  DWM, `0x200B60000`/`0x202070000`) não estava residente.
- **Leitura (H forte):** o anel de 16 MB **por device** do lote pressiona o
  segmento de 257 MB, e o VidMm despeja os buffers do swapchain. Isso explica o
  pisca-pisca: a ponte falha e o quadro fica velho. Sem o anel (97), zero falhas
  e desktop normal.

**Momento do travamento da 96:** a última linha do DWM é `BRIDGE VULKAN->WDDM
ENTER` sem LEAVE.

**Sobre o travamento, o que sobra:**
- o staging persistente (mínimo de 4 MB, até 11 MB por device);
- o **ritmo de criação e destruição de contextos**, com o laço do Explorer:
  - **Explorer:** ~290/min contra ~100/min na 93;
  - **laço:** alocação de 3.407.872 bytes → `E_INVALIDARG` → device destruído
    → recomeço a cada ~1,5 s;
  - **por que o KMD recusa essa alocação:** não se vê. As saídas
    `STATUS_INVALID_PARAMETER` do `CreateAllocation` eram **mudas**, e
    `[McdmCreateAllocation] Result=` é amostrado.

## 3. DX.52 (só instrumentação; configuração igual à 97)

**`bc250_active_miniport.c` (`Bc250ActiveCreateAllocationLifecycle`):**
- toda recusa ganhou motivo nomeado:
  - `ABI_PRIVADA_RECUSADA_PELO_PARSE`;
  - `TAMANHO_ACIMA_DO_SEGMENTO`;
  - `SEGMENTO_DE_ALOCACAO_NAO_CONFIGURADO`;
  - `SEM_ABI_*`;
  - `REGISTRO_DO_OBJETO_DE_ALOCACAO_FALHOU`;
  - `ADAPTADOR_INATIVO_OU_ABI_NAO_PRONTA`;
- sai numa linha `[CREATE_ALLOC_RECUSADO] status= motivo= idx= Bytes= PrivSize=
  AbiStatus= ObjStatus= Token= SegBytes= SegSet=`, **isenta de amostragem**.

**UMD:**
- `Allocate RECUSADO pelo runtime/KMD` com classe do token, flags e geometria;
- `CreateResource RECUSADO desc:` com dimensão, tamanho, mips, array, formato,
  samples, bind, misc, usage e acesso de CPU.

**Coleta:** o wrapper roda `icacls /reset` na pasta arquivada da 97, que traz
o log do dwm.exe da 96.

**Verificação offline:**
- build-umd e build lab EXIT=0;
- `audit_dx52_create_allocation_refusal_speaks`: **6/6** mutações;
- DX.51 3/3, DX.50 6/6;
- auditoria de segurança PASS;
- pacote auditado PASS, com as strings novas presentes no SYS e na UMD.

**Candidato:**

| Componente | Valor |
|---|---|
| KMD | lab `3.258.1.0` SYS `A15464083D5556D8D57E3E1C77570F8F137C27B176EDA852079B976E6128FE46`, INF `FCCE135A…48D6`, CAT `9D79A5B6…86B7E` |
| UMD | `8D4EE96D869B19B2AACF9798A99F6ED69360275F85D189937E0037E9FF62A3CB` |
| Pré-voo | `tools/prepare-corrida98-dx52.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA98_DX52.cmd`, elevado, 300 s, depois de **desligamento completo** (marcador do rollback de 00:21:12) |

## 4. Critério da Corrida 98, escrito antes

**Pergunta principal:** por que a alocação de 3.407.872 bytes do Explorer é
recusada?

**Controle:**
- configuração igual à 97: sem anel, `DX50_BISSECCAO_DESLIGADO`.

| | Assinatura | Implicação |
|---|---|---|
| **A** | `[CREATE_ALLOC_RECUSADO] … motivo=<X>` junto de `CreateResource RECUSADO desc:` no log do Explorer | O motivo X nomeia a correção. A Corrida 99 corrige o laço do Explorer mantendo o staging: se não travar, a causa do travamento é o ritmo de contextos; se travar, é o staging. |
| **B** | `Allocate RECUSADO pelo runtime/KMD` na UMD **sem** `[CREATE_ALLOC_RECUSADO]` no KMD | O **dxgkrnl** recusa antes de chamar o KMD. A descrição do recurso e os campos da alocação apontam o parâmetro inválido do lado da UMD. |
| **C** | Sem recusa nenhuma: o laço some sozinho | Descrever antes de julgar; comparar com o log da 97. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever. |

**Travamento:** esperado de novo (configuração igual à 97). **Não é desfecho
desta corrida.** Registrar o horário (último log do Explorer) e a taxa de
contextos.

**Depois da corrida:** as 5 seções e a tabela-checklist.
