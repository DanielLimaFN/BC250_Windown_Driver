# Passagem de bastao — RenderNode WDDM apos o candidato 3.71

**Data:** 2026-09-01  
**Hardware:** AMD BC-250, `PCI\VEN_1002&DEV_13FE`, GC 10.1.3 / Cyan Skillfish 2  
**Estado deste documento:** handoff operacional para a proxima IA  
**Objetivo imediato:** descobrir por que o `dxgkrnl` desmonta o adaptador depois de `StartDevice`, mantendo o miniport display+render em Codigo 0.  
**Objetivo final:** WDDM 2.0+ display+render real, submissao originada pelo `dxgkrnl`, fence/retirement fisicos, UMD/Vulkan e posteriormente DXVK, VKD3D-Proton e Zink.

> Regra de honestidade: o caminho direto P1/P2 foi validado no silicio, mas **RenderNode WDDM, submissao WDDM, GPUVA dinamica, UMD e Vulkan ainda NAO FORAM VALIDADOS EM HARDWARE**.

## 1. Leia antes de alterar codigo

1. Leia integralmente `AGENTS.md` e `BC250_HARDWARE_RULES.md`.
2. Preserve todas as mudancas existentes: o worktree esta deliberadamente sujo e contem trabalho de varias iteracoes.
3. Nao substitua prova fisica por fence escrita pela CPU, `KeSetEvent`, incremento manual de ponteiro, WARP ou loop em C.
4. Nao programe MMIO fora de `docs/03_CATALOGO_DE_REGISTRADORES.md` nem use itens em quarentena.
5. Toda operacao fisica deve seguir:

   `[PREFLIGHT] -> [SNAPSHOT] -> [APPLY] -> [READBACK] -> [PROVA NO SILICIO] -> [ROLLBACK/TEARDOWN]`

6. O usuario desabilitou o watchdog por decisao explicita. A recuperacao manual e `C:\BC250_RECOVERY.cmd`.
7. Nao iniciar nova corrida de boot apenas para testar mais um callback por tentativa. Primeiro produzir evidencia que identifique a decisao do `dxgkrnl`.

## 2. Estado comprovado

| Item | Estado | Evidencia/observacao |
|---|---:|---|
| Display-only/KMDOD | VALIDADO | `bc250mcdm`, Codigo 0, imagem presente e DMM funcional em varias corridas |
| Bring-up fisico | VALIDADO | `HqdActiveStatus=0`, `HqdActivePhysicalReady=1`, `ShaderPhysicalReady=1` |
| P1 persistente | VALIDADO NO SILICIO | IB real; duas fences escritas pela GPU; resultado do shader; `RPTR==WPTR` |
| P2 multi-submissao | VALIDADO NO SILICIO | 16/16 aceitas e aposentadas; fences e dados verificados byte a byte |
| Candidato atual | 3.71.0.0 | Schema 131; SYS SHA256 inicia em `B1C90C5CC0B67178` |
| Registro display+render | ALCANCADO | `DxgkInitialize=0`, `AddDevice=0`, `StartDevice=0`, uma source e um child |
| RenderNode em Codigo 0 | BLOQUEADO | O dispositivo termina em Codigo 43 logo apos a enumeracao inicial |
| Teardown | CORRIGIDO | `StopDeviceStatus=0`, `RemoveDeviceStatus=0`, timer/DPC drenados, tracker vazio |
| GPUVA WDDM dinamica | NAO VALIDADO | Gate permanece fail-closed |
| Submissao pelo dxgkrnl | NAO VALIDADO | Nenhuma DDI de render foi alcancada no boot RenderNode |
| Fence/retirement WDDM | NAO VALIDADO | Existem apenas provas do caminho direto P1/P2 |
| UMD D3D/Vulkan | NAO INTEGRADO | Nao usar UMD para mascarar o Codigo 43 do KMD; a pilha morre antes de apps |

Evidencia mais recente da base 3.71 em Codigo 0 e P1/P2:

