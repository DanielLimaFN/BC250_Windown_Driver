> **CORREÇÃO (23/09, datação em [docs/372](372_DATACAO_TDR_CORRIDA85_E_DX43_CRITERIO_CORRIDA86.md)):** a tabela da seção 3 comparou as ondas com o **último submit**, que ainda não executava. Comparando com o IB em `CP_IB1`, os **4 de 4** TDRs têm ondas no VMID do IB **anterior**, com o PC programado pelo IB em `CP_IB1`. TDRs 2 e 4 **não** são classe nova; a diferença permissão/mapeamento é só como a página aparece na tabela do processo anterior.

# Corrida 85 — DX.42: o preâmbulo perde o conteúdo depois de um TDR (desfecho C)

> 23/09/2026. O bind começou às 14:15:11, e o rollback às 14:17:12 terminou em
> `RECOVERY_OK_ONLINE` às 14:17:22.
>
> **Candidato:** KMD/UMD lab `3.246.1.0`, os mesmos da 84. A RADV DX.42
> `FCAA11FE…` foi conferida em `binding-provenance.json`.
>
> **Critério:** [docs/370](370_DX42_IDENTIDADE_DO_PREAMBULO_E_CRITERIO_CORRIDA85.md).
>
> **Evidência:** `docs/evidence/2026-09-23_14-15-11-DESKTOP-HQAG6T4-lab-binding/`,
> com FINAL `complete_at_capture=true`. A coleta pós-rollback trouxe 194 `.bin`
> e 194 `.meta`, 12 fluxos e nenhum órfão.

## 1. Houve avanço?

**Diagnóstico: sim. Funcional: não.**

- A pergunta da corrida teve resposta com todos os sinais.
- Daniel viu tela preta e depois o desktop alternando com blocos corrompidos a
  cada ~2 s.
- O harness teve 0/60, contra 1/60 na 84.
- Quatro TDRs, como na 84.

## 2. Sucessos

- **`RADV_PREAMBLE_IDENTITY` apareceu 293 vezes**, em todos os `CONCAT`.
  - `changed=1` ocorreu 12 vezes, uma por contexto novo. Não houve regeneração
    de preâmbulo no meio de um contexto.
- **Desfecho C, com todos os sinais (P0).** No harness (PID 4768, contexto
  `0x40000240`), o preâmbulo de serial 3 serviu os submits 1–3 com
  `fin_match=1`. No submit 4 veio `changed=0 uses=4 fin_hashed=1 fin_cdw=180
  fin_zero_prefix=0 fin_match=0 src_zero_prefix=180`.
  - `fin_fnv=4d383cf55995e9c4` e `src_fnv=copy_fnv=a987b600a8b7fb65`: a CPU
    escreveu bytes bons, e depois o **mesmo BO**, com a mesma VA de IB
    `0x240414000` e o mesmo `buf`, passou a ler zero.
  - A cópia bateu com a origem, então o slot está inocente, como previsto em 370.
- **O veto DX.41 funcionou (P0):** `RADV_CONCAT_REJECT
  layout=REJECT_ZERO_PREAMBLE`. Não houve `SubmitVirtual` com
  `DmaVA=0x240030000` depois do TDR 3. O IB zerado **não chegou ao CP**, e o
  TDR "sem ondas" da 84 **não se repetiu**.
- **O que aconteceu entre o submit 3 e o 4 (P0, ordem do log KMD):**
  1. o harness submeteu nas fences 58, 60 e 62;
  2. houve o TDR 3, com `TDR_GFX_PACKET IB1 Base=0x240030000`, o IB **do
     próprio harness**;
  3. vieram `McdmResetFromTimeout` e `BuildPagingBuffer Op=2`, que o KMD trata
     como no-op com SUCCESS;
  4. depois disso, o preâmbulo daquele processo lê zero;
  5. o harness recebeu `DXGI_ERROR_DEVICE_REMOVED` com motivo `DEVICE_RESET` no
     quadro 1.
- **Controles:**
  - 4 de 4 `TDR_GFX_RESTART_PROOF PASS`, sem BugCheck, rollback online na
    primeira tentativa, BasicDisplay em Código 0 nas 5 amostras;
  - autoteste `nodcc,nonggc` 5/5; `nodcc` e `nodcc,nongg` sem 5/5, como na 84.

## 3. Falhas

**Quatro TDRs, em duas assinaturas:**

