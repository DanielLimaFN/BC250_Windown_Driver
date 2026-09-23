# Emenda ao critério da Corrida 67 — Cid corrigido e faixa DX.21

> **Escrita em 22/09/2026, ANTES de qualquer corrida com o candidato** (Lei 9). Nenhuma corrida
> foi feita com 3.228.1.0 nem com 3.229.1.0; nada foi observado que pudesse contaminar o critério.
> **Substitui o candidato** de [docs/330](330_CRITERIO_CORRIDA67_ELIMINACAO_DCC_E_DESBLOQUEIO_DRAW2.md):
> a Corrida 67 roda o **lab 3.229.1.0**, não o 3.228.1.0.
> **Estado:** NÃO VALIDADO EM HARDWARE.

---

## 1. Por que emendar

O docs/330 foi escrito sobre um nome de cliente de fault **sem proveniência**, e isso muda a
previsão da corrida.

1. **`Cid=10` não é `TCP_UTCL0`, e `Cid=9` não é `SQC_DCACHE`.** O docs/329, o docs/330 e o
   docs/331 afirmam que o nome vem de `inc/linux_ref/gc_10_1_0_sh_mask.h`. **Esse arquivo não tem
   tabela de clientes** — só nomes de registradores que contêm `UTCL0` (conferido por grep em
   22/09). A BASE, na Corrida 62, já registrava: *"o campo `Cid` não tem enumeração local — não
   invento (Lei 7)"*. A afirmação do handoff violava a Lei 7.
2. **O nome correto, com fonte (P1):** em GC 10.1.x o `gmc_v10_0_set_gfxhub_funcs` cai no
   `default` → `gfxhub_v2_0_funcs` (`gmc_v10_0.c:593-609`, upstream, sha256 `69B5DB51...`). A
   tabela `gfxhub_client_ids[]` de `gfxhub_v2_0.c:34-53` (sha256 `BF032415...`) dá:

   | Cid | Cliente |
   |---|---|
   | 8 | TCP |
   | **9** | **SQC (inst)** — busca de instrução |
   | **10** | **SQC (data)** — leitura escalar |

