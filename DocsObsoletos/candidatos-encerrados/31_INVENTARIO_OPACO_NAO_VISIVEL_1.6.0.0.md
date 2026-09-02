# Inventário opaco da VRAM não visível - pacote 1.6.0.0

Data: 2026-08-25.

Status: **PACOTE CONSTRUIDO; NAO VALIDADO EM HARDWARE**.

Após duas capturas idênticas, incluindo uma pós-reboot, GCVM, CP, SDMA e GPCOM
não possuem estruturas herdadas ativas. A TMR interna do PSP continua sem
descrição Windows autoritativa. Para avançar sem usar o offset histórico Linux,
o miniport reserva conservadoramente toda a VRAM não visível pela CPU.

Com a topologia física observada, a política produz:

- VRAM total: 512 MiB;
- abertura CPU-visible: 256 MiB;
- reserva opaca: `[256 MiB, 512 MiB)`, 256 MiB;
- reserva scanout: 11.059.200 bytes no início da abertura;
- região candidata livre CPU-visible: 256 MiB menos o scanout.

Os limites vêm apenas do snapshot RCC/GCMC e do BAR PnP/ReBAR. Não há constante
TMR. O inventário somente será considerado completo se as três fontes de
evidência estiverem presentes e ambas as reservas passarem bounds/overlap.

Telemetria schema 13 acrescenta bytes livres totais e CPU-visible.

- source SYS: `5D37F43EB3E11E30784502DD38B767A6F9996AE4F81CC9F2FBBF936147B9ED93`;
- signed SYS: `C1887B445DEFDE80E6829C578732979E92D97B0F1E5507BC90AA336CFFEA92EB`;
- CAT: `476F965747076C8E18E6B78618C565FB09936B1DFBFEE9963ECEFCE0ECC19E95`;
- build KMD `/W4 /WX`, testes e auditoria: PASS.

Não há alocação, escrita VRAM, GPUVA, PM4 ou compute neste pacote.

## Resultado físico

Evidência: `docs\evidence\2026-08-25_20-46-41-DESKTOP-HQAG6T4-mcdm-replace`.

- schema 13 e hash do SYS ativo iguais ao pacote;
- `RuntimeInventoryStatus=0`, flags `0x7`, completo;
- duas reservas, total `279.494.656` bytes;
- bytes livres totais/CPU-visible: `257.376.256` (`0x0F574000`);
- scanout continua com `11.059.200` bytes;
- `RuntimeMissing=1` ainda é emitido pela primeira passagem do preflight;
- Code 43 continua intencional; nenhuma escrita ou aceleração ocorreu.

Gate de inventário integral: **PASS EM HARDWARE**.
