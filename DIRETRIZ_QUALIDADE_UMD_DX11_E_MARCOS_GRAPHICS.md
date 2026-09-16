# Diretriz do proprietário — qualidade, UMD D3D11 traduzida e marcos gráficos reais

**Data:** 16/09/2026. **Destino:** próxima IA responsável pela implementação.
**Natureza:** ordem de trabalho e critérios de aceitação; não é relatório de execução.

> Daniel: “não estou com pressa de finalizar quero algo bom”.

O objetivo é um driver WDDM display+render utilizável no Windows 10 22H2, com desktop acelerado pela BC-250 e jogos abertos normalmente. Priorize correção, compatibilidade, estabilidade e manutenção. Não troque o objetivo por uma demonstração mais fácil. Não declare “funcionando perfeitamente” sem um escopo e provas verificáveis.

## 1. Crítica técnica à condução anterior

A crítica é aos resultados, inferências e decisões documentadas. Não há evidência para atribuir preguiça ou intenção de enganar a outro agente. Há, contudo, problemas graves de rigor que precisam ser corrigidos.

| Falha | Evidência e limite | Correção exigida |
|---|---|---|
| Confundir uma imagem 3D com prova do pipeline gráfico | A fonte atual de `C:/Users/Daniel/Desktop/bc250-win/bc250-win/Vulkan-Cube/Cube.cpp` cria `vkCreateComputePipelines` (linha 533) e chama `vkCmdDispatch` (linha 733). Não contém `vkCmdDraw` ou `vkCreateGraphicsPipelines`. docs/280 apresenta o marco como cubo Vulkan 3D e o runner o descreve como demonstração oficial Khronos. A identidade com o executável histórico ainda precisa de hashes. | Retirar a inferência de graphics geral. Identificar origem e alterações do programa. Refazer o cubo com vertex/fragment shaders e `vkCmdDraw`. |
| Usar o Marco 3 como atalho para prontidão gráfica | `driver/tests/bc250_radv_present_gate.c` cria pipeline compute e usa dispatch; o `.comp` escreve pixels no framebuffer. | Preservar seu valor como prova de compute e apresentação; criar uma prova separada de rasterização por draw. |
| Tratar uma flag como implementação | A base recomenda habilitar a fila GRAPHICS após a falha do DXVK. Isso não demonstra que o backend executa comandos gráficos. | Auditar fila, ring, firmware, estado e submissão antes de anunciar capacidades. A falha de inicialização explica um bloqueio, não prova que o próximo estágio funciona. |
| Promover apresentação direta a desktop funcional | docs/280 registra cópia ao scanout por escape. docs/267 registra falhas de Present e ciclos de device-lost no DWM. | Provar composição, compartilhamento e apresentação pelo contrato Windows. O desktop que retorna após BasicDisplay é recuperação, não sucesso do nosso DWM. |
| Sugerir render-only como garantia de estabilidade | A proposta afirma que o desktop “NUNCA” fica preto e pressupõe seleção automática adequada do renderizador. Nada disso é garantido pela arquitetura. O caminho também está encerrado nas regras do projeto. | Manter display+render na própria BC-250. Não reabrir essa alternativa como destino. |
| Propor WARP/GDI com cópia SDMA como saída | Cópia de pixels por GPU não prova renderização/composição por GPU. | Não usar fallback de software como entrega ou como PASS. Corrigir a UMD real. |
| Equiparar DXVK por jogo a DirectX integrado | DLLs de substituição de API não implementam automaticamente as DDIs de uma UMD Windows. | Desenvolver a integração de driver. Testes com DLL local podem diagnosticar Vulkan, mas não fecham transparência ao usuário. |
| Transformar latência isolada em expectativa de FPS | docs/280 associa aproximadamente 1,35 ms a aproximadamente 740 FPS teóricos. O inverso de uma etapa não mede throughput de jogo nem frequência do monitor. | Separar tempo de render, cópia, espera, frame time total e refresh. Publicar FPS medido de ponta a ponta. |

**Não apagar as conquistas reais.** Compute acelerado e cópia SDMA são trabalho real da GPU. A falha é extrapolar essa prova para outros subsistemas. Preserve logs, binários e relatórios históricos; acrescente correções explícitas sem reescrever o passado como se o novo teste já tivesse ocorrido.

## 2. Ordem obrigatória: recriar a tela colorida do Marco 3 com vkCmdDraw

Crie uma variante gráfica identificada como **MARCO3_GRAPHICS_DRAW**, mantendo a variante compute identificável para regressão. O caminho que produz os pixels avaliados deve usar:

