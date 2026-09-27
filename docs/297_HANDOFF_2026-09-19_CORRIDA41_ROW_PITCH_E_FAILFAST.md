# 297 — Handoff da Corrida 41 — escrita chegou, upload recusou o pitch e o DWM perdeu a promoção

> **Corrida física:** 19/09/2026 19:29:03 (`America/Sao_Paulo`)  
> **Candidato:** lab 3.199.1.0 · SYS SHA-256
> `749BA458F5F235B702C25204E17DF0DC85E98C9D6F0B5DD1F8E88926A7875041`  
> **Evidência:**
> `docs/evidence/2026-09-19_19-29-03-DESKTOP-HQAG6T4-lab-binding/`  
> **Critério escrito antes do ensaio:** [docs/296](296_CRITERIO_CORRIDA_41_RECURSO_SRV_E_DUMP_DWM.md)

## Veredito executivo

**Houve avanço diagnóstico: SIM. Avanço funcional contra a Corrida 40: NÃO.** O critério
primário caiu exatamente no **Desfecho B**: o recurso recusado recebeu uma DDI de escrita, mas
continuou em `VkLayout=UNDEFINED`. A história do mesmo ponteiro revela a pré-condição que falhou:
o shadow tem `RowPitch=2560` para uma imagem de 590 texels e o upload Vulkan ainda recusa linhas
com padding.

Também ocorreu **Desfecho E**, agora com causa offline fechada: o runner
`test-d3d11-native-conformance.ps1` executou `New-Item -Force` sobre a chave **já existente**
`HKLM:\SOFTWARE\BC250\Umd` para gravar `GfxIbDumpDir`. No Registry provider do Windows
PowerShell 5.1 essa operação recria a chave e apaga seus valores. Uma reprodução isolada em uma
chave temporária de `HKCU` confirmou `Before=1`, `After=null`, `Survived=false`; a chave de teste
foi removida em seguida. Assim, o próprio runner apagou `D3D11LabPromotion` antes de lançar o
harness.

A linha do tempo confirma a causa sem depender apenas da reprodução: o DWM carregado já havia
cacheado `ARMADO` às 19:29:14, mas o harness novo leu `desarmado` às 19:29:19 — **antes** do
teardown do DWM. O processo antigo continuou usando o valor cacheado até descarregar a DLL; as
novas cargas passaram a ler a chave já apagada. Os dois dumps preservados mostram o `dwmcore`
fazendo fail-fast em `CD3DDevice::CreateD3D11Device`, com HRESULT `0x887A0004` preservado em
`rbx` no frame de `MilFailFastForHR`; `bc250umd.dll` não está carregada no contexto do crash.
Os logs da UMD mostram `OpenAdapter` recusando por promoção desarmada e retornando `E_NOTIMPL`.
Portanto, os **dois crashes capturados** são atribuíveis à falha de criação do device causada
pelo gate que o runner apagou, e não à recusa anterior do descriptor. O primeiro crash do PID
1224 não foi preservado pelo `DumpCount=2`; não se estende a prova de dump a ele por suposição.

O apagamento **não invalida o Desfecho B**: a sequência create → update → recusa ocorreu no
device do DWM que já estava criado e mantinha a promoção cacheada. Ele invalida o harness e
contamina toda conclusão sobre recriação/sustentação do device depois de 19:29:19.

**Veredito frente ao ensaio: SUCESSO DIAGNÓSTICO; PARCIAL frente ao produto.** A pergunta do
recurso foi respondida e os dois dumps disponíveis receberam atribuição limitada pela prova.
Quadro completo, pixel correto e desktop visível continuam **NÃO VALIDADOS EM HARDWARE**.

## 1. Resultado primário: Desfecho B

O recurso recusado foi `000000000930AC70`, criado como textura 2D `590x800`, formato 87,
quatro bytes por texel e `TotalByteSize=2.048.000` (`2560 * 800`). A sequência do PID 1224 foi:

