# Staging transacional de firmware

## Escopo

`driver/core/bc250_firmware_staging.{h,c}` implementa somente o contrato de
copia e restauracao. Ele nao mapeia BAR, nao acessa MMIO, nao chama PSP e nao
esta ligado a `DxgkDdiStartDevice` ou ao `.sys` instalavel.

## Gates antes da primeira escrita

- plano PSP tipado e limitado a PFP/ME/CE/MEC/MEC_JT1;
- regiao com papel firmware, CPU-visible, persistente e naturalmente alinhada;
- preflight integral ainda valido, sem mudanca no inventario ou snapshot;
- lease de staging vinculada ao token, geracao, offset, comprimento, selo do
  inventario e selo do snapshot;
- evidencias separadas de adaptador exclusivo, espaco livre runtime, exclusao
  de scanout runtime e inventario runtime de reservas;
- faixa integral dentro da janela declarada pelo transporte;
- transporte declara CPU-visible e readback coerente;
- buffers de payload, rollback e verificacao nao se sobrepoem;
- SHA-256 do payload coincide com a identidade esperada.

Os hashes esperados dos cinco payloads ficam no manifesto compilado
`driver/wdk/bc250_cp_firmware_manifest.{h,c}`. O auditor Python deriva MEC
principal e MEC_JT1 do blob MEC e bloqueia divergencia entre arquivo,
`firmware/manifest.json`, tamanhos e hashes compilados.

## Transacao

1. ler e guardar os bytes anteriores da regiao;
2. calcular o SHA-256 do backup;
3. escrever o payload por transporte injetavel;
4. reler em blocos e conferir SHA-256;
5. manter o estado `STAGED` somente depois do readback integral;
6. restaurar os bytes anteriores e conferir o SHA-256 original;
7. zerar buffers auxiliares ao fechar a transacao restaurada.

Falha de escrita ou readback dispara restauracao automatica. Se a restauracao
nao puder ser comprovada, o contexto fica `CONTAMINATED`; ele nao pode ser
fechado nem reutilizado como se o rollback tivesse passado.

## Testes offline

`driver/tests/firmware_staging_test.c` cobre:

- vetor SHA-256 `abc` e payload no limite de bloco de 64 bytes;
- identidade incorreta antes de qualquer I/O;
- staging, readback, restauracao e limpeza normais;
- escrita parcialmente aplicada que retorna falha;
- readback corrompido;
- rollback impossivel e estado contaminado;
- evidencia de lease incompleta, lease adulterada e snapshot obsoleto;
- transporte sem readback coerente;
- faixa fora da janela;
- scratch insuficiente e buffers sobrepostos.

O build WDK passa com `/W4 /WX`. Todos os callbacks usam memoria falsa.

## Bloqueios restantes

`bc250_staging_lease` remove a possibilidade de o proprio transporte declarar
exclusividade. A lease so e emitida com os quatro tipos de evidencia e fica
selada ao preflight e ao plano exatos. Isso foi exercitado apenas com evidencia
sintetica offline; o selo e uma guarda de integridade, nao uma prova fisica nem
uma credencial criptografica.

O miniport atual ainda nao possui produtores runtime para provar que a faixa
esta livre e fora de todas as reservas. A faixa do framebuffer herdado agora
pode ser obtida futuramente por `DxgkCbAcquirePostDisplayOwnership[2]`, usando o
adaptador offline documentado em `docs/17_POST_DISPLAY_OWNERSHIP.md`; isso ainda
nao constitui inventario completo. O Discovery TMR ganhou leitor separado pelos
scratch registers, documentado em `docs/18_DISCOVERY_TMR_RUNTIME.md`; ele nao
substitui a futura TMR do PSP nem comprova todas as reservas. Antes de integrar este modulo, o driver
precisa obter essas evidencias no ciclo PnP, mapear somente o BAR autorizado,
prover os blobs do pacote sem I/O inseguro em StartDevice e definir unwind para
surpresa/Stop/Remove. O transporte PSP continua ausente.

**NAO VALIDADO EM HARDWARE**
