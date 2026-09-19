# Passagem de bastão — reconciliação de cursor MEC (3.116.1.0)

> **Data:** 2026-09-09  
> **Pacote:** laboratório `3.116.1.0`, assinado e auditado  
> **Estado:** executado no silício às 17:47; reconciliação validada, consumo Node 1 reprovado

## Resultado físico — 09/09 17:47

O pacote `3.116.1.0` foi instalado após boot frio, com hashes conferidos e watchdog armado.
O helper atingiu o efeito físico esperado: o estado lógico anterior era
`Submitted=Retired=17`, `Wptr=204`, enquanto a leitura direta da HQD encontrou
`RPTR=0/WPTR=0`; o descritor seguinte foi publicado a partir de zero e o novo `WPTR` foi
`12`, não `216`.

Isso valida a correção de geometria do cursor, mas **refuta que a divergência de cursor fosse
a causa suficiente da falta de consumo**. O resultado do harness foi:

```text
accepted 17 -> 18
retired  17 -> 17
WPTR     204 -> 12
RPTR     204 -> 0
fence observada = 0
tripé/canários = falha fechada
```

No instante imediatamente anterior ao doorbell, a fotografia física já mostrava
`RPTR=0/WPTR=0`, `Active=1`, `VMID=0x900` e `CpcStalled=0x00040000`. Depois do doorbell:
`RPTR=0/WPTR=12`, `HqStatus=0x00008040`, `CpcStalled=0x00040000`. A captura TDR manteve
`FaultStatus=0`, endereço de fault zero e `PqControl=0xD030090D` (`PQ_EMPTY=0`). Logo, o
stall `MEC2_WAIT_ON_RCIU_READ` reapareceu antes do kick; sua ausência na corrida 3.115 não
era uma resolução estável nem causal.

O watchdog preservou 76.526 linhas do log vivo, SHA-256
`85E58D12767B76236F7DD50ACB1F95E8CB7605DE8E29F4979EE9DF629343FEAB`, removeu
`oem35.inf` e restaurou o Adaptador de Vídeo Básico da Microsoft em Código 0 às 17:50:43.
A evidência e a análise estão em
`docs/evidence/2026-09-09_17-47-40-DESKTOP-HQAG6T4-lab-binding/`.

**Veredito:** a reconciliação do cursor está **VALIDADA EM HARDWARE**; o consumo, a fence,
os canários e o tripé do Node 1 continuam **NÃO VALIDADOS EM HARDWARE**. Não repetir o
pacote 3.116.1.0. O próximo discriminante deve atacar/explicar o stall RCIU pré-doorbell,
sem voltar a atribuir a falha à aritmética do cursor.

## Origem da mudança

A corrida 3.115.1.0 retirou o stall RCIU e confirmou `PQ_EMPTY=0`, mas mediu dois domínios
incoerentes imediatamente antes do Node 1:

```text
logico:  Wptr=204, Submitted=17, Retired=17
fisico:  RPTR=0, WPTR=0
publicado sem reconciliar: WPTR=216
```

O anel não foi consumido. A análise completa está em
`docs/evidence/2026-09-09_16-55-02-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md`.

## Implementação

`Bc250PersistentRingReconcileDrainedCursor` reduz os cursores físicos pela dimensão do
anel e permite rebase somente quando:

- `LastSubmittedFence == LastRetiredFence`;
- `PhysicalRptr == PhysicalWptr` depois da redução;
- o anel está ativo e sua dimensão é válida.

O helper não toca em registrador nem em fence. Na integração KMD,
`Bc250SubmissionReconcileCursorLocked` seleciona a HQD, lê `CP_HQD_PQ_RPTR/WPTR`, restaura
o seletor e chama o contrato antes de planejar qualquer pacote. Depois de um rebase seguro,
sincroniza apenas o shadow de WPTR e a contabilidade lógica. **RPTR nunca é escrito.**

Se a leitura/restauração falhar, os ponteiros físicos divergirem ou houver fence em voo, o
submit é recusado. Os logs novos são `MEC_CURSOR_RECONCILE PASS/RECUSA`.

## Testes

Casos cobertos no contrato:

1. medição real `Logical=204`, físico `0/0` → rebase 0;
2. físico drenado `12/12` → rebase 12;
3. cursores crus `16396/16396` → redução para 12;
4. físico `0/12` → `CURSOR_BUSY`, sem mutação;
5. fence `18/17` → `CURSOR_BUSY`, sem mutação;
6. anel inativo e argumentos inválidos → recusa.

Quatro mutações adicionais foram rejeitadas: remoção do gate de fence, remoção do gate
físico, retirada da reconciliação antes do planejamento e tentativa de escrever RPTR.

`build-contract.bat` terminou com `EXIT=0`; auditoria de segurança, pacote lab e UMD Node 1
passaram.

## Identidade do pacote

| Artefato | SHA-256 |
|---|---|
| SYS | `E145A7DD55332010D145E887261F99A8A5F9CDCD82136065733F3CC68930488C` |
| UMD | `2A25BE2C6D807917BDCA2D8A03BDDEA94FB3DD018B312EC1FDAF1AD5D460969B` |
| INF | `7CB3DC16FF85A0982F62AA9374143ABD87A83DEB705114D2E73C552E0400379C` |
| CAT | `0279E1D18102C15D8C640F04D6CB1F9D47C332FB24D758FC94C56C1281788545` |

Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, persistent submission habilitada, promoção GPUVA
default-off e D3D10/11 native-ready falso.

## Critério que foi exercitado

O ensaio confirmou pelo efeito publicado a reconciliação esperada:

```text
[MEC_CURSOR_RECONCILE] PASS: Logical=204 PhysicalRptr=0 PhysicalWptr=0 Rebased=0 ...
```

O submit produziu `StartDW=0`/`NewWptr=12`, mas não fechou: `RPTR=0`, fence ausente e
canários intactos. O critério físico recusou corretamente declarar sucesso.
