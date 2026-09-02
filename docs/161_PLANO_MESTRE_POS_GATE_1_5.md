# Plano mestre — pós Gate 1.5

**Data-base:** 2026-09-02
**Origem:** candidato 3.80, evidência `docs/evidence/2026-09-02_08-59-42-DESKTOP-HQAG6T4-lab-binding`
**Meta final (inalterada):** BC-250 como GPU normal no Windows — DisplayPort, desktop acelerado, WDDM display+render, Vulkan, jogos e LLMs na GPU.

> Compilar, assinar ou retornar `STATUS_SUCCESS` não equivale a funcionar.
> Onde não existir prova física, consta **NÃO VALIDADO EM HARDWARE**.

---

## 1. O que está provado, o que é hipótese

Separar isto é o que impede o plano de virar adivinhação cara.

### Provado por medição

| Fato | Evidência |
|---|---|
| Compute físico funciona | P1 17/17, P2 16/16, zero mismatches, fence física em VRAM, reproduzido n=2 (3.79 e 3.80) |
| O dxgkrnl abandona o adaptador **imediatamente** no retorno do `StartDevice` | Código 43 na 1ª amostra de PnP, 5 s após o bind |
| A sequência completa é curta | Anel de DDI: `AddDevice → QueryInterface → QAI(29) → QueryInterface → StartDevice → QueryInterface → StopDevice → RemoveDevice` |
| `DXGKQAITYPE_DRIVERCAPS` **nunca** é perguntado | `V5QaiTypeSeenLo = 0x20000000` (só o bit 29) |
| Portanto `MiscCaps.ComputeOnly` é **inalcançável** | O bit só trafega por `DRIVERCAPS` |
| As 8 recusas de `QueryInterface` **não** são a causa | Todas de interfaces opcionais: I2C/DDC, OPM ×4, Miracast, WDDM3_ON_VB |
| O número de sources/children **não** é o discriminador | 12 corridas `RenderNode=1`: 9 com 1/1 e 3 com 0/0 — **todas** param em `QaiCalls=1, LastType=29` |
| Contrato 2.0 e 2.7 falham igual | Item 93 do checklist, confirmado no corpus |

### Hipótese principal (não provada)

**O adaptador é rejeitado por não declarar user-mode driver.**

Sustentação estrutural, obtida por comparação com drivers reais desta própria máquina:

| | `rdvgwddmdx11.inf` (WDDM DX11 real) | `basicrender.inf` (render-only da MS) | `bc250mcdm.inf` (nosso) |
|---|---|---|---|
| `Class` | `Display` | **`System`** | `Display` |
| Seção `.HW` | sim | — | **ausente** |
| `InstalledDisplayDrivers` | sim | — | **ausente** |
| `UserModeDriverName` / `Wow` | sim | — | **ausente** |

Dois corolários:

1. Todo miniport WDDM Display-class real declara UMD numa seção `.HW`. Nós não temos a seção.
2. O render-only da Microsoft **não é classe Display**. O Windows modela adaptador de render sem display de outra forma — o que explica, independentemente, por que o perfil `RENDER_ONLY_LAB` sob `Class=Display` nunca teve chance.

Bloqueador da hipótese: **não existe D3D UMD no repositório** (`grep OpenAdapter` = 0 ocorrências). O que existe em `driver/umd/` é um runtime de compute próprio e um esqueleto de ICD Vulkan — nenhum dos dois é user-mode display driver.

### Resultado negativo registrado

O perfil `RENDER_ONLY_LAB` está **encerrado com reprovação**. A premissa dele — "provar barato que a tabela render sobrevive em Código 0" — é intestável: a recusa acontece antes de qualquer capacidade de render ser consultada, e o custo prometido ("um FAIL não custa tela preta nem boot frio") não se realizou: custou tela preta e reboot nas duas corridas.

Isto invalida as ordens **2** e **3** da seção 12 do checklist.

---

## 2. Princípios de execução