- `docs/evidence/2026-09-01_15-59-39-DESKTOP-HQAG6T4-lab-binding/`
- `p1-submission-acceptance.json`
- `p2-multi-submission-acceptance.json`

Evidencia mais recente do RenderNode 3.71/WDDM 2.0 em Codigo 43:

- `docs/evidence/2026-09-01_16-02-37-DESKTOP-HQAG6T4-rendernode-inspect/rendernode-inspect.json`

## 3. Assinatura exata da falha atual

No candidato 3.71, usando contrato WDDM 2.0:

- `SchemaVersion=131`
- `V5ContractVersion=20`: o A/B foi realmente consumido pelo `DriverEntry`.
- `DxgkInitializeStatus=0`
- `AddDeviceStatus=0`
- `V5StartDeviceStatus=0`
- `V5StartDeviceSources=1`
- `V5StartDeviceChildren=1`
- `V5ChildRelationsCalls=1`, status 0, count 1
- `V5PostStartStage=3`
- `V5QaiCalls=0`: nem `DRIVERCAPS`, nem segmentos/caps posteriores foram solicitados.
- `V5QiCalls=8`: todas as oito interfaces opcionais retornaram `STATUS_NOT_SUPPORTED`.
- Nenhuma `CreateDevice`, `CreateContext`, `CreateAllocation`, `SubmitCommandVirtual` ou outra DDI de render foi tocada.
- Em seguida o `dxgkrnl` solicita teardown e o PnP publica Codigo 43.
- O teardown termina limpo: pending=0, reserved=0, submit-in-flight=0, `StopDeviceStatus=0`, `RemoveDeviceStatus=0`.

Sequencia observada:

```text
DriverEntry
  -> DxgkInitialize SUCCESS
  -> AddDevice SUCCESS
  -> StartDevice SUCCESS (1 source, 1 child)
  -> QueryInterface x8 (todas STATUS_NOT_SUPPORTED)
  -> QueryChildRelations SUCCESS (1 child + terminador)
  -> decisao interna ainda desconhecida do dxgkrnl
  -> StopDevice SUCCESS
  -> RemoveDevice SUCCESS
  -> Codigo 43
```

## 4. Hipoteses que ja foram fechadas

Nao repetir estas iteracoes sem nova evidencia:

1. **Fence pendente causando `STATUS_DEVICE_BUSY`: descartada.** Pending e reserved sao zero. O antigo erro era autoinfligido quando `DxgkCbSynchronizeExecution` falhava durante teardown; schema 129/130 comprovou e 3.67 corrigiu a consulta usando snapshot protegido.
2. **Teardown sujo como causa do Codigo 43: descartado.** Ele ocorre depois da decisao de remover e agora finaliza com sucesso.
3. **Contrato WDDM 2.7 como causa: descartado por A/B.** WDDM 2.0 e 2.7 morrem no mesmo `V5PostStartStage=3`. A unica diferenca e que 2.7 consulta uma vez `DXGKQAITYPE_WDDMDEVICECAPS`, enquanto 2.0 nao consulta QAI.
4. **Tamanho de `QueryChildRelations`: auditado e correto.** Foram escritos 56 bytes: um `DXGK_CHILD_DESCRIPTOR` de 28 bytes mais o terminador zerado obrigatorio.
5. **Quatro callbacks comuns ausentes:** 3.71 adicionou `DxgkDdiRenderKm`, `DxgkDdiCancelCommand`, `DxgkDdiSetPowerComponentFState` e `DxgkDdiPowerRuntimeControlRequest`; o hardware morreu no mesmo ponto.
6. **`DxgkDdiControlInterrupt2`:** ja adicionado anteriormente; nao mudou o ponto de falha.
7. **Adicionar `DxgkDdiLinkDevice`:** nao ha base; ele e opcional e o exemplo oficial RosKmd tambem nao o registra.
8. **Implementar cegamente as oito `QueryInterface`: proibido.** Elas sao opcionais e retornar `STATUS_NOT_SUPPORTED` pode ser correto. Deve haver prova de qual interface, se alguma, e obrigatoria nessa configuracao.
9. **Ausencia de UMD como causa imediata:** ainda nao comprovada e improvavel neste estagio, pois o adaptador e desmontado antes de caps, segmentos e abertura por aplicativo. O UMD sera necessario depois, nao deve ser usado para esconder a falha atual.