1. Fila com `VK_QUEUE_GRAPHICS_BIT` e backend gráfico efetivamente implementado.
2. Pipeline criado por `vkCreateGraphicsPipelines`, com shaders SPIR-V vertex e fragment.
3. Render target/color attachment, render pass ou dynamic rendering realmente suportado, viewport e scissor válidos.
4. Geometria desenhada por **`vkCmdDraw`**. Barras podem ser retângulos triangulados ou um triângulo de tela inteira com fragment shader; não podem ser uma imagem inteira pré-calculada por CPU/compute e apenas reapresentada.
5. Quadro de calibração determinístico e animação posterior. Cores e coordenadas precisam de referência independente da implementação.
6. Barriers, layouts, fences e apresentação corretos. Transferência/detiling auxiliar por GPU é permitida e deve ser identificada; não substitui o draw.

**Gate M3-GFX:** 600 quadros concluídos; prova física do trabalho submetido, fence sobre poison e conferência byte a byte da região determinística de saída; imagem observada no monitor; zero fallback e zero cópia de framebuffer por CPU para apresentação. Não exigir que a CPU fique inativa: preparar comandos, enviar geometria e ler dados para validação é legítimo.

Verificar também alteração de viewport/scissor e cores, para evitar que uma imagem fixa satisfaça o teste. A ausência de `vkCmdDispatch` na aplicação, sozinha, não prova execução gráfica no hardware: rastrear a rota até a engine que executa o draw. Não rotular MEC como engine gráfica só porque o Node 1 é anunciado como 3D ao Windows.

## 3. Ordem obrigatória: recriar o vkcube com vkCmdDraw

Crie **MARCO4_GRAPHICS_VKCUBE** com geometria real de cubo e pipeline gráfico convencional. Use explicitamente `vkCmdDraw` — por exemplo, 36 vértices para os 12 triângulos —, vertex shader com transformação e projeção, fragment shader com textura e depth test com attachment próprio.

- Não gerar a imagem do cubo por compute, ray marching ou rasterização na CPU.
- Se partir do vkcube upstream, registre repositório, revisão e alterações. Não chame um programa reescrito de “oficial” sem qualificar sua origem e modificações.
- Identifique família de fila, comandos gráficos, shaders e engine real. A aparência de um cubo não substitui essa prova.
- Exercite textura, perspectiva, oclusão, culling e profundidade com cenas discriminantes. Um teste adicional deve cobrir blending.
- Separe **prova gráfica com apresentação de laboratório** de **WSI integrado ao desktop**. Se o escape de scanout continuar, declare a limitação no título e no veredito.

**Gate M4-GFX:** 600 quadros e encerramento limpo, pixels conferidos em poses determinísticas, fence/ring e atribuição ao dispositivo físico, confirmação visual, zero fallback, zero TDR/BugCheck. Defina regiões interiores e tolerâncias justificadas para filtragem/arredondamento; não use comparação ingênua de bordas como oráculo. Para cores exatas, mantenha um subteste determinístico sem filtragem.

**Gate WSI-DESKTOP separado:** janela, resize, minimizar/restaurar, Alt+Tab e dois processos com DWM ativo. O teste deve provar que uma aplicação não sobrescreve o desktop inteiro por fora da composição. Sucesso de M4-GFX não fecha automaticamente esse gate.

## 4. UMD D3D11 traduzida: entrega de produto, não demonstração

Implemente a arquitetura desejada:

```text
DWM / aplicação D3D11 não modificada
  → runtime D3D11 + DXGI do Windows
  → DDIs reais da UMD BC-250
  → backend de tradução D3D11 para Vulkan
  → RADV + winsys WDDM
  → KMD BC-250 + GPU
  → apresentação integrada ao Windows
```

Antes da implementação extensa, escreva o desenho de integração com interfaces, ownership e uma matriz DDI → implementação → teste. Reaproveitar código maduro é desejável; adaptar DXVK não significa renomear suas DLLs para UMD. Avalie interfaces internas, licença, dependências e custo de manutenção.

O desenho deve resolver explicitamente:

- Tradução de shaders e semântica de formatos, estados, draws, cópias e queries.
- Identidade e lifetime de recursos entre UMD, Vulkan, KMD e runtime; recursos compartilhados e sincronização usados pelo desktop.
- Residência, GPUVA, pressão de VRAM, concorrência e destruição de processos.
- Importação/exportação e apresentação sem duplicar incoerentemente as mesmas alocações.
- Ausência de recursão: Vulkan usado pela UMD não pode depender, na apresentação interna, de um caminho D3D11 que volte à mesma UMD.
- Erros e device removal reais, recuperação e readmissão após TDR.
- Feature levels e capabilities publicados somente quando implementados. Priorizar DWM não autoriza callbacks falsos nem um driver que só aceita shaders conhecidos do teste.
- Instalação e versões x86/x64 conforme suporte planejado. Não substituir DLLs de sistema nem exigir configuração por aplicativo como estado final.

