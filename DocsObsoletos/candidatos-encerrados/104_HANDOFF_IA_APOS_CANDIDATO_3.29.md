# Handoff para próxima IA — resultado físico 3.29 e retomada

Data local: 30/08/2026  
Último pacote ensaiado: **3.29.0.0 / SchemaVersion 100**  
Veredito: **NÃO VALIDADO EM HARDWARE**

## 1. Objetivo inegociável

Chegar a um shader realmente executado nos CUs com, no mesmo ciclo físico:

1. resultado saindo do buffer envenenado e conferido byte a byte;
2. `RPTR == WPTR == ShaderDispatchDwords`;
3. fence física posterior ao `DISPATCH_DIRECT` saindo do poison;
4. rollback de memória e registradores comprovado;
5. nenhum avanço de ponteiro, fence ou resultado simulado pela CPU.

Enquanto qualquer item faltar, declarar **NÃO VALIDADO EM HARDWARE**.

## 2. Estado físico ao encerrar

O watchdog do ensaio 3.29 concluiu rollback com sucesso:

```text
watchdog state             ROLLED_BACK
detail                     BasicDisplay restoration completed
exit_code                  0
device                     BasicDisplay / Código 0
hardware_validated         false
```

O pacote `oem19.inf` foi removido pelo watchdog. Como nos ciclos anteriores,
considere provável a existência de `bc250mcdm.sys` residual bloqueado em
`C:\Windows\System32\drivers` após o unload. Antes de novo binding:

1. reboot manual pelo usuário;
2. `quarantine-bc250mcdm-lab-residue.ps1` se o SYS existir;
3. `audit-bc250mcdm-prebinding.ps1 -LabPackage` deve terminar em PASS;
4. nunca executar uma segunda submissão física no mesmo boot.

## 3. O que o 3.29 mudou

O candidato 3.29 adicionou a etapa que faltava da sequência KFD:

```text
CP_HQD_EOP_RPTR (dw 0x1FD1, BAR5 0xC8C4)
    INIT_FETCHER[31] = 1
    imediatamente antes de CP_HQD_ACTIVE.ACTIVE = 1
```

Foi implementado com:

- entrada prévia no catálogo de registradores;
- allowlist de leitura/escrita do transporte MMIO;
- snapshot antes do APPLY;
- readback tolerando auto-clear de `INIT_FETCHER` e o status
  `RPTR_EQ_CSMD_WPTR[30]`;
- rejeição de RPTR, RESET ou DEQUEUE inesperados;
- restauração exata depois de desativar a fila;
- cinco campos novos de telemetria;
- teste sticky e teste de auto-clear;
- SchemaVersion 100 e pacote 3.29 assinado.

Arquivos centrais:

- `driver/core/bc250_compute_mqd_contract.c`
- `driver/core/bc250_compute_mqd_contract.h`
- `driver/wdk/bc250_mmio_topology_transport.c`
- `driver/wdk/bc250_probe_telemetry.h/.c`
- `driver/wdk/bc250_active_miniport.c`
- `driver/tests/compute_mqd_contract_test.c`
- `driver/tests/mmio_topology_transport_test.c`
- `tools/test-shader-dispatch-acceptance.ps1`
- `tools/package-bc250mcdm-lab.ps1`
- `docs/03_CATALOGO_DE_REGISTRADORES.md`

Registro offline anterior: `docs/103_CANDIDATO_3.29_INIT_FETCHER_EOP.md`.

## 4. Resultado físico exato do 3.29

O binding entrou em `bc250mcdm / Código 0`, com o hash correto e display
preservado. O resultado foi **FALHA**:

| Evidência | Valor físico |
|---|---:|
| Schema | `100` |
| `HqdActiveStatus` | `7 / PM4_NOT_CONSUMED` |
| EOP_RPTR antes | `0x40000000` |
| EOP_RPTR aplicado | `0x80000000` |
| EOP_RPTR readback | `0x40000000` |
| EOP_RPTR restaurado | `0x40000000` |
| rollback EOP | `1 / PASS` |
| `ACTIVE` aplicado | `1` |
| doorbell | índice `0x44`, offset `0x110`, valor `40` |
| direct WPTR readback imediato | `40` |
| WPTR amostrado durante polling | `0` |
| RPTR durante polling | `0` |
| `HQ_STATUS0` após kick | `0x40000000` |
| `HQ_STATUS0` final | `0x40008000` |
| `PQ_CONTROL` final | `0xD030890D` (`PQ_EMPTY` ativo) |
| `HQD_ERROR` / UTCL1 faults | `0` |
| MEC PC | 29 transições / 196 amostras |
| ring GPUVA | `0x10000` |
| ring físico | `0xF400AA0000` |
| PTE | `0xF400AA00E1` |
| tradução/readback do ring | PASS / zero mismatches |
| resultado | `0x43DAFFFE` — poison intacto |
| mismatches do resultado | `4` bytes |
| fence | `0xA55A0FF0` — poison intacto |
| memória restaurada | `1 / PASS` |
| rollback de registradores interno | `0 / FAIL` |
| tripé / PhysicalReady | `0 / FAIL` |

Conclusão estrita: `INIT_FETCHER` chegou ao silício, auto-limpou e deixou
`RPTR_EQ_CSMD_WPTR` ativo, mas isso não iniciou o packet queue fetch. A fila
ficou `QUEUE_IDLE/PQ_EMPTY`, e o WPTR direto, embora lido como 40 imediatamente,
voltou a zero nas amostras seguintes. Resultado e fence nunca mudaram.

## 5. Evidência canônica

Diretório:

