# Critério da Corrida 52 — DX.7 fatia mínima: o mapa de VMID por contexto

> **Escrito ANTES do ensaio** (Lei 9). Candidato **lab 3.213.1.0**.
> Perfil `DISPLAY_RENDER_LAB` (RenderNode=1, RenderOnlyLab=0), WDDM 2.7,
> modo **P1 PERSISTENT SUBMISSION** — o mesmo perfil da Corrida 51.
> **NÃO VALIDADO EM HARDWARE.**

---

## 0. O que esta corrida NÃO é

Esta faixa **não conserta nada**. A submissão GFX continua indo em
`BC250_GCVM_USER_VMID` (VMID 7), exatamente como nas Corridas 44–51.

Isso é deliberado e está travado por portão. Se eu trocasse o VMID da submissão
junto com o mapeamento, um mapa **errado** produziria exatamente o mesmo sintoma
do defeito atual — fault de tradução com `WALKER_ERROR` — e a corrida não saberia
dizer qual dos dois falou. Primeiro a medição prova o mapa; depois a submissão
passa a usá-lo.

**Portanto: esperar o mesmo fault, o mesmo TDR e a mesma tela preta.**
Qualquer melhora visual nesta corrida deve ser atribuída ao rollback/BasicDisplay
até prova em contrário.

---

## 1. A pergunta única

As Corridas 48 e 49 mediram divergência de raiz em **5 de 5** comparações, com
**6 raízes distintas** em 82 submissões e o **mesmo IB sob três raízes**. Isso
prova que a raiz do VMID 7 muda por baixo das submissões. **Não prova por quê**,
e as duas leituras possíveis exigem consertos opostos:

| Leitura | O que significa | Conserto |
|---|---|---|
| **Vários contextos** disputam o VMID 7 | cada um instala a sua raiz por cima do outro | VMID por contexto (o alocador) |
| **Um contexto** troca de raiz sozinho | a tabela dele é republicada durante o voo | sincronizar publicação, e o alocador **não ajuda** |

Os carimbos de raiz não distinguem as duas: ambas produzem o mesmo número de
divergências. O mapa novo distingue, porque conta **contextos**, não raízes.

---

## 2. Como ler o log

Linhas novas, todas com o prefixo `[VMID_POOL]`:

```
[VMID_POOL] INICIALIZADO: N slots livres de 16 (reservados 0=MEC 7=usuario 8=paginacao 9=compute)
[VMID_POOL] ATRIBUIDO:    Ctx=... -> VMIDnn (Alocados=N InUseMask=0x....)
[VMID_POOL] LIBERADO:     Ctx=... devolveu VMIDnn
[VMID_POOL] MAPA:         Ctx=... -> VMIDnn Raiz=0x... Geracao=N      (uma por slot ocupado)
[VMID_POOL] MAPA NO FAULT: Ocupados=N Alocados=N Liberados=N Exaustoes=N InUseMask=0x....
```

`Ocupados` é o número que responde a corrida. Ele é **contado percorrendo os
slots** no instante do fault, não acumulado — um portão reprova se virar constante.

---

## 3. Desfechos

Cada desfecho tem **assinatura** (o que o log mostra) e **implicação** (para onde
a investigação vai) em campos separados, e desfecho parcial **não é desfecho**:
se a assinatura exige dois sinais e aparece um, o desfecho **não ocorreu**.

### A — Disputa entre contextos CONFIRMADA

**Assinatura:** `MAPA NO FAULT` com `Ocupados >= 2`, **e** pelo menos duas linhas
`[VMID_POOL] MAPA:` com `Ctx=` diferentes, **e** ao menos duas dessas linhas com
`Raiz=` diferentes entre si.

**Implicação:** o alocador é o conserto certo. A próxima faixa liga a submissão ao
VMID do pool (`Bc250GcvmInstallVmid(poolVmid, ...)` e o mesmo `poolVmid` no
`INDIRECT_BUFFER`, que já o codifica por pacote) e o portão de escopo da faixa
atual é revisto junto, de propósito.

### B — Disputa entre contextos REFUTADA

**Assinatura:** `MAPA NO FAULT` com `Ocupados == 1`, **e** as linhas
`[TDR_GCVM_FAULT] CARIMBO` continuando a mostrar `(RAIZ DIFERENTE)`.

**Implicação:** as raízes trocam **dentro de um único contexto**. O alocador não
resolveria nada e a faixa DX.7 vira trabalho arquivado, não descartado — o
suspeito passa a ser a republicação de tabela durante o voo, e o próximo alvo é
o par `Bc250GpuvaSnapshotPublish` / `SetRootPageTable` com os contadores
`LastPublishMapCount`/`LastPublishUnmapCount` que já existem.

**Este é o desfecho que eu não quero, e por isso está escrito primeiro no log.**
Três hipóteses minhas já morreram nesta investigação por eu só ter previsto o
desfecho que queria.

