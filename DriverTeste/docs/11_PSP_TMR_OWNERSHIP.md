# PSP TMR — modelo de ownership e gates de reabilitação

Atualizado em 2026-08-24. Este documento é um desenho offline; não autoriza
escrita no hardware.

Veredito: **NÃO VALIDADO EM HARDWARE**.

## Fatos já medidos

- Linux detectou 512 MiB de VRAM:
  `0xF400000000..0xF41FFFFFFF`.
- O amdgpu reservou 4 MiB para PSP TMR em `0xF41F800000`, isto é, offset
  `0x1F800000` dentro dessa VRAM.
- O laboratório Windows mapeia somente 256 MiB do BAR0 e ainda usa
  `BC250_BAR0_DEFAULT_LEN=0x10000000`. A captura PnP/CIM readonly das 18:44
  confirmou uma janela disjunta de exatamente 256 MiB; ele não possui nem
  consegue conferir a região Linux no offset `0x1F800000`.
- O preflight Windows das 18:27 encontrou TOS pronto, sem ring do laboratório,
  e não produziu evidência de uma TMR pertencente ao laboratório.

## Semântica upstream que deve ser preservada

O amdgpu cria um BO com domínio `GTT | VRAM`, alinhamento próprio de TMR e o
mantém reservado até o teardown. A mesma alocação produz:

1. tamanho real por `amdgpu_bo_size`;
2. endereço MC/GPU do BO para `buf_phy_addr`;
3. endereço físico de VRAM por `amdgpu_gmc_vram_pa` para
   `system_phy_addr`.

O endereço MC não é intercambiável com `MmGetPhysicalAddress` de RAM do host.
Depois de `SETUP_TMR`, o firmware de entrada também é fornecido ao PSP por seu
endereço MC, e MEC e MEC_JT1 são itens separados.

## Por que o laboratório atual não pode implementar isso ainda

O driver `bc250dbg` é legacy, não um miniport PnP proprietário da GPU. Ele lê
os BARs da configuração PCI e mapeia uma janela presumida; não recebe uma lista
de recursos com tamanho, não possui gerenciador de BO/VRAM e não coordena uma
reserva com o Basic Display Adapter. Escolher um intervalo “livre” por inspeção
de conteúdo não estabelece ownership: outro agente pode usá-lo depois.

Uma alocação contígua do host também não resolve sozinha. Ela fornece PA de CPU,
mas não fornece um endereço MC/GPU válido sem uma tradução GART pertencente ao
driver e mantida durante toda a vida da TMR.

## Caminho Windows autorizado

Para um display miniport, o ponto de entrada correto é `DxgkDdiStartDevice`:

1. salvar `DXGKRNL_INTERFACE` e seu `DeviceHandle`;
2. chamar `DxgkCbGetDeviceInformation` para obter a lista de recursos
   traduzidos do adapter;
3. classificar todos os descritores sem constantes de tamanho;
4. mapear apenas recursos atribuídos usando `DxgkCbMapMemory`;
5. desfazer mapeamentos e reservas em toda falha parcial e em
   `DxgkDdiStopDevice`.

As referências WDDM antigas desta árvore não podem ser copiadas nesse ponto.
Elas examinam somente `TranslatedResourceList->List[0]`, classificam qualquer
descritor `>= 256 MiB` como framebuffer e usam `MmMapIoSpaceEx`. Na captura
atual, o descritor agregado tem `0x10200000` bytes e contém a janela de 2 MiB;
essa heurística mapearia além do segmento disjunto de 256 MiB.

Mesmo o caminho correto de Dxgkrnl concede ownership do recurso PCI, não uma
subalocação TMR automaticamente. O miniport ainda precisa de um gerenciador de
VRAM com reservas não sobrepostas e lifecycle explícito.

O classificador independente de sistema já existe em
`driver/core/bc250_resource_contract.{h,c}`. Ele não toca no dispositivo e não
substitui `DxgkCbGetDeviceInformation`; serve como núcleo testável para cruzar
os descritores normalizados com os BARs por índice/base. A adaptação WDK já foi
implementada em `driver/wdk/bc250_cm_resource_adapter.{h,c}`: ela percorre todas
as full lists, normaliza `CmResourceTypeMemory` e `MemoryLarge` e só então chama
o contrato. Ela está compilada e testada somente offline; a consulta Dxgkrnl, o
mapeamento dos resultados e o teardown continuam pendentes. O percurso foi
exercitado em host com as estruturas reais do WDK, não com uma lista fornecida
por Dxgkrnl.

