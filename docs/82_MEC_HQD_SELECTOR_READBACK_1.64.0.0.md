# MEC/HQD selector readback — 1.64.0.0

## Resultado físico

O candidato executou a máquina transacional:

`PREFLIGHT → SNAPSHOT → RMW GRBM_GFX_CNTL → READBACK → ROLLBACK → READBACK`

Telemetria observada no silício:

- seletor original: `0x00000000`;
- seletor solicitado: `0x00000004`;
- primeiro readback: `0x00000000`;
- segundo readback: `0x00000000`;
- seletor restaurado: `0x00000000`;
- leituras/escritas: `5/2`;
- status: `BC250_HQD_SELECTOR_READBACK_FAILED` (`4`);
- apply readback: `0`;
- restore: `1`;
- physical ready: `0`.

Como os dois readbacks falharam, a transação não leu nem publicou o banco como
MEC. Nenhum campo HQD/MQD foi escrito. A evidência está em
`docs/evidence/2026-08-27_14-44-43-DESKTOP-HQAG6T4-mcdm-replace`.

## Interpretação

Os offsets e máscaras conferem com os headers GC10.1 locais: `mmGRBM_GFX_CNTL`
é `0x0DC2`, `BASE_IDX=0`, resultando em BAR5 `0x8088`; `MEID` usa bits 3:2.
O upstream seleciona o banco com `WREG32_SOC15_RLC_SHADOW`, que em bare metal
degrada para escrita MMIO direta. A medição mostra, porém, que nesta sessão do
BC-250 o registrador não ecoou `MEID=1`. Isso não autoriza assumir que a seleção
falhou nem que foi aplicada.

## Pacote e binding

- DriverVer: `08/27/2026,1.64.0.0`;
- SchemaVersion: `70`;
- source SYS: `FA8C2776B5B75405C54B5AE193E86D3CB6408BCC603EA1F440ECDC3B6F1F34C7`;
- signed SYS: `677CDF8897170215ACDBF5F4553EB8953497F8BF7661E1E3BDFD876CAEF1100B`;
- INF: `5CAF24DEAAE5A79A60D406B21F66C9B07FD4B2A7AAB732F2C545C7E5F3BFB206`;
- CAT: `8BEB1E3DF765A05BD949ECFC5DEA47DE0999318257F84569A563D908072864DD`;
- binding: `oem19.inf`, serviço `bc250mcdm`, Código 43 fail-closed;
- auditoria de binding: PASS.

## Próximo gate proposto

Para distinguir “seleção aplicada sem eco” de “escrita ignorada”, a próxima
prova deve usar bancos inativos e um campo HQD sem efeito de execução, com
poison, readback cruzado e restauração de ambos os bancos. Isso amplia o escopo
de escrita para HQD e exige autorização explícita antes do ensaio físico.

**NÃO VALIDADO EM HARDWARE.**
