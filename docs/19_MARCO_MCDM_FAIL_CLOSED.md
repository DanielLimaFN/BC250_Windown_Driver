# Marco MCDM fail-closed antes do lifecycle WDDM funcional

Atualizado em 2026-08-25. Veredito: **NAO VALIDADO EM HARDWARE**.

## Resultado deste marco

O pacote `bc250mcdm` deixou de promover o probe readonly a acelerador ativo.
O miniport agora nasce sem runtime staged e nao cria por conta propria:

- ownership exclusivo do adapter;
- inventario completo de VRAM;
- reserva TMR fixa;
- snapshot com geracao constante;
- tamanhos sinteticos de firmware.

`DxgkDdiStartDevice` preserva o resultado do preflight readonly. Enquanto os
produtores runtime nao existirem, o retorno esperado e fail-closed e o adapter
nao e anunciado como operacional.

As DDIs de allocation, contexto, paging, preempcao, status de engine e reset
que ainda nao possuem implementacao fisica retornam `STATUS_NOT_SUPPORTED`.
Elas nao anunciam mais sucesso, engine responsiva, timestamp GPU ou reset sem
efeito correspondente no silicio.

## Recuperacao

`tools/BC250_RECOVERY.cmd` agora reconhece dois pares estritos sob a classe
`ComputeAccelerator` e o hardware ID `PCI\VEN_1002&DEV_13FE`:

- `bc250readonly` + `bc250readonly.sys`;
- `bc250mcdm` + `bc250mcdm.sys`.

O `DRY-RUN` detectou o pacote ativo atual `oem18.inf`, o servico `bc250mcdm` e
a ausencia do binario em `System32\drivers`, sem alterar o sistema.

A copia instalada em `C:\BC250_RECOVERY.cmd` ainda diverge da fonte. Por isso
`tools/build-bc250mcdm-package.ps1` bloqueia a geracao do pacote assinado. Nao
sincronizar nem ativar automaticamente: a atualizacao da recuperacao e o novo
binding sao operacoes separadas, elevadas e explicitamente aceitas.

## Gates offline

`tools/audit-bc250mcdm-safety.py` reprova o pacote quando encontra:

- runtime default sintetico no miniport;
- ownership autoatribuido;
- reserva herdada fixa;
- promocao de `ReadonlyGatePassed` a sucesso;
- DDI operacional sem falha fechada;
- recovery sem identidade exata do `bc250mcdm`.

O build WDK completo e os testes offline passam. Isso prova compilacao,
linkage e comportamento fail-closed; nao prova carregamento do novo binario,
memoria WDDM, firmware, ring, interrupcao, PM4 ou compute.

## Proximo gate de implementacao

Antes de permitir `StartDevice=STATUS_SUCCESS`, implementar nesta ordem:

1. contexto de device real, com handles unicos e teardown completo;
2. private data de allocation e segmentos derivados apenas do inventario
   runtime selado;
3. GPUVA/page tables e paging buffer pertencentes ao lifecycle do miniport;
4. monitored fence, IH/DPC e reset/TDR fisicos;
5. `DxgkDdiSubmitCommand` ligado a um ring owned, com enderecos vindos das
   allocations do Dxgkrnl;
6. primeiro PM4 somente com RPTR/WPTR, fence GPU e resultado integral.

Nenhum desses itens pode usar os contratos offline como prova de hardware.