```text
19:29:20.381 CreateResource: res=000000000930AC70 ... Size=2048000 (590x800 bpp=4)
19:29:20.388 UploadImageData RECUSADO: RowPitch=2560 nao e multiplo exato da largura=590
19:29:20.389 ResourceUpdateSubresourceUP: sincronia ... RECUSADA
19:29:20.390 ResourceUpdateSubresourceUP: res=000000000930AC70 pitch=2360
19:29:22.623 CreateShaderResourceView: ... res=000000000930AC70 Format=87
19:29:22.644 [VK_DESCRIPTOR] RECUSADO: ... SRV_RESOURCE=000000000930AC70
             WxH=590x800 Format=87 ... TextureWrites=1 VkLayout=UNDEFINED
```

A escrita **chegou**: `TextureWrites=1` e a DDI informou pitch 2360, exatamente `590 * 4`.
O helper, entretanto, tenta subir o shadow inteiro com o pitch alinhado do recurso, 2560. O
uploader exige hoje que o pitch represente linhas compactas e usa
`VkBufferImageCopy.bufferRowLength=0`; por isso recusou corretamente em modo fail-closed.

Há uma segunda falha de correção no mesmo caminho: quando `pDstBox == NULL`,
`ResourceUpdateSubresourceUP` copia apenas `res->Width` bytes — 590 neste caso — em vez das 800
linhas de 2360 bytes para o shadow de stride 2560. Apenas aceitar o pitch no Vulkan enviaria uma
imagem quase toda vazia. A correção precisa, em conjunto:

1. copiar a origem linha a linha, com largura validada pelo formato (`590 * 4 = 2360`) e strides
   separados de origem e destino;
2. subir o shadow preservando o stride, usando `bufferRowLength=2560/4=640` texels quando a
   divisibilidade e os limites forem provados, ou fazer repack explícito para um staging compacto;
3. manter fail-closed para formatos, subrecursos e geometrias ainda não modelados;
4. testar byte a byte padding, limites e conteúdo da última linha antes de nova corrida.

O mesmo bloqueio por padding apareceu ainda em `1288x...` com pitch 5376 e `873x...` com pitch
3584. Não é um caso isolado do recurso 590x800.

## 2. O que ainda funcionou antes da parede

No primeiro device do `dwm.exe` (PID 1224) foram observados seis uploads concluídos, um
`[VK_DESCRIPTOR] BOUND`, um draw gráfico com fence concluída e uma recusa de descriptor. O draw
registrou `VkDraws=1`, 54 vértices, uma instância e indexed; **pixels não foram conferidos**.

No KMD, a coleta completa contém:

- 129 `[GfxEngine] SUBMIT SUCESSO`, fences 2 a 130;
- zero `NAO_CONVERGIU`, zero `TDR_GFX`, zero `GFXHUB_FAULT` e nenhum BugCheck observado;
- 485 linhas observadas de `TRIPE_PROVADO` no Node 0; a amostragem do logger impede converter
  essa contagem no total real de operações;
- quatro `BYTE_PROOF INCONCLUSIVO`;
- 12 `IB_LINEAR_CONFIRMADO` e 51 `INDETERMINADO`; o limite P4 não foi relaxado.

Há duas falhas KMD reais que a primeira análise omitiu: `ActiveFlushTlb` devolveu
`STATUS_IO_DEVICE_ERROR` duas vezes depois da invalidação física do GFXHUB. Em ambas,
`Journal status=7` significa `PUBLISH_FAILED`, `PublishStatus=3` significa
`BC250_GPUVA_NO_SPACE`, e não houve contenção (`Attempts=1`, `Busy=0`). Isto é inesperado porque
o snapshot ativo ainda reportou 904 e 892 slots reutilizáveis, com zero `TEARDOWN`. A telemetria
atual não registra o índice/range cuja substituição exigiu mais espaço; portanto, a causa interna
continua **INCONCLUSIVA**. As submissões seguintes continuaram funcionando, mas um erro devolvido
por `BuildPagingBuffer/FlushTlb` pode participar do teardown inicial e não pode ser apagado do
veredito só porque não houve TDR.

Na UMD houve ainda 137 ocorrências de `Bc250WinsysUploadAllocation` falhando no callback
`pfnLockCb` com `0x80070057`. Os chamadores de recursos descartam esse retorno com `(void)` e
seguem para a cópia direta em memória Vulkan. Isso explica como os seis uploads Vulkan podem
terminar apesar da falha WDDM, mas deixa a alocação WDDM sem a atualização pretendida. A classe
já aparecia na Corrida 40; não é uma regressão atribuível ao candidato, porém é dívida funcional
e de propagação de erro.

