# 647 — Corrida 246b: G.1 com o KD ativo, trava aos ~5,4 min (a 246 foi dispersão) e os avisos de assinatura do `libvkd3d-shader-1.dll`

**07/10/2026.**
- **Corrida:** bind às 23:08:36, trava por volta de 23:14, reinício pelo Daniel.
- **Pacote:** o mesmo 3.390.1.0 da 246 (SYS assinado `55255FB0…`) + RADV `B4AE2E14`.
- **Configuração:** **KDNET ativo** por decisão do Daniel. As opções de boot foram gravadas em `docs/evidence/vram-cmos/*-corrida246b-SystemStartOptions.txt`.
- **Evidência:** `docs/evidence/2026-10-07_23-08-36-DESKTOP-HQAG6T4-lab-binding/`, com o log do KMD tirado do cumulativo pelo offset do baseline, e a saída do WinDbg colada pelo Daniel no chat.
- **Critério:** [doc 646](646_CORRIDA246_G1_APLICADA_TRAVA_AOS_33S_B1_E_O_PROBLEMA_DA_DISPERSAO.md) §4.1.

## 1. Medido (P0)

- **G.1 armada como na 246:** 29 aplicados, 4 já iguais, 1 restaurado (`RMI_SPARE`). RLC na ROM e P3.3 desligada.
- **Duração:** ~323 s do StartDevice ao último registro (QPC), cerca de 5,4 min depois do bind.
- **Atividade:**
  - 13.849 IBs e 2.467 Presents;
  - IH 1:1 até o fim: ISR 15.112, EOP 2.505 e `outros=0`;
  - nenhum TDR, nenhum fault e nenhum timeout;
  - última linha: composição e paginação normais, com a assinatura de sempre.
- **Boot seguinte:** fusível disparado e Código 43.

## 2. Veredito

Pela regra do doc 646 §4.1, a corrida **passou dos minutos**. A 246 (~33 s) foi **dispersão**, e a G.1 **fica** por paridade com o amdgpu. Os goldens **não são causa suficiente** da trava dura.

**Ressalva:** o KD estava ativo nesta corrida e não na 246. O tempo de 323 s cai dentro da faixa histórica sem KD (45–584 s). O DbgPrint saiu do log na DX.86, mas o KD continua sendo uma variável a mais (H: efeito pequeno).

## 3. O que o WinDbg mostrou (P0, saída colada)

- **Avisos de assinatura do `libvkd3d-shader-1.dll`:**
  - Aconteceram no boot das 23:05:50, com ~21 s de uptime, **antes do bind** das 23:08:36. Nesse momento a placa estava no BasicDisplay e o nosso driver não estava carregado.
  - O processo que tentou carregar foi o `msedgewebview2.exe`.
  - **Mecanismo (P1, leitura do registro):** o nosso ICD `driver\umd\output\bc250_icd.json` está registrado globalmente em `HKLM\SOFTWARE\Khronos\Vulkan\Drivers`. Quando o Chromium do WebView2 sonda o Vulkan, o loader carrega a `C:\BC250_LIB\vulkan_radeon.dll`, que depende do `libvkd3d-shader-1.dll`, sem assinatura.
  - A política de integridade de código do processo do WebView2 recusa a DLL. Com o KD ligado, cada recusa vira um break no depurador, por isso o `int 3` e os `g`.
  - **Não tem relação com a trava.** Mas, com o KD ligado, se ninguém der `g`, a máquina fica parada no break e **parece** travada.
- **Primeira sessão (boot das 22:56):** o depurador perdeu o transporte (`Retry sending...`) e o break seguinte já veio do boot novo, com 1 processador e `!amli` do ACPI. Não houve bind nesse boot (nenhuma pasta de evidência). O que levou à perda do transporte não foi identificado.
- **A trava das 23:14 não aparece na saída colada:** a colagem termina no `.logopen`. Se o Ctrl+Break foi tentado durante a trava, falta o resultado. Na Corrida 164 ele não parou a máquina.

## 4. Estado das hipóteses da trava precoce

| Mudança | Corridas | Tempos | Leitura |
|---|---|---|---|
| P3.3 | 242, 242b, 243 | 11, 22 e 49 s | Correlação forte (3/3); fica **desligada** |
| RLC rodando | 244 | 40 s | **n=1, inconclusivo**; fica desligado até a repetição |
| G.1 | 246, 246b | 33 s e 323 s | Dispersão; **fica ligada** |

## 5. Próximo (proposto)

1. **Repetir a 244 sobre a base atual**, ou seja, a RLC.1 religada em cima da G.1. É a única mudança ainda em aberto. Se travar de novo em menos de 60 s, o RLC sai por correlação (n=2); se passar dos minutos, ele fica.
2. **Opcional, higiene:** registrar o ICD só durante as corridas, ou assinar as DLLs de `C:\BC250_LIB` com o certificado de teste, para o WebView2 parar de tentar carregar a RADV fora delas.

**NÃO VALIDADO EM HARDWARE:**
- a causa da trava dura;
- o efeito do RLC rodando;
- a GFXRST.1, que segue sem nenhum TDR.

## 6. Adendo: as pausas do depurador durante a corrida (P0, saltos de QPC no log do KMD)

**Correção do §3:** os breaks não aconteceram só antes do bind. O log do KMD tem **seis saltos de QPC** dentro da corrida, sem nenhuma linha durante cada um. É a assinatura da máquina inteira parada num break do WinDbg à espera do `g`:

| Pausa | Duração |
|---|---|
| t = 196 s | 5,7 s |
| t = 222 s | 20,5 s |
| t = 243 s | 15,1 s |
| t = 265 s | 39,3 s |
| t = 304 s | 5,3 s |
| t = 312 s | 9,8 s |

- **Depois de cada `g`, o driver continuou normal:** IBs, Presents e paginação voltaram, e não houve TDR nem fault.
- **A trava veio ~11 s depois da última retomada**, com a assinatura de sempre.
- **Tempo real de execução:** ~228 s, descontados ~95 s de pausa. Ainda é "minutos"; o veredito da G.1 (§2) não muda.
- **O WebView2 voltou a abrir durante a corrida,** depois do preflight, e é ele que gera os breaks de assinatura. Isso é carga fora do roteiro (só o menu) e uma segunda variável.
- **O que a pausa explica:** não fica demonstrado que ela causou a trava (H, não excluída). Pausar todas as CPUs por até 39 s com a GPU viva é condição que nunca houve nas corridas sem KD.

**Ação:** o `C:\BC250_LIB\libvkd3d-shader-1.dll` foi assinado com o certificado de teste. O original ficou em `*.pre-assinatura`. Se a política do WebView2 for "só Microsoft", os breaks continuam; o próximo boot com o KD responde.
