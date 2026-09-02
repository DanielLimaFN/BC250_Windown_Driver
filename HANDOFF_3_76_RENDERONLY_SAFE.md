# Handoff 3.76 — RENDERONLY_SAFE, Gate 1.5

**Data:** 2026-09-01
**Candidato:** 3.76.0.0, `BuildKind = RENDERONLY_SAFE`, telemetria schema **133**
**Estado:** assinado, auditado, **NAO INSTALADO**
**Veredito:** **NAO VALIDADO EM HARDWARE**

---

## 1. Diagnostico do Codigo 39 — o que a evidencia sustenta

### Fatos

| Fonte | Fato |
|---|---|
| `setupapi.dev.log` 18:40:21 | Instalacao **SUCESSO**: servico criado, `Image Path = \SystemRoot\System32\drivers\bc250mcdm.sys`, arquivos hardlinkados, `Restart verified`, `[Exit status: SUCCESS]` |
| Telemetria (schema 132) | `DriverEntryReached=1`, `DxgkInitializeStatus=0`, `AddDeviceStatus=0`, `V5StartDeviceStatus=0`, `PostDisplayStatus=0`, `DmmDisplayMapped=1` |
| Telemetria | `V5PostStartStage=0`, `V5LastDdi=0`, `DmmCommitStatus=0`, `DmmPresentStatus=0`, `DmmVsyncCount=0`, `StopDeviceEntered=0` |
| Kernel-PnP 219 @ 18:51:43 e 18:51:45 | `O driver \Driver\bc250mcdm falhou ao carregar` |
| Kernel-PnP 219 @ **17:31:29 e 17:31:31** | **A MESMA mensagem, no boot do 3.74** |
| Estado final | Codigo 39 (`CM_PROB_DRIVER_FAILED_LOAD`) |

### Leitura

A telemetria e' do **restart de dispositivo no bind das 18:40**, nao do boot das 18:51 — foi ali que a imagem apagou. No boot seguinte o driver deixou de carregar.

**O Codigo 39 nao e' novidade do 3.75.** A mesma assinatura — duas falhas de carga com ~2 s de intervalo — existe no 3.74, que rodava a tabela KMDOD antiga, sem perfis, sem schema 132 e sem nenhuma das alteracoes do 3.75.

### O que NAO esta provado

- **Que o CP soft reset foi a causa da tela preta.** O post-mortem anterior afirmou isso com confianca excessiva. O mesmo bring-up executou com imagem visivel em candidatos anteriores. Rebaixado a **hipotese**.
- **Que as alteracoes do 3.75 contribuiram.** Nenhum campo aponta para elas — registro, perfil, sources/children e `StartDevice` leram todos corretos — mas nao existe corrida de referencia para afirmar inocencia.
- **O motivo da falha de carga.** `setupapi.dev.log` cobre instalacao, nao carga. Fica em aberto, e o 3.76 acrescenta os campos que o tornam diagnosticavel.

---

## 2. O candidato 3.76

| | |
|---|---|
| `BuildKind` | `RENDERONLY_SAFE` |
| `DriverVer` | `09/01/2026,3.76.0.0` |
| `TelemetrySchemaVersion` | 133 |
| `UnsafeStartDeviceBringup` | **false** |
| `SkipHwBringup` | **1** |
| `ShaderDispatch` / `CpSoftReset` / `PersistentSubmission` / `GpuvaLabPromotion` / `Vmid0PageTable` | 0 |
| `RenderNode` / `RenderOnlyLab` no INF | 0 / 0 |
| `PackageSysSha256` | `B5B997AAC62D7592AFF5968008F889BA8E00C2569F75E0ECC1AEB0AA87FFA58E` |
| `RecoverySha256` | `E50EFE8449F661A02A0D8F6AC49AC0AD234F5F62A91951C58CDB8E202CE29456` |
| Signer | `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B` |
| Diretorio | `driver\wdk\package\output_renderonly_safe\` |
| Manifesto | `driver\wdk\package\output_renderonly_safe.manifest.json` |

**O SYS foi recompilado, nao copiado do laboratorio com outro INF.** Ele vem de `driver\wdk\output\bc250mcdm.sys`, produzido por `build-contract.bat` sem `BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP`.

### Prova por conteudo, nao por confianca

O binario inseguro agora carrega uma string que so' existe dentro do `#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)`:

