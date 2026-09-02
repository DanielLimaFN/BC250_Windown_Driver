# Handoff para a proxima IA — apos o ensaio fisico do candidato 3.52

**Data:** 30/08/2026, 22:02 (America/Sao_Paulo)  
**Repositorio:** `C:\Users\Daniel\Desktop\NamixWDDM`  
**Hardware:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2)  
**Fase atual:** P1 — submissao persistente MCDM  
**Veredito global:** **NAO VALIDADO EM HARDWARE**

Este documento e autocontido. Ele registra o estado exato deixado pelo ensaio
3.52, as hipoteses ja encerradas e o proximo experimento recomendado. Nao
recomecar a investigacao pelos candidatos anteriores sem nova evidencia.

## 1. Barra de status vigente

```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ██████████████████▋░  93%   Estavel e Isolado
SDMA (DMA Engine & Robustez)     ███████████████████░  95%   Provado em Silicio
GCVM & Traducao de Memoria       ██████████████████░░  90%   PTE 0x10 provado; sem GPUVA dinamica
RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
Telemetria & Observabilidade     █████████████████░░░  85%   96 campos P1
Compute & Async Queues (MEC/MQD) ██████████████████░░  90%   Unhalt seletivo provado; fila ainda nao consome
Miniport WDDM / MCDM Producao    ███████░░░░░░░░░░░░░  35%   P1 apos candidato 3.52 fisico
=============================================================================================
Veredito Global: NAO VALIDADO EM HARDWARE
=============================================================================================
```

O compute one-shot observavel foi provado no silicio tres vezes. Isso nao
valida a fila persistente exigida por `DxgkDdiSubmitCommandVirtual`, nem torna o
miniport pronto para producao.

## 2. Leitura obrigatoria antes de alterar codigo

Ler nesta ordem:

1. `AGENTS.md`;
2. `BC250_HARDWARE_RULES.md`;
3. este handoff;
4. `docs/143_RESULTADO_FISICO_3.51_QUEUE_MASK_NAO_DESTRAVA.md`;
5. `docs/144_CANDIDATO_3.52_UNHALT_SELETIVO_PERSISTENTE.md`;
6. `docs/evidence/2026-08-30_22-02-28-DESKTOP-HQAG6T4-p1-submission/p1-submission-acceptance.json`;
7. `docs/evidence/2026-08-30_22-02-12-DESKTOP-HQAG6T4-lab-binding/probe-registry-at-bind.txt`;
8. `driver/wdk/bc250_submission_engine.c` e `.h`;
9. `driver/core/bc250_compute_mqd_contract.c` e `.h`;
10. `driver/core/bc250_persistent_ring_contract.c` e `.h`;
11. `docs/03_CATALOGO_DE_REGISTRADORES.md`.

## 3. Regras que nao podem ser relaxadas

Toda operacao de hardware segue:

```text
[PREFLIGHT] -> [SNAPSHOT] -> [APPLY] -> [READBACK] -> [PROVA NO SILICIO] -> [ROLLBACK/TEARDOWN]
```

- Nao fabricar fence, resultado, RPTR ou aposentadoria pela CPU.
- Nao usar WARP, loop em C ou fallback de software como prova de GPU.
- Nao escrever MMIO nao catalogado ou em quarentena.
- Escritas SOC15 devem preservar bits fora da mascara:
  `(lido & ~mask) | (or_val & mask)`.
- Falhar fechado se preflight ou readback divergirem.
- Compilacao, assinatura, Codigo 0 e display visivel nao provam compute.
- A prova P1 continua sendo o tripe fisico: `RPTR == WPTR`, fence GPU com
  poison check e resultado/integridade corretos.
- Conservar o snapshot e restaurar tudo no teardown.
- Manter literalmente **NAO VALIDADO EM HARDWARE** enquanto o tripe faltar.

## 4. Estado instalado e evidencias do 3.52

- Pacote: `3.52.0.0`.
- `SchemaVersion`: `123`.
- SHA-256 do SYS assinado:
  `94357936D791648E83C1EE6546D1B97AE731C45693B610065AA9126ED806941A`.
- Servico instalado: `bc250mcdm`.
- Dispositivo: Codigo 0.
- Display: presente; watchdog desarmado por confirmacao visual.
- Evidencia do bind:
  `docs/evidence/2026-08-30_22-02-12-DESKTOP-HQAG6T4-lab-binding/`.
