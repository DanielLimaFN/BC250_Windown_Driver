# DX.73: pixel central da copia e criterio da Corrida 118

> 25/09/2026. Base: [Corrida 117](435_CORRIDA117_DX72_TEMPO_PRESENT_E_TRIANGULO_AUSENTE.md). Alteracao **OFFLINE; NAO VALIDADA EM HARDWARE**.

## Lacuna medida e alteracao

Na 117, Daniel nao viu o triangulo. A UMD leu `0xFF7F4040` no centro do alvo Vulkan (1280x720), mas `PRESENT_OK` nao le pixels finais. O KMD fez 60 copias `COMPOSITE_BLT` 1280x720 com `Match=5/5`; a prova inclui o centro, mas o log antigo so mostrava quantos pontos casaram, sem os valores. O `CenterPixel` de `[Present #N]` usa pitch do modo GOP 2560x1080 mesmo quando a origem e' 1280x720, logo nao e' evidência desse centro.

A DX.73 publica `[DX73_COMPOSITE_CENTER] Src=... Dst=... CornerSrc=... CornerDst=... Match=... SrcMc=... DstMc=...` **depois da fence da copia**, usando os valores que a prova fisica ja leu e os pitches reais `SourcePitch`/`DestPitch` do escape. Apenas para copias 1280x720; o DWM 2560x1080 nao ganha log novo. Nao le pixels extras, nao escreve em GPU nem muda a operacao de copia. Mantem a DX.72 de tempo KMD para comparar com a 117. Este probe identifica se o pixel esperado chegou a cada lado da copia; se ambos estao corretos e o triangulo continua invisivel, a falha esta depois dessa fronteira, sem ainda localizar o ponto exato no DWM/Present.

## Portoes e candidato

- Build contratual, teste offline, build SYS lab, auditoria de seguranca, assinatura, auditoria de pacote e preflight somente leitura: **PASS**. Nenhum bind com DX.73.
- Pacote lab **3.277.1.0**, perfil `DISPLAY_RENDER_LAB`, mesmos tunables da 117. SYS assinado SHA-256 `5C33FCB9633CD58EAEA61ED7F83ED9BDB640AD1C27D8FBA346512CA6834C70C5`; PDB `93292A51839BC090A3B5F7C9753BD8B3C23A7D851DB4877C1D795901E900E036`; UMD inalterada `884A0A60CB5B1AE3C55D849A72491CC961C64EF268285EB248FDE0E2DAB29AC7`; INF `F0584C70021402B180F0256CAC3F4726CD6511090823E88CE184ABB7C52B64E6`; CAT `A777CB930152953C1374E7BCF72CD018D0DC3A84FD5FAF980B0D2A2CE844ACF7`.
- `tools/prepare-corrida118-dx73.ps1` confere hashes/manifesto/RADV, auditorias, bloqueio do Radeon Software e configuracao de dump. Runner elevado: `tools/EXECUTAR_CORRIDA118_DX73_180S.cmd`. `CLEAN_BOOT_REQUIRED` da 117: **boot frio com corte de energia antes do bind**.

## Criterio escrito antes da Corrida 118

Controles: pacote acima, 60 frames do harness, logs KMD e UMD finais completos e guardiao no mesmo boot. Agrupar as linhas DX.73 das **60 copias 1280x720**; exigir `Match=5/5`, origem/destino correlacionados e comparar com `Center(640,360)=0xFF7F4040` e canto `0xFF2673E5` que a UMD observou na 117. A leitura final do monitor e' o relato visual do Daniel; pedir descricao separada da janela, fundo azul, triangulo e qualquer piscada. Comparar callback UMD, tempo KMD e taxa DWM com a 117 (444 ms, 0,676 ms, 2,78 Presents/s nos primeiros 120 s).

| Desfecho | Condicao | Veredito |
|---|---|---|
| **A** | Pixel central esperado nos dois lados de todas as copias mensuraveis, 60/60 `PRESENT_OK`, triangulo visivel so na janela, desktop normal e recuperacao BasicDisplay/0 em cinco amostras no mesmo boot | Fronteira de copia e visual corretas por 180 s; estabilidade de 600 s e velocidade ainda abertas. |
| **B** | Centro `Src=Dst=0xFF7F4040` nas copias, mas triangulo ausente | Pixel chegou ao destino da copia; investigar Present/composicao DWM e janela, sem chamar a copia de falha. |
| **C** | UMD le `0xFF7F4040`, mas centro `Src` diverge ou `Dst` diverge de `Src` | Falha localizada na ponte para a origem WDDM ou na copia, respectivamente; conferir faixas, endereco e prova fisica. |
| **E** | Travamento, BugCheck, device-lost, falha de pacote/log ou rollback, ou resultado fora das categorias acima | Descrever a falha concreta; nenhuma prova de visual/estabilidade. |

Se travar, tentar Ctrl direito + Scroll Lock duas vezes para dump antes do reset; se teclado nao responder, anotar e reiniciar pelo botao sem cortar energia naquele instante. O ensaio mede pixels e tempos; nao e' uma otimizacao de FPS.
