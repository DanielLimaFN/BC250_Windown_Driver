# 292 — Descritores da UMD e correção da sonda, 19/09/2026

Trabalho OFFLINE. Fonte INF reservada: **3.196.0.0**; futuro lab **3.196.1.0**.
Nenhum bind, escrita MMIO, instalação ou novo pacote de laboratório nesta sessão.
**NÃO VALIDADO EM HARDWARE.**

## Corrida 37: relato consolidado

Ensaio de **19/09/2026 12:34:24**, lab **3.195.1.0**, SYS
`B11DADBC5DE10C74614580B56A65547D6148A16585F85C7A022537335D948E13`.
Evidência: `docs/evidence/2026-09-19_12-34-24-DESKTOP-HQAG6T4-lab-binding/`.

O manifesto `display-log-final-archive.json` dizia `complete_at_capture=false`.
A cauda foi recuperada de `C:\BC250_DISPLAY_LOG.txt` a partir do offset **1631742141**,
em `display-log-this-bind-RECUPERADO.txt`: **290611 bytes, 2612 linhas**, SHA-256
`BD5BCD3B560268DA437F652D53DC4A2F06151276A6F05A65F44811D375350692`.
O arquivo original foi preservado. A cauda recuperada termina no teardown/fusível CLEAN.

- **Houve avanço? Sim, diagnóstico**, contra a Corrida 36, cuja sonda não rodou.
  Não há avanço funcional de rasterização demonstrado.
- **Sucessos:** uma onda com `STATUS=0x00818019`, PC `0x000000020000004C`,
  `IB_STS=0`, `INST_DW0=0x10040C04`, `TRAPSTS=0x50000100`.
  Os campos do header local decodificam VALID, FATAL_HALT, XNACK_ERROR e EXCP bit 8.
  `radv_shader.c:2238-2239` associa bit 8 a violação de memória nos estágios gráficos.
  A leitura não sustenta a hipótese anterior de espera em `s_waitcnt`.
- **Falhas:** TDR persistiu; três amostras `0xBEBEBEEF` foram contadas como ondas e
  consumiram o limite de quatro relatos. Restauração do seletor não foi confirmada
  (`readback=0`). Nenhum pixel de rasterização foi conferido; tela preta relatada no handoff.
  A cauda contém um quiesce/restart e nenhuma ocorrência de `GFXHUB_FAULT` ou `BugCheck`;
  ausência dessas strings não substitui auditoria dos eventos do Windows.
- **Veredito:** PARCIAL: Desfecho A do docs/290 com endereço de uma onda, mas também
  Desfecho C, que mantém ressalva sobre o reset posterior. A linha de restart não encerra
  a dúvida do seletor. Descritores ausentes continuam **H**, não causa provada: o triângulo
  sem CB/textura também travou na Corrida 34. Que XNACK explique a ausência de interrupção
  no GFXHUB permanece interpretação de mecanismo, não uma segunda medição.
- **Recuperação:** `safe-bind-final.json` registra `RECOVERY_OK_ONLINE` às **12:36:38**,
  BasicDisplay Código 0 em cinco amostras. Isso descreve o final da corrida, não consulta
  do driver ativo agora. Draw concluído, pixels e monitor pelo nosso caminho continuam
  **NÃO VALIDADO EM HARDWARE**.

## Implementação de descritores

Desenho DDI → objeto → teste:

| Entrada | Implementação | Teste offline |
|---|---|---|
| VS/PS SetConstantBuffers | b0..b13 → VkBuffer/range; upload conferido | `UMD_DESCRIPTOR_BINDING`, VS b0 diferente de PS b0 |
| VS/PS SetShaderResources | t0..t127 → VkImageView 2D/array e transição de layout fora do render pass | spy SRV t3 e ida/volta de layout |
| VS/PS SetSamplers | s0..s15 → VkSampler a partir do estado DDI | spy sampler s2, filtro e endereço |
| Compilação DXBC | interface pública vkd3d com mapeamento explícito | `test-vkd3d-shader-real.ps1 -Descriptors` |
| Draw/DrawIndexed/instanciados | pool → allocate → update → bind antes do draw | spy de ordem e recusa de alocação |
| Flush/teardown | retenção dos objetos até fence; falha bloqueia reutilização | spy de ownership e portão de retirement |

