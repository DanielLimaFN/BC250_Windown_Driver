# P6 — Base MC nos endereços de VRAM das entradas de page table (candidato 3.83)

> **Data:** 2026-09-05 · **Classe:** offline
> **Origem:** corrida de 05/09 00:55, medição do `PT_WALK`

---

## 1. A medição que fechou o diagnóstico

```text
[SetRootPageTable] ROOT_MC: VramBase=0x450000000 + LocalSegBase=0xA90000 + Offset=0x0 -> PT=0x450A90000
[Vmid8Install] readback OK: PT_BASE=450a90000

[PT_WALK] RootMC=0x450A90000 VramMcBase=0x450000000 LocalSegBase=0xA90000 Janela=134217728 bytes
[PT_WALK IB] L1 idx=1 VRAM+0xA90008 PDE=0000000000A91061 -> leafMC=0xA91000 flags=0x61 valid=1
[PT_WALK IB] L0 folha: endereco 0xA91278 esta ABAIXO da base MC 0x450000000
[PT_WALK IB] L0 idx=79 VRAM+0xA91278 PTE=000000010721D0E7 -> pageMC=0x10721D000 flags=0xE7 valid=1
[PT_WALK IB] RESOLVIDO: VA=0x24F000 -> 0x10721D000 (root=MC, folha=RAW)
[PT_WALK IB] DIVERGENCIA DE ESPACO DE ENDERECO: root=MC folha=RAW
```

Dois resultados independentes:

**(a) A correção da root está provada.** `LocalSegBase=0xA90000` apareceu medido — igual ao
previsto por `0x20000000 - 0x1F570000` e a `2560*1080*4` arredondado para 64 KiB. O
primeiro nível agora decodifica um PDE real (`flags=0x61` = VALID|READ|WRITE). Antes da
correção o ponteiro de root apontava 0xA90000 bytes antes disso, dentro do framebuffer
reservado do GOP.

**(b) O defeito restante ficou medido, não inferido.** O PDE guarda `0xA91000` — offset
**cru** de VRAM. A GPU caminha em espaço MC: ela lê esse campo e busca a tabela folha no
MC `0x0000A91000`, que não é VRAM (a VRAM local começa em `0x450000000`). O segundo nível
cai fora, o fetch do IB falha, a engine drena o anel sem executar, e por isso a fence
**do anel** — que não depende de tradução — também nunca roda.

O nível folha estava correto: `flags=0xE7` traz o bit `SYSTEM` e `pageMC=0x10721D000` é
RAM do host. Endereço de memória de sistema **não** leva base de VRAM.

## 2. Correção

`Bc250McdmEncodeVidMmPteWithLevel` passa a somar a base MC de VRAM quando o endereço
aponta para VRAM:

| Caso | Antes | Agora |
|---|---|---|
| PDE (nível > 0) — tabela filha mora em VRAM | `LocalSegBase + off` | `VramMcBase + LocalSegBase + off` |
| PTE folha, `Segment == 1` (VRAM) | `LocalSegBase + off` | `VramMcBase + LocalSegBase + off` |
| PTE folha, `Segment == 0` (sistema) | `off` cru | `off` cru — **inalterado** |

`VramMcBase` entrou em `BC250_MCDM_RUNTIME_CAPS` como **fonte única**, consumida tanto
pelo codificador quanto por `Bc250McdmSetRootPageTable`. As duas não podem mais sair de
fase — que foi precisamente o defeito medido.

Fallback único e nomeado: `BC250_GCVM_VRAM_MC_BASE_FALLBACK = 0x450000000`.

## 3. Gate de tradução endurecido

Na corrida de 00:55 a interpretação `RAW` retornava sucesso para permitir ler os dois
níveis numa corrida só. O preço foi que a submissão seguiu, travou a engine e custou um
reboot. Agora `RAW` é **diagnóstico e recusa**: nomeia o defeito e não autoriza tocar o
silício.

## 4. Testes

Novo: **`P6_PTE_VRAM_MC_BASE_OFFLINE`** (`driver/tests/mcdm_callbacks_test.c`) — cobre os
três casos da tabela acima, incluindo a asserção de que `BC250_PTE_SYSTEM` continua
ligado e sem base no Segmento 0.

Regressão atualizada no mesmo arquivo: a asserção de `UpdatePageTable` passou de
`0x0000000000A8E025` (offset cru) para `0x0000000450A8E025`. **Ela pegou a mudança** —
é a evidência de que o teste cobre o caminho de verdade.

`driver\wdk\build-contract.bat` — **TODOS OS TESTES APROVADOS (0 falhas)** ·
`audit-mcdm-ddi-matrix.py` PASS · `audit-bc250mcdm-safety.py` PASS ·
`audit-ddi-noop-callbacks.py` APROVADO (0 falhas) · `audit-bc250mcdm-package.ps1` PASS.

```text
DriverVer:          3.81.0.0
Source SYS SHA256:  9BF15716BDB4543B3FDF58B1C50FDD7E207ACFA29E14BF04A8722ABCB8B06661
Signed SYS SHA256:  F5D060BA9BF0E00E06850995308C381592D0771C55BB525035EB5B1791B57984
CAT SHA256:         A245C2708A98FB308D7109D2772970D2BFE5E51C628F6E79D58C95F81965C0AB
```

## 5. Como ler a próxima corrida

| Observado | Significado |
|---|---|
| `PT_WALK IB RESOLVIDO ... (root=MC, folha=MC)` seguido de `IB_TRIPOD PASS` | tradução sob VMID8 fechou; a IB de paginação retirou |
| `RESOLVIDO (root=MC, folha=MC)` mas `IB_TRIPOD TIMEOUT` | as tabelas estão coerentes e o problema é outro — provavelmente o **valor** da base |
| `L0 folha: endereco ... ABAIXO da base MC` | a correção não alcançou algum caminho de escrita de entrada |
| `PT_WALK RECUSOU a submissao` | fail-closed funcionou; o BugCheck que vier é da recusa, não de engine travada |

**Ressalva aberta:** a caminhada pela CPU **não valida o valor** da base — ele cancela na
conta, porque lemos por offset. Segue de pé o desacordo entre `VramBaseOffset =
0x450000000` (leitor de topologia) e `BC250_SDMA_VRAM_MC_BASE = 0xF400000000` (usado
pelos blts provados por byte proof, que são endereçamento físico **sem** tradução — as
duas aperturas podem ser legitimamente diferentes). Consistência primeiro, porque
inconsistência é comprovadamente fatal; se ainda falhar, a próxima variável é o valor.

**Abandono:** se a máquina reiniciar antes de qualquer linha `PT_WALK`, parar e analisar
o dump.

## 6. Estado honesto

Tradução sob VMID8, byte proof do `FillVirtual`, composição do desktop, UMD 3D
(`CreateDevice = E_NOTIMPL`) e jogos acelerados — **NÃO VALIDADO EM HARDWARE**.
