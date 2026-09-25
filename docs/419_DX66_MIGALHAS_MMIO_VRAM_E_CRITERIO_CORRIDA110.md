# DX.66: migalhas de MMIO em VRAM que sobrevivem ao reinício a quente; critério da Corrida 110

> **Correcao posterior (Corrida 110):** a suposta folga em 0x0FE80000 estava dentro da regiao TMR do alocador bootstrap. O arme foi recusado; portanto o critério DX.66 teve desfecho E e nao testou retencao de VRAM. Ver docs 421 e 422. O codigo atual reserva 0x0FA00000 antes do bootstrap.

> **Data:** 24/09/2026. Faixa **offline**, sem bind. Só instrumentação: nenhum acesso MMIO muda de valor, ordem ou número.
> **Base:** [Corrida 109](418_CORRIDA109_SEM_RADEON_TRAVOU_3MIN_BATIMENTO_B_LINHA.md). A máquina para inteira, com a thread PASSIVE e a paginação no mesmo período de 100 ms. H de alta prioridade: uma leitura MMIO que nunca completa.
> **Reinício:** o Daniel reinicia a quente ("reiniciar apenas"), que é o modo em que a VRAM costuma sobreviver.

## 1. Mecanismo

**Página fixa de VRAM:** `BC250_CRUMB_VRAM_OFFSET = 0x0FE80000`.
- Fica na folga entre o teto do segmento do VidMm (0x0FE00000, `bc250_active_miniport.c`) e o anel SDMA legado (0x0FF00000).
- Endereço físico = BAR 0 de 256 MB, lido de `DxgkCbGetDeviceInformation` → `TranslatedResourceList`, + offset.
- Mapeada **UNCACHED** (`PAGE_NOCACHE`): escrita postada, em ordem, que não fica presa em buffer de write-combining se a CPU parar.

**Layout:**
- Cabeçalho: magic `CMRB`, versão, nonce (hora do arme), última sequência, estado `RUN1`/`CLN1`.
- 32 slots, um por processador: sequência global, sítio<<24 | offset, fase `ANTES` (0xB0) / `DEPOIS` (0xA0).

**Sítios instrumentados** (ANTES do acesso, DEPOIS dele):

| Sítio | Onde | O que cobre |
|---|---|---|
| `TRANSPORT_READ` / `TRANSPORT_WRITE` | `Bc250MmioTopologyRead32/Write32` | todo registrador pelo transporte central (GFX, WPTR readback, SQ, TLB, MMU, retire), 246 usos no miniport |
| `SDMA_REG_READ` / `SDMA_REG_WRITE` | `Bc250SdmaRegRead/Write` | registradores do SDMA0 (RPTR/WPTR/STATUS do polling) |
| `OTG_FRAMECOUNT_READ` | DPC de VSync, **a cada 4 ms** | registrador do DCN (BAR5+0x14030, só leitura) |
| `RINGWORK_VRAM_READ` / `_WRITE` | `Bc250SdmaDeviceRead/Write` | anel e páginas de trabalho em VRAM pelo BAR |
| `VRAM_SAMPLE_LOOP` / `VRAM_COMPARE_LOOP` | `Bc250DisplaySampleSurface` / `CompareSurfaceBytes` | laços de leitura de VRAM pela CPU no Present. Uma migalha por laço, não por byte |

**Não cobertos (declarados):**
- leituras de fence/writeback em VRAM fora dessas funções;
- `bc250_compute_service.c` (acesso direto a `g_MmioBase`, fora do caminho da corrida);
- leituras de VRAM de outros componentes (dxgkrnl, UMD por mapeamento).

**Ciclo de vida, no wrapper `Bc250DxgkDdiStartDeviceV5Telemetry`:**
1. `Bc250CrumbReportPrevious`, **antes** da implementação, portanto antes do fusível que recusa o start no boot após travamento: lê a página e registra `[DX66_MIGALHA] ANTERIOR PRESENTE/AUSENTE`, com os 32 slots do mais novo para o mais antigo.
2. `Bc250CrumbArm`, **só** se o StartDevice teve sucesso e nenhuma região em uso dos dois alocadores internos sobrepõe a página (`ARME RECUSADO` caso contrário): zera, grava o cabeçalho e publica o ponteiro.
3. `Bc250CrumbMarkClean` no StopDevice: estado `LIMPO`, ponteiro nulo. Unmap no unload.

