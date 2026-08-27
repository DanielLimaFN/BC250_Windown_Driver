# Arquitetura AMDGPU relevante para a BC-250

## Objetivo

Este documento transforma o fluxo do AMDGPU em uma especificação de decomposição para o novo driver Windows. Ele não propõe copiar código Linux nem código das tentativas anteriores. A referência é o comportamento e a ordem das dependências.

## Identidade arquitetural adotada

- ASIC: Cyan Skillfish / Cyan Skillfish2, PCI alvo observado `1002:13FE`.
- GFX/Mesa: `gfx1013`.
- versão do IP GC usada pelos headers e pelo fluxo analisado: **10.1.3**.
- modelo de registro: SOC15, com bases por IP e segmentos por `BASE_IDX`.
- GMC: v10.
- SDMA: v5, duas instâncias previstas pelo Linux; detectar instâncias realmente presentes/ativas.
- PSP: v11.0.8 na variante analisada.
- display: DCN 2.01 nos materiais do Cyan Skillfish.
- memória: GDDR6 compartilhada; a quantidade fisicamente instalada não equivale automaticamente à VRAM/apertura entregue ao driver.

“RDNA2”, “GFX1013” e “GC 10.1.3” aparecem juntos na documentação comunitária. Para seleção de registros e caminhos de inicialização, a versão do **IP block** tem precedência sobre o rótulo comercial da arquitetura.

## O modelo mental correto

O dispositivo não é um único bloco que recebe uma sequência monolítica de writes. Ele é um conjunto de IP blocks com estado de software e de hardware separados:

```text
PCI/PnP e recursos
        |
        v
descoberta de ASIC, IPs e bases SOC15
        |
        v
early_init de cada IP
        |
        v
sw_init: estruturas, BOs, rings, IRQs, MQD, firmware em memória
        |
        v
COMMON hw_init -> GMC hw_init/GART -> firmware/SMU/PSP
        |
        v
GFX/SDMA hw_init -> ring tests -> schedulers
        |
        v
submissão, fence, interrupções e recuperação
```

Cada seta é um gate. O novo driver não deve continuar para a próxima etapa se a anterior não produziu evidência suficiente.

## Sequência observada no AMDGPU

### 1. Probe e criação do dispositivo

`amdgpu_pci_probe` identifica o PCI device e conduz à criação/carregamento do dispositivo. O equivalente Windows deve:

- aceitar somente IDs explicitamente suportados;
- enumerar recursos traduzidos PnP;
- mapear apenas BARs entregues pelo SO;
- registrar tamanho, cache type e endereço físico sem constantes de fallback;
- não tocar no hardware antes de validar limites e identidade.

### 2. Descoberta e `early_init`

`amdgpu_device_ip_early_init` seleciona os IP blocks, executa `early_init`, obtém informações de BIOS/GPU e marca cada bloco como válido ou inválido.

Na BC-250, as bases rígidas do Cyan Skillfish substituem parte do IP discovery convencional. A lição para Windows é manter uma tabela de descrição do ASIC, não espalhar offsets finais pelo código:

```c
struct bc250_ip_layout {
    enum bc250_ip ip;
    uint32_t segment_dw[3];
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_rev;
};
```

A tabela é selecionada pela identidade detectada e auditada contra headers upstream. Ela não contém “registros confirmados” por coincidência de leitura.

### 3. `sw_init`

`amdgpu_device_ip_init` percorre os blocos válidos e chama `sw_init` antes do `hw_init`. Para GFX10.1.3, `gfx_v10_0_sw_init` configura, entre outras coisas:

- topologia lógica de ME/MEC, pipes e queues;
- fontes de interrupção CP;
- estruturas RLC;
- BOs MEC;
- ring GFX;
- rings compute;
- KIQ e MQD/HQD associados.

Para SDMA, `sdma_v5_0_sw_init` prepara instâncias, rings, IRQs e firmware antes de qualquer unhalt.

No novo driver, `sw_init` precisa ser reversível e não pode fazer MMIO destrutivo. Tudo que for alocado deve possuir owner, tamanho, GPU address, CPU mapping, cache policy e rotina de unwind.

