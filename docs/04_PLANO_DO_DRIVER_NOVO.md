# Plano do novo driver BC-250

## Decisão de base

O novo driver será escrito do zero. As tentativas anteriores são fontes de evidência, vetores de teste e armadilhas conhecidas, não bases de código.

O desenvolvimento deve manter dois produtos separados:

1. **driver de laboratório (`dbg`)**: acesso read-only por padrão, scripts de experimento, logging e operações perigosas explicitamente armadas;
2. **miniport de produção**: superfície mínima exigida pelo modelo Windows escolhido, sem MMIO/SMN arbitrário e com capacidades fechadas por gates físicos.

O miniport não deve carregar rotinas experimentais apenas “desabilitadas por registro”. Código destrutivo fica fora do binário de produção sempre que possível.

## Escolha do primeiro alvo

Antes de implementar, verificar no WDK/Windows alvo:

- suporte e requisitos atuais de MCDM;
- DDIs obrigatórias para adapter, process, device/context, GPUVA, paging, submit, interrupt, reset e power;
- biblioteca correta de inicialização (`displib.lib` quando aplicável), sem depender de scan de exports de `dxgkrnl.sys`;
- implicações de coexistência com KMDOD/BasicDisplay.

Preferência técnica: **compute-first/MCDM**, se suportado pelo ambiente alvo, porque permite provar memória, filas e scheduler sem assumir controle do DCN. Caso o alvo exija full WDDM, implementar inicialmente um render-only adapter sem anunciar display acelerado; manter o KMDOD de recuperação separado.

## Estrutura proposta

```text
driver/
  core/        PnP, lifecycle, locks, logging, error/unwind
  asic/        identidade Cyan Skillfish e tabela de IPs
  reg/         headers gerados, cálculo SOC15, acesso MMIO/SMN tipado
  memory/      BARs, VRAM aperture, GTT, GPUVA, page tables, HDP/TLB
  firmware/    parser, manifest, PSP transport, direct loaders por engine
  smu/         mailbox, queries, DPM, GFXOFF e thermal safety
  gfx/         CP GFX ring, PM4 mínimo, fence e reset
  compute/     MQD, HQD, KIQ, EOP e filas compute
  sdma/        firmware, ring, pacotes e fault capture
  irq/         IH, interrupções, DPC e monitored fences
  wddm/        DDIs e adaptação VidMm/VidSch
  tests/       testes offline, integração e hardware
tools/
  reggen/      gera offsets finais de headers upstream
  fwinspect/   hashes, headers e versões de firmware
  capture/     coleta uma sessão de bring-up reproduzível
docs/evidence/
  <data-build-unidade>/ manifest + logs + dumps + veredito
```

## Princípios de implementação

### 1. Tipos de endereço não intercambiáveis

Usar tipos/estruturas distintas para:

- `bar_byte_offset`;
- `soc15_mmreg_dw`;
- `smn_address`;
- `cpu_va`;
- `cpu_pa`;
- `gpu_mc_address`;
- `gpu_va`;
- `vram_bar_offset`.

Conversões devem ser funções explícitas, validadas e logadas. Isso elimina a classe de bug que confundiu `0x16000` dwords com bytes e PAs de host com endereços GPU.

### 2. Registro gerado, não copiado

O gerador recebe:

- nome do IP e versão;
- bases Cyan Skillfish;
- header de offsets;
- header de masks/shifts.

Produz:

- enum estável;
- `mmREG`, `BASE_IDX` e byte offset calculado;
- assert de alinhamento/limite BAR;
- tabela de nomes para logs;
- teste que bloqueia os endereços em quarentena.

### 3. Bring-up transacional

Cada etapa implementa:

```text
precheck -> snapshot -> apply -> readback -> functional probe -> commit
                                  |
                                  +-> rollback/unwind em falha
```

Não há função única que “tenta tudo”. O estado do adapter registra qual gate foi atingido e a causa crua da parada.