### C — O mapa não existe

**Assinatura:** nenhuma linha `[VMID_POOL] MAPA NO FAULT` no log.

**Implicação:** duas causas possíveis, distinguíveis pelas outras linhas:
- Há `[VMID_POOL] INICIALIZADO` e há `[TDR_GCVM_FAULT] DECODIFICADO`, mas não há
  mapa → defeito meu na ligação, no sítio do fault.
- Não há `[TDR_GCVM_FAULT] DECODIFICADO` → **não houve fault nesta corrida**, e
  então a pergunta não chegou a ser feita. Não é falha do driver; é corrida sem
  o evento. Ver desfecho E.
- Não há `[VMID_POOL] INICIALIZADO` → o SYS em execução não é o 3.213.1.0.
  Conferir o hash contra `831C79B7...` antes de qualquer conclusão.

### D — Exaustão do pool

**Assinatura:** uma ou mais linhas `[VMID_POOL] EXAUSTAO #N`, ou
`MAPA NO FAULT` com `Exaustoes > 0`.

**Implicação:** existem mais de 12 contextos simultâneos submetendo GFX, e VMID
por contexto **não escala como desenhado**. O conserto passa a exigir reciclagem
por LRU com invalidação de TLB, que é substancialmente mais caro e precisa de
critério próprio. Nesta faixa a exaustão **não** altera comportamento — a
submissão já ia em VMID 7 de qualquer forma — e o log declara isso na própria
linha.

### E — Algo fora do mapa

**Assinatura:** qualquer coisa que não caiba em A–D.

**Instrução:** **descrever o observado antes de julgá-lo.** Na Corrida 39 o maior
resultado do ensaio não estava em nenhuma das quatro letras previstas, porque o
inesperado tinha sido tratado só como "controle a não regredir". Casos concretos
a observar sem enquadrar às pressas:

- `Ocupados` alto mas `Alocados` muito maior que `Liberados` → vazamento de slot,
  que seria defeito meu no `DestroyContext`, não medição do silício.
- `Geracao` crescendo num mesmo VMID → contexto sendo recriado com o mesmo
  endereço de handle, o que tornaria a contagem de contextos otimista.
- Sumiço do fault, ou BugCheck novo.

---

## 4. Controles que não podem regredir

Não são o objeto do ensaio, mas se caírem o resultado não vale:

| Controle | Estado nas corridas recentes |
|---|---|
| BugCheck | **zero** em 7 corridas seguidas |
| `[GCVM_INVENTARIO]` no bring-up | presente, 16 contextos lidos |
| `[GCVM_INVENTARIO_2]` veredito | `offsets DISTINTOS, inventario CONFIRMADO` |
| Submissões GFX indiretas | anel consome, `RPTR` avança |
| Rollback | online e limpo |

---

## 5. Procedimento

1. **Boot frio obrigatório** — estado de engine não cai com desbind.
2. Conferir o fusível (`tools\inspect-boot-fuse.ps1`), resetar se `TRIPPED`.
3. Bind do candidato **3.213.1.0**, SYS `831C79B7...`.
4. Coleta `-FINAL` já está no caminho comum do binder; a cauda do log cumulativo
   é resgatada automaticamente antes do bind seguinte.

---

## 6. Proveniência do candidato

| Artefato | SHA-256 (início) |
|---|---|
| SYS empacotado e assinado | `831C79B70CE4EB7C...` |
| PDB | `BB377C98154CDA55...` |
| UMD | `934DBD84E04F1002...` |
| INF | `4CF3837FAE78DC6B...` |
| CAT | `8F0A04EDB626DCF3...` |

`Lab package audit: PASS`. Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, P1 PERSISTENT
SUBMISSION, schema 135.

**Binários conferidos por string** (o empacotador não compila, só copia), em
ANSI dentro do SYS assinado: `[VMID_POOL] INICIALIZADO`, `[VMID_POOL] ATRIBUIDO`,
`[VMID_POOL] EXAUSTAO`, `[VMID_POOL] LIBERADO`, `[VMID_POOL] MAPA NO FAULT`,
`NAO usado na submissao nesta faixa`, `PRECISA recusar a submissao`.

---

## 7. Estado offline desta faixa

- `build-contract.bat` **EXIT=0**, com `VMID POOL: OK (12 slots, reservados
  0/7/8/9 medidos, exaustao falha fechada)`
- Três auditorias **PASS**; `git diff --check` limpo
- **Nove mutações verificadas**: quatro no contrato (reservado entrando no pool,
  exaustão caindo no VMID 7, VMID 0 perdendo a reserva, contador removido) e
  cinco na ligação (aquisição removida, raiz não registrada, slot não devolvido,
  inicialização removida, contagem do mapa virando constante). **Todas reprovam.**

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