```
BC250-UNSAFE-STARTDEVICE-BRINGUP-COMPILED
```

O empacotador seguro recusa qualquer SYS que a contenha, e a auditoria confere de novo no pacote assinado.

A primeira versao dessa checagem procurava a string `SkipHwBringup` e dava **falso positivo**: aquele texto existe no `.rdata` de qualquer imagem, como nome de campo de telemetria. O marcador dedicado corrige isso.

Em tempo de execucao o mesmo fato e' publicado como `V5UnsafeBringupCompiled`, que o inspetor exige em 0 antes de interpretar qualquer outra coisa.

### PDB

`build-contract.bat` nao gerava PDB para a imagem de producao. Passou a gerar (`/DEBUG /PDB:`), e o pacote o transporta — sem simbolos privados uma sessao KD sobre o Gate 1.5 seria cega, e descobrir onde o `dxgkrnl` para e' o proposito do gate.

---

## 3. Telemetria schema 133

### Reset integral

`DriverEntry` agora chama `Bc250ProbeResetAllVolatile()`, que zera **todo o enum**, no topo da funcao, antes de qualquer gravacao.

Os resets anteriores eram por familia e deixavam `Shader*`, `HqdActive*` e `Dmm*` fora de qualquer varredura. Foi por isso que a corrida 3.75 publicou `DmmChildRelationsCalled=56` e `DmmIsSupportedCount=41` herdados de boots anteriores. A auditoria de seguranca exigia os dois resets antigos e passava enquanto a contaminacao acontecia; agora exige o integral.

O reset precisa ficar no topo: colocado depois da resolucao de perfil, apagaria os campos recem-publicados.

### Marcadores de entrada

Ate o 132, um campo em zero podia significar "nao executou" **ou** "executou e o valor sobreviveu ao boot". Estes sao escritos na entrada, nao na saida:

| Campo | Significado |
|---|---|
| `V5UnsafeBringupCompiled` | 1 se a macro insegura foi compilada nesta imagem |
| `V5UnsafeBringupEntered` | 1 se o bloco de bring-up executou |
| `V5PostDisplayAttempted` | 1 se houve tentativa de post-display ownership |
| `V5DisplayInitEntered` | 1 se `Bc250DisplayInit` foi chamada |
| `V5RenderOnlyEarlyReturn` | 1 se o early-return render-only executou |
| `V5StartDeviceReturnStage` | por onde o `StartDevice` saiu (0–6) |

---

## 4. Isolamento do eixo experimental

A tabela KMDOD do 3.76 e' **exatamente a da baseline conhecida**. `SetPalette`, `SetDisplayPrivateDriverFormat` e o stub de `CollectDbgInfo` foram **reintroduzidos de proposito**.

Motivo: a retirada estava certa em si, mas viajou no mesmo candidato que estreou o perfil `RenderOnlyLab` e um bind de bring-up fisico. Quando aquela corrida falhou, nenhum eixo podia ser responsabilizado.

A divida esta' registrada, nomeada e visivel:

- `ACCEPTED_DEBT` em `tools/audit-ddi-noop-callbacks.py` — a auditoria passa mas **imprime a divida em toda execucao**.
- `WDDM_RESTORED_AS_DEBT` em `tools/audit-mcdm-ddi-matrix.py` — **reprova se a tabela KMDOD divergir da baseline congelada**.

O unico eixo novo do 3.76 e' o perfil render-only. A limpeza volta em candidato proprio, depois que o Gate 1.5 fechar.

---

## 5. Fluxos separados

