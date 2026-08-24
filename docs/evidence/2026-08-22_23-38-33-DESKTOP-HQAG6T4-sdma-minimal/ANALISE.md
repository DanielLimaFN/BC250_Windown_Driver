# Preflight ABI 4 e telemetria readonly

Esta captura não executou firmware, ring ou comando. O ensaio mínimo foi
recusado antes de `arm write` porque a sessão ainda continha WPTR SDMA `0x44`,
resíduo já conhecido do teste anterior. `RB_CNTL=0x80840000` estava desabilitado
e a base era zero. A recusa confirma o novo preflight fail-closed.

O laboratório ABI 4 foi instalado em `readonly`. A captura standalone de
telemetria registrou:

- `SMUIO_GFX_MISC_CNTL=0x00000000`, estado físico `0` (GFXOFF);
- `GRBM_STATUS=0x00003028`;
- `SDMA_STATUS_REG=0x46DEE557` e `RB_CNTL=0x80840000`;
- RPTR `0`, WPTR `0x44`, writeback/shadow zero;
- F32 fora de HALT e PC variável porque o firmware da sessão anterior persiste;
- `CLK_CTRL=0xFF0C0100`;
- SMU gfxclk bruto `1500`, ActiveWgp `0`, features `0xDD602C7D`, todos com
  status `1`;
- térmica não calibrada e potência indisponível.

O caminho de leitura física de GFXOFF respondeu, mas só o estado ocioso foi
observado. Falta reboot limpo e uma captura before/kick/after durante nova carga.
O campo `reboot_required` do JSON foi corrigido para `true`: esta execução não
carregou firmware novo, mas detectou o firmware persistente da sessão anterior.

**NÃO VALIDADO EM HARDWARE**