## 5. Trabalho principal pedido para a outra IA

### Fase A — identificar a decisao real do `dxgkrnl`

Preparar diagnostico WinDbg/KD para capturar por que o `dxgkrnl` desmonta o adaptador depois de `QueryChildRelations`.

Entregaveis esperados:

1. Um runbook na raiz ou em `docs/` com configuracao segura de kernel debugging para esta maquina, incluindo recuperacao.
2. Simbolos Microsoft configurados e simbolos privados/PDB do `bc250mcdm.sys` preservados no pacote de teste.
3. Script `.cmd`/`.ps1` ou arquivo de comandos WinDbg reproduzivel que:
   - limite a captura ao dispositivo `1002:13FE`/servico `bc250mcdm` quando possivel;
   - marque entrada/saida de `DriverEntry`, `AddDevice`, `StartDevice`, `QueryInterface`, `QueryChildRelations`, `StopDevice` e `RemoveDevice`;
   - capture stack e status no ponto onde o `dxgkrnl` decide parar/remover o adaptador;
   - capture o valor que resulta em `STATUS_INTERNAL_ERROR (0xC00000E5)`/Codigo 43, caso esse status continue presente;
   - nao altere registradores nem execute submissao adicional.
4. Evidencia contendo stack simbolizada, thread, status e condicao exata que falhou.
5. Uma conclusao que diferencie claramente:
   - callback/tabela obrigatoria ausente;
   - capacidade ou estrutura invalida;
   - inconsistencia source/child/VidPN;
   - requisito INF/UMD;
   - interface obrigatoria recusada;
   - falha interna anterior ou posterior a `QueryChildRelations`.

Nao basta registrar que `StopDevice` foi chamado: isso ja sabemos. A captura deve explicar **quem decidiu desmontar e qual validacao falhou antes disso**.

### Fase B — corrigir somente a causa comprovada

Depois da captura KD:

1. Produzir uma analise de causa raiz citando stack, status, campo/callback e evidencia.
2. Implementar a menor correcao coerente com o contrato WDDM.
3. Adicionar teste offline que falhe antes da correcao e passe depois.
4. Atualizar schema apenas se novos campos forem realmente necessarios.
5. Atualizar versao do candidato, manifesto, INF de laboratorio, pacote e assinatura de modo consistente.
6. Rodar build integral, testes do core/WDK, matriz DDI, auditoria fail-closed, auditoria de seguranca, package audit e verificacao de hash/assinatura.
7. Nao instalar automaticamente. Entregar o candidato pronto e assinado para o usuario executar a corrida fisica.

### Fase C — teste fisico de aceite do RenderNode

Antes de promover:

1. Recuperar para `BasicDisplay / Codigo 0` e fazer boot limpo.
2. Vincular o candidato com `RenderNode=0`, `ShaderDispatch=1`, `PersistentSubmission=1`, `GpuvaLabPromotion=0`.
3. Confirmar hash instalado igual ao pacote, schema correto e `bc250mcdm / Codigo 0`.
4. Executar os validadores P1 e P2 somente leitura e exigir novamente o tripe fisico.
5. Preparar `RenderNode=1`, reiniciar e executar o inspector.

Aceite minimo desta fase:

- `bc250mcdm / Codigo 0` apos boot com `RenderNode=1`;
- `V5RegistrationMode=1`;
- `StopDeviceEntered=0` durante a inicializacao estavel;
- progresso real alem de `V5PostStartStage=3`;
- consultas de caps/segmentos coerentes quando solicitadas;
- display continua funcional;
- nenhum TDR, BSOD, fence CPU falsa ou fallback de software.

Se continuar Codigo 43, a corrida falha e deve produzir evidencia nova que invalide ou refine a causa raiz. Nao chamar de progresso apenas porque compilou.

