# MEC/HQD readonly preflight — 1.62.0.0

## Objetivo

Fechar a etapa de observabilidade anterior à programação de uma fila MEC sem
alterar seleção, HQD, MQD ou doorbell. Este candidato não ativa compute e não
submete PM4 pelo MEC.

## Contrato implementado

- mapeamento do BAR5 pelo transporte PnP existente;
- duas passagens de 13 leituras cada;
- `GRBM_GFX_CNTL` capturado antes e depois de cada passagem;
- rejeição de leitura instável, `0xFFFFFFFF`, fila herdada ativa, falha de
  transporte ou qualquer contagem de escrita diferente de zero;
- sucesso MEC somente quando `(GRBM_GFX_CNTL & 0x7FF) == 0x4`, correspondente
  ao candidato MEC1/pipe0/queue0/VMID0;
- status separado `SELECTOR_NOT_COMPUTE` quando o snapshot é estável e inativo,
  mas pertence a outro banco.

Os offsets HQD autorizados para leitura são `0xC824`, `0xC828`, `0xC82C`,
`0xC830`, `0xC844`, `0xC848`, `0xC84C`, `0xC860`, `0xC868`, `0xC8FC` e
`0xC900`. Nenhum deles está na allowlist de escrita.

## Evidência offline

- `driver\wdk\build-contract.bat`: PASS;
- auditorias estrutural e de segurança: PASS;
- Inf2Cat: zero erros e zero avisos;
- auditoria de integridade do pacote: PASS;
- DriverVer: `08/27/2026,1.62.0.0`;
- SchemaVersion de telemetria: `68`;
- source SYS SHA-256: `7901F566C0E729DBB3793AC0908973C5235719F8426979D082AE5249243146BF`;
- signed SYS SHA-256: `F9B9FE2D89D1B8609F5911B2B9FD913286094C80512953C3DA34632A7B749BA3`;
- INF SHA-256: `1245B471A58BA431F9EEFDD4498C18D33804F5FC505A2AE2347DF64D163CF98A`;
- CAT SHA-256: `39D4733EC593DA2099C6CBAC9EF7D7AF84C4D248EFE86FB8D0B7865C72B78981`.

## Próximo gate

Instalar o candidato pelo fluxo transacional existente e coletar o snapshot
físico. Se o seletor continuar em `0`, a captura serve apenas para provar o
estado do banco atualmente selecionado. Uma transação futura de seleção MEC
deverá ter snapshot, RMW mascarado, readback e rollback próprios antes de
qualquer programação MQD/HQD.

**NÃO VALIDADO EM HARDWARE.**