### 4. Fail-closed

- capacidades começam falsas;
- cada gate só pode ser elevado pelo teste físico correspondente;
- falha de um bloco não é convertida em sucesso por fallback;
- diagnóstico pode retornar sucesso de transporte, mas carrega `engine_status` e `proof_bits` obrigatórios;
- UMD não recebe entry point de execução se o KMD não abriu o gate físico.

### 5. Evidência como ABI

Estruturas de diagnóstico devem ter:

- `schema_version`, `struct_size`, `build_id`;
- configuração detectada e origem;
- engine/instância/ring;
- firmware hash/versão/método/status;
- RPTR/WPTR antes/depois;
- fence address, poison, esperado e observado;
- resultado hash/comparação;
- status/fault registers antes/depois;
- clock/GFXOFF/thermal antes/durante/depois;
- `proof_bits` e veredito.

Consumidor de schema antigo deve falhar, não interpretar zeros.

## Fases e gates

### Fase 0 - higiene e reprodutibilidade

Entregas:

- build reproduzível e assinado;
- manifest do binário instalado;
- coletor de recursos PnP e configuração;
- logging persistente com build ID;
- reggen SOC15;
- testes offline de framing PSP e parsers de firmware;
- lista de operações perigosas fora do binário de produção.

Gate:

- duas builds idênticas produzem os mesmos hashes quando o ambiente permite;
- o coletor distingue binário no disco do binário instalado;
- nenhum endereço `Q` aparece no código ativo.

### Fase 1 - PnP, BARs e MMIO read-only

Entregas:

- mapear recursos recebidos pelo SO;
- detectar PCI ID/revision, tamanhos BAR/ReBAR e CPU cores ativos;
- acesso MMIO bounds-checked;
- detector de fill patterns;
- snapshot de registros P0/P1 read-only.

Gate:

- scratch correto faz round-trip host MMIO com restauração;
- endereços de quarentena são identificados como diferentes/fill;
- nenhuma quantidade é anunciada por constante.

Isto prova acesso do host, não execução da GPU.

### Fase 2 - SMN/SMU seguro

Entregas:

- lock único INDEX/DATA;
- Q0/Q3 query-only;
- tabela DMA de métricas, se disponível;
- thermal trip;
- readback de GFXOFF;
- parser/estado de DPM e PPTable antes de qualquer force.

Gate:

- versão/interface/clock/VID retornam status válido;
- telemetria varia de forma coerente sob uma carga externa conhecida;
- GFXOFF é observado desativado, não apenas solicitado;
- nenhum teste automatizado muda clock/voltagem.

### Fase 3 - memória e GPUVA mínima

Entregas:

- inventário de VRAM aperture, GTT e ReBAR;
- alocação GPU-visible com domínio explícito;
- page table mínima, GFXHUB/MMHUB, HDP e TLB;
- página de fence e writeback.

Gate:

- tradução validada por uma engine física já disponível (PSP pode servir para validar DMA para host memory, mas não substitui teste posterior de GFX/SDMA);
- fault registers permanecem limpos;
- nenhum `cpu_pa` é usado como `gpu_va` sem mapeamento registrado.

### Fase 4 - PSP e firmware manifestado

Entregas:

- ring GPCOM do zero, framing validado offline;
- comando PSP suportado com fence real;
- caminho direto 35/36/37 reproduzido em teste isolado;
- parsers por tipo de firmware;
- loaders direct SRAM/IC base baseados no upstream exato;
- halt/unhalt sempre explícito.

Gates separados:

1. `PSP_TRANSPORT_PROVEN`: WPTR/fence/response de comando suportado.
2. `FW_ACCEPTED_<engine>`: status de transporte/firmware.
3. `FW_ACTIVE_<engine>`: versão/status da engine e ring test físico.

Nunca colapsar os três em `FirmwareReady` sem bits individuais.

### Fase 5 - SDMA mínima