- Evidencia P1:
  `docs/evidence/2026-08-30_22-02-28-DESKTOP-HQAG6T4-p1-submission/p1-submission-acceptance.json`.

O ensaio foi fisicamente valido, mas P1 foi **REPROVADO**.

### 4.1 O que passou

| Evidencia | Valor |
|---|---:|
| `P1EngineState` | `1` |
| `P1ActivateStatus` | `0` |
| submissao aceita | `1` |
| IB staged/readback | `45 dwords`, passou |
| WPTR planejado/shadow/MMIO | `12 / 12 / 12` |
| fence baseline escrita | `1` |
| coerencia HDP | `1` |
| readback GL2 | `0x00000000` |
| invalidate VMID0 ACK | `1`, em 1 poll |
| scheduler antes/depois PFP/preflight | `0x58504840` em todos |
| scheduler persistente aplicado | `0x585048C0` |
| `CP_MEC_CNTL` persistente | `0x40000000` |
| `CP_ME_CNTL` persistente | `0x15000000` |
| `QUEUE_MASK` no re-arme | apply/readback/pass = `1/1/1` |
| `HQD_ERROR` | `0` |

### 4.2 O que falhou

| Evidencia | Observado | Esperado |
|---|---:|---:|
| RPTR em memoria | `0` | `12` |
| RPTR MMIO | `0` | `12` |
| dispatch fence | `0xA55A0FF0` | `0xBC250D15` |
| resultado | `0x43DAFFFE` | `0xBC250C01` |
| mismatches | `4` | `0` |
| ring fence | `0` | `1` |
| retired | `0` | `1` |
| tripe/physical ready | `0/0` | `1/1` |

Depois da submissao:

```text
ACTIVE                  = 0x00000001
HQ_STATUS0              = 0x00008040
PQ_CONTROL              = 0xD030090D
DOORBELL_CONTROL        = 0x40000110
CP_PQ_STATUS            = 0x00000002
CP_CPC_STATUS           = 0xA0000082
HQD_ERROR               = 0x00000000
WPTR / RPTR             = 12 / 0
MEC_ME2 header          = 0xC0000A00
CP_MEC_CNTL             = 0x40000000
CP_ME_CNTL              = 0x15000000
RLC_CP_SCHEDULERS       = 0x585048C0
WPTR_POLL_CNTL / CNTL1  = 0 / 0
```

O re-arme completo repetiu `ACTIVE=0 -> QUEUE_MASK=1 -> ACTIVE=1`, manteve
esses estados, fez 20.000 polls e nao moveu o RPTR. Os poisons ficaram
inalterados.

## 5. O que o candidato 3.52 provou

O caminho persistente antigo zerava globalmente `CP_MEC_CNTL` e
`CP_ME_CNTL`. O 3.52 passou a aplicar a mesma politica seletiva do one-shot:
libera apenas MEC2 por RMW e preserva `CP_ME_CNTL`. O hardware confirmou
`0x40000000 / 0x15000000` depois da ativacao, depois do submit e depois do
re-arme.

Portanto, **unhalt global incorreto nao e a causa isolada do RPTR parado**.
Nao voltar a zerar os dois registradores e nao repetir esse candidato.

## 6. Hipoteses ja encerradas

Nao gastar novo boot tentando isoladamente:

| Hipotese | Resultado fisico |
|---|---|
| borda simples de `ACTIVE` | 3.45: nao destravou |
| coerencia HDP/GL2/GCVM por submit | 3.46/3.48+: passa; RPTR continua 0 |
| scheduler zerado pelo PFP | 3.47: ja era zero antes do PFP |
| publicar scheduler0 ME2/P0/Q0 | 3.48: aplicado; RPTR continua 0 |
| apenas observar MEC/ME/scheduler/poll | 3.49: sem fault; RPTR continua 0 |
| re-armar `QUEUE_MASK` junto com `ACTIVE` | 3.51: `1/1/1`; nao destravou |
| zerar globalmente MEC/ME era a causa | 3.52: seletivo provado; nao destravou |

O ensaio rotulado 3.50 foi invalidado por build hibrido e nao deve ser usado
como evidencia de hardware. O pipeline de build foi corrigido no 3.51.

## 7. Diferencial mais util contra o one-shot aprovado

