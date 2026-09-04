# CLAUDE.md — Diretrizes de Operação para Claude Code

> **Governança de engenharia do repositório BC250-WinDv**
> *Target: AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 11 x64*
> **Última revisão: 2026-09-04**

---

## 🎯 0. Onde queremos chegar

**Rodar jogos no Windows 11 na BC-250**, usando as 24 CUs e os 16 GB de GDDR6, com o
monitor ligado ao DisplayPort da própria placa, via D3D12/Vulkan, **sem WARP e sem
fallback de software**.

**Fonte única de verdade sobre o que já existe e o que falta:**
[`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md) — Tabela 1 (já feito, com prova física) e
Tabela 2 (o que falta, em ordem de prioridade, com o teste que fecha cada item).
**Roadmap detalhado:** [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md).

**Decisão de arquitetura vigente:** display v1 é **por herança do UEFI GOP**. Não
implementamos DCN no caminho crítico. Escrita em registrador de DCN é **proibida** fora
da prioridade P9; leitura é permitida.

---

## 🛑 1. Barra de status obrigatória

Inicie toda proposta e análise com a barra abaixo. **Ela reflete medição, não desejo.**
Se você a atualizar, cada número deve ter evidência em `docs/evidence/`.

```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ██████████████████▋░  93%   Estavel e Isolado
SDMA (DMA Engine & Robustez)     ███████████████████░  95%   Arm provado no miniport de producao em 04/09 11:11 (RPTR=52, Fence=0x1001, 4/4 OK, 1 poll)
GCVM & Traducao de Memoria       ███████████████████░  95%   04/09 17:05: 135 UPDATE_PAGE_TABLE com journalStatus=0 (inclui pagina zero) e 2 FLUSH_TLB com AckObserved=1
RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
Telemetria & Observabilidade     ██████████████████░░  90%   Schema 134; telemetria ativa em Present, dirty rects e VidPn
Compute & Async Queues (MEC/MQD) ██████████████████░░  90%   P1 17/17 e P2 16/16 provados; fila persistente nao consome
Miniport WDDM Display+Render     █████████████████░░░  85%   Código 0 por 120 s sem BugCheck; primary nunca criada em 17:05; blt P5 nao fechado
=============================================================================================
Veredito Global: a corrida 04/09 17:05 provou no silicio a correcao da pagina zero -- 135 UPDATE_PAGE_TABLE
com journalStatus=0 (incluindo FirstPteVA=0x0, Num=16, que abortava em 16:47), dois FLUSH_TLB com AckObserved=1
e commit de journal SUCCESS. Nao houve BugCheck, nao houve TDR, nenhum evento de erro no log do sistema e o
fusivel anti-boot-loop NAO disparou; o dispositivo ficou em Codigo 0 os 120 s inteiros. A tela segue preta por
OUTRO motivo, medido: o dxgkrnl NUNCA cria a alocacao primaria. GetStandardAllocationDriverData devolvia
AllocationPrivateDriverDataSize=0, entao o VidMm abandonava a criacao antes de chamar CreateAllocation (0
ocorrencias, com log na entrada da DDI), comitava VidPN vazia com hPrimaryAllocation=NULL 7 vezes seguidas e
desligava a fonte. Corrigido em e458b8e reemitindo BC250_ALLOCATION_PRIVATE_V1 de 40 bytes, o mesmo formato que
funcionou as 12:23 (50 CreateAllocation, primary de 11.075.584 bytes, SetVidPnSourceAddress chamado).
Criacao da primary e blt de apresentacao com este binario — NAO VALIDADO EM HARDWARE.
Handoff completo: docs/HANDOFF_2026-09-04.md
=============================================================================================
```

> ℹ️ **O adaptador WDDM alcançou Código 0 em 02/09/2026** (itens 97 e 102 fechados com prova
> física arquivada em `docs/evidence/2026-09-02_21-58-02-DESKTOP-HQAG6T4-mcdm-display-binding/`).
> `DRIVERCAPS`, `QUERYSEGMENT4`, Paging Context e VidPn ativos por 121 s. A etapa atual é P5
> (pipeline de blt por SDMA no scanout GOP para manter tela visível).

---

## ⚖️ 2. Leis inegociáveis

### Lei 1 — Sinceridade absoluta
Reporte o que a medição diz, não o que seria bom. Se uma prova estiver ausente, escreva
literalmente **`NÃO VALIDADO EM HARDWARE`**. Nunca suavize, nunca arredonde para cima,
nunca chame hipótese de causa. Se você errou antes, corrija em uma frase e siga.

### Lei 2 — Tripé de validação no silício
Não declare sucesso, aceleração ou conclusão sem as três provas físicas:
1. `RPTR == WPTR` na engine correspondente;
2. fence física gravada **pela GPU** na VRAM, com verificação de poison;
3. dados destino conferidos **byte a byte** contra gabarito da CPU.

### Lei 3 — Todo item fecha com um teste, e o teste é nomeado
Ao propor ou concluir qualquer trabalho, diga **qual teste** comprova execução no hardware
e **onde a evidência será arquivada**. A Tabela 2 da `BASE_DE_AVANCO.md` já traz a prova
exigida de cada item — use-a. Trabalho sem teste nomeado não é entregável.

### Lei 4 — Zero fakes, zero fallbacks de software
- Nunca incremente fences ou timestamps pela CPU.
- Nunca use `KeStallExecutionProcessor`, `Sleep` ou loop de delay simulando execução.
- Nunca retorne `STATUS_SUCCESS` em DDI sem backend funcional.
- Nunca trate compilação, assinatura, `vulkaninfo`, enumeração de API, carregamento de DLL
  ou abertura de swapchain como prova de aceleração.
- Nunca proponha WARP, GDI, llvmpipe ou emulação por CPU disfarçada de GPU.

### Lei 5 — Ciclo de vida transacional
Toda interação de baixo nível com hardware:
```text
[PREFLIGHT] → [SNAPSHOT] → [APPLY] → [READBACK] → [PROVA NO SILÍCIO] → [ROLLBACK/TEARDOWN]
     |                                                    |                      |
     +---(abortar se inválido)                            +---(falhar fechado)---+
```

### Lei 6 — Fórmula de bits SOC15
```c
uint32_t val = (lido & ~mask) | (or_val & mask);
```

### Lei 7 — Endereços só com proveniência
Use apenas offsets catalogados em
[`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md), com fonte
citada. Nada em quarentena (`Q`). No DCN o endereço é
`DMU_BASE.seg[BASE_IDX] + reg` — **não existe base plana**
(`DMU_BASE = {0x12, 0xC0, 0x34C0, 0x9000}`).