`bc250_descriptor_binding.h` é a fonte única: VS base 0, PS base 256;
CB base 0, SRV base 32, sampler base 160, set 0. Não inferimos o slot D3D pela ordem
de declaração SPIR-V. A interface de tradução define a correspondência e a reflexão
descobre somente os bindings efetivamente presentes. Tipos/arrays não reconhecidos e
reflexão malformada recusam o shader, em vez de produzirem um layout vazio.

Procedência da ABI: [header público Wine/vkd3d](https://github.com/wine-mirror/wine/blob/master/libs/vkd3d/include/vkd3d_shader.h),
`vkd3d_shader_resource_binding`, `vkd3d_shader_interface_info`, enums de visibility/type.
Tamanhos e offsets principais protegidos pelo compilador. O teste real com o provedor
instalado **vkd3d-shader 1.17** produziu VS b0=0 e PS b0=256, t3=291, s2=418,
inclusive pelo caminho de tokens DDI. Isso é teste do compilador, sem GPU.

Cada draw usa um novo conjunto; os anteriores nunca são atualizados. A implementação
conservadora termina e espera comandos anteriores antes de uploads/evicção de PSO, e
espera a fence do draw antes de liberar pool, views e samplers. Erro/timeout retém os
objetos até teardown e bloqueia novos comandos. O custo de serializar draws é deliberado;
não há alegação de desempenho. O log de draw diz que a fence concluiu e que pixels não
foram conferidos. Falha Vulkan não cai na substituição experimental por blt.

Removida a troca por shaders padrão em caso de falha de compilação: ela mascarava justamente
o programa cujos bindings precisam ser conferidos. O upload de CB passa a propagar erro de
flush e mapeia toda a alocação antes de usar `VK_WHOLE_SIZE` na manutenção de cache.

### Limites explícitos

Esta faixa implementa CB, sampler e SRV de imagem 2D/array para VS/PS. Ainda recusa
CB/SRV/sampler ausente, textura multisample, buffer SRV, combined-image-sampler,
descriptor arrays, estágios GS/HS/DS/CS, anisotropia e modos border/mirror-once.
O suporte geral a esses casos continua dívida de produto, sem fallback.

**Imagem Vulkan sem conteúdo/layout inicializado é recusada.** O backend anterior cria
texturas, mas não implementa upload geral dos dados iniciais para VkImage nem importação
coerente de todos os recursos compartilhados do DWM. Vincular uma imagem vazia não resolve
essa dívida. A faixa não anuncia DWM funcional e pode encontrar essa recusa antes do draw.
Ainda é necessário completar upload/importação e testes de residência/compartilhamento.

## Sonda e seletor

`Bc250WaveProbeStatusIsValid` exige o bit VALID real (`0x10000`) e rejeita poison.
Leitura poison incrementa `PoisonReads`, emite `INDETERMINADO_POISON` e não consome o
limite de ondas. Cada payload tem `ValidMask`; um campo não lido não se torna zero medido.
O orçamento agora é estrito por tentativa de leitura, inclusive transporte recusado:
**52 leituras SQ máximas = 32 slots máximos + 4 ondas × 5 campos**, mais um readback de
restauração. O plano atual continua com 16 slots (máximo 36 leituras SQ), sem espera ativa.

A ordem agora visita todos os pares SE/SA antes de passar à próxima instância. Isso impede
que quatro ondas do primeiro par encerrem a coleta antes de visitar os demais.
**Não deduzimos uma topologia SQ completa de “24 CUs”.** O catálogo associa a contagem ao
`CC_GC_SHADER_ARRAY_CONFIG`; isso não prova a codificação de instância/SIMD usada pela sonda.
Mantidos 2 SE × 2 SA × 4 instâncias candidatas, wave 0, com cobertura incompleta declarada.
Mapear instâncias habilitadas e slots de wave continua pendência antes de ampliar a sonda.

**Tarefa 3: inconclusiva quanto ao hardware.** O header `gc_10_1_0_sh_mask.h:27813-27825`
define posição e máscara, não permissões de leitura. Mesa `ac_cmdbuf.c:182-197` e
`ac_sqtt.c` emitem writes de broadcast, sem comparação de readback. A ausência da comparação
não demonstra que os bits sejam write-only. Portanto, não removi a checagem nem converti
readback zero em sucesso. O teste reproduz write aceito + readback zero e exige recusa;
o portão e sua mutação preservam esse limite. `selector_stuck` significa restauração não
confirmada pelo contrato, não prova conclusiva de seletor fisicamente preso.

**Próximo pacote deve sair com GfxWaveProbe=0.** O INF fonte já mantém esse default.
O `output_lab` antigo 3.195.1.0 continua ARMADO; não foi alterado nem substituído.

## Validação e artefatos

- `driver/wdk/build-contract.bat`: Exit 0, incluindo `Bc250RunWaveProbeTest`.
- `driver/umd/build-umd.bat`: Exit 0, incluindo `UMD_DESCRIPTOR_BINDING`.
- `tools/test-vkd3d-shader-real.ps1 -DllPath C:\BC250_LIB\libvkd3d-shader-1.dll -Descriptors`:
  PASS, compilação/tradução sem GPU, bindings exatos.
- `tools/test-descriptor-wave-mutations.py`: 14 mutações rejeitadas, em cópias temporárias.
- Auditorias safety, matriz DDI, no-op, RADV, espelho do fork e teste do decoder: PASS offline.

Logs e hashes em `docs/evidence/2026-09-19-descriptor-wave-offline/`.
Nenhum artefato assinado novo: hashes são de desenvolvimento, não de pacote instalado.
O build lab destrutivo e o empacotamento ficam com Daniel, como solicitado no handoff.

## Critério prévio: UMD_DESCRIPTOR_DRAW_38

**Não armado. Orçamento: um bind após revisão dos limites e preparação do pacote.**
Abandonar/revisar se aparecer recusa de recurso não implementado, regressão de SDMA,
BugCheck ou coleta incompleta. Não gastar um segundo bind repetindo a mesma configuração.
Boot frio obrigatório, watchdog/rollback preservados, sonda desarmada. Não alterar DCN.

Após Daniel gerar 3.196.1.0, auditar pacote, conferir hashes e strings no SYS/UMD assinados;
confirmar `GfxWaveProbe=0` no INF e manifesto. Não usar o output_lab antigo.

| Desfecho | Medição exigida | Interpretação |
|---|---|---|
| A | `VK_DESCRIPTOR BOUND`, draw/fence concluídos, anel convergente e pixels conferidos por gabarito | progresso físico apenas no escopo das três provas; descritores como causa suficiente ainda exigem comparação controlada |
| B | `BOUND` e mesmo hang/TDR, sem conclusão | corrigir vínculos não foi suficiente; voltar ao ISA/vertex fetch/GPUVA |
| C | `VK_DESCRIPTOR RECUSADO` ou erro de upload/alocação | draw não foi emitido; defeito/limite nomeado, hipótese de cura não testada |
| D | zero binding porque shader não usa recursos, ou harness chega após device-lost | teste sem exposição à variável; não concluir sobre descritores |

O DWM e o triângulo sem descritores devem ser identificados por processo/log. O harness atual
não confere pixels; sozinho não pode fechar A. Nenhum Exit 0, contador de draws ou fence
isolada fecha rasterização. Evidência física futura em `docs/evidence/<data>-UMD_DESCRIPTOR_DRAW_38/`.
