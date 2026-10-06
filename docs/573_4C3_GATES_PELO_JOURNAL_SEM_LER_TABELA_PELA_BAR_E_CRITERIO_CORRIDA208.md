# 573 — 4c.3: gates de submissão pelo journal, sem a CPU ler tabela de página pela BAR, e o critério da Corrida 208

**Data:** 02/10/2026 · **Tipo:** implementação offline da etapa 3 da 4c ([doc 567](567_4C_COMPLETA_EM_5_ETAPAS_E_CRITERIO_CORRIDA205.md))
**Antecede:** [doc 572](572_CORRIDA207_L1_SEM_0x34_TRAVA_DURA_AOS_96S_NO_ZERAMENTO_COM_4C2_ARMADA.md) (Corrida 207: desfecho B; a 4c.2 não tira a trava; a última linha do KMD antes da parada é uma caminhada de tabela pela BAR).

---

## 1. O que a CPU lia na submissão (P0, leitura do fonte)

| Caminho | Leitura pela BAR, a cada submissão | Para quê |
|---|---|---|
| Node 0 (paginação) | Caminhada de **todas as páginas do IB**, mais primeira e última página da **origem** e do **destino** | Gate do IB; PA do IB para casar a prova de paginação; `destPhysStart` (só o PLANO A, desligado) |
| Node 1 gráfico | Caminhada de **todas as páginas do IB** (gate físico); inspeção do IB (amostrada); **fingerprint do IB inteiro** (lendo os dados do IB da VRAM, a cada submissão, porque o `GfxWaveProbe` está armado); DW0 do IB (DX.43) | Gate; diagnóstico; aquecimento na troca de VMID |
| Node 1 compute | Caminhada de todas as páginas do IB | Gate |

Na 207 isso deu 655 caminhadas de IB, mais 652 de origem e 652 de destino só no Node 0, e mais as 419 submissões gráficas. Tudo **enquanto o SDMA escrevia na VRAM**.

**Precedente (DX.97, Corrida 158):** no instante da trava, uma CPU escrevia PTE pela BAR e outra **caminhava a tabela** para validar um IB. A DX.97 reduziu as caminhadas de origem e destino às pontas; a 4c.3 tira o resto.

## 2. O que muda (uma variável: leituras da CPU sobre tabela e IB no submit)

- **`Bc250ActiveJournalResolveRange`**: resolve a **primeira e a última página** do IB pelo **journal de software** (snapshot publicado, adquirido e **solto na hora**), sem ler a VRAM.
  - Medido na 207 (P0, 4 de 4 amostras do Node 0): para IB na memória do sistema (segmento 0), o offset do journal é **idêntico** ao PA que a caminhada achava.
  - Para o segmento 1, o MC sai pela mesma conta do encoder de PTE (`VramMcBase + LocalSegmentBaseOffset + offset`). Isso é P1 do código; nenhuma amostra de VRAM foi conferida.
- **Node 0:** o gate do IB passa pelo journal. A prova de paginação continua casada pelo PA (vindo do journal). As caminhadas de **origem e destino saem**; `destPhysStart` fica 0, o que só afeta o PLANO A (desligado).
- **Node 1 (gráfico e compute):** `Bc250ActiveSubmitIbGate` (gate pelo journal). Saem a **inspeção** e o **fingerprint** do IB (o `KMD_GFX_SUBMIT` passa a sair com `hash_complete=0`). O **DW0 do DX.43 continua sendo lido**, mas com o cache do leitor semeado pelo journal, então sem caminhar a tabela (sobra uma leitura de 4 bytes da VRAM por submissão, declarada).
- **A lição da C3.17 manda no desenho:** o snapshot do journal **já falhou como autoridade** (`BUSY`, `NO_SPACE`, 12/09). Por isso **o journal nunca recusa sozinho**: se não resolver, a caminhada antiga roda e decide, e isso fica **contado e logado** (`[PT_JOURNAL] … NAO resolvido pelo journal … #N`, 8 primeiras e a cada 256).
- **Tunable `PtWalkOnSubmit`** (padrão 0). Com 1 no registro, volta o comportamento das 204–207 sem reinstalar. O `StartDevice` loga o modo (`[PT_JOURNAL] PtWalkOnSubmit=…`).

