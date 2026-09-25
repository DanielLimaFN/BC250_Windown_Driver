# DX.59 — sonda de CreateAllocation e critério da Corrida 103 (24/09/2026)

## Pergunta a medir

Na [Corrida 102](408_CORRIDA102_DX58_RESULTADO.md), o Explorer teve 294 recusas `E_INVALIDARG` da textura BGRA compartilhada 1024×832 (`Bind=0xA8`, `Misc=0x2`), enquanto uma textura do DWM com a mesma geometria e `Bind=0x8` passou. A prioridade normal corrigida em DX.58 não foi suficiente. O log KMD não contém o mesmo token nas amostras da recusa, mas a ausência pode ser perda do logger. A DX.59 pergunta, pelo **token exato**, se `DxgkDdiCreateAllocation` foi executada e qual `NTSTATUS` devolveu. O travamento duro pode ter outra causa.

A [documentação Microsoft de `DXGKARG_CREATEALLOCATION`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_createallocation) distingue os dados de entrada e saída do KMD; o [contrato de `DxgkDdiCreateAllocation`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_createallocation) permite que `hResource` de saída seja nulo. Portanto, a sonda registra esse campo, mas não o trata isoladamente como causa da recusa. Na [alocação da UMD](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-_d3dddicb_allocate), o handle de recurso compartilhado na entrada segue outro contrato.

## Implementação offline

A UMD consulta um escape privado após `pfnAllocateCb` para texturas 1024×832; as primeiras 16 tentativas por processo e depois uma a cada 32 são amostradas. O log `DX59_ALLOC_QUERY` traz `AllocateHr`, `EscapeHr`, validade da resposta, token, `Seen`, `KmdStatus`, prioridade, segmentos, tamanho, flags e handles. A consulta não muda o HRESULT original nem o fluxo de alocação. O KMD conserva os 256 resultados mais recentes com token em memória protegida pelo lock já usado no lifecycle; o escape lê a cópia. Não faz MMIO, escrita DCN ou mudança em submissão, residência ou apresentação.

Build completo KMD e UMD, auditoria de segurança e auditoria do pacote passaram offline. Pacote lab `3.264.1.0`: SYS assinado SHA-256 `F38D4F63C5C1BDD32047DD46AD908B3360EE1FD3870D1314EF71E491E36E3CA0`; UMD assinada `0FC62149B358D9ACEFDBBF7D84395E63B76A8D2F47B34EB691F9A41505E1D1B2`; RADV `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`; INF `4086129FD260DC3B884D3E6213E644C5F9DCF5C334C193C858CE0ABD9C4052D7`; CAT `FB8C798AE8CDAA566405F5A89E18E2F7F1D25BD97A99499DDD42FD7DE5733226`. O pré-voo fixa hashes e perfil.

## Critério definido antes da Corrida 103

Boot frio confirmado em 24/09/2026 às 13:40:56 após a 102. Uma corrida física de **até 180 s** pelo Daniel, executada por `tools\EXECUTAR_CORRIDA103_DX59.cmd`; o pré-voo é `tools\prepare-corrida103-dx59.ps1`. A classe recusada apareceu ~10 s após o bind da 102. A janela curta visa diagnosticar a fronteira do callback; não serve para declarar estabilidade de 600 s. O guardião de usuário pode novamente ficar impedido num travamento duro; se ocorrer, recuperar após reboot e exigir outro boot frio antes de novo bind.

| Desfecho da recusa `Bind=0xA8/Misc=0x2` | Interpretação permitida |
|---|---|
| `ReplyValid=1`, `Seen=0`, `AllocateHr=0x80070057` | A DDI KMD não registrou aquele token; investigar validação anterior ao callback **ou** perda do registro circular antes de atribuir causa. |
| `ReplyValid=1`, `Seen=1`, `KmdStatus` de falha | Recusa ocorreu no KMD; comparar campos registrados e motivo de `CREATE_ALLOC_RECUSADO`. |
| `ReplyValid=1`, `Seen=1`, `KmdStatus=0`, mas `AllocateHr=0x80070057` | O KMD entregou sucesso e a recusa veio na validação posterior do runtime/VidMm; comparar saída com o controle DWM. |
| `ReplyValid=0` ou escape falha | Sonda inconclusiva; não inferir pela ausência de log. |
| Textura deixa de ser recusada | Registrar avanço local, mas exigir pixel, responsividade e estabilidade antes de afirmar desktop funcional. |

O controle `Bind=0x8` deve ter seu `DX59_ALLOC_QUERY` comparado na mesma corrida. Registrar tempo, TDR, BugCheck, recuperação no mesmo boot, harness, pixels e eventual travamento. `hResource` KMD nulo, isoladamente, não condena o contrato. Se não houver tentativa da textura ou o anel circular perder o token, o corte é inconclusivo. **DX.59 NÃO VALIDADA EM HARDWARE. Nenhum bind ocorreu na preparação.**
