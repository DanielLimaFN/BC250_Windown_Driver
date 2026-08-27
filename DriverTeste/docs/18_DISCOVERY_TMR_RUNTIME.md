# Localizacao runtime do TMR de IP Discovery

## Fonte upstream

O caminho AMDGPU para IP Discovery nao assume imediatamente uma constante no
fim da VRAM. Primeiro tenta a localizacao default e confere a assinatura. Se ela
nao estiver valida, consulta `DRIVER_SCRATCH_0`, `DRIVER_SCRATCH_1` e
`DRIVER_SCRATCH_2`: os dois primeiros formam o offset de 64 bits e o terceiro
informa o tamanho. Somente outros ramos usam informacao ACPI/TMR.

Na familia de registradores usada pela referencia, os indices sao `0x94`,
`0x95` e `0x96`; no BAR MMIO byte-addressed isso corresponde a `0x250`,
`0x254` e `0x258`. A propria referencia declara esses registradores consistentes
entre SoCs.

## Contrato implementado

`driver/core/bc250_discovery_tmr_reader.{h,c}`:

- aceita apenas transporte identificado como GC 10.1.3, MMIO owned e readonly;
- le os tres registradores em duas passagens;
- reprova callback, mudanca entre passagens, overflow e faixa fora da VRAM;
- retorna `NOT_REPORTED` quando `DRIVER_SCRATCH_2=0`;
- preserva offset, tamanho, leituras, passagens e geracao do snapshot.

O allowlist de `bc250_mmio_topology_transport` foi ampliado somente para esses
tres offsets. Os testes exercitam o leitor puro e o transporte BAR falso. O
exemplo `0x1FFF0000/0x10000` usado no teste e sintetico; nao e medida da BC-250.

## Classificacao das reservas

- framebuffer pre-OS: vem de post-display ownership;
- Discovery TMR: vem dos scratch registers ou de assinatura real verificada;
- GART, page tables, rings, MQDs e scratch: sao alocacoes futuras do proprio
  driver, feitas depois que o gerenciador de memoria existe;
- TMR do PSP para carga de firmware: continua sendo uma reserva futura separada
  e nao deve ser confundida automaticamente com o Discovery TMR.

## Bloqueio do fallback

Quando os scratch registers nao reportam a faixa, o upstream pode procurar a
assinatura no fim da VRAM. Na topologia observada, esse local fica fora dos
256 MiB CPU-visible. O driver Windows ainda nao possui um reader autorizado
para essa faixa. Portanto nao existe fallback por constante nem side-load de
arquivo: `NOT_REPORTED` permanece fail-closed.

O leitor ainda nao esta ligado a telemetria da DDI/sys e nao foi executado no
silicio.

**NAO VALIDADO EM HARDWARE**

## Resultado fisico e efeito no inventario - 2026-08-25

Nos pacotes 1.1.0.0 e 1.2.0.0, as duas passagens fisicas sobre
`DRIVER_SCRATCH_0/1/2` foram estaveis, mas o tamanho retornado foi zero. O
resultado e `BC250_DISCOVERY_TMR_NOT_REPORTED`, nao uma falha de transporte.

O pacote 1.2.0.0 registra que a fonte foi sondada por meio do flag
`BC250_RUNTIME_EVIDENCE_DISCOVERY_TMR_PROBED`, mas nao cria uma reserva TMR.
Junto do scanout, isso produz evidence flags `0x3` e inventario
`INCOMPLETE`. Ausencia reportada nao e convertida em memoria livre.

**NAO VALIDADO EM HARDWARE**