### 4. Ordem especial de `hw_init`

O AMDGPU inicializa COMMON cedo e GMC cedo, porque o restante depende de memória GPU endereçável. A ordem não é cosmética:

1. COMMON torna o acesso básico ao ASIC coerente.
2. GMC cria memória scratch/writeback e habilita GART/GPUVM.
3. GFXHUB e MMHUB recebem tabelas e aperturas.
4. HDP é inicializado e flushed.
5. TLBs são invalidadas.
6. Firmware/SMU/PSP ficam disponíveis conforme o método da variante.
7. GFX e SDMA programam SRAM/IC base, rings e filas.
8. Cada ring executa um teste físico antes de entrar no scheduler.

As tentativas anteriores frequentemente programaram rings antes de existir uma tradução de endereço demonstrada, ou usaram endereço físico de host como GPU address. O driver novo deve impedir isso por tipo: um `CPU_PA` nunca pode ser aceito onde se exige `GPU_VA` sem um mapeamento registrado.

## Firmware

### Seleção do método

O método é propriedade do ASIC/IP/firmware, não uma preferência global:

- ring GPCOM PSP é um transporte válido e já demonstrado para comandos suportados;
- `LOAD_IP_FW` via PSP pode retornar `ITEM_NOT_FOUND` ou `UNKNOWN_COMMAND` sem indicar erro no framing;
- direct load de GFX/SDMA exige o fluxo específico do upstream: halt da engine, parse do header, localização de ucode/jump table, escrita em SRAM ou programação de IC base, versão e unhalt controlado;
- “mailbox aceitou” e “microcódigo executa” são gates distintos.

### Evidência mínima por firmware

Registrar por blob:

- nome lógico e SHA-256;
- origem/licença;
- versão do header e tamanho;
- engine e instância;
- método (`PSP_RING`, `PSP_DIRECT`, `SRAM_DIRECT`, `IC_BASE`);
- status cru do transporte;
- versão/status lido da engine após carga;
- ring test da engine.

Sem os três últimos itens, o firmware está “submetido” ou “aceito”, não “ativo”.

## GMC, GART e GPUVM

O upstream `gmc_v10_0_gart_enable` exige um objeto GART válido, habilita GFXHUB e MMHUB, inicializa HDP, faz flush e invalida TLB. O novo driver precisa separar:

- **VRAM física compartilhada**: propriedade do hardware/plataforma;
- **apertura CPU visível**: recurso BAR entregue pelo PnP;
- **GTT**: memória de sistema mapeada para a GPU;
- **GPUVA**: endereço consumido por engines;
- **PA do host**: endereço físico de uma alocação do Windows;
- **segmentos VidMm**: contrato exposto ao dxgkrnl.

Nenhuma conversão entre esses domínios deve ser implícita.

Primeiro gate de memória: a GPU escreve uma fence em uma página cujo GPUVA foi criado pela MMU e a CPU observa o valor. Depois vêm page-table switch, múltiplos VMIDs e paging buffers.

## GFX ring, KIQ, MQD e HQD

O ring GFX e as filas compute são problemas relacionados, mas diferentes.

### Ring GFX

O upstream configura base, control, RPTR writeback, WPTR/doorbell e então executa pacotes de inicialização de contexto. Para validar um ring mínimo:

1. ring em memória GPU endereçável;
2. RPTR writeback separado e envenenado;
3. fence separado e envenenado;
4. pacote PM4 mínimo que escreva valor B em scratch/memória onde a CPU escreveu A;
5. kick do WPTR na ordem exigida;
6. WPTR avança;
7. RPTR alcança WPTR;
8. fence da GPU chega;
9. A vira B;
10. CP/GRBM mostra atividade e não registra fault.

### KIQ/compute

`gfx_v10_0_kiq_init_register` mostra que uma fila compute não nasce de escrever apenas `COMPUTE_DISPATCH_INITIATOR`. Ela envolve:

- MQD completo em memória GPU;
- EOP buffer;
- base/control do HQD;
- RPTR report e WPTR poll;
- doorbell;
- VMID e prioridade;
- estado persistente/IB control;
- ativação da HQD;
- PM4 privilegiado para mapear filas conforme o caminho escolhido.

