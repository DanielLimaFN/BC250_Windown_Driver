# Post-display ownership e reserva do framebuffer herdado

## Conclusao da auditoria WDK

O WDK instalado oferece uma fonte autoritativa para o framebuffer que estava
ativo antes do miniport: `DxgkCbAcquirePostDisplayOwnership` e, em WDDM 2.2+,
`DxgkCbAcquirePostDisplayOwnership2`. O retorno `DXGK_DISPLAY_INFORMATION`
inclui largura, altura, pitch, formato, endereco fisico e target. A variante 2
tambem informa se o framebuffer foi inicializado por firmware ou driver.

O callback nao e uma leitura neutra: seu contrato transfere post-display
ownership. Por isso ele nao foi ligado a `bc250_dxgk_readonly_ddi` nem ao
`bc250readonly.sys` atual.

`QuerySegment` nao descobre reservas preexistentes; ele e a DDI pela qual o
proprio miniport descreve ao Dxgkrnl os segmentos que pretende administrar.
`SetVidPnSourceAddress` ocorre depois, quando existem adapter, segmentos e
alocacoes. Nenhum dos dois substitui o inventario necessario em StartDevice.

## Contrato offline implementado

`driver/wdk/bc250_post_display_reservation.{h,c}` prepara a futura aquisicao e
recusa antes de produzir uma reserva quando:

- a interface/callback nao esta disponivel;
- o callback falha;
- a variante 2 devolve estado de framebuffer desconhecido;
- formato, dimensoes ou pitch sao invalidos;
- `pitch * height` transborda;
- o BAR de abertura VRAM nao e unico;
- a faixa fisica nao cabe integralmente na abertura PnP resolvida.

Quando todos os gates passam, o resultado e uma
`BC250_INHERITED_VRAM_RESERVATION` com papel `SCANOUT`, offset relativo ao BAR,
comprimento `pitch * height`, CPU-visible e persistente. O snapshot preserva a
geracao usada pelo inventario.

Os testes usam callbacks sinteticos e cobrem callback 1/2, estado desconhecido,
pitch insuficiente, formato nao suportado, faixa fora do BAR, BAR duplicado e
falha do callback. O build passa com `/W4 /WX`.

## Limite restante

Essa reserva prova somente a faixa do framebuffer herdado. Ela nao prova que o
restante da VRAM esta livre, nao enumera page tables, carveouts de firmware,
PSP/TMR ou outras reservas da plataforma e nao autoriza staging. Ainda e
necessario um produtor runtime de inventario completo e uma fase mutante
explicitamente recuperavel para adquirir post-display ownership.

**NAO VALIDADO EM HARDWARE**
