# 298 — Critério da Corrida 42 — row pitch 2D e gate preservado

> **Escrito em 20/09/2026, antes do ensaio físico.**  
> **Candidato:** lab `3.200.1.0`, `DISPLAY_RENDER_LAB`, WDDM 2.7, schema 135,
> `GfxWaveProbe=1`, `HardwareValidated=false`.  
> **Objetivo:** provar ou refutar no silício as correções derivadas da Corrida 41 sem
> permitir que o runner apague `D3D11LabPromotion`.

## 1. Hipótese e mudanças sob teste

A textura do DWM `590x800`, formato 87, chegou com `SrcPitch=2360`, mas o shadow usa
`DstPitch=2560`. O candidato agora:

1. copia todas as linhas pelo pitch da origem e do destino, validando formato, box,
   subrecurso, limites e a última linha;
2. passa o pitch acolchoado ao Vulkan como `bufferRowLength=640` texels;
3. propaga a causa exata de descriptor/pipeline até a recusa final do Draw;
4. preserva a chave `HKLM:\SOFTWARE\BC250\Umd`: `New-Item` só ocorre se ela não existir;
5. se `BC250_GPUVA_NO_SPACE` reaparecer, registra tamanho do batch, índice, map/unmap,
   base e tamanho da faixa que falhou.

Isso é implementação offline. Não prova upload, descriptor, pixels nem desktop no hardware.

## 2. Pré-condições obrigatórias

- boot frio e fusível inspecionado; resetar se estiver `TRIPPED`;
- confirmar que o pacote selecionado é `09/20/2026,3.200.1.0` pelos quatro hashes abaixo;
- confirmar antes do bind que `D3D11CapsProbe` está desarmado;
- usar o binder normal, que arma `D3D11LabPromotion`; não editar a chave manualmente;
- manter captura de dumps do DWM opt-in e restauração transacional;
- não atribuir ao nosso driver a imagem que só reaparecer depois do rollback para BasicDisplay.

## 3. Critério primário

O teste primário fecha somente se a evidência correlacionar, no mesmo recurso de textura:

```text
ResourceUpdateSubresourceUP: textura copiada linha-a-linha
[VK_RES] UploadImageData SUCESSO: ... RowPitch=2560 bufferRowLength=640 texels
[VK_DESCRIPTOR] BOUND
Bc250UmdExecuteVkDraw: DRAW fence concluida
```

Para a textura `590x800`, a linha lógica deve ser 2360 bytes e o staging deve cobrir
2.048.000 bytes. `TextureWrites` deve subir e o layout deve sair de `UNDEFINED`.

Além disso, após o runner iniciar o harness, uma nova carga da UMD deve continuar registrando
`D3D11_LAB_PROMOTION: ARMADO`. Qualquer leitura `desarmado` torna o ensaio contaminado, mesmo
que o device antigo continue vivo por cache.

## 4. Desfechos pré-declarados

- **A — avanço funcional:** cópia linha a linha, upload acolchoado, descriptor e draw concluem;
  há ao menos um Present aceito. Pixel correto/desktop só são declarados se houver medição
  própria, não por ausência de crash.
- **B — cópia recusada:** aparece a recusa de geometria/box/formato antes do upload. Arquivar a
  linha completa e o recurso; não relaxar validação por aproximação.
- **C — upload ainda falha:** a cópia passa, mas `UploadImageData` recusa ou a fence falha.
  O motivo Vulkan exato é a nova parede.
- **D — descriptor/draw falha por outra causa:** a recusa final deve repetir o motivo real do
  backend Vulkan. A frase genérica de PM4 não pode substituir uma causa medida.
- **E — publicação GPUVA falha:** arquivar `PUBLICACAO_FALHA Batch=... FailedIndex=... Map=...
  Base=... Size=...` junto com a ocupação. Sem esses campos, a causa continua inconclusiva.
- **F — gate perdido:** qualquer processo novo lê a promoção desarmada. É falha do roteiro ou
  de outra mutação de registro e invalida conclusões de sustentação/recriação.
- **G — crash/TDR/BugCheck:** coletar a cauda integral e dumps; não atribuir causa por
  proximidade temporal.

## 5. Métricas obrigatórias no relato

- contagem de uploads Vulkan aceitos/recusados, descriptors bound/recusados e draws com fence;
- Present aceitos/falhos, frames do harness e `GetDeviceRemovedReason`;
- `SUBMIT SUCESSO`, última fence aposentada, RPTR/WPTR, TDR, `GFXHUB_FAULT`, BugCheck;
- falhas `Upload LOCK`, separadas dos uploads Vulkan;
- todas as falhas `ActiveFlushTlb`, com a nova geometria da publicação;
- pixel/readback quando existir; ausência de medição deve ser declarada;
- estado do nosso driver antes da recuperação e BasicDisplay depois do rollback.

## 6. Execução e recuperação

Executar elevado:

```text
tools\EXECUTAR_D3D11_NATIVO.cmd -CaptureDwmDump
```

Se a recuperação automática não concluir, usar `tools\BC250_RECOVERY.cmd` elevado. O pacote
não foi instalado nem validado fisicamente nesta faixa.

## 7. Pacote assinado e auditado

| Artefato | SHA-256 |
|---|---|
| SYS | `7B6E1F5832A2C7D08BAE440A55A681F142A9ACE81E4CCE8DFD54AB4649F0E901` |
| UMD | `934DBD84E04F10024F6B7CAB28FBE014CAA41D35E103600F160BBF395AB5236A` |
| INF | `29666314205550CF67E26355F1C6C98803BECFE88F0D23EBAC224893FD9F474D` |
| CAT | `DC95AB4776CBC3BD44C1D81EBE94F2CF6F7FE7C883E9B624FB4DFA0565313358` |

`build-umd.bat`, `build-contract.bat`, auditoria de segurança, mutações da Corrida 41,
mutações de descriptor/wave e auditoria do pacote: **PASS**. Instalação/bind: **não executados**.
Tudo desta faixa permanece **NÃO VALIDADO EM HARDWARE**.

## 8. Dívida explicitamente não fechada

As falhas de `Bc250WinsysUploadAllocation/pfnLockCb=0x80070057` não foram convertidas em
sucesso nem tratadas como irrelevantes. O caminho de update agora registra que a alocação WDDM
não foi sincronizada mesmo quando a cópia Vulkan é separada, mas ainda falta definir por contrato
quais recursos precisam de espelho WDDM lockable e quais têm memória Vulkan como backing
autoritativo. Essa dívida deve aparecer no relato da corrida e não pode ser confundida com o
resultado do row pitch.
