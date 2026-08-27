# Análise da captura ampliada

Captura: 2026-08-22 19:10:17. Último boot do Windows observado:
2026-08-22 15:45:51. Logo, esta execução não ocorreu depois de reinicialização
ou ciclo frio.

## SMU

O preflight repetiu exatamente o estado anterior:

- Q0 CMD = `0x37` (`GetGfxFrequency`);
- Q0 ARG = `0`;
- Q0 RSP = `0`.

O coletor não enviou mensagens. Esta captura confirma persistência do mesmo
estado dentro do mesmo boot, não uma falha reproduzida após reset de energia.
O transporte permanece **NÃO VALIDADO EM HARDWARE**.

## Ring SDMA externo

Os registradores ampliados permitiram decodificar:

- ring base: `GPU 0xF40FD00000`;
- RPTR writeback: `GPU 0xF40FD02000`;
- WPTR poll: `GPU 0xF40FD02000`;
- `RB_CNTL = 0x00001001` (enable e RPTR writeback enable);
- `WPTR_POLL_CNTL = 0x00000004` (poll enable);
- `IB_CNTL = 1`;
- doorbell desativado;
- `MINOR_PTR_UPDATE = 0` no endereço confirmado `0x04C54`.

Os dois ponteiros externos apontam para o mesmo endereço, 8 KiB depois da
base do ring. Isso é suspeito: um único dword não deve ser promovido
automaticamente a fonte simultânea de WPTR produzido pela CPU e RPTR produzido
pela GPU. Pode ser configuração deixada por firmware, por driver anterior ou
por uma tentativa desta mesma sessão de boot.

Nenhum conteúdo de VRAM foi lido e nenhum registrador foi escrito. Não há
prova de consumo de ring, fence ou resultado. SDMA permanece
**NÃO VALIDADA EM HARDWARE**.

## Decisão

Não reutilizar nem modificar esse ring antes de uma captura feita após ciclo
frio real. A próxima coleta deve registrar o horário do último boot e comparar
base, ponteiros e controles. Se os endereços persistirem desde o POST, podemos
estudar observação passiva do dword `0xF40FD02000`; se mudarem, ficam
classificados como resíduo de software anterior.
