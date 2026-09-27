# 304 — Critério prévio da Corrida 45 — onda viva com o CP parado

> **Data:** 20/09/2026, escrito **antes** do ensaio.
> **Candidato:** lab `3.205.1.0`, `DISPLAY_RENDER_LAB` (RenderNode=1, RenderOnlyLab=0),
> WDDM 2.7, schema 135, modo **P1 PERSISTENT SUBMISSION**.
> *(Substitui o 3.204.1.0, que nunca foi instalado: a faixa D3 entrou depois e pegou carona.)*
> **Estado:** assinado e auditado; **NÃO INSTALADO; NÃO VALIDADO EM HARDWARE**.

## 1. Mudança isolada

Uma variável move em relação ao 3.202.1.0, que rodou a Corrida 44: **`GfxWaveProbe=1`**.
Todo o resto do perfil é idêntico, conferido campo a campo contra o manifesto commitado do
3.202 — `RenderNode`, `ShaderDispatchEnabled`, `CpSoftResetEnabled`, `SdmaAutoCtxSwEnabled`,
`PersistentSubmissionEnabled` e `ProfileMode` não mudaram.

Viajam junto duas faixas que são **somente telemetria** e não tocam caminho de GPU:

- **D1** — a perna 3 do tripé GFX parou de prometer um delegado inexistente, e o veredito passa
  a publicar `Submetidas`, `Aposentadas`, `EmVoo` e `RetiredCount`.
- **D3** — `[ActiveFlushTlb] PUBLICACAO_COMPOSICAO` passa a publicar `Maps`, `Unmaps` e os
  slots livres do lote de GPUVA. Contagem pura na entrada da publicação; zero mudança de
  comportamento.

## 2. Por que esta corrida existe

Na Corrida 44, no **5º TDR**, apareceu `SQ_DEBUG_STS_GLOBAL=0x00000011 Busy=1 WaveLevelSA0=1`:
**onda viva com o CP parado**. Nos outros quatro TDRs, e em toda a Corrida 43, era `Busy=0`
com zero ondas. Pela legenda do próprio log, isso aponta **shader que não retira** — o primeiro
discriminante entre "estado ou pacote" e "shader" desde que o stall em `RPTR=24/WPTR=32`
apareceu.

E a medição que responderia **não foi feita**: nos cinco TDRs a sonda indireta saiu
`[TDR_GFX_WAVEPC] DESARMADO: GfxWaveProbe=0 present=1`, inclusive no único TDR com onda. Com a
sonda armada, ela lê por onda `SQ_WAVE_STATUS`, `PC_LO/HI`, `IB_STS`, `INST_DW0` e `TRAPSTS` —
ou seja, **em que instrução a onda está parada**.

## 3. Risco declarado

A sonda **escreve registrador**: `GRBM_GFX_INDEX` fora de broadcast e `SQ_IND_INDEX`, ambos
seletores. Se a restauração do broadcast falhar, o soft-reset que o quiesce dispara logo em
seguida pode atingir **um único shader engine**. Mitigações já no código, verificadas por
leitura nesta preparação: a restauração é feita com **readback**, e falha fechada em
`SELECTOR_STUCK`; a sonda só corre quando o SQ disse que **há** onda; e `Bc250WaveProbeStatusIsValid`
recusa leitura inválida em vez de interpretá-la.

Os offsets foram conferidos **um a um** contra `inc/linux_ref/gc_10_1_0_offset.h` e catalogados
em `docs/03` nesta preparação — os seis `ix` batem exatamente, e os dois `mm` de idx0 dão base
implícita idêntica de `0x1260` dwords, a mesma já catalogada.

## 4. Hipótese falsificável

**H45:** o stall do IB GFX em `RPTR=24/WPTR=32` é **shader que não retira**, e não estado ou
pacote. Se for, quando o TDR ocorrer com `Busy=1` a sonda nomeia PC e instrução da onda parada.

## 5. Sucesso da faixa — o que fecha H45

A faixa é de **medição**, não de correção. Sucesso é **obter a medida**, não consertar o stall:

1. `[TDR_GFX_WAVEPC]` sai **ARMADO** — a linha `DESARMADO: GfxWaveProbe=0` não aparece;
2. em pelo menos um TDR com `Busy=1`, a sonda executa e publica PC, `INST_DW0`, `IB_STS` e
   `TRAPSTS` de pelo menos uma onda;
3. `SelectorRestored=1` — o broadcast volta, com readback, em **todas** as execuções da sonda;
4. a faixa D1 aparece: o veredito do tripé traz `TRIPE INCOMPLETO, 2 de 3 pernas` e os
   contadores `Submetidas/Aposentadas/EmVoo`.

**Não** faz parte do sucesso: o stall desaparecer, haver desktop, ou haver pixel.