O resto é igual à 207: L1, BPG1, LockUs e 4c.2 armada.

**Risco declarado:** sem a caminhada, um PTE que o journal acha válido mas que no silício está errado deixa de ser recusado antes do envio. Ele vira fault de GPU ou TDR (o `GFXHUB_FAULT` continua instrumentado), e não mais uma recusa.

**Testes e portões:**
- `build-contract` EXIT=0, auditoria de segurança PASS, lab package audit PASS.
- O portão C3.17 (`audit_node1_physical_gpuva_gate`) exigia a chamada literal da caminhada no Node 1. Ele foi **reapontado** ao gate novo, passou a exigir a **caminhada de reserva** quando o journal falha, a conferência da **última** página e a **liberação** imediata do snapshot.
- **Duas mutações verificadas**, ambas reprovadas: tirar a caminhada de reserva; o journal conferir só a primeira página.
- Armadilha registrada outra vez: o heredoc do Bash transformou `\n` em quebra de linha real dentro de duas strings C (memória `bash-heredoc-corrompe-backslash`). O compilador pegou; corrigido com o Edit.

**Pacote (assinado, auditado, NÃO INSTALADO):** lab **3.355.1.0**.
- SYS `11C53A57…8468`, INF `F2ECA621…5E28`, CAT `3E56E704…84F6`; UMD `A1BD1657…` e RADV `5447A17D…` iguais às da 207.
- Strings conferidas no SYS: `[PT_JOURNAL] PtWalkOnSubmit=%u`, `NAO resolvido pelo journal` e o nome do tunable.
- Preflight `prepare-corrida208-4c3-gates-pelo-journal.ps1` aprovado.

## 3. Critério da Corrida 208 (escrito antes, Lei 10)

**Roteiro:** recuperação, boot frio, CMOS em 256 MB, `tools\EXECUTAR_CORRIDA208_4C3_GATES_PELO_JOURNAL_600S.cmd`, mesmo uso das 201–207. Se travar: botão de reset e, no boot seguinte, **`tools\coletar-pos-trava.ps1` elevado ANTES da recuperação**.

**Sinais:** `[PT_JOURNAL] PtWalkOnSubmit=0`; quantidade de `[PT_JOURNAL] … NAO resolvido` (caminhadas de reserva); `[PT_RANGE` (só deve aparecer nas de reserva); `GFXHUB_FAULT`, `TDR_QUIESCE`, recusas de gate; trava e BPG1.

| Desfecho | Assinatura (o que o log mostra) | Implicação (para onde vai) |
|---|---|---|
| **A** | `PtWalkOnSubmit=0`, **poucas ou nenhuma** caminhada de reserva, **~600 s sem trava**, sem fault novo | Compatível com a leitura da tabela pela CPU como causa (n=1; repetir antes da 4c.4) |
| **B** | `PtWalkOnSubmit=0`, poucas caminhadas de reserva, **trava dura** com a assinatura de sempre | A leitura da tabela pela CPU no submit não é causa suficiente. Seguir para a **4c.4** (PTE pelo SDMA, `GPU_VIRTUAL`) |
| **C** | Aparecem `GFXHUB_FAULT`, TDR real ou recusas de gate novas | O journal aceitou algo que o silício não tinha. Voltar offline; `PtWalkOnSubmit=1` reverte sem reinstalar |
| **D** | **Muitas** caminhadas de reserva (o journal falha com frequência, como na C3.17) | A CPU continuou lendo a tabela; a corrida não testou a 4c.3. Primeiro tornar o journal confiável |
| **E** | Algo fora do mapa | Descrever antes de julgar |

**Regras:**
- Desfecho parcial não conta. A e B exigem que as caminhadas de reserva sejam **raras**; se não forem, é D.
- "Raras" fica definido agora: até 1% das submissões, contadas pelo `IB EXECUTADO` do Node 0 mais o `KMD_GFX_SUBMIT`.

**NÃO VALIDADO EM HARDWARE.**
