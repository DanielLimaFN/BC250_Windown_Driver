# AGENTS.md — Diretrizes Universais para Agentes de IA

> **Governança para qualquer agente autônomo neste repositório** (OpenAI Codex, ChatGPT,
> Copilot Workspace, Cursor, auto-agents).
> *Target: AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 11 x64*
> **Última revisão: 2026-09-02** · Núcleo normativo idêntico a `CLAUDE.md`, `GEMINI.md` e `.cursorrules`.

---

## 🎯 1. Onde queremos chegar

**Jogos no Windows 11 rodando na BC-250** — 24 CUs, 16 GB GDDR6, monitor no DisplayPort da
própria placa, D3D12/Vulkan, **sem WARP e sem fallback de software**.

**Fonte única de verdade:** [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md).
- **Tabela 1** — o que já está validado no silício (45 itens, com prova arquivada).
- **Tabela 2** — o que falta, em ordem de prioridade `P1→P9`, **com o teste que fecha cada item**.

**Roadmap:** [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md).

### Arquitetura vigente
Display v1 é **por herança do UEFI GOP**: VidPn de um modo único, scanout em VRAM real,
`DxgkDdiPresent` no modelo **blt** por SDMA, VSync **lido** do frame counter do OTG.
**Não implementamos DCN no caminho crítico.** Escrita em registrador de DCN é **proibida**
fora da prioridade P9. Leitura é permitida.

### Onde estamos, de verdade
O adaptador WDDM **alcançou Código 0 em 02/09/2026** com `DRIVERCAPS`, `QUERYSEGMENT4`,
Paging Context e VidPn ativos no silício por 121 s (itens 97 e 102 fechados com evidência
arquivada). O compute direto e o display KMDOD/GOP também estão provados no silício.
A etapa atual é a **P5** (manter imagem na tela via blt por SDMA no scanout GOP).

---

## 🛑 2. Barra de status obrigatória

Abra toda resposta técnica com esta barra. **Ela reflete medição, não desejo.**

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

> ⚠️ **Aviso histórico.** Até 2026-09-02 este arquivo continha uma barra declarando
> *"Miniport WDDM Display+Render 100% — Canal Display/VidPN/VSync/Present Ativo em Código 0"*
> e o veredito *"COMPROVADOS NO SILÍCIO"*. **Era falso** e contradizia a evidência medida.
> Agentes que leram aquela versão produziram análises otimistas sem base. Não reintroduza.

---

## ⚖️ 3. Leis inegociáveis

1. **Sinceridade absoluta.** Reporte a medição, não a expectativa. Prova ausente →
   escreva literalmente **`NÃO VALIDADO EM HARDWARE`**. Hipótese nunca é apresentada como causa.
2. **Tripé no silício.** `RPTR == WPTR` + fence física gravada **pela GPU** em VRAM com
   poison check + dados conferidos **byte a byte**. Sem os três, não houve execução.
3. **Todo item fecha com um teste nomeado.** Diga qual teste comprova execução no hardware
   e onde a evidência será arquivada (`docs/evidence/`). A Tabela 2 da `BASE_DE_AVANCO.md`
   já traz a prova exigida de cada item. Trabalho sem teste nomeado não é entregável.
4. **Zero fakes.** Proibido: fence ou timestamp incrementado pela CPU; loop de delay
   simulando execução; `STATUS_SUCCESS` em DDI sem backend; stub/mock/no-op anunciado como
   pronto; telemetria fixa ou estimada; WARP/GDI/llvmpipe; fallback silencioso.
5. **Não é prova de aceleração:** compilar, assinar, carregar DLL, enumerar API, criar
   contexto ou pipeline, abrir janela ou swapchain, `vulkaninfo`, `dxdiag`, `glGetString`,
   ou qualquer string que o próprio driver anuncia.
6. **Ciclo transacional fail-closed:**
   `PREFLIGHT → SNAPSHOT → APPLY → READBACK → PROVA NO SILÍCIO → ROLLBACK/TEARDOWN`
   Abortar se o preflight for inválido; falhar fechado em qualquer etapa.
7. **Máscara SOC15:** `uint32_t val = (lido & ~mask) | (or_val & mask);`
8. **Endereços com proveniência.** Só offsets catalogados em
   [`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md), com
   fonte citada; nada em quarentena. No DCN: `DMU_BASE.seg[BASE_IDX] + reg`, com
   `DMU_BASE = {0x12, 0xC0, 0x34C0, 0x9000}` — **não existe base plana**.
9. **Vocabulário de confiança:** P0 provado no silício · P1 derivado do upstream ·
   P2 parcialmente observado · H hipótese · Q quarentena (proibido). Ver [`rules.md`](rules.md).
10. **Economia de corridas.** A corrida física é o recurso escasso e é o **Daniel** quem a
    executa. Responda primeiro por leitura de fonte e mineração de `docs/evidence/`. Defina
    teto de corridas e critério de abandono antes de armar. Boot frio entre binds.

---

## 🚫 4. Anti-alucinação

Como agente, você **não deve**:
- Afirmar que algo está "pronto", "acelerado" ou "funcionando" porque compilou ou retornou sucesso;
- Declarar causa-raiz sem trace, dump ou telemetria que a sustente;
- Reabrir caminhos encerrados: render-only sob `Class=Display` (reprovado, `docs/162`),
  `MiscCaps.ComputeOnly` (inalcançável), cross-adapter/MCDM como destino (APU de die único);
- Propor escrita em registrador de DCN fora da prioridade P9;
- Omitir a cláusula **`NÃO VALIDADO EM HARDWARE`** quando faltar evidência física.

---

## 📂 5. Mapa do repositório

| O quê | Onde |
|---|---|
| **Base de avanço** | [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md) |
| **Roadmap** | [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md) |
| Regras normativas | [`rules.md`](rules.md) |
| Catálogo de registradores | [`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md) |
| Provas físicas P1 / P2 | `docs/148_...` · `docs/159_...` |
| Ownership de display (prova) | `docs/26_...` · `docs/17_...` |
| Encerramento do render-only | `docs/162_...` |
| Corpus de evidência | `docs/evidence/` |
| Miniport WDDM | [`driver/wdk/`](driver/wdk/) |
| UMD D3D / Vulkan | [`driver/umd/`](driver/umd/) |
| Driver de laboratório KMDF | [`dbg/`](dbg/) |
| ABI de diagnóstico | [`inc/bc250dbg_abi.h`](inc/bc250dbg_abi.h) |
| Harness e auditorias | [`tools/`](tools/) |
| Recuperação | [`RECOVERY_BC250V4.md`](RECOVERY_BC250V4.md) · `tools/BC250_RECOVERY.cmd` |
| Arquivo morto (nada apagado) | [`DocsObsoletos/INDICE.md`](DocsObsoletos/INDICE.md) |

---

## 🔄 6. Manutenção

`CLAUDE.md`, `AGENTS.md`, `GEMINI.md` e `.cursorrules` compartilham o mesmo núcleo
normativo. **Ao mudar uma lei ou a barra de status, atualize os quatro.** Divergência entre
eles já produziu agentes declarando sucesso inexistente.