## 5b. Segunda pergunta, respondida pela mesma corrida

`[ActiveFlushTlb] PUBLICACAO_COMPOSICAO Maps=N Unmaps=M Livres=L`. Na Corrida 44 as seis
falhas de publicação tiveram `FailedIndex` **exatamente igual** a `Reusable`, seis de seis,
sempre com `Mapped + Reusable = Cap = 1024` — o que fecha o mecanismo (o slot inativo é cópia
do ativo e o lote entra por cima) e **refuta** o diagnóstico anterior de exaustão do teto de
256 slots, já que `Teardown=0` e a reciclagem está sã.

O que falta é a composição do lote, e ela decide o conserto:

- **`Maps > Livres`** → é capacidade real; reordenar não resolveria nada, e a discussão passa a
  ser o teto de 1024 contra a pressão real do DWM.
- **`Unmaps` altos** → a **ordem** é o defeito: aplicar unmaps antes de maps derruba o pico sem
  mudar o estado final, porque o journal garante intervalos não sobrepostos e operações
  disjuntas comutam.

Nenhum dos dois é declarado agora, de propósito: propor a reordenação sem esse número seria fix
de efeito imprevisível.

## 6. Controle a não regredir

O 3.205.1.0 deve reproduzir a Corrida 44 no que ela conquistou. Registrar, sem que rebaixe o
veredito por si só:

- `[TDR_GFX_RESTART_BASE] Pass=1` e `[TDR_GFX_RESTART_PROOF] PASS` continuam saindo;
- submissões GFX na casa das centenas, com fence da GPU acompanhando;
- ausência de BugCheck.

Se algum destes regredir, a sonda passa a ser suspeita de ter causado, e isso é informação.

## 7. Falha informativa — assinatura e implicação separadas

| # | Assinatura no log | Implicação |
|---|---|---|
| A | `[TDR_GFX_WAVEPC] DESARMADO` de novo | O tunable não chegou ao StartDevice. O defeito é de empacotamento/ordem de registro, não de silício — mesma classe do doc 186. A corrida não testou H45. |
| B | Armado, mas **nenhum TDR com `Busy=1`** | A condição não reapareceu. H45 fica **sem teste**, não refutada: na Corrida 44 ela ocorreu 1 vez em 5. Registrar quantos TDRs houve e o `SQ_DEBUG_STS_GLOBAL` de cada um. |
| C | Armado, `Busy=1`, e a sonda publica PC/instrução | **H45 testável a sério.** O PC e o `INST_DW0` dizem onde a onda parou; `TRAPSTS` diz se há exceção. Daqui sai a próxima faixa. |
| D | `SELECTOR_STUCK` ou `PoisonReads != 0` | A sonda não conseguiu restaurar broadcast ou leu lixo. **Desarmar antes de qualquer outra corrida** e tratar como defeito da sonda, não do alvo. |
| **E** | **Qualquer coisa fora de A–D** | **Descrever o observado ANTES de julgá-lo.** Inclui BugCheck de qualquer código, regressão do controle da §6, ondas em SA1, número de ondas muito maior que 1, e **qualquer resultado melhor que o previsto** — a Corrida 44 caiu aqui e foi o maior achado dela. Não forçar em A–D. |

## 8. Desfecho parcial não é desfecho

Lei 10, regra 3. Sonda armada sem leitura publicada **não** é sucesso parcial: é desfecho A ou
B, conforme a assinatura. Não existe "C parcial".

## 9. Falha de contrato

- declarar H45 fechada sem PC e instrução publicados;
- deixar a sonda armada depois de `SELECTOR_STUCK`;
- tratar ausência de TDR com `Busy=1` como refutação de H45;
- perder a cauda do KMD;
- tratar BasicDisplay ou reboot como recuperação do **nosso** adaptador;
- alterar este critério depois de ver o resultado.

## 10. Sequência física

Estado na preparação: BC-250 em BasicDisplay `Status OK`, fusível **inexistente** (próximo boot
assume CLEAN), sem reboot pendente. Ainda assim, **boot frio é obrigatório** — estado de engine
não cai com desbind.

```powershell
& 'C:\BC250_RECOVERY.cmd'
powershell -ExecutionPolicy Bypass -File .\tools\inspect-boot-fuse.ps1 -Reset
shutdown /s /t 0
```

Depois do boot frio:

```powershell
.\tools\EXECUTAR_D3D11_NATIVO.cmd -CaptureDwmDump
```

O binder agora **recusa** um candidato com perfil mais fraco que o do último bind, nomeando a
capacidade que caiu. Esse preflight nasceu desta preparação: o primeiro pacote que gerei era
`DISPLAY_LAB` em modo READ-ONLY e teria gasto a corrida medindo o nada.
