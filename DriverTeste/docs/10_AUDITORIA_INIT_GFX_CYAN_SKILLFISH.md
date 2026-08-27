# Auditoria da inicialização GFX — Cyan Skillfish

Atualizado em: 2026-08-24, após a evidência ABI 27 das 16:28.

Veredito global: **NÃO VALIDADO EM HARDWARE**.

## 1. Motivo e limite desta auditoria

O pulso real de reset das pipes foi confirmado por readback e não alterou
`JT_LOADED=0` nem os PCs constantes em `0x041E`. Antes de qualquer nova escrita,
esta auditoria compara o laboratório com a sequência local do amdgpu em
`referencias/BC250-windowsDriverTest/docs/upstream/gfx_v10_0.c`.

Este arquivo é deliberadamente estático. Ele não autoriza ensaio físico e não
transforma valores do Linux em valores seguros para Windows sem snapshot,
rollback e confirmação de endereço.

## 2. Achados que mudam o próximo passo

1. `BC250DBG_CP_MODE_GOLDEN_INIT` não implementava a tabela golden Cyan
   Skillfish. Ele misturava 16 valores steady-state do sweep, dos quais apenas
   `GRBM_GFX_INDEX`, `GE_FAST_CLKS`, `CGTT_CPF_CLK_CTRL` e `CP_SD_CNTL` também
   aparecem na tabela upstream de 34 entradas.
2. A tabela upstream usa `SOC15_REG_GOLDEN_VALUE`: leitura, aplicação de máscara
   e escrita. O laboratório escrevia valores integrais, portanto não preservava
   os bits fora da máscara.
3. O laboratório escrevia `CPG_PSP_DEBUG` e `CPC_PSP_DEBUG` como efeito lateral
   do comando “golden”, embora esses registros não estivessem na lista de 30
   registros da transação do CP.
4. A ordem upstream é golden → cleaner shader → SMU/GPA no direct load → CAM →
   constants → RLC resume → CP resume. Os ensaios atuais montam partes dessa
   cadeia em outra ordem.
5. Dentro do direct load gráfico, a ordem upstream é **PFP → CE → ME**. Os
   scripts atuais usam PFP → ME → CE.
6. O header de offsets sozinho não resolve o endereço BAR: o macro SOC15 soma a
   base fornecida por `cyan_skillfish_ip_offset.h`. A fonte do kernel confirma
   `GC_BASE[0] = { 0x1260, 0xA000 }`, em dwords. A fórmula registrada no próprio
   sweep é `byte = (base_do_segmento + mmREG) * 4`. Os 34 endereços derivados
   abaixo existem no sweep Linux e ficaram estáveis nas duas passagens.

Como proteção imediata, `cp golden apply` foi colocado em quarentena fail-closed:
o driver devolve `BC250DBG_OPS_BADOP` sem emitir escrita. A ABI 29 acrescenta
`cp golden preflight`, que lê duas vezes os 34 registros e os dois GPA sem
escrever. O auditor estático `tools/audit-cp-golden-static.ps1` confere nomes,
ordem, máscaras, valores e endereços contra o upstream.

A tentativa de desenhar uma transação única encontrou outra fronteira: o
primeiro golden escreve `GRBM_GFX_INDEX=0xE0000000`, habilitando broadcast. Um
snapshot único por offset não preserva valores possivelmente distintos por
SE/SH/instância. Aplicar e depois restaurar esse único valor em broadcast seria
um rollback fabricado. Por isso os quatro harnesses de escrita do CP foram
bloqueados na entrada até existir enumeração de topologia e snapshot individual.

## 3. Ordem upstream que deve orientar o desenho

| Ordem | Função / fase | Situação no laboratório |
|---:|---|---|
| 1 | `gfx_v10_0_init_golden_registers()` | incompleta e semanticamente diferente; em quarentena |
| 2 | `amdgpu_gfx_cleaner_shader_init()` | ainda não auditada; não presumir necessária nem dispensável |
| 3 | carregar SMU e `gfx_v10_0_disable_gpa_mode()` no direct load | GPA já foi tocado, mas estava acoplado ao comando errado |
| 4 | conferir/configurar GRBM CAM remapping | não auditado |
| 5 | `gfx_v10_0_constants_init()` | apenas valores isolados do sweep foram copiados |
| 6 | `gfx_v10_0_rlc_resume()` | existe ensaio com prova de saída da ROM |
| 7 | `gfx_v10_0_cp_resume()` | carga direta e unhalt incompletos em relação à ordem upstream |
| 7.1 | direct load gráfico PFP → CE → ME | scripts usam PFP → ME → CE |
| 7.2 | direct load compute/MEC | staging existe; latch/execução não provados |
| 7.3 | KIQ/KCQ e ring gráfico | bloqueados até latch e movimento real do CP |