Por ser a única engine que mostrou atividade parcial, SDMA é o melhor laboratório para memória/ring/fence.

Ordem de testes após cada init limpo:

1. snapshot de status/fault;
2. `NOP` + fence;
3. `CONST_FILL` + fence + comparação integral;
4. COPY pequeno + fence + comparação integral;
5. sequência de 1, 2, 3, 4, 20 submissões;
6. variar intervalo, tamanho e wrap;
7. capturar o instante de transição para estado latchado;
8. testar reset/reinit isolado.

Gate:

- 1.000 operações alternadas fill/copy, zero divergências;
- RPTR/WPTR e fence corretos em cada uma;
- wrap de ring exercitado;
- resultado integral correto;
- status/fault limpo;
- clock/energia coerentes;
- throughput só é reportado depois da correção funcional.

Antes disso: **NÃO VALIDADO EM HARDWARE**.

### Fase 6 - CP GFX ring mínimo

Entregas:

- direct load validado de PFP/ME/CE conforme upstream;
- ring GFX com GPUVA, RPTR writeback e fence;
- pacote PM4 mínimo de scratch/memória;
- captura de bad opcode/priv fault/VM fault;
- reset controlado.

Gate T2:

- CPU escreve A;
- somente o PM4 recebe instrução para escrever B;
- WPTR avança;
- RPTR alcança;
- fence da GPU chega;
- memória/scratch contém B;
- GRBM/CP mostra transição coerente;
- repetir após wrap e após idle.

### Fase 7 - KIQ/MQD/HQD e compute

Entregas:

- MQD em memória GPU;
- EOP buffer;
- KIQ/HQD com VMID 0 inicialmente;
- `SET_RESOURCES`/`MAP_QUEUES` conforme GFX10.1.3;
- ring compute;
- shader mínimo pré-compilado com manifesto ISA;
- fence e resultado.

Gate:

- vetor/matriz simples produzido pela GPU e comparado à CPU;
- nenhum loop de resultado no KMD/UMD;
- RPTR/WPTR/fence/resultado/status/clock/GFXOFF completos;
- teste negativo prova que remover o kick ou corromper o opcode impede o resultado.

### Fase 8 - WDDM scheduler e memória completa

Entregas:

- processes/devices/contexts;
- múltiplos VMIDs e root page tables;
- residency, paging e eviction;
- interrupts/DPC e monitored fences;
- timeout, preempção e reset;
- isolamento entre processos.

Gate:

- dois processos concorrentes não acessam memória um do outro;
- paging sob pressão funciona sem corrupção;
- timeout injeta falha e recupera;
- Driver Verifier e stress prolongado sem leak/bugcheck.

### Fase 9 - UMD/API mínima

Somente depois dos gates físicos anteriores:

1. UMD interno de teste: buffer, copy, clear, dispatch.
2. API compute mínima ou Vulkan ICD pequeno.
3. renderização simples offscreen.
4. apresentação/display depois.

Cada entry point não implementado retorna erro apropriado; nunca stub de sucesso.

## Protocolo padrão de teste físico

### Preparação

- registrar BIOS, firmware, build e hash;
- detectar CPU/CU/WGP/memória/BAR/ReBAR/GTT;
- registrar clocks, temperatura, potência e GFXOFF;
- salvar todos os registros que serão alterados;
- definir recuperação: rollback, restart do device, reboot ou modo seguro.

### Execução

- envenenar destino e fence com valores dependentes da sequência;
- submeter comando a ring identificado;
- amostrar estado durante a janela de carga;
- aguardar fence com timeout limitado;
- invalidar coerência pelo mecanismo correto;
- comparar todo o resultado, não o primeiro byte;
- preservar logs mesmo quando a engine falha.

### Aprovação

Um recurso só é aprovado quando todas as provas aplicáveis de `BC250_HARDWARE_RULES.md` estão presentes. Para ring físico, no mínimo:

