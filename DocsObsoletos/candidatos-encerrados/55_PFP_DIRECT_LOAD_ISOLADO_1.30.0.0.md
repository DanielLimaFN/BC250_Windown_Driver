# PFP direct-load isolado - pacote 1.30.0.0

Data: 2026-08-26.

Status: **CANDIDATO CONSTRUIDO; ENSAIO FISICO PENDENTE; PM4/COMPUTE NAO VALIDADO**.

O primeiro gate do CP depois da SDMA e do RLC libera somente o PFP. ME, CE e
MEC permanecem em HALT. O objetivo e distinguir firmware apenas copiado de uma
microengine realmente executando antes de configurar um ring GFX.

Contrato do firmware:

- arquivo `cyan_skillfish2_pfp.bin`: 263424 bytes, SHA-256
  `50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1`;
- payload em `+0x100`: 263168 bytes, SHA-256
  `5AA30F3B2E1C697AD2BFE02643777552D5351EC6813572271F01320F65CA7367`;
- jump table: dword 65536, 128 dwords;
- versao do ucode: `0x94`.

Transacao:

1. reserva 0x41000 bytes de VRAM owned, CPU-visible e alinhada a 4 KiB;
2. guarda toda a regiao, copia o payload e compara 65792 dwords;
3. guarda `CP_ME_CNTL`, `RLC_CNTL`, PC e fault GFXHUB;
4. ativa temporariamente o F32 do RLC;
5. invalida a IC do PFP, programa `IC_BASE` com MC owned e escreve a jump table;
6. limpa somente `PFP_HALT`, preservando ME/CE e MEC;
7. coleta 32 amostras do `CP_PFP_INSTR_PNTR` e faults;
8. restaura HALT/RLC e confere a restauracao completa da VRAM.

As portas `IC_BASE`, `IC_OP_CNTL` e `UCODE_ADDR/DATA` sao write-only nesta
geracao, inclusive no Linux funcional. Elas sao registradas como programadas,
mas nao recebem readback fabricado. O gate fisico e `PfpPcTransitions > 0`, PC
diferente de all-ones e rollback legivel/memoria aprovado.

Mesmo com esse gate aprovado, ainda faltam ME/CE/MEC, ring GFX, RPTR/WPTR,
fence GPU e resultado. Portanto: **PM4/COMPUTE NAO VALIDADO EM HARDWARE**.

Build completo, contratos offline, assinatura e auditoria do pacote: PASS.

- source SYS: `3FE32E19B93D6B9350A104878F8CA7635A1C1A990F63ABF193A066365BBB88F4`;
- signed SYS: `44E03C585949667465C2E5D5882CBD6F4954714FFD605743D800DDCBECD9C20B`;
- CAT: `1AA60DD61EBC89E5C8B16DD3ED42C491D6316812785A2531EF6CD6944B067B82`.