**Build:**
- O módulo é só cabeçalho (`bc250_mmio_breadcrumb.h`, globais `__declspec(selectany)`), sem novo `.c` nos scripts.
- Fora da imagem do driver, tudo vira no-op, e o ciclo de vida no miniport tem stubs para a build contratual.

## 2. Verificação

- Build de laboratório OK; `build-contract` EXIT=0.
- Auditoria de segurança PASS com o portão `audit_dx66_mmio_breadcrumbs`:
  - página dentro da folga, com o teto do VidMm presente;
  - ANTES/acesso/DEPOIS em ordem em cada sítio;
  - UNCACHED;
  - teste de sobreposição antes de mapear e publicar;
  - leitura antes do StartDevice, arme só após sucesso, LIMPO no StopDevice.
- Mutações `tools/test-dx66-breadcrumb-mutations.py` **9/9**. Suítes DX.62–65 22/22 e DX.61 7/7 repassadas.
- Pacote lab **3.270.1.0**: SYS `C3739348FD445FFF4EF4B93B7981469C8864D33636671652C18B7A780DE842F8`, INF `12E54F16…A465`, CAT `7E215CD5…F12C`, UMD `B6AB6788…` (inalterada).
  - Auditoria do pacote PASS; marcas `[DX66_MIGALHA]` no SYS.
  - Pré-voo `tools/prepare-corrida110-dx66.ps1` PASS, **mantém a exigência da Radeon Software desligada**.

**NÃO VALIDADO EM HARDWARE:** se a VRAM sobrevive ao reinício a quente nesta placa, e se o endereço do BAR da lista de recursos é o mesmo do GOP (0xC0000000).

## 3. Execução

`tools\EXECUTAR_CORRIDA110_DX66_600S.cmd`, elevado, 600 s, depois de **desligamento completo com corte de energia**. Isso é esperado limpar a página; na corrida, `ANTERIOR AUSENTE` no bind confirma.

**Se travar:**
1. **Reinicie a quente** (reset ou reiniciar). **Não** corte a energia.
2. Deixe o Windows subir e **espere o guardião** (ele age quando o prazo de 600 s do bind vencer).
3. A leitura acontece sozinha no StartDevice do boot novo e vai para `C:\BC250_DISPLAY_LOG.txt`, recuperável pelo offset.

## 4. Critério da Corrida 110, escrito antes

**Controle:**
- no boot da corrida: `[DX66_MIGALHA] ARMADA: Phys=0xC0E80000 …`, com o BAR igual ao do GOP;
- nenhum `ARME RECUSADO`;
- Radeon Software ausente.

**Se travar, a assinatura está nas linhas `[DX66_MIGALHA]` do StartDevice do boot seguinte:**

| | Assinatura | Implicação |
|---|---|---|
| **A** | `ANTERIOR PRESENTE estado=EM_EXECUCAO`, e o slot `<- ULTIMA DA MAQUINA` (ou outro recente) com `fase=ANTES(acesso NAO concluido)` | **Localizado:** a CPU parou naquele sítio e offset. Os demais slots dizem o que os outros núcleos faziam. Próximo: por que aquele acesso não completa (estado da GPU naquele instante). |
| **B** | `ANTERIOR PRESENTE estado=EM_EXECUCAO`, com todos os slots recentes em `DEPOIS` | Nenhum acesso instrumentado ficou pendente. O travamento está fora dos sítios cobertos (fence/writeback em VRAM, outro componente, ou não é MMIO). A última sequência e o horário do rastro DX.65 delimitam. |
| **C** | `ANTERIOR AUSENTE` depois de reinício a quente | A VRAM não preservou a página nesta placa. A abordagem está descartada, com uma corrida. |
| **D** | 600 s sem travar; guardião reverte no mesmo boot; `ESTADO LIMPO` no StopDevice | Sem travamento a medir. Repetir, ou mudar de frente. |
| **E — fora do mapa** | Qualquer outra coisa (incluindo `ARME RECUSADO`) | Descrever antes de julgar. |

**Regras:**
- Desfecho parcial não é desfecho.
- A exige `EM_EXECUCAO` **e** um slot em `ANTES`.
- Um slot `ANTES` com sequência muito menor que a última é de um núcleo que parou antes. Registrar, sem chamá-lo de "último".

**Secundárias:**
- hora do travamento (rastro DX.65);
- se o mouse estava sendo mexido;
- FPS.
