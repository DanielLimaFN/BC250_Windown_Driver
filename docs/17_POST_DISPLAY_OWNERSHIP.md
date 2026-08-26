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

## Integracao e ensaio fisico de 2026-08-25

O pacote MCDM 1.1.0.0 ligou o adaptador ao `StartDevice` ativo depois da
captura readonly de PCI, recursos, topologia e ReBAR. O retorno continua
fail-closed depois da coleta; nenhuma allocation ou submissao foi habilitada.

Na BC-250 de teste, `DxgkCbAcquirePostDisplayOwnership2` retornou sucesso:

```text
FrameBufferState=1 (InitializedByFirmware)
TargetId=0xFFFFFFFF
Width=2560
Height=1080
Pitch=10240
PhysicalStart=0x00000000C0000000
Length=11059200
VramOffset=0
```

A faixa coube integralmente no BAR VRAM CPU-visible atribuido pelo PnP. A
evidencia bruta esta em
`docs\evidence\2026-08-25_19-29-47-DESKTOP-HQAG6T4-mcdm-replace`.

## Limite restante

Essa reserva prova somente a faixa do framebuffer herdado. Ela nao prova que o
restante da VRAM esta livre, nao enumera page tables, carveouts de firmware,
PSP/TMR ou outras reservas da plataforma e nao autoriza staging. Ainda e
necessario completar o produtor runtime com page tables, carveouts, rings,
firmware e PSP/TMR. A aquisicao de post-display ownership agora esta comprovada
fisicamente, mas so representa a reserva de scanout.

**NAO VALIDADO EM HARDWARE**
