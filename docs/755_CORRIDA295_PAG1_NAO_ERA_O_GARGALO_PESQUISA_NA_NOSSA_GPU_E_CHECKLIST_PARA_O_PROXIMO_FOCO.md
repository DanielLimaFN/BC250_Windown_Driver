# Doc 755 — Corrida 295: a PAG.1 não era o gargalo, a Pesquisa já nasce na nossa GPU e a checklist para escolher o próximo foco

> **Data:** 14/10/2026, bind às 12:12:10, rollback online às 12:17:27 (`RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em 5 amostras).
> **Candidato:** lab **3.449.1.0** (SYS `D7DE13EA`, UMD `76DA7661`), [doc 754](754_PAG1_ESPERA_DE_PAGINACAO_ADIADA_MENU1_PSO1B_PESQUISA_APOS_BIND_PACOTE_3449_E_CRITERIO_CORRIDA295.md).
> **Evidência:** `docs/evidence/2026-10-14_12-12-10-DESKTOP-HQAG6T4-lab-binding/`.
> **Observado pelo Daniel:** a Pesquisa ainda demora a abrir e os ícones dela demoram muito (12:14); o Win+Tab também demora (12:15). "Mas assim está ok". **Decisão do Daniel:** sair da velocidade do desktop e escolher outro foco; volta-se a ela se precisar.

## 1. Estabilidade (P0)

| Medida | Valor |
|---|---|
| TDR, fault de página, BugCheck | 0 |
| Composições pelo KMD (`COMPOSITE_BLT OK VIA_VA`) | 0 (o zero cópia continua) |
| Latch tardio real (PISC.1) | 1 |
| VSYNC perdidos | 897 de 16.385 (5,5%) |
| `NO_SPACE` do journal | 62 (294: 278) |
| T3 | não rodou nesta corrida (o `BC250_PROCESSO_NOVO.log` é o da 294) |

## 2. Menu: PAG.1 funcionou, mas não era o gargalo (P0) — desfecho B

Linhas `[MENU1]` do `StartMenuExperienceHost` (pid 7460):

| Abertura | Até o 1º Present | CreateResource | Dentro dele: Vk | Espera de paginação | Upload | Submissões |
|---|---|---|---|---|---|---|
| 12:14:13 | 2,22 s | 63 recursos, 1,45 s | 0,56 s | 0 ms (62 adiadas) | 65, 0,15 s | 3, 0,10 s |
| 12:14:22 | 1,29 s | 53 recursos, 0,90 s | 0,41 s | 0 ms (52 adiadas) | 55, 0,05 s | 3, 0,08 s |

**A PAG.1 funcionou:**
- adiou 114 esperas;
- `[PAG1] … feitas depois=0`: nenhum consumidor precisou esperar tarde;
- nenhuma textura errada, piscada nova ou fault.

**Mas o tempo da abertura quase não caiu:**

| Abertura | 294 | 295 |
|---|---|---|
| Até o 1º Present | 1,38–1,49 s | 1,29–2,22 s |
| Dentro do CreateResource | 0,87–1,65 s | 0,90–1,45 s |

**Onde fica o custo agora (P0 pela MENU.1):**
- cada recurso custa ~15–23 ms;
- ~8 ms são a criação Vulkan, que inclui a alocação da RADV e a espera de paginação dela (`radv_wddm2_bo.c`, P1);
- o resto são as chamadas de alocação, mapeamento e residência da nossa alocação WDDM (`Allocate`, `MapAllocation`, `MakeResident`), lentas por si só, sem espera explícita (P2: o split entre as três não foi medido).

**Conclusão:**
- a espera de paginação na CPU não era o gargalo; é o desfecho B do critério;
- a PAG.1 fica, porque é correta e não custa nada;
- quando voltarmos: **criar menos alocações**, com sub-alocação de memória (um bloco grande dividido entre recursos pequenos), como os drivers de verdade fazem. Hoje cada recurso tem duas alocações de kernel: a nossa e a da RADV.

## 3. Pesquisa: agora nasce na nossa GPU e o tradutor carrega, mas o WebView2 larga a GPU (P0) — desfechos D1 + D3

**Reinício:** `pesquisa-reinicio.log` mostra a BC-250 OK aos 21 s, o SearchApp antigo encerrado às 12:12:44 e o novo nascido às 12:12:46: `FEITO`.

**Tradutor (SHD.1):** quatro processos `msedgewebview2` da Pesquisa carregaram a nossa UMD. Em todos:
- `SHADER_TRANSLATOR: ARMADO (vkd3d-shader 1.17)`;
- 2 shaders traduzidos para SPIR-V;
- **zero recusas**; o err=577 da 293b não voltou.

A SHD.1 está validada no WebView2 (P0).

**Mas eles largam a GPU (P0):** cada processo cria o device, cria alguns buffers e traduz 2 shaders. Depois o log da UMD para em ~0,5 s, sem draw, sem Present e sem `DestroyDevice`. É o mesmo padrão da 293b. H: o Chromium testa a GPU, não gosta de algo (nível de recurso 10_x, caps ou recurso compartilhado) e passa a desenhar por software. Isso explicaria a Pesquisa e os ícones lentos. A causa exata só sai do diagnóstico do próprio Chromium (`chrome://gpu`), que no AppContainer não grava log (294).

**Win+Tab lento (H):** é o mesmo caminho de criação de recursos do menu; não foi medido separadamente.

## 4. PSO.1b (P0)

- **DWM:** gravou 2 vezes quando o cache cresceu (32.452 bytes), com 3 gravações evitadas.
- **Menu e CBS:** 9 linhas `sem mudanca … gravacoes evitadas (PSO.1b)` na destruição do device. O cache só é regravado quando muda, como queríamos.

## 5. Veredito

**Parcial.**
- PAG.1, MENU.1, PSO.1b, o reinício da Pesquisa e a SHD.1 no WebView2 estão validados (P0).
- O objetivo de velocidade (menu e Pesquisa mais rápidos) **não foi atingido**: as causas agora têm nome (alocações por recurso e o Chromium largando a GPU).
- Estabilidade mantida, rollback limpo.

**NÃO VALIDADO EM HARDWARE:**
- sub-alocação (não existe ainda);
- motivo do Chromium largar a GPU.

## 6. Checklist para o Daniel escolher o próximo foco

**Feito e provado:**
- APU.2b/2c;
- COMP.VA/.2/.3;
- SDMA1, IH e GFX na RAM;
- zero cópia (ZC.1/ZC.1b);
- PISC.1;
- `Upload LOCK` no menu em 0;
- T3;
- flip pelo DCN a 60 Hz;
- cursor por hardware, DPMS e teardown;
- watchdog sem reboot;
- PSO.1 e PSO.1b;
- SHD.1, inclusive no WebView2;
- reinício da Pesquisa depois do bind;
- MENU.1;
- PAG.1.

**Opções (a ordem é decisão do Daniel):**

| | Foco | Por quê |
|---|---|---|
| **A** | App 3D ou jogo real (D3D11/Vulkan: vkcube, demo, jogo leve) | Caminho direto para a meta final; mostra o que realmente falta (recomendado) |
| **B** | VSYNC pela interrupção (D06/D07) | 5,5% de VSYNC perdidos; quadros mais regulares |
| **C** | Journal cheio (`NO_SPACE`) | 62 nesta corrida (278 na 294); pode virar travamento sob carga de jogo; talvez pré-requisito do A |
| **D** | Velocidade do desktop: sub-alocação; Pesquisa: por que o Chromium larga a GPU | Menu 1,3–2,2 s, Pesquisa, ícones e Win+Tab lentos |
| **E** | Tirar a espera da CPU pelo GFX antes de cada flip | O flip espera a fence da GPU |
| **F** | Pequenos | Sombra de CPU em cópia sem alias; validar DCN.4/LOG.2; volta à tabela de 4 KB se a GART falhar |

**DCN:** nada mudou.
- Prontos: D01–D05, D08, D10, D15, D32–D33.
- Abertos: D06/D07 (VSYNC, 5,5% perdidos), D11, D12–D14, D18–D23, D25–D29, D31.

**Meta final:** jogo real na tela da placa, com o TDR recuperando sem perder o display.
