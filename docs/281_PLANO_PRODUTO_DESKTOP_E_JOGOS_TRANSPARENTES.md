# Plano de produto — desktop acelerado e jogos sem configuração por aplicação

Revisão documental de 16/09/2026. Pedido do proprietário: instalar o driver, usar o desktop acelerado e abrir jogos normalmente pela Steam. Nenhuma corrida, bind ou alteração de registrador nesta revisão.

## Diagnóstico

A BASE_DE_AVANCO registra 600 quadros do Marco 4, trabalho MEC/SDMA e rollback para BasicDisplay. Isso é avanço físico, mas não fecha desktop DWM nem compatibilidade com jogos arbitrários. docs/267 registra Present da UMD falhando e ciclos de device-lost; docs/280 descreve apresentação por escape diretamente ao scanout.

A fonte atualmente disponível em `C:/Users/Daniel/Desktop/bc250-win/bc250-win/Vulkan-Cube/Cube.cpp` cria pipeline compute (linha 533) e usa vkCmdDispatch (linha 733), sem vkCmdDraw ou vkCreateGraphicsPipelines. Portanto, esse programa não serve de prova de rasterização pelo pipeline gráfico Vulkan. A correspondência exata entre essa fonte e o executável da corrida exige hash/proveniência; não foi reconstituída nesta análise. A conclusão geral de compatibilidade gráfica não decorre dos 600 quadros.

O diagnóstico do Marco 5 na base aponta ausência de fila GRAPHICS. Expor a flag é insuficiente: antes é necessário comprovar ring gráfico, firmware, estado, submissão e pipelines coerentes com as capacidades anunciadas. Pipeline gráfico geral: NÃO VALIDADO EM HARDWARE nesta análise.

## Arquitetura de destino

- Um adaptador WDDM display+render BC-250, monitor na própria placa.
- Desktop: runtime D3D11/DXGI do Windows → UMD D3D real → backend GPU → KMD → display.
- Jogos Direct3D: runtimes do Windows → DDIs das UMDs correspondentes → backend de tradução Vulkan integrado → RADV/winsys → KMD. Esta integração é desenvolvimento substancial, não instalação de DLLs DXVK.
- Vulkan: loader/ICD registrado → RADV → KMD, com apresentação integrada à composição do Windows.
- Display inicial: scanout GOP herdado + SDMA. Display de produto: controle DCN em P9 para modos, sincronização e recuperação do link, conforme hardware suportado.

Usar D3D11 no DWM não impede D3D12/Vulkan em jogos. Renderização/composição e controle de display são problemas distintos. O DCN já opera configurado pelo firmware; programá-lo no nosso driver não implementa shaders nem corrige a UMD.

DXVK e vkd3d-proton podem apoiar testes e reaproveitamento de código. Seus frontends de API não implementam automaticamente as DDIs de um driver Windows. O reaproveitamento precisa de estudo de interfaces, recursos compartilhados, sincronização, apresentação e licenças. Não substituir DLLs do sistema. Uma integração por perfis de jogos seria intermediária e não satisfaz a meta universal sem configuração.

## Entregas e provas

Todos os gates abaixo são propostos; não foram executados nesta revisão. Arquivar logs, hashes, traces, resultados e confirmação visual em `docs/evidence/<data>-<gate>/`.

| Ordem | Entrega | Teste de fechamento proposto |
|---|---|---|
| 0 | Proveniência e capacidades honestas | G0: associar executáveis/DLLs/fontes por hash; distinguir compute de graphics; conferir caps contra backends |
| 1 | Pipeline gráfico GPU geral | G1: aplicação independente com vkCmdDraw, vertex/fragment shaders, textura, depth e blending; pixels esperados, fences e ring; zero fallback |
| 2 | Desktop D3D11 WDDM | G2: DWM produz pixels não nulos corretos, Present aceito, sem device-lost; Explorer, janelas sobrepostas, transparência, resize e navegador por 60 min sem WARP |
| 3 | Vulkan integrado ao desktop | G3: dois processos, janela/borderless, resize, minimizar, Alt+Tab e encerramento; sem escape sobrescrevendo indiscriminadamente o desktop |
| 4 | Direct3D 9/10/11 transparente | G4: aplicações não modificadas x86/x64 usam UMD instalada; instalar jogo pela Steam e abrir sem DLL local, variável ou script; corpus por API e testes de conformidade |
| 5 | Direct3D 12 transparente | G5: UMD/DDI D3D12 integrada, feature level honesto, sincronização/residência corretas; jogo D3D12 não modificado e suite dedicada |
| 6 / P9 | DCN básico de produto | G6: flip/VBlank, AUX/EDID, modos suportados, desligar/ligar monitor e reconexão; link recuperável; readback e rollback. Respeitar pré-requisito P5 e catálogo |
| 7 | Robustez e distribuição | G7: carga simultânea e pressão de VRAM por 8 h; TDR com desktop sobrevivendo; boot/reboot, energia suportada, instalação limpa, atualização e remoção; assinatura/distribuição apropriadas |

Planejar DCN desde já, mantendo escritas exclusivamente em P9. Para o produto pedido, DCN básico deixa de ser uma melhoria dispensável; não bloqueia a primeira prova de desktop no modo GOP. HDR, VRR, MPO, HDCP, áudio DP e múltiplos monitores continuam condicionados a suporte físico e provas específicas; não são prometidos em bloco. Investigar as limitações de HPD já registradas, sem presumir que software as elimina.

Render-only com BasicDisplay não atende à meta e já foi encerrado no repositório. WARP/GDI com cópia SDMA também não atende: transportar pixels na GPU não prova composição na GPU. Resolver E_FAIL do Present é necessário, porém não substitui renderização real.

Não há estimativa percentual ou prazo confiável para paridade com driver AMD comercial. O alvo inicial deve ser um conjunto explícito de APIs, modos e jogos, ampliado com testes. Decodificação de vídeo, OpenGL e recursos avançados precisam de entregas próprias se incluídos no produto.

## Fontes

- [Microsoft — UMDs e DDIs](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/user-mode-display-drivers)
- [Microsoft — arquitetura WDDM](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-architecture)
- [DXVK — limitações no Windows](https://github.com/doitsujin/dxvk/wiki/Windows)
- [vkd3d-proton — projeto e integração](https://github.com/HansKristian-Work/vkd3d-proton)
- Evidência local: BASE_DE_AVANCO.md; docs/267; docs/280; fonte Cube.cpp acima.