O gerenciador de subintervalos também existe em
`driver/core/bc250_vram_allocator.{h,c}`. Ele oferece reservas fixas,
persistência, tokens com geração e busca alinhada top-down/bottom-up. Seu estado
continua abstrato: `Offset` não é PA nem MC. Essa separação impede que a camada
PSP volte a derivar endereços apenas somando uma constante.

A camada seguinte agora existe em `driver/core/bc250_address_contract.{h,c}`.
Ela não recebe endereços escolhidos pelo chamador: recebe token e snapshot
runtime, valida a topologia inteira e produz MC/PA a partir do mesmo offset. O
serializador `driver/core/bc250_psp_contract.{h,c}` só aceita essa origem para
montar `SETUP_TMR` e `LOAD_IP_FW`. Ambos passam em testes sintéticos e compilam
como objetos WDK, mas ainda não estão ligados a uma reserva real nem ao ring.
`LOAD_IP_FW.fw_size` é serializado em bytes; o `DwordCount` da ABI antiga era
convertido para bytes antes do comando e não é causa do `0x80000203`.

O lifecycle offline foi fechado em
`driver/core/bc250_compute_bootstrap.{h,c}`. A API abre um inventário para
reservas herdadas, sela esse inventário junto de um snapshot runtime e só então
monta, de forma atômica, uma reserva TMR persistente e um staging de firmware
persistente e CPU-visible. Mudança no alocador depois do selo reprova o
preflight. Falha de espaço ou de payload trabalha numa cópia e não deixa uma
TMR parcial reservada.

O manifesto CP aceito tem exatamente cinco entradas, uma vez cada, na ordem
observada na inicialização upstream de Cyan Skillfish:
`PFP -> ME -> CE -> MEC -> MEC_JT1`. Os tamanhos vêm do firmware em runtime e
são bytes; MEC e sua jump table não podem ser fundidos. Um único staging do
tamanho da maior entrada é reutilizado, como o BO primário do caminho PSP.
O relatório contém tokens, geração do snapshot, planos tipados e os seis
payloads, mas a camada não possui função de submit nem recebe endereços brutos.

Teste host reproduzível (`/W4 /WX /Brepro`):
`A917F7DFF1A84102A3DFE7C94984C5F8F2C500E2A8C583F198ADAF5D671EFACC`.
Objeto WDK reproduzível:
`FD8E3B2A72FBD77D90E95F30FA34221BEF786B234698BE8A12A326F30B45F017`.

A fronteira anterior ao bootstrap agora existe em
`driver/core/bc250_owned_adapter_contract.{h,c}`. Ela combina a lista de BARs
já resolvida, a topologia runtime e as reservas herdadas, mas só sela o
bootstrap quando sete origens estão explicitamente presentes: recursos
traduzidos, identidade PCI do BAR, total de VRAM runtime, faixa MC runtime,
`vram_base_offset` runtime, ownership exclusivo do adapter e inventário
completo. A abertura CPU vem do comprimento do BAR resolvido; ela nunca é
promovida ao total de VRAM.

Esses flags são uma exigência da API, não prova autocriada pelo contrato. O
futuro `StartDevice` terá de obtê-los de operações reais da stack e manter o
ownership. O `bc250dbg` legacy e a captura CIM atual não possuem as duas últimas
provas, portanto não podem chamar esse caminho como proprietário.

Teste host reproduzível:
`6DE6F6205944EECBACC0C54938A270158B8452846D2C51FB24A2EC74B82D6E17`.
Objeto WDK reproduzível:
`38F8780B96042977B931123D4F480EECF33F6BDF020F7BF1891618E14591128A`.

O adaptador WDK `driver/wdk/bc250_start_device_adapter.{h,c}` fecha a ponte
entre uma `CM_RESOURCE_LIST` e esses contratos. Ele percorre a lista real pelo
adaptador CM, resolve os BARs, acrescenta as duas evidências que essa operação
de fato estabelece (recursos traduzidos e identidade PCI), exige separadamente
as cinco evidências runtime restantes e constrói o preflight readonly.