No mesmo bind 3.52, o one-shot continua fisicamente aprovado:

```text
ShaderPhysicalReady                = 1
HqdActiveRptrAfter                 = 45
HqdActiveWptrApplied               = 45
HqdActiveCpMecCntlReadback         = 0x40000000
HqdActiveCpcStatusBeforeKick       = 0xA0000082
HqdActiveCpcStatusAfterKick        = 0xA0000482
HqdActiveCpcBusyStatAfterKick      = 0x04000000
HqdActiveCpcStalledStat1AfterKick  = 0x00000000
HqdActiveCpStatAfterKick           = 0x88000000
HqdActiveCpStalledStat1AfterKick   = 0x00000C00
```

O persistente fica em `CP_CPC_STATUS=0xA0000082`, exatamente o valor que o
one-shot tinha **antes** do kick. No one-shot, o kick adiciona `0x00000400`.
O header local `inc/linux_ref/gc_10_1_0_sh_mask.h` cataloga esse bit como
`CP_CPC_STATUS__TCIU_BUSY_MASK` (bit 10).

Isto nao prova a causa, mas e o melhor marcador diferencial atual: o kick
persistente publica WPTR, porem nao reproduz a transicao CPC/TCIU observada
quando o one-shot consome trabalho.

Ha ainda uma diferenca estrutural importante:

- one-shot: a fila contem diretamente os 45 dwords do dispatch;
- persistente: a fila contem 12 dwords, formados por `INDIRECT_BUFFER` para um
  IB separado de 45 dwords, seguido por `RELEASE_MEM` da ring fence.

RPTR=0 pode significar que o primeiro pacote nao foi buscado ou que o CP ficou
preso antes de aposenta-lo. Nao concluir qual dos dois sem telemetria.

## 8. Proximo candidato recomendado: 3.53 observacional

O proximo boot deve primeiro aumentar observabilidade, sem inventar uma nova
receita de escrita. Objetivo: provar se a fila aponta para o backing correto e
em qual fronteira o kick deixa de progredir.

### 8.1 Enderecos e conteudo do anel

Capturar depois da ativacao e depois do submit:

- `CP_HQD_PQ_BASE_LO/HI` lidos do HQD;
- base esperada codificada a partir de `Buffers.RingGpuAddress`;
- `CP_HQD_PQ_RPTR_REPORT_ADDR_LO/HI`;
- `CP_HQD_PQ_WPTR_POLL_ADDR_LO/HI`;
- VMID e `MQD_CONTROL` relevantes;
- endereco fisico completo do bloco, anel, IB, fence, RPTR e WPTR;
- os 12 dwords reais do anel persistente, lidos do mapeamento CPU depois da
  emissao e antes do kick;
- os 12 dwords esperados gerados pelo contrato e comparacao dword a dword;
- header e endereco completos do `INDIRECT_BUFFER`, tamanho `45`, VMID e bits
  de controle;
- readback dos 45 dwords do IB ja existe e deve continuar obrigatorio.

O codigo atual inicializa `RingGpuAddress` e `IbGpuAddress` com
`BasePhysical + offset`, sob o comentario de modo Host PA/VMID0. O one-shot
aprovado tambem pode operar com enderecos fisicos no ramo atual, embora a page
table de Contexto 0 seja montada e validada. Portanto, **nao afirmar ainda que
ha erro de dominio de endereco**. Provar especificamente se o endereco dentro
do pacote `INDIRECT_BUFFER` e interpretado no mesmo dominio que a base do HQD e
os enderecos PM4 diretos do one-shot.

### 8.2 Fronteira CPC/TCIU

Fotografar antes do kick, imediatamente depois e durante polls limitados:

- `CP_CPC_STATUS`;
- `CP_CPC_BUSY_STAT`;
- `CP_CPC_STALLED_STAT1`;
- `CP_CPF_STATUS`, `CP_CPF_BUSY_STAT`, `CP_CPF_STALLED_STAT1` se ja catalogados;
- `CP_STAT` e `CP_STALLED_STAT1`;
- PC MEC e headers ME2 ja allowlisted;
- `HQ_STATUS0`, `CP_PQ_STATUS`, `HQD_ERROR`, WPTR e RPTR.

Usar somente offsets ja catalogados. Se algum campo nao estiver no catalogo,
catalogar a partir dos headers locais e documentar antes de adiciona-lo ao
caminho de hardware.

