# Transação VRAM reversível - pacote 1.8.0.0

Data: 2026-08-26.

Status: **PACOTE CONSTRUIDO; MUTACAO NAO VALIDADA EM HARDWARE**.

Primeiro ensaio físico da Fase 3. Depois de inventário completo e segundo
preflight aprovado, o miniport usa o offset de staging alocado pelo contrato e:

1. localiza unicamente o BAR VRAM resolvido pelo PnP;
2. valida `offset + 64` contra o comprimento atribuído;
3. mapeia o BAR pelo callback Dxgkrnl;
4. salva os 64 bytes originais;
5. escreve um padrão derivado dos bytes originais;
6. confirma o readback byte a byte;
7. restaura os 64 bytes originais e confirma a restauração;
8. desfaz o mapeamento.

Qualquer falha de readback/restauração permanece registrada e o driver continua
fail-closed. O teste não toca o scanout, a metade não visível, PSP, firmware,
GCVM, CP, SDMA ou PM4.

Gate físico esperado, schema 15:

- `VramProbeStatus=0`;
- `VramProbeBytes=64`;
- `VramProbeOffsetLow32=0x0FBBE000` (ou o offset derivado pelo novo preflight);
- `VramProbeReadbackPassed=1`;
- `VramProbeRestorePassed=1`;
- Code 43 intencional.

Hashes:

- source SYS: `A8B697F5281DB7FC57DB48B20650D1D86A8A6DB8BEF78A5EF02EA815E74FBD85`;
- signed SYS: `5AD910B742C6952E2FD812B05C0E9C0D5303DF0A7813F1F3235E7DE39DCAA4FA`;
- CAT: `DBA01ACFB8C754F385DE999C08645232F0352E47A41E7AA9FEFB6017AF52E1AA`.

Build KMD `/W4 /WX`, suíte offline e auditoria final: PASS. Aceleração continua
não validada.

## Resultado físico

Evidência: `docs\evidence\2026-08-26_01-41-23-DESKTOP-HQAG6T4-mcdm-replace`.

- schema 15, inventário e segundo preflight aprovados;
- offset do ensaio: `0x0FBBE000` dentro da abertura VRAM;
- 64 bytes escritos e lidos com sucesso;
- bytes originais restaurados e verificados;
- `VramProbeStatus=0`, readback `1`, restore `1`;
- SYS ativo igual ao pacote;
- zero staging de firmware, zero chamada PSP e Code 43 intencional.

Gate da primeira mutação VRAM owned: **PASS EM HARDWARE**.