- firmware/engine ativo;
- RPTR/WPTR;
- fence escrita pela engine;
- resultado correto;
- estado/clock reagindo;
- GFXOFF físico;
- ausência de fallback.

Estado de 2026-08-22: o ensaio SDMA mínimo já obteve firmware/engine, RPTR/WPTR,
fence e resultado correto em quatro dwords. Ainda faltam confirmar o rollback
corrigido em boot limpo, capturar clock/potência/temperatura durante carga e
obter readback físico de GFXOFF. Até esses itens passarem, o marco continua
**NÃO VALIDADO EM HARDWARE** e as fases de CP/compute/WDDM permanecem bloqueadas.

Atualização ABI 4: `SMUIO_GFX_MISC_CNTL` em BAR5 `0x5A314` foi derivado do
upstream Van Gogh e lê o estado físico de GFXOFF nos bits `2:1`. O laboratório
agora captura esse campo, GRBM, estado SDMA/F32/clock e térmica com QPC, tanto em
sampler CSV concorrente quanto no ponto exato do kick. O estado ocioso `0` foi
observado; a transição durante carga ainda é gate pendente. Potência permanece
indisponível por falta de fonte calibrada.

### Testes negativos obrigatórios

- não atualizar WPTR -> nenhuma fence/resultado;
- opcode inválido -> fault/timeout, nunca resultado válido;
- fence em endereço errado -> timeout controlado;
- destino sem poison -> teste rejeitado;
- UMD/KMD incompatível por schema -> falha explícita;
- remover firmware -> gate bloqueado;
- desabilitar engine -> resultado não aparece.

Eles demonstram que o teste realmente depende do caminho de hardware alegado.

### Requisito de projeto vindo da medição de 2026-08-23

Um pacote com opcode reservado deixa a SDMA0 num estado em que ela anuncia
conclusão — RPTR alcançando o WPTR e fence física escrita pela GPU — **sem
produzir o dado**. Nenhuma ação por software recupera: nem reset do F32, nem
recriar o anel, nem recarregar o firmware pelo PSP, nem soft reset do bloco via
`GRBM_SOFT_RESET`. Medido em seis boots.

Consequências obrigatórias para o driver definitivo:

1. O construtor de pacotes é fronteira de confiança. Opcode vem de enumeração
   fechada, nunca de dado externo, e toda entrada é validada antes de virar
   pacote.
2. A conclusão de qualquer operação exige **ponteiro, fence física e dado
   conferido**. Fence sozinha já foi observada mentindo no silício.
3. Detectada a assinatura de fence-sem-dado, a engine é marcada inutilizável e
   novas submissões são recusadas. Sem fallback silencioso.
4. Recuperação real exige reset em nível de dispositivo, que nesta plataforma
   compartilha a GPU com o display e por isso é requisito separado, ainda não
   auditado.

Estado da implementação em 2026-08-23 (nenhum executado no silício ainda):

| Teste negativo | Como está coberto |
|---|---|
| não atualizar WPTR | `sdma write -nokick` (ABI 5, `BC250DBG_SDMA_FLAG_NO_KICK`). |
| opcode inválido | `sdma write -badop`, header com opcode reservado e sem pacote FENCE. |
| fence com valor errado | `sdma write -badfence`; a fence em endereço arbitrário continua recusada pelo driver, que só aceita o dword reservado do snapshot. |
| destino sem poison | o driver escreve e relê o poison antes de submeter; falha aborta a operação. |
| UMD/KMD incompatível | `Size` e `AbiVersion` conferidos em toda abertura; ABI 5 recusa ferramenta ABI 4. |
| remover firmware | `sdma init`/`sdma write` recusam sem ring e sem F32 fora de HALT; o preflight do ensaio recusa firmware previamente ativo. |
| desabilitar engine | preflight recusa `RB_CNTL` ativo, base ou WPTR residual antes de armar escrita. |