### Lei 8 — Vocabulário de confiança
Classifique toda afirmação técnica: **P0** provado no silício · **P1** derivado do upstream
· **P2** parcialmente observado · **H** hipótese · **Q** quarentena (proibido).
Detalhe em [`rules.md`](rules.md).

### Lei 9 — Economia de corridas físicas
A corrida no hardware é o recurso escasso, e o **Daniel** é quem a executa — eu compilo,
analiso e preparo. Antes de propor ensaio: responda o que puder por leitura de fonte,
mineração de `docs/evidence/` e comparação com drivers reais da máquina. Defina teto de
corridas e critério de abandono **antes** de armar. Boot frio entre binds.

---

## ⚡ 3. Comandos rápidos

### Driver de laboratório (`bc250dbg.sys`)
- Build: `dbg\build.bat` · Instalar: `dbg\install.bat`
- Iniciar: `dbg\start.bat` · Parar/remover: `dbg\uninstall.bat`

### Driver WDDM de produção (`bc250mcdm.sys`)
```
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-package.ps1
python tools\audit-mcdm-ddi-matrix.py
python tools\audit-bc250mcdm-safety.py
python tools\audit-ddi-noop-callbacks.py
```
> ⚠️ O empacotador **não compila o SYS** — ele copia. Pular o build de laboratório assina
> o binário do candidato anterior.

### Ensaios físicos
```
powershell -ExecutionPolicy Bypass -File tools\capture-baseline.ps1
powershell -ExecutionPolicy Bypass -File tools\test-sdma-robustness.ps1 -Execute
powershell -ExecutionPolicy Bypass -File tools\test-gcvm-translate.ps1 -Execute -IAcceptTheRisk
```