Há ainda uma linha de diagnóstico falsa por agregação: imediatamente depois da recusa exata do
descriptor, `Bc250UmdRejectUnbackedDraw` registra
`compilador/shader-state/draw PM4 ausente`, embora o pipeline do slot 1 tenha acabado de compilar
com sucesso e a causa medida tenha sido o layout indefinido da SRV. A recusa funcional é correta,
mas o motivo final não é. O executor precisa propagar um enum de causa em vez de converter toda
falha de `Bc250UmdExecuteVkDraw` nessa frase genérica.

Contra a Corrida 40, que teve 137 submits GFX, seis uploads, dois descriptors e um draw, não há
progressão até um quadro completo. Há estabilidade do anel no escopo medido, mas não avanço
funcional do desktop.

O harness separado não mediu quadros: `D3D11CreateDeviceAndSwapChain` falhou com `0x887A0004`;
o runner corrigiu o falso Exit 0 interno e encerrou com **Exit 3**.

## 3. Fail-fast e perda da promoção — resultado independente

O primeiro device leu `D3D11LabPromotion: ARMADO` duas vezes às 19:29:14. O runner então apagou
o valor ao recriar a chave, e seu próprio harness já mediu o efeito:

```text
19:29:19.115 [pid=11180] D3D11_LAB_PROMOTION: desarmado
19:29:19.120 [pid=11180] OpenAdapter10_2: ... -> E_NOTIMPL
D3D11CreateDeviceAndSwapChain falhou: 0x887A0004
```

O DWM original manteve o `ARMADO` no cache estático da DLL até o teardown completo. Ao recarregar:

```text
19:29:25.453 D3D10 DestroyDevice
19:29:25.616 Bc250UmdVkBridgeShutdown LEAVE: Desmontado com sucesso
19:29:25.618 CloseAdapter -> S_OK
19:29:25.646 D3D11_LAB_PROMOTION: desarmado
```

O PID 1224 e os DWM posteriores passaram então a recusar a abertura. O log de Aplicativo tem
**oito** eventos `APPCRASH` de `dwm.exe`, entre 19:29:26.121 e 19:30:12.322, todos com a
assinatura `0xe0464645`/`KERNELBASE+0x12d862`.

O WER foi configurado com `DumpCount=2`, portanto reteve somente os dois últimos:

| Dump | Tamanho | SHA-256 |
|---|---:|---|
| `dwm-crash-01-dwm.exe.10964.dmp` | 176.299.313 | `93E4D63A9BD5185B614EEAE2777C2502807F253119A813C35DEA8DDC81C37412` |
| `dwm-crash-02-dwm.exe.1508.dmp` | 176.103.051 | `2C3A5989DD7837C447B6BB9F0C78D2B4C98DC493662712EA410368624E31FBB2` |

As duas pilhas são equivalentes:

```text
KERNELBASE!RaiseFailFastException
dwmcore!MilFailFastForHR
dwmcore!ModuleFailFastForHRESULT
dwmcore!MilInstrumentationHandleFailure_MaybeFailFast
dwmcore!MilInstrumentationCheckHR_MaybeFailFast
dwmcore!CD3DDevice::CreateD3D11Device
dwmcore!CD3DDevice::Create
dwmcore!CDeviceManager::CreateNewDevice
```

Nos dois dumps, o frame de `dwmcore!MilFailFastForHR` preserva
`rbx=ffffffff887a0004`. `lm m bc250umd` não encontrou o módulo porque cada tentativa de abertura
falhou e a DLL foi descarregada. A conclusão permitida é estreita: nesses dois dumps, o fail-fast
ocorreu no DWM ao criar o device D3D11 com `DXGI_ERROR_UNSUPPORTED`, depois que o runner apagou a
promoção; não ocorreu dentro da pilha da nossa DLL.

Os relatórios CDB arquivados têm SHA-256
`39E4182F4C55C32132DD629807FB7CDAA41A13918C0F2D671A8762EC5852962B` e
`2AD28F4EDCEE33BD17DABFC65DEFBCB6DE68DD02E1E8B7A5D6DF89B86D00D504`.

## 4. Coleta e duplicidade

