# 507 — Pesquisa: o controlador de memória, SMU/PSP, fontes externas, e o próximo ensaio

> 28/09/2026, offline. Premissa confirmada pelo Daniel: todas as travas foram reiniciadas pelo **botão reset**, sem corte de energia. O evento Kernel-Power 41 da 157 traz `LongPowerButtonPressDetected=false`. Logo, o decaimento da VRAM (doc 506) aconteceu **com a placa energizada**, e **o controlador de memória parou de fazer refresh durante a trava**. Classificação: P0 no decaimento, P1 na leitura de que refresh ausente implica controlador parado.

## 1. O que a pesquisa trouxe

| Fato | Fonte | Confiança | Serve para |
|---|---|---|---|
| O SMU do Cyan Skillfish liga `FEATURE_FCLK_DPM_BIT`, `FEATURE_SOC_DPM_BIT` e `FEATURE_GFX_DPM_BIT`; as métricas trazem `MemclkFrequency`, `SocTemperature` e `GfxTemperature` por tabela copiada do SMU (`TransferTableSmu2Dram`) | `drivers/gpu/drm/amd/pm/swsmu/smu11/cyan_skillfish_ppt.c` (upstream) | P1 | O firmware muda sozinho o relógio do **fabric** conforme a carga |
| FCLK e UCLK são controlados **só pelo PSP** (PSPSMC 0x0E/0x0F), fora do caminho normal do SMU | notas da comunidade `lorek123/bc250-notes` | P2 externo | Não há como um driver comum travar esses relógios pelo SMU |
| Algumas filas de mensagem do SMU (Q2 0x11 em diante, toda a Q4) e a 0x98 com arg 0 **travam o SMU até o reboot** | mesmas notas | P2 externo | Risco se o driver mandar mensagem ao SMU |
| O display do Cyan Skillfish é **DCN 2.0.1**; o `dcn201_clk_mgr` acompanha `p_state_change_support`, sem chamada de SMU própria | `dcn201_clk_mgr.c` (upstream) | P1 | O protocolo de troca de estado da memória existe do lado do display |
| "O microcódigo SDMA da placa nunca completa uma transferência acima de 16.384 bytes"; o do Navi12 resolve | `akandr/bc250-rocm` | P2 externo, **contradito por nós** | ver §2 |
| ROCm #6313: trava do sistema com cargas de computação, **precedida** de `ring sdma0 timeout` e resets | issue do ROCm | P2 externo | Assinatura diferente da nossa |
| BIOS recomendadas: P2.00–P3.00 ou P5.00; evitar a P4.00 | notas da comunidade | P2 externo | A placa do Daniel tem a **P3.00** (P0, `Win32_BIOS`) |

## 2. O que foi conferido localmente

- **O nosso driver não manda mensagem ao SMU durante as corridas.** As chamadas (`Bc250SmuGetVersion`, `GetGfxFrequency`, `GetGfxVid`) só existem no serviço de IOCTL de diagnóstico (`bc250_compute_service.c`), e a 157 tem zero linhas de SMU (P1 código + P2 log). Não somos a origem de uma mensagem que trave o SMU.
- **A alegação dos 16 KiB não vale para o nosso caminho (P0):**
  - carregamos `firmware/cyan_skillfish2_sdma.bin` (33.792 bytes);
  - os nossos fills de 4 MiB e 11 MB concluem com fence escrita pela GPU;
  - as cópias de 11 MB do present passam no `BYTE_PROOF` do scanout, e o desktop aparece por elas.

  A falha deles deve estar em outro caminho (as filas do ROCm) e não explica a nossa trava. Não adotada.
- **O quadro da nossa trava difere do ROCm #6313:** aqui não há nenhum timeout, TDR ou falha antes; a máquina para de uma vez.

## 3. O que sobra

**Fato:** o controlador de memória do SoC para, **mais vezes quanto mais paginação da GPU acontece** (0/2 com KD lento, 0/2 com M1, 5/6 depois da FPS.1).

**Candidatos (H):**
1. **Transição de energia ou relógio do fabric ou da memória** (DPM de FCLK pelo SMU, estados de baixo consumo, UCLK pelo PSP) que dá errado sob a nossa carga. O display não participa, porque está no estado do GOP.
2. **Um padrão de tráfego da GPU que trava o fabric** (acesso a endereço inválido, coerência entre VRAM e memória de sistema).

Os dois estão abaixo do sistema operacional, e **nenhuma leitura de registrador com proveniência disponível hoje** os separa. Ler as métricas do SMU exige mandar mensagem ao SMU, que é exatamente o que as notas dizem poder travá-lo. Isso não será feito agora.

## 4. Próximo ensaio proposto: separar a plataforma da carga, sem código

**Pelas configurações da BIOS (decisão e mãos do Daniel; eu não mudo configuração do sistema):**
1. Anotar o que existe em Advanced / AMD CBS (ou equivalente): **Global C-state Control**, **DF C-states** (ou "Data Fabric C-states"), **Power Supply Idle Control**, APM/CPPC e opções de memória. Só anotar, sem mudar, e me passar a lista.
2. Com a lista, escolhemos **uma** opção de estado de baixo consumo do fabric para desligar (por exemplo, DF C-states, ou Power Supply Idle Control em "Typical Current Idle") e repetimos a mesma corrida, 3.306.1.0, **duas vezes**.

**Critério (escrito antes):**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Duas corridas de 600 s sem trava | A transição de energia da plataforma é o gatilho; a trava passa a ter nome e a correção é de configuração ou firmware. |
| **B** | Trava como antes | Os estados de baixo consumo não são a causa; resta o candidato 2 (padrão de tráfego) e a instrumentação de última operação (doc 505 §5). |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Se a BIOS não tiver nenhuma dessas opções, o ensaio não existe, e o próximo passo passa a ser o candidato 2.

Fontes:
- [cyan_skillfish_ppt.c (Linux upstream)](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/pm/swsmu/smu11/cyan_skillfish_ppt.c)
- [dcn201_clk_mgr.c (Linux upstream)](https://codebrowser.dev/linux/linux/drivers/gpu/drm/amd/display/dc/clk_mgr/dcn201/dcn201_clk_mgr.c.html)
- [lorek123/bc250-notes](https://github.com/lorek123/bc250-notes)
- [akandr/bc250-rocm](https://github.com/akandr/bc250-rocm)
- [ROCm issue #6313](https://github.com/ROCm/ROCm/issues/6313)
