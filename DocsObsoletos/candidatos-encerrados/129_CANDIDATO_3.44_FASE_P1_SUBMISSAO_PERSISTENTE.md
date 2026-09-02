# Candidato 3.44.0.0 (SchemaVersion 115) — Fase P1: submissão persistente

**Data**: 30/08/2026
**Estado**: construído e testado offline. **NÃO VALIDADO EM HARDWARE.**
**Tunable que liga a fase**: `PersistentSubmission` (desligado por padrão).

---

## 1. O problema que este candidato resolve

Tudo o que foi provado no silício até o 3.43 é de **tiro único**: o bind programa
a fila, submete um lote fixo, confere o tripé e reverte tudo. O pipeline do
Windows exige o oposto — a fila fica viva e cada `DxgkDdiSubmitCommandVirtual`
anexa um Indirect Buffer novo.

Até aqui, o executor de submissão era literalmente isto:

```c
InterlockedIncrement((volatile LONG *)&context->SubmissionsCount);
return STATUS_SUCCESS;   /* nenhum anel, nenhuma engine */
```

Um DDI devolvendo sucesso sem backend — exatamente o que a Lei 3 do
[`CLAUDE.md`](../CLAUDE.md) proíbe. Este candidato substitui isso por um caminho
real e mantém o resto do projeto intacto.

## 2. O que mudou

### 2.1 Contrato do anel persistente (offline, testado)

[`driver/core/bc250_persistent_ring_contract.c`](../driver/core/bc250_persistent_ring_contract.c)
é a máquina de estados do anel que sobrevive entre submissões: cursores,
espaço livre, quebra de volta e contabilidade de fence.

Duas decisões que valem registro:

- **Nenhum pacote cruza o fim do anel.** Quando o lote não cabe até o fim, o
  plano preenche o rabo com `PACKET2` (`0x80000000`), o NOP de **um** dword, e
  recomeça no offset zero. É a lição de
  [`bc250_pfp_direct_load.c:1037`](../driver/wdk/bc250_pfp_direct_load.c:1037):
  um `PACKET3` NOP declara `COUNT=n-2`, ou seja, dois dwords, e o CP se recusa a
  consumir pacote incompleto — foi a origem determinística do `RPTR=15` contra
  `WPTR=16` em 40/40 ciclos.
- **A fence avança exatamente um por submissão.** É isso que torna a
  aposentadoria não ambígua a partir dos 32 bits que a GPU grava, sem tabela de
  busca. Um valor fora da janela em voo — o poison `0xA55A0FF0`, por exemplo —
  é classificado como **corrupção**, não como progresso, e marca o motor com
  falta.

### 2.2 Modo persistente na transação provada

[`bc250_compute_mqd_contract.c`](../driver/core/bc250_compute_mqd_contract.c)
ganhou `BC250_HQD_TRANSACTION_PERSISTENT_ACTIVATE`, que sobe a fila **vazia** e
**não** executa o rollback — quem chama vira dono do teardown.

Para não existirem duas implementações de restore, o estágio 10 (dequeue +
rollback LIFO) foi extraído para `Bc250HqdRunRestoreStage()` e as ~48 variáveis
locais de snapshot viraram `BC250_HQD_PERSISTENT_STATE`. **O caminho de tiro
único usa exatamente a mesma função, na mesma ordem.** O
`compute_mqd_contract_test`, que exercita a transação observável inteira
incluindo restore, continua passando.

Com `PersistentSubmission=0` o comportamento é o do 3.43, sem desvio.

### 2.3 Motor de submissão

[`driver/wdk/bc250_submission_engine.c`](../driver/wdk/bc250_submission_engine.c)
é o dono do estado persistente: bloco de 128 KiB em VRAM, mapeamento, PTEs do
Contexto 0, fila ativa e cursores. O mapa de memória é **o mesmo do 3.43**, para
que a fila persistente veja a topologia que o tripé físico já validou.

A publicação do cursor faz três coisas, **telemetradas separadamente**:

1. escreve o WPTR no shadow em memória (a fonte que o CP lê por polling);
2. toca o doorbell na abertura BAR2;
3. escreve direto no `CP_HQD_PQ_WPTR_LO` do banco selecionado.

Isso é deliberado. A evidência do 3.43 mostra `pre_active_wptr_readback=0` com
`post_active_wptr_observed=45`: o WPTR **não** assentou por escrita direta, e
mesmo assim a fila andou — foi o CP que o buscou no shadow via `QUEUE_MASK`.
Como ainda não se sabe qual caminho é o que vale em regime permanente, os três
são exercidos e cada um é contado. O ensaio físico dirá qual moveu a engine.

### 2.4 Auto-teste

Um `DxgkDdiSubmitCommandVirtual` de verdade precisa de cliente de modo usuário,
que só chega com o UMD (P4). Para provar o **motor** antes disso, o auto-teste
submete **um** Indirect Buffer pelo mesmo caminho persistente — e o conteúdo
desse IB é exatamente a sequência de dispatch aprovada no 3.43.

O tripé fica **mais forte** que o do 3.43, não mais fraco:

| Prova | Onde | Valor esperado |
|---|---|---|
| Fence do dispatch, gravada pela GPU | `+0x2300` | `0xA55A0FF0` → `0xBC250D15` |
| Fence de sequência do anel, gravada pela GPU | `+0x2200` | `0` → `1` |
| Resultado do shader, byte a byte | `+0x5000` | `0x43DAFFFE` → `0xBC250C01` |
| Cursor drenado | anel persistente | `RPTR == WPTR == 12` |

As duas fences vivem em endereços distintos de propósito: se compartilhassem
endereço, uma sobrescreveria a outra e a aposentadoria do anel leria lixo.

## 3. O que este candidato **não** faz

- **Nó SDMA (ordinal 0)**: sem anel persistente ainda. `Submit` devolve
  `STATUS_NOT_SUPPORTED` — recusa honesta, não sucesso falso.
- **Caminho fim-a-fim do Dxgkrnl**: o DDI está ligado ao motor, mas nada no
  Windows submete até existir UMD. O auto-teste prova o motor, não o pipeline.
- **P2, P3 e P4**: intocados. `PreemptCommand` e `ResetEngine` continuam
  devolvendo `STATUS_SUCCESS` sem tocar em registrador, e `ResetFromTimeout`
  continua `STATUS_NOT_SUPPORTED`. Isso é dívida conhecida, registrada aqui para
  não ser confundida com fase concluída.

## 4. Como ensaiar

```bash
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
```

```bash
powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission
```

Instale o pacote de laboratório pelo procedimento habitual, e depois do bind:

```bash
powershell -ExecutionPolicy Bypass -File tools\test-p1-submission-acceptance.ps1
```

O gate exige, no mesmo ciclo: `SchemaVersion=115`, hash do driver instalado
igual ao do pacote, Código 0, motor online, IB relido byte a byte, `RPTR ==
WPTR == 12`, as duas fences fora do poison, zero mismatches no resultado,
exatamente uma submissão aposentada e nenhuma regressão de fence.

## 5. Estado de compilação

`driver\wdk\build-contract.bat` passa limpo em `/W4 /WX`, com todos os testes
offline aprovados, incluindo os dois novos:

- `PERSISTENT RING CONTRACT` — 9 grupos: validação, submissão de computação,
  modo de interrupção, wraparound sem pacote truncado, anel cheio, contabilidade
  de retire, submissão SDMA, padding SDMA e guardas de emissão.
- `active_miniport_test` — passou a exigir que a submissão **falhe** de forma
  explícita com o motor offline.

**Veredito: NÃO VALIDADO EM HARDWARE.**