1. **Offline antes de silício.** Corrida física é o recurso escasso. Toda pergunta que puder ser respondida por leitura de fonte, mineração do corpus de evidência ou comparação com drivers reais da máquina é respondida assim primeiro.
2. **Mineração do corpus antes de cada corrida.** Hoje ela refutou uma inferência minha que teria custado corridas. É barata e já pagou.
3. **Toda corrida é fire-and-collect.** Janela fixa, retrato, rollback e reboot incondicionais. Sem confirmação humana no escuro.
4. **Boot frio entre binds.** Estado de engine não cai com desbind nem com restart morno.
5. **Orçamento por gate** (checklist #177, hoje não formalizado): máximo de corridas, critério de abandono e o que se aprende se falhar — definidos **antes** de armar.
6. **Nenhum falso sucesso.** Um UMD que reporta honestamente "não suporto" é fail-closed e legítimo; um que retorna sucesso sem backend é proibido.

---

## 3. Fases

### Fase 0 — Fechar o ciclo atual · offline · sem risco

| # | Tarefa | Critério |
|---|---|---|
| 0.1 | Commit das Fases A/B/C + evidência do 3.80 | Árvore limpa, baseline nomeada |
| 0.2 | Documento de encerramento do Gate 1.5 | Resultado negativo registrado com evidência |
| 0.3 | Atualizar `CHECKLIST_COMPLETO...md` | Estados reais; ordens 2-3 riscadas; #133/#134 promovidos ao caminho crítico |
| 0.4 | Corrigir vazamento de handle em `Bc250ProbeRecord` | `ZwClose` fora do teste de `fieldName`; + 4 cases faltantes; + teste que varre o enum e exige nome não-nulo |
| 0.5 | Corrigir guarda de engine suja | Hoje lê a chave `Probe`, que some com o serviço no rollback, e passa vazio. Precisa de fonte de verdade que sobreviva ao rollback |

**Por que 0.4 importa:** 1009 membros no enum contra 1005 cases; os 4 órfãos vazam um handle de kernel cada, a cada reset integral, que roda no `DriverEntry`. O `default: return NULL` impede `/W4 /WX` de acusar. O teste proposto transforma a classe inteira de defeito em falha de build.

### Fase 1 — Descobrir a causa do Código 43 · o gargalo real

Esta é a fase crítica. Tudo depois dela depende do resultado.

**1.A — Trilha barata, em paralelo, sem corrida**

| # | Tarefa | Entrega |
|---|---|---|
| 1.A.1 | Diff completo do nosso INF contra `rdvgwddmdx11.inf` | Lista fechada de tudo que um WDDM real declara e nós não |
| 1.A.2 | Mapear o que o dxgkrnl exige entre `StartDevice` e `DRIVERCAPS` | Da documentação WDK e dos headers; não de suposição |
| 1.A.3 | Auditar `basicrender.inf` (`Class=System`) | Entender como o Windows modela render sem display — pode ser um caminho alternativo inteiro |
| 1.A.4 | ETW `Microsoft-Windows-DxgKrnl-Diagnostic` | Verificar **offline** se o canal existe e o que expõe, antes de gastar corrida com ele |

**1.B — Trilha decisiva: depurador de kernel**

> **SUPERSEDED em 2026-09-02:** nao existe segunda maquina. A trilha 1.B foi substituida
> pelas quatro camadas da P3 na [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md).

Pré-requisito original: **segunda máquina** + KDNET. O runbook atual (#175) está marcado como rascunho e o #124 já registra que `-kl` local não permite breakpoint/stack/`g`.

| # | Tarefa | Critério de PASS |
|---|---|---|
| 1.B.1 | Corrigir e validar o runbook KDNET com host separado | Breakpoint, stack e `g` funcionando |
| 1.B.2 | Corrida instrumentada com depurador anexado | Capturar o `NTSTATUS` exato com que o dxgkrnl abandona o adaptador |

**Gate de decisão da Fase 1:** ou temos o `NTSTATUS`, ou temos a lista fechada de divergências do INF. Sem um dos dois, **não** se avança para a Fase 2 — implementar UMD por palpite é caro demais.

### Fase 2 — Fechar o Código 43

Conteúdo depende do gate anterior. Se a hipótese do UMD se confirmar:

| # | Tarefa | Critério |
|---|---|---|
| 2.1 | Seção `.HW` no INF com `InstalledDisplayDrivers`, `UserModeDriverName`, `UserModeDriverNameWow` | Apontando para DLL que **existe e carrega** |
| 2.2 | D3D UMD mínimo, real e fail-closed | Exporta `OpenAdapter10_2`; reporta honestamente o que não suporta; **jamais** sucesso sem backend |
| 2.3 | Corrida com orçamento definido | **PASS: `bc250mcdm / Código 0` sustentado**, e o dxgkrnl avança além de `WDDMDEVICECAPS` |

**Marco:** primeiro Código 0 em `DxgkInitialize`. É o marco que destrava tudo o mais.

### Fase 3 — Provar que o adaptador é usável

| # | Item do checklist | Critério |
|---|---|---|
| 3.1 | #103 `QueryAdapterInfo` caps | `DRIVERCAPS` e segments consumidos e corretos |
| 3.2 | #104 Nodes/engines | Nodes WDDM descrevendo o compute que já existe fisicamente |
| 3.3 | #111 D3DKMT harness | Abrir adapter, criar device/context/allocation pelo kernel gráfico |
| 3.4 | #105-#108 Create Device/Context/Allocation | Chamadas reais do dxgkrnl com handles válidos |

### Fase 4 — Submissão WDDM de verdade

O ponto onde o compute já provado passa a ser dirigido pelo Windows.

| # | Item | Critério |
|---|---|---|
| 4.1 | #110 `SubmitCommandVirtual` | Entrada originada pelo dxgkrnl |
| 4.2 | #112 Fence WDDM | Fence física da GPU vinculada ao scheduler |
| 4.3 | #113/#114 Interrupt/DPC e retirement | O dxgkrnl reconhece completion **física** |
| 4.4 | #25/#116 TDR | Timeout controlado recupera sem sujar a engine |

**Tripé obrigatório em cada um:** `RPTR == WPTR`, fence física em VRAM com verificação de poison, e conferência byte a byte.

### Fase 5 — DCN read-only · pode correr em paralelo desde já

Read-only é seguro e não compete com a Fase 1 pelo mesmo recurso.

| # | Item | Critério |
|---|---|---|
| 5.1 | #4/#64 IP discovery de display | DCN/DMU/DIO/IH derivados da própria placa em runtime |
| 5.2 | #65/#66 Base e inventário DCN | Probe **sem escrita**, dois snapshots estáveis, proveniência |
| 5.3 | #67/#68 OTG e HUBP | Frame counter vivo; surface, pitch, tiling, viewport, in-use |
| 5.4 | #80 ATOM/board tables | Connector/encoder/clock extraídos da própria placa |

**Valor independente:** mesmo que a Fase 1 demore, isto constrói o catálogo que a Fase 6 vai exigir, sem gastar corrida destrutiva.

### Fase 6 — Display real

Só depois de Código 0 sustentado (Fase 2) e do inventário DCN (Fase 5).

`#81 takeover do GOP` → `#82 master update lock` → `#83 flip A/B` → `#84 VBlank real` → `#75/#76/#77 AUX/DPCD/EDID` → `#78 link training` → `#85/#86 mode-set e modos` → `#87 hotplug`.

### Fase 7 — APIs, jogos e LLM

`#135-#145 Vulkan` (loader, memory, queues, submit, sync, compute, graphics, WSI) → `#146 RADV winsys` → `#147 shader compiler` → `#148-#150 Zink/DXVK/VKD3D` → `#158-#164 triângulo, jogos, LLM, long-run`.

---

## 4. Caminho crítico

```
Fase 1 (causa do 43)  →  Fase 2 (Código 0)  →  Fase 3  →  Fase 4
                                              ↘
                                                Fase 6 (display)  →  Fase 7
Fase 5 (DCN read-only) ─────────────────────────↗
Fase 0 ─ paralela, sem bloquear nada
```

Tudo está atrás da Fase 1. Enquanto a causa do Código 43 não for conhecida, nenhuma outra fase pode ser concluída — e é por isso que ela merece o depurador em vez de mais corridas às cegas.

---

## 5. Honestidade sobre magnitude

O destino equivale a escrever um driver de display AMD do zero mais um driver Vulkan. As fases 6 e 7 são, cada uma, maiores que tudo que o projeto fez até aqui. Isso não é argumento para desistir; é argumento para que cada fase tenha **valor próprio**:

- Fase 2 entrega o primeiro adaptador WDDM aceito pelo Windows.
- Fase 4 entrega compute dirigido pelo Windows — já suficiente para LLM via caminho de compute.
- Fase 6 entrega imagem.
- Fase 7 entrega jogos.

Um projeto que para na Fase 4 ainda é um resultado real.

---

## Veredito

- **Compute direto no silício:** VALIDADO EM HARDWARE.
- **Display KMDOD/GOP:** VALIDADO EM HARDWARE.
- **Render-only WDDM:** REPROVADO — estruturalmente inviável neste caminho.
- **Render WDDM, display acelerado, Vulkan, jogos e LLM via driver:** **NÃO VALIDADO EM HARDWARE.**