Nenhum script seguro compartilha caminho com o de laboratorio. `tools/bc250-safe-common.ps1` implementa o portao comum, e `Assert-Bc250SafePackage` recusa:

- `BuildKind` diferente de `RENDERONLY_SAFE`;
- `UnsafeStartDeviceBringup` verdadeiro;
- `SkipHwBringup` diferente de 1;
- shader, fila persistente, GPUVA lab ou VMID0 habilitados;
- `RenderNode`/`RenderOnlyLab` diferentes de 0 no INF;
- hash divergente do manifesto;
- assinatura invalida ou signer divergente;
- SYS contendo o marcador de bring-up inseguro.

A auditoria executa o portao contra um manifesto de laboratorio sintetico e **exige que ele recuse** — sem essa prova negativa, a protecao poderia estar inoperante sem ninguem notar.

### Preflight corrigido

`prepare-bc250mcdm-renderonly-safe.ps1` **nao exige** prontidao de shader, HQD, P1 ou P2. Exige o oposto: `P1EngineState=0`, `P1SelfTestPhysicalReady=0`, `P2PhysicalReady=0`, mais `V5UnsafeBringupCompiled=0` e `V5UnsafeBringupEntered=0`.

O perfil render-only nao usa nada disso. A exigencia antiga obrigava um boot de bring-up so' para satisfazer o portao — e foi ela que levou o ensaio 3.75 a rodar sobre a imagem de laboratorio.

---

## 6. Arquivos alterados

**Driver**
- `driver/wdk/bc250_probe_telemetry.h` — schema 133, 6 campos novos, sentinela `BC250_PROBE_FIELD_COUNT`, estagios de retorno, `Bc250ProbeResetAllVolatile`
- `driver/wdk/bc250_probe_telemetry.c` — nomes dos campos, implementacao do reset integral
- `driver/wdk/bc250_active_miniport.c` — marcador `g_Bc250UnsafeBringupMarker`, reset no topo de `DriverEntry`, marcadores de entrada, restauracao da tabela KMDOD
- `driver/wdk/bc250_display_ddi.c` — restauracao de `SetPalette` e `SetDisplayPrivateDriverFormat` como divida
- `driver/wdk/build-contract.bat` — `/DEBUG /PDB:`, auditoria no-op no pipeline, normalizacao CRLF

**Testes**
- `driver/tests/active_miniport_test.c` — `Bc250RunAdapterProfileTest` ajustado a tabela congelada

**Ferramentas novas**
- `tools/bc250-safe-common.ps1`
- `tools/package-bc250mcdm-renderonly-safe.ps1`
- `tools/bind-bc250mcdm-renderonly-safe.ps1`
- `tools/prepare-bc250mcdm-renderonly-safe.ps1`
- `tools/inspect-bc250mcdm-renderonly-safe.ps1`
- `tools/audit-bc250mcdm-renderonly-safe.ps1`

**Ferramentas alteradas**
- `tools/audit-ddi-noop-callbacks.py` — `ACCEPTED_DEBT`
- `tools/audit-mcdm-ddi-matrix.py` — `WDDM_RESTORED_AS_DEBT`
- `tools/audit-bc250mcdm-safety.py` — exige o reset integral
- `tools/package-bc250mcdm-lab.ps1` — declara `UnsafeStartDeviceBringup`

---

## 7. Verificacao offline realizada

| Prova | Resultado |
|---|---|
| `build-contract.bat` em `/W4 /WX` | limpo |
| Suite de contratos completa | aprovada |
| `audit-mcdm-ddi-matrix.py` | PASS, `restauradas como divida: 2/2` |
| `audit-bc250mcdm-safety.py` | PASS |
| `audit-ddi-noop-callbacks.py` | APROVADO, 3 dividas visiveis, 0 falhas |
| `audit-bc250mcdm-renderonly-safe.ps1` | PASS, incluindo a prova negativa |
| Marcador inseguro na imagem de producao | **ausente**, verificado por conteudo |
| Duas tabelas de perfil distintas e coerentes | provado em `active_miniport_test` |
| Scripts seguros recusam pacote de laboratorio | provado por execucao |