| TDR | Ondas (VMID, PC) | Último submit | Falta na linha do tempo |
|---|---|---|---|
| 1 | VMID 1, `0x200040C00` | fence 29, VMID 2 | `Vmid=1 Cid=9` de **permissão** em `0x200040000` |
| 2 | VMID 2, `0x201800D00` | fence 51, VMID 2 | `Vmid=2 Cid=9` de **mapeamento** (`MappingError=1`, `WalkerError=6`) em `0x201800000` |
| 3 | VMID 4, `0x2003C0B00` | fence 63, VMID 3 | `Vmid=4 Cid=9` de permissão em `0x2003C0000` |
| 4 | VMID 1, `0x201600D00` | fence 79, VMID 1 | `Vmid=1 Cid=9` de mapeamento em `0x201600000` |

- **TDRs 1 e 3:** a classe conhecida, com as ondas no VMID **anterior**.
- **TDRs 2 e 4:** as ondas estão no VMID **certo**, mas a página do shader não
  está mapeada na tabela desse VMID no momento da busca. Não é a assinatura de
  VMID antigo. É uma segunda forma, que este critério não previa. Pelo
  desfecho E, fica registrada sem julgamento de causa.

**Outras falhas:**

- **Harness:** 0/60, com o device removido no quadro 1.
- **Display:**
  - 12 `DIRTY_RECTS_RECUSADOS` com `SubRectCnt=1`, todos caindo em cópia de
    tela cheia, contra 4 retângulos aceitos;
  - 7 `REJECT source contract`;
  - nenhum `BYTE_PROOF PASS` de tela cheia;
  - as provas `PRIMARY_COPY` são amostrais, com 66 a 214 de 256 amostras não
    nulas.
  - H: a alternância a cada ~2 s vem das cópias de tela cheia de superfícies
    parciais que sobrescrevem o scanout, a mesma classe da Corrida 79. Não está
    provado.
- **Atribuição das fotos:** a foto com blocos mostra o console ainda dentro da
  janela de observação, logo é do nosso driver. As duas fotos limpas mostram o
  console já com "Coleta FINAL", perto do rollback de 14:17:12, e podem ser do
  BasicDisplay. A atribuição fica **indeterminada**.
- Continuam as escritas sem permissão em `0x2401A1000`/`0x2401A2000` (`Cid=0`).

## 4. Veredito frente ao critério

- **Pergunta 1 (desfecho C): diagnóstico com sucesso.**
  - Todos os sinais apareceram: `changed=0`, `uses>1`, `fin_zero_prefix=0`,
    `fin_match=0`, `src_zero_prefix=180`.
  - A coleta ficou completa.
- **Pergunta 2:** o veto impediu o IB zerado de chegar ao KMD, e o TDR sem
  ondas não reapareceu.
- **Função: falha.** Tela corrompida, 0/60, 4 TDRs e device reset.

**Leitura do C:**

- **P0:** a perda do preâmbulo veio **depois** de um TDR que atingiu o próprio
  processo.
- **Consequência, com confiança P2:** nesta corrida, o preâmbulo zerado é
  **consequência** do TDR, não causa. A RADV continuou submetendo num device que
  o reset já tinha invalidado.
- **Ressalva:**
  - o caso do PID 12816 na 84 **não** está explicado por isto: aquele processo
    nasceu depois do TDR 3 da 84 e não sofreu TDR antes da fence 107;
  - `bo_handle=0` em todas as linhas mostra que o campo `handle` do BO WDDM não
    é preenchido nesse caminho. Falha da instrumentação, sem efeito no desfecho.

## 5. Recuperação e pendências

- **Recuperação:** `RECOVERY_OK_ONLINE` às 14:17:22, BasicDisplay em Código 0
  em 5 amostras. O marcador de boot frio foi regravado pelo watchdog.
  **Desligamento completo antes de outra corrida.**
- **NÃO VALIDADO EM HARDWARE:** desktop íntegro, 60/60, ausência de TDR, causa
  do caso 12816 da 84 e causa das duas assinaturas de TDR.

**Próximo passo, offline:**

1. **Estreitar as duas classes de TDR, que seguem como bloqueador.**
   - Para os TDRs 2 e 4 (mapeamento no VMID certo), datar no log KMD o
     `UpdatePageTable` da página do shader (`0x201800000`, `0x201600000`) e o
     `FlushTlb` daquele VMID frente ao submit que despachou.
   - H: TLB desatualizada ou mapeamento publicado depois do dispatch.
   - Se confirmado, a hipótese dos TDRs 1 e 3 também muda de lugar: da troca de
     VMID para a ordem mapeamento → invalidação → dispatch.
2. **Preâmbulo:** a contenção DX.41 basta por ora. Opcional: a RADV consultar o
   estado do device antes de submeter depois de um reset.
3. **Display:** entender por que `SubRectCnt=1` é recusado (`Src!=Dst` ou fora
   da tela) e não cair em cópia de tela cheia de superfície parcial.