### 8.3 Perguntas que o 3.53 precisa responder

1. O HQD le a mesma base de anel que o software acredita ter programado?
2. Os 12 dwords estao fisicamente presentes e corretos antes do kick?
3. O pacote `INDIRECT_BUFFER` carrega o endereco/VMID/tamanho esperados?
4. O kick persistente provoca alguma transicao de CPC, CPF, TCIU ou MEC PC?
5. O primeiro pacote e buscado e fica preso no IB, ou nem chega a ser buscado?

Somente depois dessas respostas escolher entre corrigir publicacao do WPTR,
dominio/endereco do IB, formato do pacote indireto ou programacao do HQD.

## 9. Locais principais de implementacao

- Motor persistente e self-test:
  `driver/wdk/bc250_submission_engine.c` e `.h`.
- Ativacao/desativacao MQD/HQD e snapshots globais:
  `driver/core/bc250_compute_mqd_contract.c` e `.h`.
- Planejamento/emissao dos 12 dwords:
  `driver/core/bc250_persistent_ring_contract.c` e `.h`.
- Builder PM4, inclusive `INDIRECT_BUFFER` e `RELEASE_MEM`:
  `driver/core/bc250_pm4_packet_builder.c` e `.h`.
- Telemetria:
  `driver/wdk/bc250_probe_telemetry.c` e `.h`.
- Testes:
  `driver/tests/compute_mqd_contract_test.c`,
  `driver/tests/persistent_ring_contract_test.c` e testes do packet builder.
- Aceitacao fisica:
  `tools/test-p1-submission-acceptance.ps1`.

## 10. Pipeline offline obrigatorio

O builder foi endurecido depois do falso ensaio 3.50. Nao voltar a linkar
objetos avulsos antigos. Usar exatamente:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission
powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-lab-package.ps1
python tools\audit-bc250mcdm-safety.py
```

O primeiro comando agora executa o `driver/wdk/build-contract.bat` completo,
confere timestamps de todos os objetos linkados e procura os campos novos
dentro do SYS. Antes de liberar hardware, registrar versao, schema e SHA-256.

## 11. Proximo ciclo fisico

A sessao atual foi mutada pelo bind 3.52. Antes de novo candidato, em terminal
elevado:

```powershell
powershell -ExecutionPolicy Bypass -File tools\rollback-bc250mcdm-lab.ps1 -Execute -IAcceptDisplayInterruptionRisk
shutdown /s /t 0
```

Depois de desligamento completo e power-on, confirmar `BasicDisplay` em Codigo
0. Entao:

```powershell
powershell -ExecutionPolicy Bypass -File tools\quarantine-bc250mcdm-lab-residue.ps1 -Execute -IAcceptExactResidueQuarantine
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission
```

Executar o comando de confirmacao emitido pelo bind apenas se o display estiver
visivel. Por fim:

```powershell
powershell -ExecutionPolicy Bypass -File tools\test-p1-submission-acceptance.ps1
```

Guardar integralmente os diretorios de evidencia. Cada alteracao de receita
fisica deve manter watchdog, snapshot, readback e rollback.

## 12. Estado do worktree

O worktree esta intencionalmente sujo e nao ha commit de consolidacao. Ha
arquivos modificados e arquivos novos desde o candidato 3.44, inclusive o
motor persistente, seus testes, scripts e evidencias. Essas mudancas pertencem
ao usuario.

**Nao executar** `git reset --hard`, `git checkout --`, `git clean` ou qualquer
limpeza ampla. Inspecionar `git status --short`, editar somente o necessario e
preservar alteracoes nao relacionadas.

## 13. Criterio de saida da P1

P1 so passa quando a evidencia fisica mostrar simultaneamente:

```text
RPTR em memoria == RPTR MMIO == WPTR publicado
dispatch fence: 0xA55A0FF0 -> 0xBC250D15 por escrita da GPU
resultado:       0x43DAFFFE -> 0xBC250C01, mismatches = 0
ring fence observada == fence atribuida
retired_count >= 1
triad_passed == 1
physical_ready == 1
```

Ate la, o estado correto e: fila aceita a submissao, mas nao ha prova de
consumo nem aposentadoria. **NAO VALIDADO EM HARDWARE.**
