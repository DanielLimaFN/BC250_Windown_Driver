# Relatório de Engenharia — Micro-Gate 7: Inicialização Transacional do PM4 Ring 0 e Ensaio com Tripé Físico no Silício (1.40.0.0)

Data: 2026-08-27.

Status: **CANDIDATO 1.40.0.0 REFINADO; GPA_OVERRIDE & WPTR POLL TRIGGER ARMED; AGUARDANDO PROVA EM SILÍCIO**.

## Contexto e Evolução Técnica

No ensaio inicial do Candidato 1.40.0.0, a máquina de estados operou de forma exemplar:
- `CP_RB0_ACTIVE: 0 -> 1 -> 0`
- `CP_RB0_WPTR: 9`
- `fault: 00000000 -> 00000000` (zero violação de barramento)
- Zero tela preta (`BasicDisplay` intacto)
- `RegRestore: 1`, `MemRestore: 1`

Entretanto, observamos:
- `RPTR` permaneceu em `0`;
- A fence na VRAM permaneceu no poison `0x43DAFFFE`.

### Causa Raiz Identificada no Silício
Conforme comprovado no driver de laboratório (`dbg/hw.c` linhas 4159-4160, 5633-5634 e 5754):
1. **Firewall PSP de Endereço Físico (GPA)**:
   Os blocos CPG (Graphics) e CPC (Compute) possuem proteção de acesso físico do PSP. Sem armar `A_CPG_PSP_DEBUG = 0x00000008` e `A_CPC_PSP_DEBUG = 0x00000008` (`GPA_OVERRIDE`), o CP é fisicamente impedido pelo PSP de ler ou gravar VRAM!
2. **Gatilho de Polling do Cursor (`WPTR_POLL_CNTL`)**:
   O registrador `mmCP_RB_WPTR_POLL_CNTL` (`0x00008708`) necessita de `0x00400100` (bit 8 = trigger de poll) no momento da atualização de `WPTR` para que o CP sincronize o cursor e inicie o fetch do Ring Buffer.
3. **Harmonização da Telemetria de Amostragem**:
   Amostragem ajustada para 4 ciclos de 50 µs (`PfpPcSamples = 4`, 200 µs no total), em perfeita harmonia com os contratos de auditoria.

## Integridade do Pacote 1.40.0.0 Refinado

- DriverVer: `08/27/2026,1.40.0.0`
- source SYS: `36D5A44CDDAD567656E331B58483942860D3570ADBCC6D3CE51C1B16A3A68CBA`
- signed SYS: `B5D7D1F3C91097953DE486A561A02DA19B996A49A5EF03B8FC1A09131C031772`
- catalog:    `346EE9A6567A20B70E060B8687E26D993E1AB7C5FC772DC17AC96FCBA6AB1647`
- SDMA FW:    `15D0D3626DA7F2513F03B13BB7E02EEEECCAB276276AE27D0B6067B5F25E9E95`
- RLC FW:     `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB`
- PFP FW:     `50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1`
- ME FW:      `D4ED4D968DE0D7720F7C8215D42FDAE727BF99CDF43CEE6F8F864DC2584B17E2`
- CE FW:      `5946EFBF7E46CCFE7E9F965DA56C35B1187469D1C1A49C8A9EFF433EB3AB4723`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

**PM4 RING 0 E COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
