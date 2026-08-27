# Análise da captura de baseline

Data da captura: 2026-08-22. Esta análise trata os valores como observações de
estado deixado pelo firmware/POST. Ela não atribui inicialização ao
`bc250dbg` e não demonstra execução de comandos.

## O que foi confirmado

- O dispositivo PCI é `1002:13FE`, revisão `00`, em `01:00.0`.
- O BAR5 está mapeado em `0xFE800000`, com 512 KiB.
- `GPU_ID = 0x9FFF9700`.
- O mapa SOC15 é o mapa coerente nesta placa: `GRBM_STATUS` em `0x08010`
  respondeu `0x00003028` e `CC_GC_SHADER_ARRAY_CONFIG` em `0x089BC`
  respondeu `0xFFF80000`; os offsets legados retornaram zero.
- O PSP/TOS está pronto: `C2PMSG_64 = 0x80020000` (bit 31 ligado).
- O PSP já possui um anel de 4 KiB em `0x000000044FFCC000`, com WPTR
  `0x80`. O anel não foi criado pelo `bc250dbg` e não deve ser sobrescrito ou
  destruído.
- A abertura de sistema observada é `GPU 0xF400000000..0xF420000000`.
  O contexto GCVM 0 está desativado, sem page table configurada, e o status de
  fault lido foi zero.
- Existe configuração SDMA0 deixada pelo POST: `RB_CNTL = 0x00001001`,
  `RB_BASE(raw) = 0xF40FD000`, que codifica `GPU 0xF40FD00000`, e
  `WPTR = 0x44C`.

## O que os valores não provam

- Os zeros de RPTR writeback e WPTR shadow exibidos pela versão capturada da
  ferramenta não pertencem necessariamente ao anel externo. O driver somente
  sabia ler os shadows do anel criado por ele próprio. Portanto, a antiga
  linha `Pendente: 0 bytes` é inválida e foi corrigida na ferramenta.
- A variação de `SDMA0_STATUS_REG` entre `0x46DC6452` e `0x46DC6C52` mostra
  estado mutável entre amostras, mas não comprova consumo de pacote.
- `CP_STAT = 0` não prova que CP/MEC executa comandos. Os controles
  `CP_MEC_CNTL = 0x50000000` e `CP_ME_CNTL = 0x15000000` precisam ser
  decodificados e correlacionados com firmware, ring, ponteiros e fence.
- Os masks `SPI_PG_ENABLE_STATIC_WGP_MASK = 0x7` e
  `RLC_PG_ALWAYS_ON_WGP_MASK = 0x3` são configuração observada; não são uma
  contagem validada de WGP/CU utilizável.

## Falha SMU

As oito chamadas Q0 foram encerradas externamente após 8 segundos. Como o
IOCTL era síncrono, encerrar o processo não garantia cancelar imediatamente a
rotina em kernel; as chamadas seguintes podiam aguardar o mesmo mutex. A
captura, portanto, não demonstra que cada mensagem levou oito segundos nem
que cada uma chegou ao SMU.

Foram implementadas duas correções posteriores à captura:

1. prazo absoluto e espera limitada do mutex dentro de `BcSmuMsg`;
2. leitura bruta de Q0 CMD/ARG/RSP antes de qualquer envio. Se Q0 RSP estiver
   zerado, o coletor omite todas as mensagens. Após o primeiro timeout, também
   omite as restantes.

Até uma nova coleta com o binário corrigido, o transporte SMU Q0 permanece
**NÃO VALIDADO EM HARDWARE**.

## Gate atual

Identidade, BAR5, mapa SOC15 e prontidão básica do PSP estão confirmados.
Transporte SMU, propriedade/progresso do anel SDMA, firmware SDMA, CP/MEC,
compute, GPUVA e fences permanecem **NÃO VALIDADOS EM HARDWARE**.

O próximo passo seguro é reinstalar o build corrigido e repetir somente o
baseline. Nenhuma criação/destruição de anel PSP e nenhuma inicialização de
motor deve ocorrer antes de validar o preflight SMU e registrar os endereços
externos completos do SDMA.
