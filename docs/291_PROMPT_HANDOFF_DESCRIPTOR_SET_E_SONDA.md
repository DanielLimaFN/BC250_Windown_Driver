# 291 — Prompt de passagem para a próxima IA (19/09/2026)

> Texto abaixo é o prompt, em texto simples, para ser colado na próxima sessão.
> Contexto: Corrida 37 (19/09 12:34:24, lab 3.195.1.0) fechou no Desfecho A do docs/290.

---

```text
Você vai assumir o desenvolvimento do driver WDDM da AMD BC-250 (1002:13FE, GC 10.1.3 /
Cyan Skillfish 2) em Windows 10 Pro 22H2 build 19045. O repositório é
C:\Users\Daniel\Desktop\NamixWDDM.

ANTES DE ESCREVER UMA LINHA DE CÓDIGO, LEIA NESTA ORDEM:

1. CLAUDE.md — as nove Leis inegociáveis e a diretriz de relato de corridas de 16/09.
   Preste atenção especial à Lei 1 (sinceridade), Lei 2 (tripé de validação), Lei 4
   (zero fakes e zero fallback de software), Lei 7 (endereços só com procedência) e
   Lei 8 (vocabulário de confiança P0/P1/P2/H/Q).
2. BASE_DE_AVANCO.md — fonte única de verdade. Leia os blocos das Corridas 34, 35 e 36,
   que estão no topo. Eles contêm as medições que sustentam tudo abaixo.
3. docs/290_CRITERIO_CORRIDA_36_ONDE_A_ONDA_PARA.md — o critério escrito antes das
   Corridas 36 e 37, com os quatro desfechos e o que cada um significa.
4. docs/289_CRITERIO_CORRIDA_35_ONDE_O_CP_PARA.md — o critério anterior, que estabeleceu
   que o CP não espera sincronização.
5. external/radv-wddm/PROVENANCE.md — a árvore Mesa/RADV fica FORA do repositório, em
   C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm. Este arquivo explica a
   cadeia commit base -> patch -> DLL e como conferi-la.
6. docs/288_HANDOFF_2026-09-19_CORRIDA34_CURA_GCVM_E_DIAGNOSTICO_TELA_PRETA.md — LEIA A
   RETIFICAÇÃO NO TOPO. O corpo desse documento contém três afirmações refutadas por
   medição; a retificação diz quais.

ONDE O PROJETO ESTÁ

O caminho D3D11 -> Vulkan (RADV) -> PM4 -> anel gráfico CP_RB0 executa no silício. O CP
aceita, traduz sob VMID 7 e busca o IB. O que não acontece é o draw terminar: o anel
trava, o TDR dispara e a tela fica preta.

A Corrida 37 respondeu POR QUE, e o resultado está medido, não suposto:

  [TDR_GFX_WAVEPC] onda #0 SE=0 SA=0 Inst=0 Wave=0
                   STATUS=0x00818019  PC=0x000000020000004C
                   IB_STS=0x00000000  INST_DW0=0x10040C04  TRAPSTS=0x50000100

Decodificado com inc/linux_ref/gc_10_1_0_sh_mask.h:
  SQ_WAVE_STATUS  -> VALID + FATAL_HALT
  SQ_WAVE_TRAPSTS -> XNACK_ERROR + EXCP bit 8
  SQ_WAVE_IB_STS  -> 0, nenhum waitcnt pendente

O bit 8 do EXCP é violação de memória para estágios gráficos. Procedência: a própria
árvore do Mesa, src/amd/vulkan/radv_shader.c linha ~2238, onde
RADV_TRAP_EXCP_MEM_VIOL vira excp_en |= 1 << 8.

Conclusão medida: a onda NÃO está esperando memória. Ela sofreu violação de memória,
recebeu XNACK e foi fatalmente parada. Como ela nunca retira, o CP fica ocupado para
sempre e o TDR dispara. Isso também explica por que GFXHUB_FAULT sempre leu zero: com
XNACK a falha é entregue à onda em vez de virar interrupção do GFXHUB.

O QUE VOCÊ DEVE FAZER, EM ORDEM

TAREFA 1 (principal) — vincular descriptor sets na UMD.

Constatação por leitura de código, já feita: em driver/umd/, as funções
vkAllocateDescriptorSets, vkUpdateDescriptorSets, vkCmdBindDescriptorSets e
vkCreateDescriptorPool são CARREGADAS em bc250_umd_vulkan_bridge.c e NUNCA CHAMADAS em
lugar nenhum. Confira você mesmo antes de agir:

  grep -rn "vkCmdBindDescriptorSets" driver/umd/ | grep -v LOAD_VK_DEV

O caminho de draw está em driver/umd/bc250_d3d_umd.c, função Bc250UmdExecuteVkDraw, por
volta da linha 2640-2730. Ele faz vkCmdBeginRenderPass, vkCmdBindPipeline,
vkCmdSetViewport, vkCmdSetScissor, vkCmdBindVertexBuffers e vkCmdDraw. Não há
vkCmdBindDescriptorSets. Os layouts de descriptor set são criados em
driver/umd/bc250_umd_pipeline_cache.c, mas nenhum conjunto é alocado, escrito ou
vinculado.

Quem submeteu o IB que travou foi o dwm.exe (30 draws na Corrida 37, 24 na 35). Os
shaders do DWM usam constant buffers e texturas. Um shader que dereferencia um descriptor
nunca vinculado produz exatamente uma violação de memória.

RESSALVA QUE VOCÊ NÃO PODE IGNORAR: na Corrida 34 o triângulo do harness de conformidade
também travou, e ele não usa constant buffer nem textura. Ou são duas causas distintas,
ou o caminho de descritor de vertex buffer do RADV também é afetado. Trate a hipótese do
descriptor set como SUSPEITO PRINCIPAL (classe H), não como causa provada.

TAREFA 2 — corrigir dois defeitos da sonda de onda.

A sonda vive em driver/core/bc250_gfx_ring_transaction.c (função Bc250WaveProbeRun) e é
acionada no quiesce do TDR em driver/wdk/bc250_active_miniport.c. A suíte offline está em
driver/tests/gfx_ring_contract_test.c, função Bc250RunWaveProbeTest.

2a. Bc250WaveProbeStatusIsValid rejeita apenas 0 e 0xFFFFFFFF. Na Corrida 37 as ondas
    #1, #2 e #3 vieram com STATUS=0xBEBEBEEF, que é marcador de leitura inválida, e foram
    contadas como ondas reais. Elas consumiram o orçamento de 4 ondas relatadas
    (BC250_WAVE_PROBE_MAX_REPORTED) e PARARAM a varredura. Só uma onda real foi lida.
    Rejeite 0xBEBEBEEF e considere tratar leitura poison como INDETERMINADO nomeado.

2b. A varredura visita 2 SE x 2 SA x 4 instâncias, apenas wave 0, e todas as quatro
    entradas relatadas saíram com SE=0 SA=0. Depois de corrigir 2a, reavalie se a
    topologia de instância que o plano monta corresponde à da placa (24 CUs). O orçamento
    de leituras é fixo de propósito: laço longo em DISPATCH_LEVEL foi a causa do BugCheck
    0x133 em 10/09 12:16. Não remova o teto; recalcule-o.

TAREFA 3 — decidir se o veredito selector_stuck é real.

Na Corrida 37 a sonda devolveu status=selector_stuck com
SeletorRestaurado=0 (readback=0x00000000). O contrato exige que GRBM_GFX_INDEX leia de
volta 0xE0000000 depois da restauração do broadcast.

SUSPEITA: esse critério pode estar errado. O caminho MEC já existente no projeto escreve
BC250_GRBM_GFX_INDEX_BROADCAST e NUNCA lê GRBM_GFX_INDEX de volta esperando esse valor —
ele confere um registrador diferente. Os bits de broadcast podem ser write-only. Se for
esse o caso, o alarme é do nosso portão, não do silício.

Resolva isso por leitura de gc_10_1_0_sh_mask.h e da árvore do Mesa antes de mudar
qualquer coisa. Se o critério estiver errado, corrija o contrato E o portão
audit_wave_probe_restores_the_selector em tools/audit-bc250mcdm-safety.py. Se estiver
certo, o risco é real e a sonda deve sair desarmada até a causa ser entendida.

REGRAS DE TRABALHO DESTE PROJETO QUE VOCÊ PRECISA SEGUIR

- O Daniel executa as corridas físicas; você compila, analisa e prepara. Corrida é recurso
  escasso. Responda o que puder por leitura de fonte e mineração de docs/evidence/ antes
  de propor ensaio.
- Escreva o critério da corrida ANTES de armar, com desfechos distinguíveis, e diga o que
  cada desfecho significa. Veja docs/289 e docs/290 como modelo.
- Todo item fecha com teste nomeado. Toda mudança de contrato precisa de portão em
  tools/audit-bc250mcdm-safety.py, e todo portão precisa ser verificado POR MUTAÇÃO
  (reintroduza o defeito e exija que a auditoria reprove).
- ARMADILHA RECORRENTE 1: require() nessa auditoria casa SUBSTRING, e o nome certo é
  quase sempre PREFIXO do errado. Renomear um campo é a forma prática de removê-lo. Use
  require_token(), que já existe no arquivo, e anchore em atribuição quando o campo puder
  ser apenas mencionado em comentário.
- ARMADILHA RECORRENTE 2: código inserido depois de
  #if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP) em
  driver/wdk/bc250_active_miniport.c NÃO EXISTE na imagem de laboratório. Isso já custou
  quatro corridas no projeto. Tunables de laboratório devem ser lidos antes desse #if. O
  portão audit_lab_tunables_are_read_in_the_common_path existe para impedir a quinta.
- ARMADILHA RECORRENTE 3: o empacotador NÃO compila o SYS, ele copia. Sempre rode
  driver\wdk\build-contract.bat antes de empacotar, e sempre confira as strings novas no
  binário ASSINADO com grep -a.
- ARMADILHA RECORRENTE 4: heredoc do bash corrompe barras invertidas (\a vira BEL, \b vira
  BS). Para conteúdo com caminho do Windows, escreva o script com a ferramenta de escrita
  de arquivo, não por heredoc.
- Boot frio é obrigatório entre binds. O estado de engine não cai com desbind.
- C:\BC250_DISPLAY_LOG.txt é cumulativo e sobrevive a reboot. A pasta de evidência pode
  estar truncada; desde 19/09 o arquivamento declara isso em
  display-log-final-archive.json, campo complete_at_capture. Se vier false, resgate a
  cauda pelo bind_start_offset antes de analisar qualquer coisa.

FERRAMENTAS QUE VOCÊ VAI USAR

  driver\wdk\build-contract.bat                        compila KMD e suítes offline
  driver\umd\build-umd.bat                             compila a UMD
  python tools\audit-bc250mcdm-safety.py               auditoria principal
  python tools\audit-mcdm-ddi-matrix.py                matriz de DDIs
  python tools\audit-ddi-noop-callbacks.py             DDIs sem backend
  python tools\sync-radv-fork-delta.py                 confere o espelho do fork RADV
  python tools\audit-radv-wddm-safety.py --mesa-root "C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm"
  python tools\decode-gfx-ib.py <arquivo.bin> --stopped-at-opcode 0x.. --remaining-dw N
  python tools\test-decode-gfx-ib.py                   suíte do decodificador

Empacotamento de laboratório (os dois primeiros exigem o Daniel, por causa do flag de
bring-up destrutivo):

  powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
  powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -ArmGfxWaveProbe -IAcceptSelectorWritesDuringTdr
  powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-lab-package.ps1

Suba o DriverVer fonte em driver\wdk\package\bc250mcdm.inf antes de gerar cada candidato.
O atual é 3.195.0.0 e o último candidato empacotado é o 3.195.1.0. Dois artefatos
diferentes com o mesmo DriverVer já foi erro do projeto uma vez; não repita.

ESTADO ATUAL DOS ARTEFATOS

  Pacote lab 3.195.1.0, assinado e auditado, NÃO INSTALADO
    SYS  B11DADBC5DE10C74614580B56A65547D6148A16585F85C7A022537335D948E13
    UMD  372FEBCFD24C7B2217A0413B3DFEDF35A9D475D02DADF5CAADD8787DF412D678
    INF  D9D5210DC1BEE4B3B5DF70875F30C4CE0433F7BE487F69219634B5BB804251C8
    CAT  1855D8AB69129E9C1C4D839307DFE2BEE6C181D4D6AC8EB6B1882851BFF2F437
  vulkan_radeon.dll implantado em C:\BC250_LIB\
    7837c96027e5e56233fa16735cbe22a301a62f61d1e0075ad833efddeefe5868

O QUE CONTINUA NÃO VALIDADO EM HARDWARE

  - conclusão de um draw sem hang no CP_RB0
  - qualquer pixel produzido por rasterização
  - imagem no monitor físico vinda do nosso caminho

TELA PRETA É O RESULTADO ESPERADO enquanto o draw não concluir. Qualquer imagem que
aparecer deve ser atribuída ao rollback ou ao BasicDisplay até prova em contrário — esse
engano já foi cometido e corrigido duas vezes neste projeto.

Não anuncie sucesso sem as três provas da Lei 2. Se faltar prova, escreva literalmente
NÃO VALIDADO EM HARDWARE.
```
