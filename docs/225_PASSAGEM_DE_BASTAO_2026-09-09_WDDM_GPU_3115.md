# Passagem de bastão — WDDM acelerado na BC-250 (fonte 3.115.0.0)

> **Data:** 2026-09-09  
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2), Windows 10 22H2 x64  
> **Estado:** implementação offline concluída; pacote lab `3.115.1.0` assinado e auditado, não instalado  
> **Veredito:** **NÃO VALIDADO EM HARDWARE**

## 1. Onde a corrida 3.114.1.0 parou

Evidência: `docs/evidence/2026-09-09_15-47-07-DESKTOP-HQAG6T4-lab-binding`.

| Perna | Antes | Depois | Resultado |
|---|---:|---:|---|
| Submit aceito | 17 | 18 | passou |
| Submit aposentado | 17 | 17 | falhou |
| WPTR publicado | 204 | 216 | passou |
| RPTR publicado | 204 | 204 | falhou |

O `MEC_PENDING_ARCHIVE` final mediu `Fence=18`, `Observed=0`, `RPTR=0`, `WPTR=216`,
`Active=1`, `Error=0`, `Vmid=0x900`. O IB e os canários não foram executados. No último
submit, a captura pré-doorbell já continha `CpcStalled=0x00040000`; depois do doorbell o
CPF mudou para busy, mas o anel não foi consumido.

O readback zero de `GRBM_GFX_CNTL` não autoriza concluir que o banco errado foi acessado.
Na mesma corrida, a transação de ativação de HQD passou com seletor solicitado 8 e o
`CP_HQD_VMID` retornou `0x900`. A afirmação causal do handoff 224 fica, portanto,
rebaixada a hipótese. Também não há prova de que o broadcast de `GRBM_GFX_INDEX` resolveu
o consumo: a corrida 3.114 continuou pendente.

A tela preta continua sendo uma parede separada: a UMD recusa `OpenAdapter10/10_2` com
`E_NOTIMPL`, e o DWM fail-fasta com `0x887A0004`. O watchdog fez rollback limpo para
BasicDisplay Código 0. Há marcador de boot frio obrigatório antes de qualquer novo bind.

## 2. Defeito de política removido no 3.115

Os candidatos 3.111 a 3.114 passaram a escrever, imediatamente antes da submissão:

- `RLC_PG_CNTL = 0`;
- `SPI_PG_STATIC_WGP_MASK = 0x1F`;
- `RLC_PG_ALWAYS_ON_WGP_MASK = 0x1F`, em cada SE/SH e em broadcast.

Essas escritas eram uma tentativa de eliminar `MEC2_WAIT_ON_RCIU_READ`, mas não fizeram
isso. O próprio catálogo local mostra que `0x1F` vem do patch opcional
`referencias/bc250-gfx1013-fix/patches/kernel/40cu-bc250-unlock.patch`, onde ele é usado
junto com a alteração de `CC_GC_SHADER_ARRAY_CONFIG` para liberar 40 CUs. O alvo deste
projeto é 24 CUs e esse segundo registrador permanece corretamente somente leitura.
Aplicar apenas metade do contrato de 40 CUs não tem sustentação.

O 3.115 remove as três escritas e devolve a política de harvest/power-gating ao firmware.
As leituras foram preservadas e o log agora declara
`WGP_PG_READONLY ... Policy=FIRMWARE_PRESERVED`.

## 3. Proteções contra regressão

- Os três registradores saíram da allowlist de escrita do transporte MMIO e continuam na
  allowlist de leitura.
- `mmio_topology_transport_test` exige que tentativas de escrita sejam recusadas e que os
  valores originais permaneçam intactos.
- `audit_firmware_wgp_policy_is_readonly` recusa reintrodução na allowlist ou escrita no
  caminho `PrepareComputeVmid`.
- `test-wgp-policy-safety.py` provou por mutação: reabrir SPI, reintroduzir a escrita raw de
  RLC ou remover o veredito explícito faz o gate falhar.

## 4. Validação offline

| Gate | Resultado |
|---|---|
| `tools/test-wgp-policy-safety.py` | PASS, três mutações rejeitadas |
| `tools/audit-bc250mcdm-safety.py` | PASS |
| `driver/wdk/build-contract.bat` | EXIT=0, suíte completa e KMD linkado |

O INF fonte foi elevado de `3.114.0.0` para `3.115.0.0`. Depois da autorização explícita
do operador, a imagem de bring-up foi compilada e o pacote lab `3.115.1.0` foi assinado:

| Artefato | SHA-256 |
|---|---|
| SYS assinado | `6E0F0794401C7ABBA9CA12A0A6F83FA8920CCC9F1A8C0E5106B8EBA45291C652` |
| UMD assinada | `2A25BE2C6D807917BDCA2D8A03BDDEA94FB3DD018B312EC1FDAF1AD5D460969B` |
| INF | `6E74098819A21A755C57C580AEBAB6BD726E8180C7DDD05F4E65FB435AD445C2` |
| CAT | `E60892E8BB675804D0DCA0934B79F291024D8B7CB01B81DCFA28A23B6871A0EB` |

`audit-bc250mcdm-lab-package.ps1`, `audit-umd-node1-submission.py` e a auditoria da
política WGP passaram. Nenhuma instalação ou ação física ocorreu porque o preflight mediu
`CLEAN_BOOT_REQUIRED`: boot atual `15:46:14`, rollback concluído depois, às `15:50:10`.

## 5. Próximo portão

O ensaio foi autorizado e executado às 16:55. A linha `Policy=FIRMWARE_PRESERVED` apareceu
com `SPI=0xFFFF`, `RLC=0x3`; `CpcStalled` permaneceu zero antes/depois do doorbell e no TDR.
Mesmo assim, `WPTR` foi de 0 para 216 no registro físico e `RPTR` ficou em 0; na telemetria
do runner, `204->216` contra `RPTR 204->204`, sem aposentadoria, fence ou canários.

O 3.115 **corrigiu o estado especulativo e eliminou o stall observado**, mas **não resolveu
o consumo do anel**. `PqControl=0xD030090D` já tem `PQ_EMPTY=0`. O próximo discriminante é
a incoerência entre cursor lógico inicial 204 e o par físico `RPTR=0/WPTR=0` imediatamente
antes da publicação. Não repetir o pacote 3.115.1.0.

Análise completa: `docs/evidence/2026-09-09_16-55-02-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md`.
O watchdog restaurou BasicDisplay Código 0 e exigiu novo boot frio.
