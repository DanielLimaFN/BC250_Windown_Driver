# GEMINI.md — Diretrizes de Operação para Google Antigravity e Gemini

> **Contexto normativo para Antigravity / Gemini CLI neste repositório.**
> *Target: AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 11 x64*
> **Última revisão: 2026-09-02** · Núcleo normativo idêntico a `CLAUDE.md`, `AGENTS.md` e `.cursorrules`.

O Antigravity carrega `GEMINI.md` e `AGENTS.md` da raiz para o contexto hierárquico. Este
arquivo é o ponto de entrada; as leis são as mesmas dos demais.

---

## 🎯 1. Onde queremos chegar

**Jogos no Windows 11 rodando na BC-250** — 24 CUs, 16 GB GDDR6, monitor no DisplayPort da
própria placa, D3D12/Vulkan, **sem WARP e sem fallback de software**.

**Fonte única de verdade:** [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md) — Tabela 1 (já feito,
com prova física) e Tabela 2 (o que falta, prioridade `P1→P9`, com o teste que fecha cada
item). **Roadmap:** [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md).

**Arquitetura vigente:** display v1 **por herança do UEFI GOP** — VidPn de modo único,
scanout em VRAM real, `DxgkDdiPresent` no modelo **blt** por SDMA, VSync **lido** do frame
counter do OTG. **DCN não entra no caminho crítico**; escrita em registrador de DCN é
proibida fora da prioridade P9.

**Onde estamos:** o adaptador WDDM **alcançou Código 0 em 02/09/2026** com `DRIVERCAPS`,
`QUERYSEGMENT4`, Paging Context e VidPn ativos no silício por 121 s (itens 97/102 fechados
com evidência arquivada). Compute direto e display KMDOD/GOP **estão** provados no silício.
A etapa atual é a **P5** (manter imagem na tela via blt por SDMA no scanout GOP).

---

## 📊 2. Barra de progresso obrigatória

Em **toda** proposta, `implementation_plan.md` ou resposta técnica, apresente:

```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ██████████████████▋░  93%   Estavel e Isolado
SDMA (DMA Engine & Robustez)     ███████████████████░  95%   Provado em Silicio
GCVM & Traducao de Memoria       ██████████████████░░  90%   PTE 0x10 provado; sem GPUVA dinamica
RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
Telemetria & Observabilidade     █████████████████░░░  85%   Schema 134; anel de DDI e bitmap de QAI
Compute & Async Queues (MEC/MQD) ██████████████████░░  90%   P1 17/17 e P2 16/16 provados; fila persistente nao consome
Miniport WDDM Display+Render     ██████████████████░░  90%   P5 E TRIPÉ FECHADOS NO SILÍCIO (Item 63 OK); Código 0
=============================================================================================
Veredito Global: P5 E TRIPÉ DE DISPLAY FECHADOS NO SILÍCIO (Itens 61b, 62, 63, 81 e 97 Fechados)
=============================================================================================
```

> ⚠️ **Aviso histórico.** Até 2026-09-02 este arquivo trazia uma barra com
> *"Miniport WDDM Display+Render 100%"* e veredito *"COMPROVADOS NO SILÍCIO (CÓDIGO 0)"*.
> **Era falso.** Não reintroduza.

---

## ⚖️ 3. Leis inegociáveis

1. **Sinceridade absoluta.** Medição, não expectativa. Prova ausente → escreva
   **`NÃO VALIDADO EM HARDWARE`**. Hipótese nunca vira causa.
2. **Tripé no silício:** `RPTR == WPTR` + fence física gravada **pela GPU** em VRAM com
   poison check + dados conferidos **byte a byte**.
3. **Todo item fecha com teste nomeado.** Diga qual teste prova execução no hardware e onde
   a evidência será arquivada. A Tabela 2 da `BASE_DE_AVANCO.md` já traz a prova exigida.
4. **Zero fakes:** nunca fence pela CPU; nunca loop de delay; nunca `STATUS_SUCCESS` sem
   backend; nunca WARP/GDI/llvmpipe; nunca telemetria fixa ou estimada.
5. **Não é prova:** compilar, assinar, enumerar API, carregar DLL, criar contexto/pipeline,
   abrir swapchain, `vulkaninfo`, `dxdiag`, `glGetString`.
6. **Ciclo transacional fail-closed:**
   `PREFLIGHT → SNAPSHOT → APPLY → READBACK → PROVA FUNCIONAL → ROLLBACK/TEARDOWN`
7. **Máscara SOC15:** `uint32_t val = (lido & ~mask) | (or_value & mask);`
8. **Endereços com proveniência.** No DCN o offset é `DMU_BASE.seg[BASE_IDX] + reg`, com
   `DMU_BASE = {0x12, 0xC0, 0x34C0, 0x9000}` — **não existe base plana**. Nada em quarentena.
9. **Vocabulário de confiança:** P0 / P1 / P2 / H / Q — ver [`rules.md`](rules.md).
10. **Economia de corridas.** Corrida física é o recurso escasso e é o **Daniel** quem
    executa. Leitura de fonte e mineração de evidência primeiro; teto e critério de
    abandono antes de armar; boot frio entre binds.

### Caminhos encerrados — não reabrir
Render-only sob `Class=Display` (reprovado, `docs/162`) · `MiscCaps.ComputeOnly`
(inalcançável) · cross-adapter/MCDM como destino (APU de die único).

---

## 🔗 4. Navegação

Use sempre links Markdown ao referenciar arquivos.

| O quê | Onde |
|---|---|
| **Base de avanço** | [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md) |
| **Roadmap** | [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md) |
| Regras normativas | [`rules.md`](rules.md) |
| Catálogo de registradores | [`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md) |
| Provas físicas P1 / P2 | `docs/148_...` · `docs/159_...` |
| Ownership de display | `docs/26_...` · `docs/17_...` |
| Encerramento do render-only | `docs/162_...` |
| Corpus de evidência | `docs/evidence/` |
| Miniport WDDM | [`driver/wdk/`](driver/wdk/) |
| UMD D3D / Vulkan | [`driver/umd/`](driver/umd/) |
| Driver de laboratório | [`dbg/`](dbg/) |
| ABI de diagnóstico | [`inc/bc250dbg_abi.h`](inc/bc250dbg_abi.h) |
| Harness e auditorias | [`tools/`](tools/) |
| Recuperação | [`RECOVERY_BC250V4.md`](RECOVERY_BC250V4.md) · [`tools/BC250_RECOVERY.cmd`](tools/BC250_RECOVERY.cmd) |
| Arquivo morto (nada apagado) | [`DocsObsoletos/INDICE.md`](DocsObsoletos/INDICE.md) |

---

## 🔄 5. Manutenção

`CLAUDE.md`, `AGENTS.md`, `GEMINI.md` e `.cursorrules` compartilham o mesmo núcleo
normativo. **Ao mudar uma lei ou a barra de status, atualize os quatro.**