---

## 8. Sequencia fisica — todos os passos sao do operador

Ponto de partida obrigatorio: `BasicDisplay / Codigo 0`.

**0. Implantar a recuperacao corrigida** — o bind seguro recusa se o hash divergir:

```bash
copy /Y tools\BC250_RECOVERY.cmd C:\BC250_RECOVERY.cmd
```

A versao anterior removia servico, binario e pacote do Driver Store, mas **nao desfazia o vinculo do no PnP**. A chave `Enum` continuava com `Service=bc250mcdm` apontando para o que acabara de ser apagado, e toda reenumeracao parava em Codigo 19 (`CM_PROB_REGISTRY`) — laco de reboot sem saida. A corrigida remove o no e reenumera automaticamente.

**1. Vincular** (sobe em KMDOD, `RenderNode=0`):

```bash
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-renderonly-safe.ps1 -Execute -IAcceptDisplayBindRisk
```

**2. Reiniciar** e confirmar `bc250mcdm / Codigo 0`.

**3. Promover ao perfil do Gate 1.5:**

```bash
powershell -ExecutionPolicy Bypass -File tools\prepare-bc250mcdm-renderonly-safe.ps1 -Execute -IAcceptProfileChangeRisk
```

**4. Reiniciar.**

**5. Julgar:**

```bash
powershell -ExecutionPolicy Bypass -File tools\inspect-bc250mcdm-renderonly-safe.ps1
```

**Recuperacao a qualquer momento:** `C:\BC250_RECOVERY.cmd`. Se ela terminar sem confirmar `BasicDisplay / Codigo 0` e o no PnP ficar orfao apontando para o servico removido (Codigo 19), a saida e' remover o no e reenumerar:

```bash
pnputil /remove-device "PCI\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00\4&19CAF403&0&0041"
```

---

## 9. Criterio PASS/FAIL

### Portao de validade — se falhar, nao interprete o resto

`SchemaVersion=133`, `V5UnsafeBringupCompiled=0`, `V5ProfileResolveStatus=0`, `V5AdapterProfile=2`.

O inspetor sai com codigo **3** e a mensagem `CORRIDA INVALIDADA`.

### PASS

`bc250mcdm / Codigo 0` mais:

| Campo | Esperado |
|---|---|
| `V5AdapterProfile` | 2 |
| `V5RegistrationMode` | 2 |
| `V5StartDeviceStatus` | 0 |
| `V5StartDeviceReturnStage` | 3 (`RENDER_ONLY`) |
| `V5RenderOnlyEarlyReturn` | 1 |
| `V5StartDeviceSources` | 0 |
| `V5StartDeviceChildren` | 0 |
| `V5DisplaySurfaceAnnounced` | 0 |
| `V5PostDisplayAttempted` | 0 |
| `V5DisplayInitEntered` | 0 |
| `V5UnsafeBringupEntered` | 0 |
| `P1EngineState` / `P1SelfTestPhysicalReady` / `P2PhysicalReady` | 0 |

Mais desktop visivel.

### FAIL util

Codigo 43 ou 39 **continua sendo informacao valiosa**, desde que a telemetria confirme que nenhuma operacao de display ou compute foi executada. O inspetor sai com codigo **2** e registra ate onde o `dxgkrnl` avancou (`V5PostStartStage`, `V5QaiCalls`, `V5QaiLastType`, `V5QiCalls`, `V5DdiMarks`).

Se o Codigo 39 reaparecer com `V5PostDisplayAttempted=0` e `V5UnsafeBringupEntered=0`, isso descarta de uma vez tanto o bring-up fisico quanto a transicao de ownership como causa — que e' precisamente o que o post-mortem do 3.75 nao pode concluir.

---

**Nada foi instalado, vinculado ou reiniciado por mim. Veredito: NAO VALIDADO EM HARDWARE.**