O lifecycle é explícito: `Initialize -> OwnedSealed -> PreflightReady ->
Teardown`. Um segundo start é recusado; manifesto inválido zera somente o
relatório e preserva o ownership para retry; teardown apaga BARs, tokens,
payloads e selos. Esse adaptador não chama `DxgkCbMapMemory`; a DDI fina que o
envolve é descrita abaixo.

Objeto WDK reproduzível:
`6B116C9B477D6D8EF81F6C3DB7488D3FB8060F7A3675803886E347A5C1DAEF94`.
Executável de teste WDK reproduzível:
`999454280A9E170966BD665B583F95F527E216F26F161120342C78EBF270BE75`.

O esqueleto de DDI real está em
`driver/wdk/bc250_dxgk_readonly_ddi.{h,c}`. As funções são compatíveis com
`PDXGKDDI_START_DEVICE`/`PDXGKDDI_STOP_DEVICE`, validam `Size` antes de ler o
callback, copiam somente os bytes fornecidos da `DXGKRNL_INTERFACE` e chamam
`DxgkCbGetDeviceInformation`. A ausência das cinco fontes runtime termina em
`STATUS_NOT_SUPPORTED` depois da consulta de recursos.

Mesmo com todas as entradas sintéticas e o preflight aprovado, StartDevice
retorna intencionalmente `STATUS_NOT_SUPPORTED`, anuncia zero sources/children
e não chama `DxgkCbMapMemory`. Assim, esse código não pode registrar um adapter
incompleto como operacional. O teste usa callback falso; a DDI não foi
registrada, instalada nem executada por Dxgkrnl nesta máquina.

Objeto WDK reproduzível:
`61866E40A0430501DCB847D4DC2A03F727B472A08AB5F6B1F64E16D4277EF41C`.
Teste WDK reproduzível:
`88884007A894FB042A0B5087BBE0D58B1588D79165D422FE27EBFB9ED8211991`.

As bases PCI deixaram de ser entrada manual da DDI.
`driver/wdk/bc250_pci_bar_reader.{h,c}` lê uma única vez os 64 bytes do header
PCI por `DxgkCbReadDeviceSpace(DXGK_WHICHSPACE_CONFIG)`, confirma
`1002:13FE` e decodifica BARs de memória 32/64 bits. O comprimento não é medido
por escrita de `0xFFFFFFFF`: continua vindo exclusivamente da lista traduzida.

Retorno parcial, fill `0xFFFFFFFF`, tipo de memoria reservado, base zero e par
64-bit truncado zeram toda a saída. BARs de I/O VGA sao ignoradas porque o
contrato inventaria somente memoria, preservando os indices das demais BARs. A
DDI só marca identidade PCI depois dessa leitura e do cruzamento com os
recursos PnP.

Objeto WDK:
`3D6BD9FDB0E995ABA408C701C91F0126AF3F3262D53FACC83340117991AFAC63`.
Teste WDK:
`E2127E3A8CB11D3CA7D587D6C0C132438485B624B6528736F2CC22AC0990A1B4`.

`driver/wdk/bc250_rebar_reader.{h,c}` percorre a cadeia de extended
capabilities com limite e detecção de loop, localiza capability `0x0015` e
registra três fatos separados para o BAR da abertura: máscara de tamanhos
suportados, encoding selecionado e comprimento atribuído pelo PnP. A contagem
de entradas é direta, como no walker upstream do Linux; o encoding `N`
representa `2^(20+N)` bytes.

O gate exige que o tamanho selecionado esteja na máscara suportada e seja
idêntico ao comprimento resolvido. Isso prova coerência da abertura, não VRAM
total. Nenhum controle ReBAR é escrito.

Objeto WDK:
`FF5C9CA99F40793AF2686992E027DEF32E13BAA2CA393BDF77A5368774F5EBE5`.
Teste WDK:
`5793567BE34FDE2C21C12ED58F7C6F961DDCBFF45CF0F6B629358CA94A0EDCD3`.

