# BC-250 — Validação Obrigatória em Hardware Real (Fase Ativa de Produção)

## Hardware-alvo

O alvo deste projeto é a **AMD BC-250**, uma APU com 16 GB de GDDR6 compartilhados dinamicamente entre CPU e GPU.

A configuração pode variar entre unidades e modificações de hardware:
- CPU com 6 ou 8 núcleos habilitados, podendo ser modificada.
- GPU com 24 CUs habilitadas por padrão, podendo existir mais CUs fisicamente presentes ou ativáveis.
- Quantidade de memória disponível para CPU, GPU, VRAM, GTT e BAR/ReBAR dependente da configuração da plataforma.

Nunca fixe essas quantidades apenas com base em documentação, constantes do driver ou valores esperados. Detecte e registre em runtime:
- núcleos físicos e habilitados da CPU;
- CUs e WGPs físicos, habilitados e realmente utilizáveis;
- VRAM, GTT, BAR e ReBAR efetivamente mapeados;
- clocks reais;
- estado real do GFXOFF;
- firmware e engines realmente ativos.

---

## Regra Inegociável de Aceleração

Não considere enumeração de API, carregamento de DLL, criação de contexto, criação de pipeline, abertura de janela, criação de swapchain, retorno de sucesso ou valores anunciados pelo próprio driver como prova de aceleração.

Não considere como funcionamento da GPU:
- execução pela CPU;
- WARP, GDI ou qualquer renderizador de software;
- fallback silencioso;
- resultado predefinido ou fabricado;
- stub, mock, no-op ou função que apenas retorna sucesso;
- fence incrementado ou sinalizado pela CPU;
- telemetria fixa, estimada ou não lida do silício;
- `vulkaninfo`, `glGetString` ou ferramenta equivalente usada isoladamente.

---

## Evidências Obrigatórias no Silício

Toda funcionalidade de renderização, compute, cópia ou aceleração deve ser comprovada no silício da BC-250 por todas as evidências aplicáveis:

1. Firmware correto aceito e engine física inicializada sem erro.
2. Comando válido submetido a um ring físico correspondente à engine.
3. WPTR avançando após a submissão.
4. RPTR avançando até alcançar o WPTR.
5. Fence escrito pela própria GPU em memória e observado pela CPU.
6. Resultado produzido pela GPU comparado com um resultado conhecido da CPU.
7. `GRBM_STATUS` ou estado específico da engine mudando durante a carga.
8. Clock, potência e temperatura reagindo de maneira coerente à carga.
9. GFXOFF comprovadamente desativado durante a execução, não apenas solicitado por mensagem.
10. Ausência comprovada de fallback por software.

Uma leitura isolada de registrador, um ring habilitado ou um WPTR alterado não provam execução. O conjunto **RPTR/WPTR, fence físico e resultado correto** é obrigatório para declarar que trabalho foi executado.

---

## Relatório Obrigatório Antes de Declarar Sucesso

Antes de usar palavras como “funciona”, “concluído”, “acelerado”, “validado” ou equivalentes, informe:
- configuração detectada da CPU, CUs/WGPs e memória;
- firmware e versão carregados;
- engine e ring utilizados;
- RPTR e WPTR antes e depois;
- endereço e valor do fence físico;
- resultado esperado e resultado produzido pela GPU;
- clock antes, durante e depois da carga;
- estado do GFXOFF antes e durante a carga;
- método usado para excluir CPU, WARP, GDI e outros fallbacks.

Se qualquer prova obrigatória estiver ausente, declare exatamente:
**NÃO VALIDADO EM HARDWARE**

---

## Transição para a Fase Ativa Autorizada (MCDM Live Driver)

- **Pacote Ativo de Produção**: `bc250mcdm.inf`, `bc250mcdm.sys`, `bc250mcdm.cat` localizado em `driver\wdk\package\output_active\`.
- **Modo de Operação**: `Class=ComputeAccelerator` com inicialização ativa de hardware, submissão de comandos e TDR funcional.
- **Procedimento de Recuperação**: O script `C:\BC250_RECOVERY.cmd` e ferramentas PnP estão disponíveis para rollback imediato para `BasicDisplay` caso necessário.

Antes de qualquer implementação ou plano, apresente a barra de status com as porcentagens atualizadas.