Esse é um dos vazios centrais de todas as tentativas. O novo driver não deve expor `CreateContext` compute antes de uma fila mínima passar pelo gate físico.

## SDMA

O upstream `sdma_v5_0_start` faz:

1. halt;
2. direct load do microcódigo quando selecionado;
3. unhalt;
4. habilita context switch;
5. programa cada ring;
6. executa ring test.

Os dados V3/dbg acrescentam uma pista: a engine pode drenar o ring por poucas submissões, escrever algumas fences e depois entrar em falha latchada. Portanto o teste novo deve capturar, antes e depois de cada submissão:

- `SDMA5_STATUS_REG`, `STATUS1`, `STATUS2`, `STATUS3`;
- `GFX_CONTEXT_STATUS` e `GFX_STATUS`;
- `UTCL1_RD_STATUS` e `UTCL1_WR_STATUS`;
- RPTR de registro e writeback;
- WPTR de registro e shadow;
- fault status da GPUVM;
- fence e comparação integral do destino.

O teste `CONST_FILL` deve rodar primeiro após init, antes de COPY, para separar falha de escrita da falha de leitura da origem.

## SMU e GFXOFF

Consultas e controle de energia são pré-requisitos de rings, não recursos cosméticos.

O novo driver deve começar apenas com consultas whitelisted. Comandos de mutação entram um por vez, com:

- precondições de DPM/PPTable;
- faixa de tensão/frequência validada;
- mutex único do mailbox;
- timeout e status cru;
- readback independente;
- thermal trip e rollback;
- desabilitados por padrão no miniport de produção.

Desativar GFXOFF exige provar que o estado físico mudou durante a carga. Resposta `1` do mailbox prova aceitação da mensagem, não o estado final.

## Display e escolha WDDM/MCDM

O KMDOD foi útil para manter o desktop e instrumentar o hardware, mas não fornece o caminho de renderização. Há duas superfícies possíveis:

- **MCDM/compute-first**: menor superfície, sem display, boa para provar GPUVA, filas e scheduler;
- **full WDDM render/display**: necessária para integração gráfica completa, VidMm/VidSch e display real.

A decisão deve ser tomada depois de confirmar no WDK alvo qual modelo é suportado para a versão do Windows e quais DDIs são obrigatórias. Em ambos os casos, o laboratório `dbg` deve continuar separado. KMDOD pode permanecer como caminho de recuperação/display enquanto o motor compute é desenvolvido, mas não deve anunciar renderização.

## Mapeamento conceitual Linux -> Windows

| AMDGPU | Responsabilidade no novo driver |
|---|---|
| `amdgpu_pci_probe` | PnP/AddDevice/StartDevice, recursos e identidade. |
| `amdgpu_discovery_set_ip_blocks` + base init | descriptor imutável do ASIC e gerador de offsets. |
| `early_init` | seleção de versões/capacidades sem ativação destrutiva. |
| `sw_init` | objetos, rings, firmware parseado, IRQs, MQD e unwind. |
| GMC `hw_init` | segmentos, page tables, hubs, HDP e TLB. |
| PSP/SMU init | transporte seguro, DPM e estado de energia. |
| GFX/SDMA `hw_init` | firmware específico, rings, MQD/HQD e ring tests. |
| DRM scheduler | VidSch/DDIs de submit, preempção, timeout e reset. |
| BO/GEM/TTM | VidMm, alocações, residency, paging e mapeamentos. |
| Mesa/RADV | futuro UMD/ICD, somente após contrato KMD físico estável. |

## O que não deve ser portado como arquitetura

- IOCTL de escrita MMIO arbitrária no miniport de produção;
- constantes de 16 GB/24 CU/1500 MHz sem detecção;
- firmware embutido sem manifest/hash/versão;
- inicialização monolítica que continua depois de erro;
- fallback CPU dentro de submit;
- fence sinalizada pelo host;
- retorno de sucesso em stubs de API;
- offsets finais copiados de documentos históricos;
- uso de `MmGetPhysicalAddress` como GPUVA sem GART/GPUVM demonstrado.