`driver/wdk/bc250_readonly_miniport.{h,c}` acrescenta `DriverEntry`,
AddDevice e RemoveDevice e materializa uma `DRIVER_INITIALIZATION_DATA` com
somente as quatro DDIs de lifecycle. Submit, interrupt, query-adapter e todas
as DDIs de engine permanecem nulas. AddDevice aloca o contexto nonpaged;
RemoveDevice executa teardown e libera a extensão.

O ponto decisivo é que `DriverEntry` **não chama `DxgkInitialize`** e sempre
retorna `STATUS_NOT_SUPPORTED`. O `.sys` serve para validar ABI e linkage, não
para instalação. Sua import table contém somente `ExAllocatePool2` e
`ExFreePoolWithTag` de `ntoskrnl.exe`; não importa DispLib, mapeamento, PCI ou
rotina de escrita.

Objeto WDK:
`28BECE7F45742E43A24273B30FF6CB8E579DEBAD035546C48B84E9EBDE59F9A5`.
Teste WDK:
`D7AE4DA768A42EAC5FC9B258C497F4F220D2F2A236C5F0451B611D1ADA590C98`.
Imagem `bc250readonly.sys`:
`FA4F5B6137198B73ABE670F582031569FE30E81FD21F9AE009E88FA4F902708F`.

## Gates para retirar a quarentena

- [x] Detectar em runtime a abertura CPU atribuída sem escrever na configuração
  de um dispositivo vivo: 256 MiB nesta sessão, por PnP/CIM.
- [ ] Obter VRAM total e configuração ReBAR por uma fonte Windows proprietária
  e autoritativa; a captura Linux de 512 MiB é comparação, não estado Windows.
- [ ] Tornar o driver proprietário dos recursos por uma stack PnP/miniport ou
  obter uma reserva exclusiva por mecanismo equivalente e comprovável.
- [x] Implementar e testar offline o núcleo do alocador com intervalos,
  alinhamento, tokens e detecção de sobreposição.
- [x] Implementar e testar offline o vínculo token → MC/PA e os payloads PSP sem
  entrada de endereço bruto.
- [ ] Integrar o alocador ao lifecycle real do miniport e registrar scanout, rings,
  page tables, firmware e reservas de plataforma realmente herdadas.
- [x] Implementar offline a fronteira fail-closed entre BAR resolvido,
  topologia runtime, ownership explícito e bootstrap selado.
- [x] Implementar o adaptador WDK offline de `CM_RESOURCE_LIST` até preflight
  readonly, com máquina de estados, retry e teardown.
- [x] Conectar o adaptador a um esqueleto real e fail-closed de
  `DxgkDdiStartDevice`, compilado e testado por callback falso.
- [ ] Registrar a DDI num miniport offline completo e obter as cinco evidências
  runtime de fontes proprietárias; até lá StartDevice sempre falha fechado.
- [x] Ligar um `.sys` nativo reproduzível com DriverEntry/Add/Remove e tabela
  mínima, sem registrar no Dxgkrnl e sem INF.
- [x] Descobrir identidade e bases dos BARs por callback PCI readonly, sem
  sizing write, e usar a lista PnP como única fonte de comprimentos.
- [x] Validar ReBAR suportado/selecionado/atribuído sem escrever capability e
  sem tratar abertura como VRAM total.
- [ ] Derivar PA e MC/GPUVA da mesma alocação, sem constante histórica.
- [ ] Manter a reserva viva até `DESTROY_TMR` confirmado; em timeout, preservar
  a memória para impedir DMA use-after-free.
- [ ] Implementar rollback/reboot explícito para firmware PSP persistente.
- [x] Separar MEC de MEC_JT1 e usar endereço MC derivado do token para o staging
  no contrato offline.
- [x] Criar preflight offline que reporte todos os endereços, tamanhos,
  geração e interseções antes de qualquer transporte.
- [ ] Alimentar esse preflight com recursos e snapshots reais do miniport e
  conferir o relatório readonly no hardware antes de permitir escrita.

Até todos esses gates existirem, permanecem em quarentena fail-closed:
`SETUP_TMR`, `LOAD_IP_FW` dos tipos CP 1..5 e o `gfx bringup` legado.

**NÃO VALIDADO EM HARDWARE**.