## 6. Depois que RenderNode permanecer em Codigo 0

Executar nesta ordem; nao pular diretamente para DXVK/RADV:

### 6.1 Objetos WDDM e memoria

- Validar `QueryAdapterInfo`/caps/segmentos realmente consumidos.
- Implementar e validar `CreateDevice`, `CreateContext`/`CreateContextVirtual`, allocations, residency e destruicao transacional.
- Promover GPUVA dinamica somente quando journal, generation, epoch e faixa viva estiverem coerentes.
- Validar map/unmap, paging e rollback sem vazar mappings ou reutilizar VA antigo.

### 6.2 Submissao originada pelo `dxgkrnl`

- Criar harness D3DKMT minimo, antes de um UMD completo:
  `OpenAdapterFromLuid -> CreateDevice -> CreateContextVirtual/HwQueue -> CreateAllocation -> Submit`.
- Exigir que a entrada em `DxgkDdiSubmitCommandVirtual`/HwQueue seja originada pelo `dxgkrnl`, nao chamada diretamente por teste offline.
- Validar IB gerado/relocalizado, GPUVA, ring fisico, `RPTR==WPTR`, poison check e dados.

### 6.3 Fence/retirement WDDM real

- Associar fence WDDM a uma fence escrita fisicamente pela GPU.
- Notificar interrupcao/DPC pelo caminho WDDM correto.
- Provar retirement pelo `dxgkrnl`, sem completar na CPU.
- Testar sequencia, wrap, cancelamento, timeout, reset e teardown com trabalho pendente.

### 6.4 UMD e Vulkan

- Somente apos KMD WDDM e D3DKMT funcionarem, definir o ABI do UMD e declarar corretamente DLLs/INF.
- Produzir ICD Vulkan funcional e validar loader, instancia, device, memoria, command buffers, submit e fence reais.
- RADV no Windows nao e um simples arquivo a copiar: sera necessario portar/adaptar WSI, winsys e ABI para este KMD/UMD.
- Depois de Vulkan real: Zink para OpenGL; DXVK para D3D9/10/11; VKD3D-Proton para D3D12.
- Jogos e LLMs sao testes finais, nao substitutos para os testes unitarios e de contrato.

## 7. Comandos de referencia

Recuperacao manual:

```powershell
C:\BC250_RECOVERY.cmd
```

Binding seguro do candidato em KMDOD, sem watchdog por decisao do usuario:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\bind-bc250mcdm-lab.ps1" -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -DisableWatchdog -IAcceptNoAutomaticRollback
```

Validacao somente leitura de P1/P2 usando a evidencia mais recente:

```powershell
$e = (Get-ChildItem "C:\Users\Daniel\Desktop\NamixWDDM\docs\evidence" -Directory | Where-Object Name -like '*-lab-binding' | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\test-p1-submission-acceptance.ps1" -EvidenceDir $e
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\test-p2-multi-submission.ps1" -EvidenceDir $e
```

Preparacao do contrato e RenderNode:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\set-bc250mcdm-wddm-contract.ps1" -ContractVersion 20 -Execute
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\prepare-bc250mcdm-rendernode.ps1" -Execute -IAcceptDisplayRenderBootRisk
```

Inspecao pos-boot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\inspect-bc250mcdm-rendernode.ps1"
```

## 8. Definicao de pronto para o proximo handoff

A outra IA deve devolver:

- causa raiz do Codigo 43 sustentada por evidencia KD/WinDbg;
- arquivos e linhas envolvidos;
- correcao minima com testes offline;
- candidato versionado, compilado, auditado, empacotado e assinado, sem instalacao automatica;
- roteiro exato de recuperacao, binding, promocao e coleta;
- criterios objetivos de PASS/FAIL;
- lista explicita do que continua **NAO VALIDADO EM HARDWARE**.

O resultado esperado desta etapa nao e ainda “rodar jogos”. E manter **WDDM display+render em Codigo 0**, abrindo o caminho para submissao real do `dxgkrnl`, fence WDDM fisica, UMD e Vulkan.
