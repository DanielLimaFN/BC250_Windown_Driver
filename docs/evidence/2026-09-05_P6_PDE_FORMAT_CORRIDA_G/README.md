# P6 — Corrida G: formato de PDE, base PA e root com VALID (candidato 3.86)

> **Data:** 2026-09-05 · **Classe:** offline
> **Origem:** revisão externa de `docs/167_CONSULTA_EXTERNA_IB_PAGINACAO_SDMA_VMID8.md`

---

## 1. O defeito que passou seis corridas

Formato de PDE do Navi 10 (comentário em `gmc_v10_0.c`, amdgpu):

```text
PTE:  47:12 endereco da pagina | 6 write | 5 read | 4 exe | 3 Z | 2 snooped | 1 system | 0 valid
PDE:  63:59 block fragment size | 54 P | 47:6 endereco da PD/PTE
      5:3 RESERVADO | 2 C | 1 system | 0 valid
```

Gravávamos o PDE com `VALID|READABLE|WRITEABLE = 0x61`. Num PDE:

- **bit 6 não é "writeable" — é o bit menos significativo do endereço.** O hardware
  buscava a tabela folha em `+0x40`, ou seja, **8 PTEs deslocadas**.
- **bit 5 cai em campo reservado.**

E a caminhada pela CPU não pegou porque `Bc250GcvmDecodePte` mascara com
`0x0000FFFFFFFFF000` — 4 KiB. Os bits `[11:0]` eram descartados e o log reportava
`0xF400A91000` limpo. **Validávamos um PDE com o decodificador de PTE.** O `PT_WALK`
deu falso positivo desde que existe.

Classificação: **P1** (formato derivado do upstream), defeito confirmado por leitura do
nosso próprio código.

## 2. Correções

| | O quê | Fonte |
|---|---|---|
| A | `Bc250GcvmEncodePde`/`DecodePde`: endereço `[47:6]`, flags restritas a `VALID`/`SYSTEM`/`SNOOPED`, `[5:3]` reportados | formato PDE Navi 10 |
| B | PDE codificado com **apenas `VALID`** | idem |
| C | Base volta a `GCMC_VM_FB_OFFSET << 24 = 0x450000000` | `docs/03_CATALOGO_DE_REGISTRADORES.md` linha 640 |
| D | `PT_BASE_LO` do VMID8 com bit `VALID` | `amdgpu_gmc_pd_addr()`, Vega10+ |
| E | Caminhada usa `DecodePde` no nível 1 e **recusa** se `[5:3]` ligados | corrige o falso positivo |

### 2.1 Sobre a base — erro meu, documentado

Na corrida F troquei a base para `0xF400000000` argumentando com o byte proof dos blts.
Aquela prova vale para acesso **físico**; o walker do GCVM opera em **PA**. O catálogo do
próprio repositório já dizia, linha 640:

```text
PA = offset_de_VRAM + (GCMC_VM_FB_OFFSET << 24)
```

Eu não conferi o catálogo antes de propor a troca. Custou uma corrida.

## 3. Instrumentação nova

- `ENGINE_STATE`: `SDMA0_STATUS1` (ocioso se `& 0x3FF == 0x3FF`), `FREEZE`, `STATUS3`.
  **`SDMA0_STATUS2_REG` ficou de fora** — o catálogo tem a máscara do campo `CMD_OP`
  (linha 542) mas **não o offset do registrador**, e a Lei 7 proíbe inferir endereço.
  Catalogá-lo é tarefa de documentação, não chute.
- `FENCE_VISIBILIDADE`: fence relida **depois** de HDP-invalidate, mais o `RPTR` de
  writeback do `Work[]`. O laço do boot tripod faz as duas coisas; este caminho não fazia
  nenhuma. Se a fence só aparecer após o invalidate, o problema é visibilidade para a
  CPU, não execução.
- `IB_CPU_PHYS` + `IB_PA_MATCH`: o `IB_DUMP` sempre veio da VA da CPU, nunca do PA para
  onde a PTE aponta — nunca provamos que a página que a GPU busca é a nossa. Agora o
  encoder publica `MmGetPhysicalAddress(pDmaBuffer)` e a submissão compara com o PA que a
  caminhada resolveu. Divergência **recusa** a submissão.

## 4. O que a revisão externa refutou

- **CSA / `MIDCMD_PREEMPT_ENABLE`** (hipótese anterior minha): refutada com a mesma
  referência por duas revisões independentes — `amdgpu.mcbp` vem desligado por padrão,
  `get_csa_mc_addr()` devolve 0 nesse caso, e o upstream liga `MIDCMD_PREEMPT_ENABLE=1`
  mesmo assim. CSA=0 é o caso normal. **Nada foi alterado**; desligar seria afastar-se do
  upstream sem motivo.
- **Colisão de `0x04A80` entre `HDP_FLUSH` e `UTCL1_INV0`**: **não existe**. Só
  `HDP_FLUSH` usa esse offset em `bc250_sdma_present.h`; não há nenhuma definição de
  `UTCL1_INV`. O ponto de fundo — nunca escrevemos os registradores de invalidação da
  UTCL1 da própria SDMA — permanece aberto, mas a colisão estava errada.

## 5. Testes

Novos: **`P6_PDE_FORMAT_OFFLINE`** (encode com `VALID`, recusa de flags de folha, decode
expondo o `+0x40` e o bit reservado, e o contraste com o decodificador de PTE que esconde
tudo) e **`P6_VMID8_ROOT_VALID_BIT_OFFLINE`**.

Regressões atualizadas: `BaseLowProgrammed` e `PT_BASE_LO` agora com `| 0x1`; PTE de VRAM
de volta a `0x0000000450A8E025`. **As três falharam antes de eu atualizá-las** — cobertura
real, não passagem por acaso.

Suíte completa **0 falhas** · `audit-mcdm-ddi-matrix.py` PASS · `audit-bc250mcdm-safety.py`
PASS · `audit-ddi-noop-callbacks.py` APROVADO · `audit-bc250mcdm-package.ps1` PASS.

```text
Signed SYS SHA256:  B95111D398F2FFEA36661BC1AAB559E921CBD9A729D39E1C5EBC89E6133CE1B1
CAT SHA256:         8851EACF481453757F232644A5C4006F801CE8DF7360C84D128B6A974427298A
```

## 6. Como ler a corrida G

| Observado | Significado |
|---|---|
| `L1 ... reservado[5:3]=0x0` e `IB_TRIPOD PASS` | tradução sob VMID8 fechou — primeira IB de paginação a retirar |
| `IB_PA_MATCH ... DIVERGE` | a GPU buscaria página diferente da nossa; gate recusa |
| `FENCE_VISIBILIDADE` com `depois_hdp` = esperado e `antes_hdp` = poison | era visibilidade para a CPU, não execução |
| `ENGINE_STATE` com `STATUS1 & 0x3FF == 0x3FF` | engine ociosa no timeout — parou, não travou |
| `reservado[5:3]` ainda não-zero | a correção não alcançou algum caminho de escrita de entrada |

**Abandono:** se reiniciar antes de qualquer linha `PT_WALK`, parar e analisar o dump.

## 7. Estado honesto

São cinco correções numa corrida só, e assumo isso. Nenhuma é hipótese concorrente: A, B,
C, D são defeitos com fonte documentada, e E é correção de instrumentação que estava
mentindo.

Execução de IB de paginação sob VMID 8, byte proof do `FillVirtual`, composição do
desktop, UMD 3D e jogos acelerados — **NÃO VALIDADO EM HARDWARE**.
