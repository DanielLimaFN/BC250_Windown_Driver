# Análise do primeiro baseline com SMU responsivo

Captura: 2026-08-22 19:16:29, 232 segundos depois do boot.

## Marco confirmado no laboratório novo

O transporte SMN e o mailbox SMU Q0 do `bc250dbg` responderam em hardware:

- TestMessage: status `1`, resultado `1`;
- SMU version: `0x00580600`;
- driver interface: `8`;
- QueryGfxclk: `1500`;
- GetGfxFrequency: `1500`;
- GetGfxVid: `99` (valor bruto, sem conversão para tensão);
- QueryActiveWgp: `0`;
- GetEnabledSmuFeatures: `0xDD602C7D`.

O bit 2 da máscara está ligado e as referências locais identificam esse bit
como GFXOFF. A resposta zero de ActiveWgp é coerente com GFX indisponível, mas
não distingue GFXOFF, DPM ausente, power gating ou bloqueio de WGP. O clock
reportado não prova clock físico sob carga.

Assim, o **transporte SMU Q0 está VALIDADO EM HARDWARE** para essas consultas.
GFXOFF físico, WGP utilizável e execução de engine continuam
**NÃO VALIDADOS EM HARDWARE**.

## Contaminação pelo driver antigo

O dispositivo PCI estava ligado ao KMD `bc250v4`:

- descrição: AMD BC-250 High Performance Graphics Driver V4;
- versão: `1.0.822.1737`;
- INF: `oem48.inf`;
- binário: `C:\Windows\System32\drivers\bc250wddm.sys`;
- SHA-256: `3e09e17ce9bb7e72c067a9d0e88a8a1c11e9513a8556f9d6589226ea7b386c3c`.

O registro desse serviço informa `HwInitFirmware=1`, `HwInitGfxRing=1`,
`HwInitSdmaRing=1` e `Hw_SdmaRingVramOffset=0x0FD00000`. O código de
`referencias/BC250-V3` usa exatamente esse offset. Portanto, o ring SDMA em
`GPU 0xF40FD00000` e seus controles foram programados pelo driver antigo, não
pelo POST nem pelo `bc250dbg`.

Campos do registro antigo como `Hw_AccelerationAvailable`, bandwidth, fences
ou sucesso de firmware não são aceitos como prova do driver novo. Eles são
somente pistas para auditoria.

## Consequência

O baseline é válido para provar que o laboratório novo acessa PCI, MMIO, PSP,
SMN e SMU. Ele não é um baseline limpo para estado inicial de GC/CP/SDMA/RLC,
porque `bc250v4` executou antes da coleta e permanece ativo.

Não realizar qualquer teste ativo do `bc250dbg` enquanto `bc250v4` estiver
ligado ao mesmo PCI. Dois drivers programando a mesma GPU não possuem
coordenação de locks, memória, rings, reset ou rollback.
