# Doc 751 — Corrida 293b: desktop inteiro sem cópia; PSO.1 (cache de pipelines), pacote 3.447.1.0 e critério da Corrida 294

> **Data:** 14/10/2026, bind às 10:03:01, rollback online (`RECOVERY_OK_ONLINE`).
> **Candidato:** lab **3.446.1.0** (UMD `36A7D0DC`), [doc 750](750_CORRIDA293_ZERO_COPIA_ATIVA_TELA_PRETA_POR_SOMBRA_DE_CPU_ZC1B_PACOTE_3446_E_CRITERIO_CORRIDA293B.md).
> **Evidência:** `docs/evidence/2026-10-14_10-03-01-DESKTOP-HQAG6T4-lab-binding/`.
> **Observado pelo Daniel:** desktop certo. O menu abre devagar nas primeiras vezes, e a Pesquisa sempre demora mais para abrir.

## 1. O que a corrida mediu

| Medida | Valor | Nível |
|---|---|---|
| ZC.1b em efeito | linhas `[ZC1B]` (cópia de região sem subir a sombra) e `[ZC1_PRESENT]` (quadro sem cópia) presentes, inclusive em app (SystemSettings) | P0 |
| `COMPOSITE_BLT OK VIA_VA` | 0 | P0 |
| Desktop | inteiro e certo (operador) | P0 |
| `Upload LOCK falhou` no menu | 0 (eram 368 na 292c) | P0 |
| Estabilidade | zero TDR/fault; 1 latch tardio real; 56 `NO_SPACE` no journal; rollback online | P0 |

O mecanismo da tela preta (sombra de CPU por cima da cópia de GPU) está confirmado: a correção o eliminou.

## 2. Primeira abertura lenta: duas causas (P1, pelo código)

1. **Sem cache de shader.** A RADV no Windows é compilada sem cache em disco (`meson.build`: *"Shader Cache does not currently work on Windows"*), e a UMD criava pipelines com `VK_NULL_HANDLE`. Todo device novo recompila no ACO todos os shaders que usa. O menu cria dois devices só ao abrir (10:03:18 e 10:03:40).
2. **Captura de IR sempre ligada.** Desde a DX.21, todo device pedia `VK_KHR_pipeline_executable_properties`, e todo pipeline saía com `CAPTURE_INTERNAL_REPRESENTATIONS`. Isso faz a RADV guardar o IR e o disassembly, o que deixa cada compilação mais cara. Além disso, esse pedido entra na chave do cache.

## 3. Pesquisa sempre lenta: o que se sabe (P2/H)

A Pesquisa é renderizada pelo WebView2, que é Chromium. Os processos `msedgewebview2.exe`:
- abrem o nosso adaptador;
- recebem nível 10_0/10_1, sem caps de threading e de shader;
- criam o device e um buffer;
- e param de usar a UMD em ~0,2 s, sem `DestroyDevice`.

**H:** o Chromium recusa a nossa GPU (nível de recurso ou caps) e cai para renderização por software, o que explicaria a Pesquisa sempre mais lenta. **NÃO VALIDADO:** falta o log do próprio Chromium (`chrome://gpu` ou `--enable-logging`). É o próximo item.

## 4. Correção: PSO.1 (pacote 3.447.1.0)

- **Cache persistente:** um `VkPipelineCache` por device, carregado de `%TEMP%\bc250_pso_v1.bin` e gravado de volta:
  - a cada 8 pipelines novos, porque o menu e a Pesquisa podem morrer sem destruir o device;
  - e na destruição do device.
- **Gravação segura:** grava num `.tmp` por PID e troca o arquivo depois. Dados de outra RADV são descartados pelo próprio Vulkan (cabeçalho com UUID).
- **Onde o arquivo fica:** cada pacote AppContainer (menu, Pesquisa) tem a sua pasta temporária; o DWM usa `C:\Windows\Temp`.
- **Captura de IR opcional:** só com `HKLM\SOFTWARE\BC250\Umd\PipelineIrCapture=1`. Sem a chave, o device nasce sem a extensão e os pipelines sem a flag.
- **Log:** `[PSO1]` entrou no LOGMIN.
- **Efeito esperado:** a 1ª abertura de cada app continua compilando, mas mais barato (sem captura). Da 2ª em diante, inclusive depois de reiniciar, os pipelines vêm do cache.

**Build e pacote:**
- `build-umd` EXIT=0; build de laboratório OK; auditoria de segurança PASS (4 portões novos); auditoria do pacote PASS; `[PSO1]` conferido na UMD.
- **Lab 3.447.1.0:** SYS `65C07090…`, UMD `00BCFB38…`, INF `8592C855…`, CAT `8B503B68…`.
- **Roteiro:** `tools/EXECUTAR_CORRIDA294_PSO1_CACHE_DE_PIPELINES_300S.cmd`. Abrir o menu 3 vezes e a Pesquisa 3 vezes. Antes: desligar e ligar.

## 5. Critério da Corrida 294

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | `[PSO1] cache de pipelines ATIVO` no DWM e no menu; `gravado … OK`; numa 2ª abertura ou processo, `… N bytes carregados` com N > 0; o menu abre rápido da 2ª vez em diante (Daniel) | PSO.1 validada; sobra a Pesquisa (WebView2) |
| **B** | `ATIVO` e `gravado FALHOU` | permissão ou caminho da pasta temporária no AppContainer |
| **C** | `INDISPONIVEL` | a RADV não expõe o cache: olhar o res |
| **D** | o menu continua igual mesmo com `carregados` > 0 | o custo da 1ª abertura não é compilação; medir outro passo (criação de recursos, FILL) |
| **E** | algo fora do mapa | descrever antes de julgar |

**NÃO VALIDADO EM HARDWARE.**