`docs/evidence/2026-08-30_09-04-59-DESKTOP-HQAG6T4-lab-binding/`

Arquivos principais:

- `probe-registry.json` — captura integral Schema 100;
- `probe-registry.txt` — dump bruto do registro;
- `shader-dispatch-acceptance.json` — veredito fail-closed;
- `device-after.json` — `bc250mcdm / Código 0` durante o ensaio;
- `watchdog-state.json` — rollback final;
- `watchdog-rollback.txt` — remoção do OEM e retorno ao BasicDisplay;
- `setupapi-after-tail.txt` — log de binding.

Quarentena prévia:

`docs/evidence/20260830-090441-DESKTOP-HQAG6T4-f8b2-preflight/`

## 6. Análise mais forte para o próximo candidato

Não empilhar mudanças especulativas. O próximo candidato deve tratar primeiro
o protocolo de publicação de WPTR e adicionar diagnóstico de rollback.

O helper oficial `kgd_gfx_v9_hqd_load` faz, nesta ordem:

1. carrega o intervalo completo de registradores HQD do MQD;
2. ativa a lógica de doorbell;
3. escreve WPTR inicial igual ao RPTR;
4. escreve `CP_HQD_PQ_WPTR_POLL_ADDR_LO/HI`;
5. habilita a fila em `CP_PQ_WPTR_POLL_CNTL1` com
   `1 << (pipe_id * num_queue_per_pipe + queue_id)`;
6. dispara `EOP_RPTR.INIT_FETCHER`;
7. ativa o HQD.

Para MEC1/pipe0/queue0, a máscara candidata é `1 << 0 = 1`, mas isso deve ser
reconciliado com a topologia real antes de APPLY.

Nosso caminho atual diverge:

- zera/desabilita `CP_PQ_WPTR_POLL_CNTL.EN`;
- não cataloga nem programa `CP_PQ_WPTR_POLL_CNTL1` (`dw 0x1E24`, candidato
  BAR5 `0xC210`);
- publica o shadow WPTR somente depois de `ACTIVE`;
- depende de doorbell e depois de escrita direta de WPTR;
- o 3.29 provou que a escrita direta é transitória: readback imediato 40,
  amostras posteriores 0.

Hipótese recomendada para **3.30**, ainda não implementada nem validada:

```text
snapshot global POLL_CNTL + POLL_CNTL1
programar shadow WPTR=40 antes de ACTIVE
programar WPTR inicial=RPTR=0
programar endereços de polling
habilitar a máscara one-shot da fila em POLL_CNTL1
configurar POLL_CNTL com máscara SOC15 preservando bits/status
INIT_FETCHER
ACTIVE
observar se WPTR fixa em 40 e PQ_EMPTY cai
rollback LIFO exato dos dois controles globais
```

Não reutilizar cegamente `0x00403004`: os headers locais definem `EN` no bit
31, portanto esse literal histórico não contém `EN`. Há documentação antiga e
contraditória no repositório. Derivar o valor exclusivamente de masks GC10.1,
estado original e sequência upstream.

Fonte primária:
`https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/amdgpu_amdkfd_gfx_v9.c`

## 7. Falha de rollback interno que precisa ser localizada

O watchdog externo restaurou a máquina, e os seguintes rollbacks internos
passaram: memória, EOP_RPTR, baseline/direct WPTR, scheduler RLC e contexto
GCVM observado. Porém `HqdActiveRestorePassed=0` e
`ShaderRegisterRestorePassed=0`.

O código hoje só guarda um booleano agregado. Antes do próximo ensaio, adicionar
telemetria `FirstRestoreFailureStage/Offset/Expected/Observed`, incluindo:

- restore dos 16 registradores HQD;
- ponteiros e `ACTIVE`;
- `CP_PQ_STATUS`;
- `CP_PQ_WPTR_POLL_CNTL` e futuro `CNTL1`;
- GCVM;
- `CP_MEC_CNTL`/`CP_ME_CNTL`;
- seletor GRBM.

O aceite continua exigindo rollback interno e externo; não mascarar essa falha.

## 8. Estado do build e pacote

Antes do ensaio, toda a suíte passou e o pacote foi auditado:

```text
DriverVer             08/30/2026,3.29.0.0
Schema                 100
Package SYS SHA-256    20B38038B8C89F857FACBCB4076A4F0E83B1FE9534A38396DCE6E53D0439A2FB
Package INF SHA-256    AA59F0F0CB94BFBAD466C64E8AB2100F798320A3C9449DF512B44BA846EBB37C
Package CAT SHA-256    1F096A0037967C1222E2369EED15E8292907E1DEBC1EDDCE965CC2B49803ED82
```

O worktree está sujo e contém todo o desenvolvimento dos candidatos anteriores,
documentos e evidências. Não apagar, resetar ou sobrescrever mudanças do usuário.

## 9. Sequência de retomada sugerida

1. Ler integralmente `AGENTS.md`, `BC250_HARDWARE_RULES.md`, este handoff e os
   documentos 102/103.
2. Confirmar `BasicDisplay / Código 0` e aguardar reboot manual antes de tocar
   novamente no hardware.
3. Implementar primeiro a telemetria de falha de rollback.
4. Auditar/catalogar `CP_PQ_WPTR_POLL_CNTL1` e sua máscara de fila.
5. Construir candidato 3.30 de variável única: protocolo one-shot de WPTR na
   ordem upstream.
6. Rodar suíte completa, build lab, assinatura e auditoria.
7. Um único ensaio físico com watchdog em boot limpo.
8. Aprovar somente o tripé completo, comparação byte a byte e rollback.

**NÃO VALIDADO EM HARDWARE.**

