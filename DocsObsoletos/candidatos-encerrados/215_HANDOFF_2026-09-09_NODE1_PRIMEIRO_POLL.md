# 09/09 — Node1: primeiro polling, estado no StopDevice e bytes físicos do IB

Objetivo: WDDM acelerado pela BC-250, sem fallback. Esta entrega melhora a observação do bloqueador Node1; não implementa o backend D3D10/11 nem declara a tela preta resolvida.

## Evidência que determina a mudança

A corrida 3.105.1.0 das 08:26 restaurou os 26 registradores da MMU e reproduziu cinco tripés SDMA. Node1 permaneceu em WPTR=216/RPTR=204, sem fence/dados comprovados. O watchdog restaurou BasicDisplay. A caminhada CPU resolveu VA=0x11000 para PA=0x451FCC000, mas não foram arquivados MEC_PENDING_ARCHIVE nem MEC_IB_DESCRIPTOR_ARCHIVE. [Análise e log delimitado](evidence/2026-09-09_08-26-06-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md).

O gate anterior esperava 100 ms. Por código, uma falha de retirement anterior a esse prazo pode colocar o engine em FAULTED e impedir a captura posterior. Isso é um defeito de cobertura do diagnóstico, **não causa demonstrada do hang físico**. Os contadores do registro colhidos no bind antecedem o submit Node1 e não provam que o timer nunca rodou.

## Alterações offline

- O primeiro polling com fence pendente captura a observação antes de Bc250PersistentRingRetire, inclusive quando esse retirement devolve erro. Uma observação pendente inicial não é veredito de hang.
- StopDevice chama Bc250SubmissionEngineArchiveDiagnostics depois de drenar o timer/DPC e antes de liberar os recursos MEC. A fotografia independe de engine ONLINE, VMID9, fence pendente ou prazo. Se o engine não foi inicializado, registra UNAVAILABLE. Leituras de registradores têm máscara de validade, distinguindo zero de leitura ausente.
- MEC_STOP_STATE registra estado, fences submetida/retirada, polls, falhas, DPCs, último submit e fence física observada. MEC_STOP_QUEUE registra oito campos da fila; MEC_LAST_DESCRIPTOR preserva os quatro dwords publicados. A fotografia no encerramento pode ser posterior ao TDR: não confundir com o instante da falha.
- Antes do submit, captura CPU de até 32 dwords na página física do IB resolvida pelo walker. Limitada à primeira página e à janela VRAM já mapeada, com verificação de limites por subtração. Não presume páginas seguintes contíguas. NODE1_IB_CPU_ARCHIVE/WORD grava os dados no StopDevice. CapturedDwords=0 significa ausência de captura, não IB zerado. Os 68 bytes do ensaio cabem nesse limite quando acessíveis.
- Teste executável cobre primeiro polling pendente, fence regredida causando FAULTED e preservação da observação após falha. Testes de mutação verificam os gates sem alterar fontes em disco.

Não foram alterados os comandos PM4, os valores VMID, o backend D3D, os controles de fault ou DCN. Não houve bind. **NÃO VALIDADO EM HARDWARE.**

## Candidato e validação

Pacote de laboratório **3.106.1.0**, assinado e auditado, em driver/wdk/package/output_lab.

- SYS: C188B2DCA7F63350DFF71E4E91EC9ADC80B4D5C4D7CB447EFAAAE9CB73AAC1E6
- UMD: 35FBD78186D300D84F25DB76D1AF1C3025C7FECA3211CA422D35619B116AF214
- INF: 230C1CEC7284A163E2AA028C137225F167E84FA17D9EE8E9254FEABCA6C18208
- CAT: DD8078792EFB083E316EEFA684EB4412834ECEED25A69CA23F5E42205F1CAD33

Build contratual e de laboratório EXIT=0; auditorias de segurança, UMD e pacote PASS; implementação aceita e nove mutações recusadas. Logs e manifesto em [evidência offline](evidence/2026-09-09_NODE1_PRIMEIRO_POLL_OFFLINE/).

## Próximo ensaio: teto de uma corrida

Conforme AGENTS.md §3, regra 10, Daniel executa a corrida física. Boot frio entre binds; o binder mantém os preflights e o watchdog. Não armar hang induzido nem fallback. Na pasta do projeto, em PowerShell elevado:

```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180
```

Objetivo do ensaio: obter o tripé Node1 ou evidência discriminante, não desktop visível. Sucesso físico continua exigindo anel drenado, fence exata sobre poison e dados byte a byte. Se falhar, não repetir: comparar os dwords físicos do IB com WRITE_DATA/RELEASE_MEM enviados pela UMD, conferir descritor real, estado/fence/contadores MEC e validade das leituras. Bytes CPU corretos não provam busca GPU. Ausência de captura deve ser investigada como falha de observabilidade, não como diagnóstico de hardware.

## O que ainda separa o projeto do desktop acelerado

1. Execução Node1 com tripé completo, hoje aberta.
2. Backend D3D10/11 nativo: BC250_D3D10_11_NATIVE_READY permanece 0 e a criação de dispositivo continua recusada. Apenas trocar essa flag não implementa renderização.
3. DWM produzindo pixels e Present apresentando-os pelo SDMA, com prova visual sob nosso driver.
4. Recuperação MEC/display e testes de D3D12/Vulkan/jogos. A recuperação TDR já medida é SDMA, não recuperação geral de todas as filas.

Nenhum desses resultados pode ser declarado concluído por compilação ou por aceitação de submit.