**Gate DX11-TRADUZIDO:** aplicação não modificada carregando a UMD pelo runtime Windows, desenhando resultados conferidos na GPU, com evidência de tradução e apresentação. Um executável carregando `d3d11.dll` local do DXVK não fecha esse gate.

**Gate DESKTOP-60MIN:** Explorer, menu Iniciar, janelas sobrepostas, transparência, texto, movimentação, resize, navegador e aplicações simultâneas por pelo menos 60 minutos. Exigir pixels corretos, ausência de laço de device-lost, ausência de WARP e manutenção da BC-250 como adaptador ativo. CPU trabalhando no sistema é normal; a composição avaliada deve ocorrer na GPU.

**Gate ROBUSTEZ-8H:** carga prolongada, pressão de memória, abrir/fechar processos repetidamente e monitoramento de recursos. **Gate TDR-DISPLAY:** hang controlado seguido de reset, readmissão e desktop visível sobrevivendo. Esses testes são critérios propostos, não resultados existentes.

“Funcionar perfeitamente” será perseguido por correção e cobertura crescente. Não prometa compatibilidade universal a partir de uma aplicação ou de uma hora de teste. Liste limitações reproduzíveis e corrija-as, sem mascará-las por fallback.

## 5. DCN e APIs seguintes

O primeiro desktop pode usar scanout GOP herdado com cópia SDMA. Isso não dispensa controle de display no produto final. Planeje DCN básico em **P9**, depois dos pré-requisitos de segurança e P5: modos, sincronização, EDID/AUX e recuperação do link. Não escrever em DCN fora de P9 nem inventar offsets. Recursos avançados dependem da capacidade física e de testes próprios.

D3D9/10 e D3D12 transparentes permanecem no roadmap, com integrações e gates próprios. Uma UMD D3D11 funcional não implica suporte D3D12. O objetivo de abrir jogos pela Steam sem configuração permanece; o desktop não pode ser abandonado em favor de demos fullscreen.

## 6. Procedimento exigido da próxima IA

1. Ler AGENTS.md, BASE_DE_AVANCO.md, docs/267, docs/280 e docs/281. Confrontar conclusões com código e evidências.
2. Preservar alterações existentes de outros trabalhos. Registrar proveniência dos fontes externos antes de editar ou importar.
3. Auditar o backend gráfico e localizar o primeiro componente realmente ausente. Não começar por apenas remover a trava da fila.
4. Implementar os dois ensaios gráficos acima e os componentes necessários, com builds e verificações offline apropriadas. Não ajustar o oráculo para aceitar saída errada.
5. Publicar hashes de EXE, DLLs, SYS e shaders, revisão/fontes, configuração, resultados e limitações. Arquivar a execução em `docs/evidence/<data>-MARCO3_GRAPHICS_DRAW/` e `docs/evidence/<data>-MARCO4_GRAPHICS_VKCUBE/`.
6. Antes de cada campanha física, escrever hipótese, resultado discriminante, orçamento de corridas e critério de abandono. Daniel executa as corridas conforme a governança; este documento não arma tunables, não autoriza ensaio destrutivo indiscriminado e não elimina watchdog/rollback.
7. Corrigir BASE_DE_AVANCO.md e docs/280 com a distinção compute/graphics e a proveniência encontrada. Nunca promover código compilado a validação física.
8. Desenvolver a UMD D3D11 traduzida até os gates de aplicação, desktop e robustez. Não encerrar a tarefa em um stub, plano, flag ou demonstração preparada.

**Critério de entrega:** trabalho executado e verificável, pendências nomeadas e nenhuma inferência vendida como medição. Se faltar prova, escrever **NÃO VALIDADO EM HARDWARE**. Qualidade tem prioridade sobre velocidade; dificuldades técnicas devem ser resolvidas ou demonstradas com evidência, nunca contornadas pela redução silenciosa da meta.

## Referências locais

- [Base de avanço](BASE_DE_AVANCO.md)
- [Plano de produto e gates](docs/281_PLANO_PRODUTO_DESKTOP_E_JOGOS_TRANSPARENTES.md)
- [Marco 4 registrado](docs/280_MARCO4_VULKAN_WSI_CUBE_CONCLUIDO_NO_SILICIO.md)
- [Present/DWM da corrida 17:18](docs/267_CORRIDA_2026-09-13_17-18_PRIMARIA_OK_E_O_CONTEXTO_DE_APRESENTACAO.md)
- [Harness atual do Marco 3](driver/tests/bc250_radv_present_gate.c)
- [Shader compute atual do Marco 3](driver/tests/bc250_radv_present_gate.comp)

Nenhum teste físico foi executado na criação deste documento. As fontes atuais foram lidas; a reconstrução da proveniência dos executáveis históricos continua pendente.