## Riscos conhecidos a bloquear desde o primeiro commit

| Risco | Controle |
|---|---|
| offset de geração/unidade errada | reggen + asserts + quarentena + fill detector. |
| firmware aceito mas engine inativa | gates de transporte, aceitação e atividade separados. |
| PA do host usado como GPUVA | tipos distintos e registry de mappings. |
| fallback CPU | auditoria estática e poison/fence; resultado buffer read-only para CPU durante submit. |
| retorno de diagnóstico perder payload | sucesso de transporte separado de `engine_status`. |
| clock/voltagem perigoso em suite | query-only default; mutação fora da bateria e com thermal rollback. |
| documento velho virar fato | cada evidência possui build/data e relação `supersedes`. |
| 16 GB anunciados com BAR pequeno | segmentos derivados exclusivamente de recursos/topologia detectados. |
| MMIO arbitrário em produção | nenhuma escape genérica no miniport; whitelist no laboratório. |
| tela preta | compute/display separados; manter caminho de recuperação conhecido. |

## Primeira sequência concreta recomendada

1. Congelar esta documentação como baseline.
2. Implementar `reggen` e gerar apenas o conjunto read-only P0/P1.
3. Criar esqueleto do modelo Windows escolhido com PnP e logging, sem bring-up de engines.
4. Integrar memória/endereço tipado.
5. Reproduzir PSP GPCOM como primeiro teste DMA assíncrono.
6. Auditar e estabilizar SDMA com captura de estado no ponto da morte.
7. Só então implementar direct load do CP e ring GFX T2.
8. Após T2 repetível, implementar MQD/HQD compute.

Não iniciar Vulkan, OpenGL, OpenCL ou Direct3D antes do T2 e do gate compute. Isso não reduz ambição; evita construir APIs sobre um caminho físico ainda inexistente.

## Critério de encerramento desta etapa documental

Os documentos atuais são suficientes para começar o esqueleto e o gerador de registros. Ainda não autorizam declarar qualquer engine gráfica/compute/cópia como operacional.

**NÃO VALIDADO EM HARDWARE**

## Gate de memória: plano de VMID 0 derivado da medição de 2026-08-23

O estado de tradução foi medido antes de qualquer escrita
(`docs/evidence/2026-08-23_09-26-23-DESKTOP-HQAG6T4-gcvm-baseline`). O que existe
hoje na placa:

- `CONTEXT0_CNTL.ENABLE_CONTEXT = 0`, sem page table (`PT_BASE/START/END` zerados);
- `MX_L1_TLB_CNTL.SYSTEM_ACCESS_MODE = 0` e `ENABLE_ADVANCED_DRIVER_MODEL = 0`,
  ou seja, TLB L1 em modo físico;
- `L2_CNTL.ENABLE_L2_CACHE = 0`;
- abertura de framebuffer confirmada em `0xF4_0000_0000`, 512 MB.

Ou seja: o caminho que funciona hoje é bypass de tradução deixado pelo POST.

### Ordem obrigatória, e por que ela é essa

1. **Alocar e construir a page table na VRAM, sem habilitar nada.** PDE/PTE
   escritos pela CPU e conferidos por leitura da CPU. Se a estrutura não puder
   ser lida de volta idêntica, o passo falha aqui, com o hardware intacto.
2. **Snapshot transacional** dos onze registradores da tabela de divergências,
   mais `PT_BASE/START/END`, `FAULT_DEFAULT_ADDR`, AGP e `INVALIDATE_ENG0_*`. O
   rollback tem de ser provado antes de a tradução ser ligada, como foi feito
   para o anel da SDMA.