3. **Decodificação dos dois faults da Corrida 66** pelas máscaras locais
   (`gc_10_1_0_sh_mask.h:12143-12152`, P1) — os dois são **leitura** (`RW=0`) do **SQC**:

   | Status | Cid | Cliente | Endereço | Leitura |
   |---|---|---|---|---|
   | `0x0010153C` | 10 | SQC (data) | `0x2431000` (= VA da imagem `>> 8`) | escalar: `s_load`/`s_buffer_load` |
   | `0x001012B1` | 9 | SQC (inst) | `0x200040000` (heap de 32 bits do RADV) | busca de instrução |

   O `Cid=9` como **busca de instrução** casa com o histórico próprio: Corrida 57 ("PC dentro do
   alvo") e Corrida 58 ("alvo executável"), e `0x200040000` está no heap de 32 bits
   (`RADV_WDDM2_32BIT_HEAP_START = 0x200000000`, `radv_wddm2_bo.h:33`), onde o RADV põe os
   shaders.

4. **Consequência para a hipótese do DCC (P1, não medição):** o SQC não lê metadados de DCC nem
   texels — isso é o TCP (`Cid=8`). Metadado de DCC não inicializado também não gera acesso num
   endereço igual a `VA >> 8`. **A hipótese do DCC não explica o fault `Cid=10`.** Ela ainda pode
   ter relação com a *duração* do Draw 2, mas isso não está sustentado por nenhum dado. Além
   disso, o mesmo `0x2431` apareceu nas Corridas 45, 46, 48, 59 e 64 — antes de existir upload de
   textura por staging (restaurado só na 66).

5. **O `nodcc` alcança o RADV (P1, leitura do fork):** `os_get_option_internal` usa
   `GetEnvironmentVariableA` no Windows (`src/util/os_misc.c:217`), e o
   `C:\BC250_LIB\vulkan_radeon.dll` implantado é byte a byte o `build/` do fork (sha256
   `7837C960...` nos dois). O `SetEnvironmentVariableA` da DX.20 portanto **tem efeito**; a
   variável isolada é real.

---

## 2. O que o 3.229.1.0 acrescenta ao 3.228.1.0 (faixa DX.21)

Somente instrumentação e ferramenta. **SYS idêntico** ao 3.228.1.0.

1. **Despejo do programa de shader** (`[VK_PIPELINE_IR]` no log da UMD). A ponte cria o device com
   `VK_KHR_pipeline_executable_properties` e, se falhar, recria **sem** a extensão (declarado).
   Cada pipeline criado com sucesso é despejado: `Assembly` (sem LLVM o RADV entrega o
   `aco_print_program` pós-RA, com cada `s_load`/`s_buffer_load` e os SGPRs de origem), ou
   `ACO IR` como segunda escolha. Teto de 32 pipelines por processo e 96 KiB por representação,
   **com truncamento declarado**. A flag `CAPTURE_INTERNAL_REPRESENTATIONS` guarda texto; **não
   altera o código gerado** (P1, leitura do RADV) — e isto é declarado aqui como premissa.
2. **Despejo de IB que o dwm.exe consegue gravar.** Lido por `icacls` em 22/09: a pasta de
   evidência só dá acesso a SISTEMA, Administradores e Daniel; o `dwm.exe` roda como `DWM-<n>`. O
   `fopen` do RADV falhava e a linha `IB_DUMP RECUSADO` ia para stderr, que o DWM não tem — por
   isso `gfx-ib-dump/` saiu vazio nas Corridas 35, 36 e 66. Agora: staging
   `C:\Windows\Temp\BC250_GFX_IB_DUMP` com ACL para Todos e `Window Manager Group`, armado pelo
   binder **antes** do bind, copiado para `<evidência>\gfx-ib-dump\` na coleta FINAL com
   `_COLETA.txt` declarando a contagem.

Testes: `build-umd.bat` EXIT=0 com `umd_pipeline_ir_dump_test` PASS; `build-contract.bat` EXIT=0;
três auditorias PASS, com o portão novo
`audit_dx21_pipeline_ir_and_ib_dump_reach_evidence`. **Três mutações verificadas, todas
reprovando:** remover a guarda de captura desarmada (a suíte reprova), tirar a guarda da flag de
captura (o portão reprova), e o runner voltar a apontar o despejo para a pasta de evidência (o
portão reprova).

---

## 3. Perguntas desta corrida (substituem as do docs/330)

1. **P1 — a variável isolada:** com `RADV_DEBUG=nodcc` ativo, o Draw 2 do DWM
   (`CB=1, SRV=1, SAMPLER=1`) ainda leva mais de 2 s e o fault `Cid=10` (SQC dados) em `VA>>8`
   ainda ocorre?
2. **P2 — o que produz o endereço:** qual instrução escalar do programa do Draw 2 lê de um
   endereço igual ao word0 de um descritor de imagem? Responde-se cruzando `[VK_PIPELINE_IR]`
   (instruções e SGPRs) com o `gfx-ib-dump` (valores de `SPI_SHADER_USER_DATA_PS_*`).
3. **P3 — o `Cid=9`:** a busca de instrução em `0x200040000` volta a faltar, e sob qual raiz?

**Previsão registrada antes (P1, não P0):** pela seção 1.4, o `nodcc` **não** deve eliminar o
`Cid=10`. Se eliminar, a previsão estava errada e isso deve ser dito assim.

---

## 4. Desfechos (Lei 10 — assinatura e implicação separadas)

### A — O Draw 2 destrava
- **Assinatura:** `[VK_BRIDGE] RADV_DEBUG='nodcc'` presente no log do dwm.exe **e** a linha
  `DRAW fence concluida` do Draw 2 em menos de 100 ms **e** zero `[TDR_GCVM_FAULT]` com `Cid=10`
  **e** zero `TDR_QUIESCE` durante os draws do DWM. Os quatro são exigidos.
- **Implicação:** a previsão da seção 3 estava errada; o DCC tinha papel no Draw 2, **e o
  mecanismo continua sem nome** — o SQC não lê DCC. Investigar pelo `[VK_PIPELINE_IR]` o que mudou
  no programa com e sem DCC antes de chamar isso de causa.

### B — Destrava, e surge a próxima parede
- **Assinatura:** as quatro condições de A, **e** um draw posterior (`VkDraws>=3`) ou o Present
  falha com motivo nomeado no log.
- **Implicação:** a parede do Draw 2 caiu; a seguinte está nomeada pela própria linha de falha.

### C — Nada muda no Draw 2
- **Assinatura:** Draw 2 acima de 2 s **ou** `[TDR_GCVM_FAULT]` com `Cid=10` num endereço que
  resolve pela caminhada `VA<<8`.
- **Implicação:** a hipótese do DCC cai como causa do stall. A próxima faixa sai do
  `[VK_PIPELINE_IR]` + `gfx-ib-dump`, **não** de uma nova hipótese — e a hipótese de "shader trata
  `desc[0]` como ponteiro plano" do docs/330 só vira causa se a instrução aparecer no programa.

### D — Instrumentação muda
- **Assinatura:** zero linhas `[VK_PIPELINE_IR] P` no log do dwm.exe (com
  `[VK_PIPELINE_IR] captura DESARMADA` ou `INDISPONIVEL`), **ou** `_COLETA.txt` com zero arquivos
  apesar de haver submissões gráficas do DWM no log do KMD.
- **Implicação:** P2 fica sem resposta nesta corrida; o defeito é da instrumentação e se corrige
  antes da próxima. Zero arquivos de IB, agora com staging gravável, passa a ser conclusivo contra
  o RADV (o `bc250_dump_ib` não foi chamado ou recusou), não contra a ACL.

### E — Algo fora do mapa
- **Assinatura:** qualquer observação que não caiba em A–D — inclusive BugCheck, device-lost sem
  TDR, `vkCreateDevice` falhando **nas duas** tentativas, regressão nos controles (upload, fences
  do Node 1 abaixo de 66, faults de outro Cid), ou avanço que não estava previsto.
- **Implicação:** **descrever o observado antes de julgá-lo.** Não encaixar em A–D.

**Desfecho parcial não é desfecho** (Lei 10.3): A com três das quatro condições não é A.

---

## 5. Controles a não regredir

Upload por staging com fence física (Corrida 66), Draw 1 aposentando, fences do Node 1 chegando
pelo menos a 66, zero BugCheck, fusível CLEAN, `RECOVERY_OK_ONLINE`. Tela preta ou imagem vinda
do BasicDisplay **não** são sucesso nosso.

## 6. Pacote

Lab **3.229.1.0**, `DISPLAY_RENDER_LAB` (RenderNode=1, RenderOnlyLab=0), WDDM 2.7, P1 PERSISTENT
SUBMISSION, schema 135. `Lab package audit: PASS`.

| Artefato | SHA-256 |
|---|---|
| SYS assinado | `F2A3F42C3823D592CAB428ED9497EB457A85C854A676C5CFCBB2C70971DB614D` (igual ao 3.228.1.0) |
| SYS fonte | `E507F55F09E2C4557A8E8F3A911863970FB774544AF07231C77A2B81D9B24CC6` |
| UMD fonte | `05E5E4FB0334E975E4A12C1214212E6547FDEA4D17EA97084F3907F304681D65` |
| UMD assinada | `A24F3A3F438DC77941AC0341DD26A0B0F995DE1D1D08DAA50976737D66F2A731` |
| INF | `82C25BF28CB3E452B289CC9C8747FB008B3064A7457C004DF74D67F9BC67B2AE` |
| CAT | `0CA120B7616B404083378F85AC28CDAA0CB116E2754ACFB5DC3417B5569FBED2` |

UMD assinada **conferida por string** (o empacotador não compila): `[VK_PIPELINE_IR] captura`,
`TRUNCADO NO TETO`, `VK_KHR_pipeline_executable_properties` e `variavel isolada da Corrida 67`
presentes; a frase antiga `forçado para 'nodcc'` ausente.

Execução: `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`, com **boot frio** antes do bind.