A correção do prazo funcionou: `display-log-this-bind-FINAL.txt` tem 1.002.092 bytes, as duas
passagens coincidiram e `complete_at_capture=true`. A coleta permaneceu ativa durante os crashes,
em vez de terminar com o harness.

No log final do PID 1224 houve 2 pares adjacentes idênticos em 4.161 linhas (0,05%), contra
aproximadamente 50% na Corrida 40. No Explorer houve 26 em 11.500 (0,23%). A faixa validou uma
redução material da duplicação no DWM, não sua erradicação universal nem a causa histórica.

## 5. Falhas, recuperação e pendências

**Falhas observadas:** tela preta; descriptor recusado; três uploads de textura recusados por
pitch com padding; 137 falhas `pfnLockCb=0x80070057` ignoradas pelos chamadores; duas falhas de
publicação GPUVA (`STATUS_IO_DEVICE_ERROR`, `BC250_GPUVA_NO_SPACE` apesar de centenas de slots
reutilizáveis); quatro recusas `DescribeAllocation` sem geometria; milhares de escapes opcode 2
recusados (o log amostrado alcança a ocorrência 3072); oito crashes do DWM; promoção apagada pelo
runner; harness sem quadros; quatro provas de byte inconclusivas; zero readback de pixel.

**Recuperação:** `RECOVERY_OK_ONLINE`; BasicDisplay Código 0 confirmado em cinco amostras entre
19:31:12 e 19:31:16. O estado LocalDumps terminou `RESTORED`, dois dumps copiados, zero pendente,
sem limpeza pendente; `dwm_capture_restore_ok=true`. A restauração pelo BasicDisplay não é sucesso
do nosso DWM.

**Pendências imediatas, em ordem:**

1. substituir `New-Item -Force` por criação condicional da chave e adicionar um gate com mutação
   que prove que valores preexistentes sobrevivem ao runner;
2. corrigir a cópia 2D e o upload de linhas com padding, com testes de conteúdo e limites;
3. instrumentar a falha `BC250_GPUVA_NO_SPACE` com quantidade de changes, índice e faixa que
   falhou; a ocupação global atual não explica a recusa;
4. deixar de ignorar o retorno de `Bc250WinsysUploadAllocation` e decidir, por contrato, quais
   recursos precisam ser lockable e quais vivem exclusivamente na memória Vulkan;
5. propagar o motivo real de falha do draw até `Bc250UmdRejectUnbackedDraw`, sem voltar a agregar
   descriptor, shader, pipeline e emissão PM4 numa única frase;
6. preservar um dump do primeiro crash posterior ao descriptor, se a relação ainda precisar ser
   testada, pois `DumpCount=2` reteve apenas os dois últimos da tempestade;
7. escrever novo critério antes da próxima corrida. Desktop visível, pixel correto, 60 minutos,
   8 horas e recuperação TDR com display vivo seguem **NÃO VALIDADOS EM HARDWARE**.

## 6. Faixa offline posterior — 20/09/2026

Os itens 1, 2, 3, 5 e 7 acima foram implementados offline. O runner preserva a chave UMD;
a cópia 2D usa pitches independentes; o upload Vulkan representa padding por
`bufferRowLength`; a recusa final do Draw propaga o motivo Vulkan; e a falha de publicação
GPUVA agora registra batch/índice/operação/faixa. Testes cobrem o caso medido `590x800`, última
linha, padding, pitch curto, formato desconhecido, byte count insuficiente e box inválida.

O item 4 permanece aberto por decisão explícita: o caminho corrigido deixou de esconder a
divergência e registra que a alocação WDDM não foi sincronizada, mas ainda não existe contrato
fechado que classifique recursos lockable versus backing exclusivamente Vulkan. Não se marcou
essa dívida como resolvida.

Builds e auditorias passaram, incluindo 5/5 mutações específicas. Pacote assinado/auditado
`09/20/2026,3.200.1.0`, não instalado: SYS `7B6E1F58...F0E901`, UMD
`934DBD84...5236A`, INF `29666314...F474D`, CAT `DC95AB47...13358`. O critério escrito antes
da próxima corrida está em [docs/298](298_CRITERIO_CORRIDA_42_ROW_PITCH_E_GATE_PRESERVADO.md).
Tudo desta faixa segue **NÃO VALIDADO EM HARDWARE**.
