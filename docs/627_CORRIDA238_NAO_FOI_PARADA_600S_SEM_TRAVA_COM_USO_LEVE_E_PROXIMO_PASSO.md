# 627 — Corrida 238: não foi parada; ~600 s sem trava dura com uso leve

**Corrida:** 06/10/2026, bind às 21:54:55. Mesmo lab **3.384.1.0** da 236/237, CMOS em 256 MB (`vram-cmos/2026-10-06_21-54-26`). Critério: [doc 626 §4](626_CORRIDA237_PARADO_600S_SEM_TRAVA_TEARDOWNS_PROVADOS_E_CRITERIO_CORRIDA238.md).
**Evidência:** `docs/evidence/2026-10-06_21-54-55-DESKTOP-HQAG6T4-lab-binding/`.

## 1. O que aconteceu (P0)

- StartDevice `0x0`; ~597 s de log do nosso driver (QPC 1.091.882.609 → 7.063.701.707); rollback do watchdog às 22:05:11, `RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em 5 amostras.
- **Sem trava dura.** Zero TDR, `GFXHUB_FAULT`, `IB_TRIPOD TIMEOUT`, `Blt Timeout`, `INSIDE_IB`, `DEVICE_REMOVED`.
- 5.786 IBs, 471 `[Present #`, 891 cópias da primária; DWM (4 `CreateDevice`, zero `DestroyDevice`) e Explorer (6/0) estáveis até 22:04.
- **74 `[ActiveOpenAlloc] RECUSADO` com `0xC0000035`:** a lacuna conhecida de superfície compartilhada entre processos (doc 601), agora com Pesquisa/WebView2/Edge.

## 2. Não foi parada

Processos que carregaram a UMD, além de DWM, Explorer, TextInputHost e dxgiadaptercache:

| Processo | Atividade |
|---|---|
| `SystemSettings.exe` | 21:55:32–21:56 |
| `msedge.exe`, `msedgewebview2.exe`, `Claude.exe` | 21:56 |
| `SearchApp.exe` + 3 `msedgewebview2.exe` (pacote da Pesquisa) | 21:57–21:58 |
| `ShellExperienceHost.exe` | 21:55, 21:59 e 22:02 |

E a leitura da CMOS antes do bind deu `0x40080400` (bit 19): como o `-Ler` das 18:52 limpou o registro, **o boot antes da 238 foi um reinício, não um desligamento** (as 22 leituras depois de boot frio nunca têm o bit 19, doc 625 §1). Configurações, Edge e Claude reabrindo sozinhos casam com o "reabrir apps depois do reinício" do Windows (H). Se a Pesquisa e o ShellExperienceHost foram abertos pelo usuário ou pré-carregados, só o Daniel sabe.

## 3. Veredito frente ao critério

**Desfecho C:** inválida como amostra **parada**. Pela Lei 10 (desfecho parcial não é desfecho), não é A.

**Observado fora da pergunta (E, descrito antes de julgar):** ~600 s sem trava com uso leve no começo (Configurações, Edge, Claude, Pesquisa), contra a 236, que travou aos ~45 s com o menu aberto. É mais uma corrida longa sem trava, não uma resposta à pergunta "parado".

**Contagem com 256 MB (26 corridas):** 20 travas e 6 corridas acima de 600 s (201, 222, 226, 229, 237, 238). Taxa de fundo reestimada: ~1 trava a cada ~390 s (H); duas corridas seguidas sem trava por acaso ≈ 4,7%.

**Teardowns:** rodaram de novo (`[StopDevice] Entered` e rollback limpo).

## 4. Próximo passo (proposta; decisão do Daniel)

O teto do doc 626 era a 238 como última corrida parada. Com a 237 parada e a 238 de uso leve passando, e a 236 travando aos ~45 s com o menu, o passo que mais informa é a **bissecção por carga**, uma ação por corrida, com o mesmo pacote:
- **Corrida 239: só o menu Iniciar**, abrir e fechar a cada ~30 s por 600 s, sem Pesquisa nem apps.
- Antes do bind: desligar (não reiniciar) e fechar Edge e Claude antes de desligar, para eles não voltarem sozinhos.

**NÃO VALIDADO EM HARDWARE:** a causa da trava; que ela dependa de carga.