### Análise de crash dump (exige PowerShell elevado)
```
& "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -z C:\Windows\MEMORY.DMP -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols" -c "!analyze -v; lm m bc250*; q"
```

### Recuperação de emergência

| Situação | Ação |
|---|---|
| Windows inicia | `tools\BC250_RECOVERY.cmd` (ou `C:\BC250_RECOVERY.cmd`) restaura o BasicDisplay |
| Windows inicia, limpeza completa | `tools\clean_all_drivers.ps1` elevado (DriverStore + SCM) |
| **Não inicia: tela preta ou loop de boot** | `tools\BC250_WINRE_CLEAN.cmd` pelo **Prompt de Comando do WinRE**, com o arquivo num pendrive. Ver [`RECOVERY_BC250V4.md`](RECOVERY_BC250V4.md) |

> ⚠️ **Nunca** use `del C:\Windows\System32\drivers\bc*.sys`. O padrão casa com
> `bcmfn2.sys`, driver **inbox da Microsoft**. E em WinRE o Windows raramente é `C:`.

---

## 💻 4. Padrões de código C (Windows kernel mode)

- **Dialeto:** C99/C11 compatível com MSVC x64 e WDK.
- **Rigor:** todo código passa limpo em `/W4 /WX` — zero warnings.
- **SAL:** `_In_`, `_Out_`, `_Inout_`, `_Check_return_`, `_Pre_null_`.
- **Sem CRT de user-mode:** proibido `malloc`, `free`, `printf`, `exit`.
  - Alocar: `ExAllocatePool2(POOL_FLAG_NON_PAGED, size, tag)`
  - Liberar: `ExFreePoolWithTag(ptr, tag)`
  - Memória: `RtlCopyMemory`, `RtlZeroMemory`, `RtlEqualMemory`
  - Log: `DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, ...)`
- **Defensivo:** checar ponteiros contra `NULL`; checar `NTSTATUS` com
  `if (!NT_SUCCESS(status)) { ... return status; }`; cleanup garantido em toda falha de
  alocação ou mapeamento.

---

## 📂 5. Mapa do repositório

| O quê | Onde |
|---|---|
| **Base de avanço (o que falta, prioridade, testes)** | [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md) |
| **Roadmap detalhado** | [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md) |
| Regras normativas e vocabulário de confiança | [`rules.md`](rules.md) |
| Catálogo de registradores | [`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md) |
| Provas físicas P1 / P2 | [`docs/148...`](docs/148_VITORIA_FASE_P1_VALIDADA_EM_HARDWARE.md) · [`docs/159...`](docs/159_VITORIA_HISTORICA_FASE_P2_VALIDADA_EM_HARDWARE.md) |
| Prova de ownership de display | [`docs/26...`](docs/26_POST_DISPLAY_OWNERSHIP_FISICO_1.1.0.0.md) |
| Por que render-only foi reprovado | [`docs/162...`](docs/162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md) |
| Corpus de evidência física | `docs/evidence/` |
| Miniport WDDM | [`driver/wdk/`](driver/wdk/) |
| UMD (D3D / Vulkan / runtime) | [`driver/umd/`](driver/umd/) |
| Driver de laboratório KMDF | [`dbg/`](dbg/) |
| ABI de diagnóstico | [`inc/bc250dbg_abi.h`](inc/bc250dbg_abi.h) |
| Harness, auditorias e scripts | [`tools/`](tools/) |
| Recuperação | [`RECOVERY_BC250V4.md`](RECOVERY_BC250V4.md) · `tools/BC250_RECOVERY.cmd` |
| Arquivo morto (nada apagado) | [`DocsObsoletos/INDICE.md`](DocsObsoletos/INDICE.md) |

---

## 🔄 6. Manutenção destes arquivos

`CLAUDE.md`, `AGENTS.md`, `GEMINI.md` e `.cursorrules` compartilham o mesmo núcleo
normativo. **Ao mudar uma lei ou a barra de status, atualize os quatro.** Divergência entre
eles já produziu, no passado, agentes declarando sucesso inexistente.
