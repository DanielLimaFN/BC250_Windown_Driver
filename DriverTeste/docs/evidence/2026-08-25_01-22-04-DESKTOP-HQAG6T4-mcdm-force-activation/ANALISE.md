# Analise do binding 0.4.0.0

O binding forcado selecionou `bc250readonly` e terminou em Codigo 43 porque a
DDI retornou deliberadamente `STATUS_NOT_SUPPORTED`. Nao houve reboot pendente,
bugcheck ou erro de teardown.

O caminho readonly passou integralmente ate o gate anterior ao manifesto:

- PCI `1002:13FE`: 64 bytes lidos, 3 BARs descobertas e 3 resolvidas;
- topologia MMIO: 8 leituras, 2 passagens estaveis e unmap concluido;
- VRAM detectada: 512 MiB; janela CPU-visible: 256 MiB;
- FB detectado: `0xF400..0xF41F`, offset `0x450`;
- ownership: estado 1, flags `0x7F`, todos os status em zero;
- ReBAR: mascara `0x700`, encoding selecionado 8, 256 MiB selecionados e
  atribuidos, 4 leituras;
- `RuntimeMissing=1`, seguido por Stop/Remove com status zero.

Isso comprova que o miniport leu e conciliou os recursos fisicos usados pelo
preflight. Nao comprova firmware aceito, CP ativo, ring, compute ou aceleracao.

Proximo gate: contrato interno dos cinco payloads
`PFP -> ME -> CE -> MEC -> MEC_JT1`, planejamento somente-leitura e telemetria
explicita de zero bytes staged/zero chamadas PSP.

**NAO VALIDADO EM HARDWARE**