## 4. Tabela golden autoritativa do upstream local

Os campos `mm` e `BASE_IDX` são identificadores SOC15. As bases `0x1260` e
`0xA000` foram cruzadas com a
[tabela Cyan Skillfish do kernel](https://codebrowser.dev/linux/linux/drivers/gpu/drm/amd/include/cyan_skillfish_ip_offset.h.html#67)
e com a convenção gravada no JSON do sweep. `Offset BAR` é o endereço em bytes.

| Registro | `BASE_IDX` | Offset BAR | Máscara de substituição | Valor OR |
|---|---:|---:|---:|---:|
| `GRBM_GFX_INDEX` | 1 | `0x30800` | `0xFFFFFFFF` | `0xE0000000` |
| `GE_FAST_CLKS` | 0 | `0x08920` | `0x3FFFFFFF` | `0x0000493E` |
| `CGTT_CPF_CLK_CTRL` | 1 | `0x3C2C4` | `0xFCFF8FFF` | `0xF8000100` |
| `CGTT_SPI_CLK_CTRL` | 1 | `0x3C200` | `0xFF7F0FFF` | `0x3C000100` |
| `CB_HW_CONTROL_3` | 0 | `0x09A0C` | `0xA0000000` | `0xA0000000` |
| `CB_HW_CONTROL_4` | 0 | `0x09A08` | `0x00008000` | `0x003C8014` |
| `CH_DRAM_BURST_CTRL` | 1 | `0x33610` | `0x00000010` | `0x00000017` |
| `CH_PIPE_STEER` | 1 | `0x33640` | `0xFFFFFFFF` | `0xD8D8D8D8` |
| `CH_VC5_ENABLE` | 1 | `0x33650` | `0x00000003` | `0x00000003` |
| `CP_SD_CNTL` | 0 | `0x0C6DC` | `0x800007FF` | `0x000005FF` |
| `DB_DEBUG` | 0 | `0x09830` | `0xFFFFFFFF` | `0x20000000` |
| `DB_DEBUG3` | 0 | `0x09838` | `0xFFFFFFFF` | `0x00000200` |
| `DB_DEBUG4` | 0 | `0x0983C` | `0xFFFFFFFF` | `0x04800000` |
| `DB_LAST_OF_BURST_CONFIG` | 0 | `0x09868` | `0xFFFFFFFF` | `0x03860210` |
| `GB_ADDR_CONFIG` | 0 | `0x098F8` | `0x0C1800FF` | `0x00000044` |
| `GCR_GENERAL_CNTL` | 0 | `0x09F8C` | `0x00009D00` | `0x00008500` |
| `GCMC_VM_CACHEABLE_DRAM_ADDRESS_END` | 0 | `0x0A5C8` | `0xFFFFFFFF` | `0x000FFFFF` |
| `GL1_DRAM_BURST_CTRL` | 1 | `0x33410` | `0x00000010` | `0x00000017` |
| `GL1_PIPE_STEER` | 1 | `0x33440` | `0xFCFCFCFC` | `0xD8D8D8D8` |
| `GL2_PIPE_STEER_0` | 1 | `0x33894` | `0x77707770` | `0x21302130` |
| `GL2_PIPE_STEER_1` | 1 | `0x33898` | `0x77707770` | `0x21302130` |
| `GL2A_ADDR_MATCH_MASK` | 1 | `0x33884` | `0xFFFFFFFF` | `0xFFFFFFCF` |
| `GL2C_ADDR_MATCH_MASK` | 1 | `0x3380C` | `0xFFFFFFFF` | `0xFFFFFFCF` |
| `GL2C_CGTT_SCLK_CTRL` | 1 | `0x3C2B0` | `0x10000000` | `0x10000100` |
| `GL2C_CTRL2` | 1 | `0x33804` | `0xFC02002F` | `0x9402002F` |
| `GL2C_CTRL3` | 1 | `0x33830` | `0x00002188` | `0x00000188` |
| `PA_SC_ENHANCE` | 0 | `0x08BF0` | `0x08000009` | `0x08000009` |
| `PA_SC_BINNER_EVENT_CNTL_0` | 0 | `0x08B30` | `0xCC3FCC03` | `0x842A4C02` |
| `PA_SC_LINE_STIPPLE_STATE` | 1 | `0x30A04` | `0x0000000F` | `0x00000000` |
| `RMI_SPARE` | 0 | `0x09E7C` | `0xFFFF3109` | `0xFFFF3101` |
| `SQ_ARB_CONFIG` | 0 | `0x08C30` | `0x00000100` | `0x00000130` |
| `SQ_LDS_CLK_CTRL` | 1 | `0x3C240` | `0xFFFFFFFF` | `0xFFFFFFFF` |
| `TA_CNTL_AUX` | 0 | `0x09508` | `0x00030008` | `0x01030000` |
| `UTCL1_CTRL` | 0 | `0x09FA0` | `0x00800000` | `0x00800000` |

Semântica exigida para cada linha:

```text
novo = (valor_lido & ~mascara) | (valor_OR & mascara)
```

Essa é a implementação literal de `soc15_program_register_sequence()`. Quando
`mascara == 0xFFFFFFFF`, o upstream usa o atalho equivalente
`novo = valor_OR`. A máscara marca os bits que serão substituídos; ela não marca
os bits que sobrevivem. A fórmula anterior neste documento estava invertida e
foi revogada antes de qualquer `apply` físico.

Projetando os 34 RMWs sobre a evidência Windows ABI 29, 30 valores mudariam e 4
já estão no valor projetado. Dos 31 offsets presentes no sweep Linux, 30
coincidem exatamente com a projeção. A exceção é `RMI_SPARE`, projetado como
`0xFFFF3101` e medido no Linux como `0xFFFF3100`; o bit 0 pode ter mudado depois
do init, mas isso ainda não está provado e permanece classificado separadamente.

## 5. Gates antes de retirar a quarentena

- [x] derivar os 34 endereços e cruzá-los automaticamente com o upstream;
- [x] implementar preflight Windows readonly de 36 registros, com duas leituras
  e exceção explícita para `SQ_LDS_CLK_CTRL=0xFFFFFFFF`;
- [x] criar `tools/test-cp-golden-preflight.ps1`, com hash/ABI, armamento
  readonly, contagens de leitura/escrita/falha, bugcheck e evidência JSON/TXT;
- [x] executar e arquivar o preflight em ABI 29: 36 valores, zero escritas,
  sem bugcheck; recusado com 4 leituras `0xFFFFFFFF`;
- [x] classificar o resultado sem fabricar sucesso: 32 golden confirmados,
  GL2A/GL2C ambíguos e CPG/CPC GPA indisponíveis;
- [x] automatizar o cruzamento da evidência Windows com o sweep Linux em
  `tools/audit-cp-golden-preflight-evidence.ps1`;
- [x] SE/SH medidos no Linux: 2 SE, 2 SH/SE, 10 CU/SH, 24 CUs ativos;
- [ ] a quantidade de instâncias GRBM continua ausente e o snapshot ainda não existe;
- classificar leitura normal, write-only, auto-clear e registrador indexado;
- ampliar a transação para toda escrita, inclusive GPA e seleção broadcast;
- implementar RMW mascarado e conferir apenas bits que admitem readback;
- garantir restauração de `GRBM_GFX_INDEX` mesmo em saída antecipada;
- separar golden, constants, GPA/CAM, RLC e CP em gates distintos;
- manter engines haltadas durante configuração;
- após unhalt, exigir `JT_LOADED=3` e variação coerente dos PCs;
- somente então submeter PM4 e exigir RPTR/WPTR, fence física, resultado correto,
  status e rollback sem divergência.

Até todos esses itens existirem, nenhum resultado desse caminho pode ser chamado
de execução do CP.

**NÃO VALIDADO EM HARDWARE.**

## 6. Topologia: IP Discovery, fallback `gpu_info` e limite do GRBM

Existem dois caminhos upstream, nesta ordem:

1. se `adev->discovery.bin` existe, `amdgpu_device_parse_gpu_info_fw()` retorna
   sem pedir arquivo e `amdgpu_discovery_get_gfx_info()` preenche a topologia a
   partir da tabela GC da IP Discovery;
2. sem Discovery, o kernel pede `amdgpu/cyan_skillfish_gpu_info.bin`; o nome não
   contém o sufixo `2`.

Depois do init, o próprio kernel registra `SE N, SH per SE N, CU per SH N,
active_cu_number N`. Essa linha é a melhor saída operacional para arquivar as
dimensões usadas pelo Linux, independentemente de qual dos dois caminhos as
forneceu.

No fallback, o `common_firmware_header` aponta para um payload little-endian cuja
versão 1.0 contém 15 `uint32_t`. Entre eles estão:

- `gc_num_se`;
- `gc_num_cu_per_sh`;
- `gc_num_sh_per_se`;
- `gc_num_rb_per_se`.

O upstream usa os três primeiros para preencher `max_shader_engines`,
`max_cu_per_sh` e `max_sh_per_se`. A tabela GC Discovery preenche os mesmos
campos. Logo, `GB_ADDR_CONFIG` ou
`CC_GC_SHADER_ARRAY_CONFIG` não devem ser promovidos a fonte única dessas
dimensões.

`tools/fwinspect.py` reconhece `cyan_skillfish_gpu_info.bin` e
`cyan_skillfish1_gpu_info.bin`, confere os limites, CRC-32 e versão, decodifica a
topologia e deixa `grbm_instance_count=null`. O coletor
`tools/collect-linux-gpu-info.sh` arquiva a linha do `dmesg`, o sysfs de IP
Discovery e o fallback `gpu_info`, se presente. Nenhuma captura desse coletor foi
recebida nesta etapa; portanto nenhum número de SE/SH foi aceito como nova
evidência local.

Limite estrutural: nem a configuração GFX consumida pelo kernel nem o fallback
`gpu_info` fornecem diretamente a cardinalidade de
`GRBM_GFX_INDEX.INSTANCE_INDEX`. Os campos do seletor são três eixos
independentes de 8 bits (`INSTANCE`, `SA`, `SE`), e `0xE0000000` liga broadcast
nos três. Assim, mesmo um `gpu_info` autêntico não basta para provar que um
snapshot por SE/SH preserva todos os bancos de um registrador replicado.

Para retirar a quarentena ainda é necessário:

1. arquivar a linha de topologia do `dmesg`, o sysfs Discovery e, se usado, o
   blob `gpu_info` com SHA-256; `tools/collect-linux-gpu-info.sh` faz isso sem MMIO;
2. registrar SE/SH como capacidade e `active_cu_number` separadamente;
3. classificar cada uma das 34 entradas como global ou indexada por SE/SA/instância;
4. obter a cardinalidade de instância por fonte autoritativa específica do bloco;
5. implementar snapshot/restauração por banco antes de qualquer RMW em broadcast.

Nenhuma dessas análises autoriza MMIO de escrita.

**NÃO VALIDADO EM HARDWARE.**

### Captura Fedora de 2026-08-24 14:14

Diretório:
`referencias/linux-gpu-info-fedora-2026-08-24_14-14-07`.

O kernel Fedora 6.19.10-300.fc44.x86_64 registrou, para `1002:13FE`:

```text
SE 2, SH per SE 2, CU per SH 10, active_cu_number 24
```

Isso separa duas grandezas que não podem mais ser misturadas:

- capacidade máxima descrita: `2 × 2 × 10 = 40` CUs;
- CUs efetivamente ativos: `24`;
- diferença capacidade/ativos: `16`, sem inferir aqui se a causa é harvest,
  fuse ou política de firmware.

A IP Discovery estava presente; nenhum `cyan_skillfish_gpu_info.bin` existia nos
diretórios padrão. A entrada GC exposta no sysfs é IP 10.1.3, com bases
`0x1260`, `0xA000` e `0x02402C00`, confirmando novamente as duas bases SOC15
usadas pela tabela golden.

O campo `num_instance=0` no sysfs é o ordinal da instância do bloco IP GC. Ele
não significa "zero instâncias" nem prova que o seletor interno
`GRBM_GFX_INDEX.INSTANCE_INDEX` tenha apenas um valor. Portanto a captura fecha
SE/SH, mas não retira a quarentena do broadcast.

`tools/audit-linux-gpu-info-capture.ps1` torna esses gates reproduzíveis.

**NÃO VALIDADO EM HARDWARE.**