3. **Programar na ordem do upstream**, em estágios separados, cada escrita com
   readback conferido. Os estágios e o que cada um custa:

   | Estágio | Função do upstream | Escritas | Risco |
   |---|---|---:|---|
   | `gart` | `init_gart_aperture_regs` | 6 | inerte com `ENABLE_CONTEXT = 0` |
   | `system` | `init_system_aperture_regs` | 10 | baixo, `SYSTEM_ACCESS_MODE` ainda 0 |
   | `tlb` | `init_tlb_regs` | 1 | **substitui o bypass** |
   | `cache` | `init_cache_regs` | 5 | **substitui a busca física de PDE/PTE** |
   | `context0` | `enable_system_domain` | 1 | **ponto sem volta** |
   | `identity` | `disable_identity_aperture` | 6 | inerte |
   | `invrange` | `program_invalidation`, engine 0 | 2 | inerte, requisito do passo 4 |

   Os três estágios de risco exigem consentimento explícito no CLI (`-danger`) e
   ensaio próprio, depois de os quatro inertes passarem. `setup_vmid_config`
   fica de fora: contextos 1..15 existem para VMs de usuário, e este
   laboratório submete por VMID 0.

   Quatro regras que o kernel impõe, não configuráveis:

   1. exige snapshot de transação aberto — sem caminho de volta provado, nenhuma
      escrita acontece;
   2. todo registrador que um estágio escreve tem de estar na lista do snapshot,
      senão o estágio inteiro é recusado sem escrever nada;
   3. readback conferido por escrita, e divergência aborta o estágio na hora;
   4. os estágios de risco exigem o flag explícito.

   A regra 2 existe porque uma escrita fora da lista da transação produz
   alteração que o rollback não desfaz — e o sintoma aparece como "rollback
   limpo" com hardware alterado.
4. **Invalidar a TLB** pela engine **17** e confirmar o ACK. A engine 0 do plano
   original estava errada: o upstream diz literalmente "Use register 17 for
   GART". O GFXHUB não usa semáforo, e a leitura muda de `REQ` depois da escrita
   é obrigatória em GC < 10.3.0 — sem ela um ACK observado não prova nada.
5. **Primeira submissão traduzida**: um `WRITE_LINEAR` cujo endereço só resolve
   através da page table, com fence física e dado conferido no kernel. Antes
   disso, nenhuma afirmação sobre tradução.

   Como a prova discrimina tradução de abertura de framebuffer: a tabela mapeia
   VA `0x0000_0800_0000_0000` em diante para a VRAM a partir de `0x0FD00000`. O
   pacote carrega `0x0000_0800_0000_1000` e o dado tem de aparecer em
   `0x0FD01000`. **Esse offset não aparece em lugar nenhum do pacote**, e nada
   além da page table liga um endereço ao outro.

   O VA fica acima de 4 GB de propósito. Abaixo disso todo endereço é memória de
   sistema real, e um `WRITE_LINEAR` para lá, com tradução desligada, escreveria
   na RAM do host. Em 8 TB o endereço é inofensivo caso o hardware o trate como
   físico. Isso obrigou o endereço cru da ABI a passar de 32 para 64 bits.

   E um controle indispensável **antes** da submissão traduzida: uma submissão
   para a abertura de framebuffer. Sem ele, falha na prova não distinguiria "não
   traduziu" de "a tradução derrubou o caminho do anel".
6. **Rollback e prova de retorno ao bypass**: a mesma submissão que funcionava
   antes tem de voltar a funcionar depois do rollback.

### O risco que justifica essa ordem

`SYSTEM_ACCESS_MODE = 0` e `VMC_TAP_PDE/PTE_REQUEST_PHYSICAL = 1` são exatamente
o que faz o caminho atual funcionar. Ligar tradução substitui o bypass do qual
todo o sucesso medido depende. E a medição de 2026-08-23 mostrou que engine SDMA
quebrada nesta placa não volta por software: um VMID 0 mal programado custa um
reboot, no melhor caso.

Por isso a regra: **nenhuma escrita em GCVM antes de o passo 1 e o passo 2
estarem provados**, e o passo 5 é o único que pode declarar tradução
funcionando.
