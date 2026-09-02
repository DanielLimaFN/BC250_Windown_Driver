# Candidato 3.46.0.0 (SchemaVersion 117) — coerência por submissão persistente

**Data**: 30/08/2026  
**Estado**: construído, testado e auditado offline. **NÃO VALIDADO EM HARDWARE.**

---

## 1. Hipótese que este candidato testa

O 3.45 provou que a fila persistente ativa, recebe `WPTR=12` e sai de
`QUEUE_IDLE/PQ_EMPTY`, mas não consome o anel (`RPTR=0`). Também refutou a
hipótese de amostragem exclusiva do WPTR na borda de `CP_HQD_ACTIVE`.

O caminho one-shot validado no 3.43 torna as escritas da CPU visíveis à GPU
antes do kick. O caminho persistente fazia apenas `KeMemoryBarrier()`. O 3.46
testa se a invalidação ausente de HDP/GL2/GCVM era a causa do fetch parado.

## 2. Mudança implementada

Entre a publicação da shadow do WPTR e qualquer doorbell/escrita direta de
WPTR, `Bc250SubmissionPublishWptr()` agora executa, fail-closed:

1. snapshot de `GCVM_L2_CNTL2`;
2. `HDP_READ_CACHE_INVALIDATE = 1`;
3. escrita SOC15 mascarada em `GCVM_L2_CNTL2`:
   `(lido & ~mask) | (or_val & mask)`;
4. readback exigindo preservação dos demais bits e auto-clear de `mask=0x3`;
5. `GCVM_INVALIDATE_REQ = 0x00F80001`;
6. dummy-read do próprio REQ, exigido pelo GC10.1;
7. poll do bit VMID0 em `GCVM_INVALIDATE_ACK`, limitado a 1000 leituras.

Se qualquer escrita, readback ou ACK falhar, doorbell e WPTR MMIO não são
tocados. Como o lote já foi confirmado no estado lógico do anel, não existe
rollback seguro isolado: o motor passa a `FAULTED` e recusa novas submissões
até o teardown completo.

Nenhuma fence é escrita pela CPU.

## 3. Telemetria e gate

Campos adicionados ao SchemaVersion 117:

- `P1CoherHdpApplied`;
- `P1CoherL2Readback`;
- `P1CoherInvalidateAck`;
- `P1CoherInvalidatePolls`;
- `P1CoherencyPassed`.

O gate `tools/test-p1-submission-acceptance.ps1` exige `HdpApplied=1`,
`CoherencyPassed=1`, bits de invalidação GL2 auto-limpos, ACK do VMID0 e polls
na janela `1..1000`. O tripé físico anterior permanece integral.

## 4. Validação offline

- `driver\wdk\build-contract.bat`: aprovado; contratos e testes offline passam.
- Imagem de laboratório: construída com bring-up ativo e diferente da produção.
- Pacote: `3.46.0.0 / SchemaVersion 117`, shader e submissão persistente ativos.
- Auditoria do pacote: aprovada, sem instalação ou binding.
- SHA-256 do SYS empacotado:
  `C1480B18D386A379802C60DA444FF5EC850AB66CA1BA532F338D4E4E625DB973`.

## 5. Critério do ensaio físico

Além dos campos de coerência aprovados, o mesmo ciclo deve fechar:

```text
RPTR MMIO == RPTR report == WPTR == 12
fence do dispatch: 0xA55A0FF0 -> 0xBC250D15
fence do anel:     0 -> 1
resultado:         0x43DAFFFE -> 0xBC250C01
P1SelfTestRetiredCount = 1
P1SelfTestTriadPassed = 1
P1SelfTestPhysicalReady = 1
```

Até essa prova no silício, o veredito permanece **NÃO VALIDADO EM HARDWARE**.

## 6. Operação

O ensaio deve começar após desligar e ligar a máquina. Estado residual da
engine não cai com desbind. Quarentene o SYS residual antes do novo bind e use
a cadeia transacional já descrita no handoff 131.

Se a coerência passar mas `RPTR` continuar parado, o próximo diagnóstico é
capturar `CP_MEC_CNTL` (`0x00008234`) e `CP_ME_CNTL` (`0x000086D8`) depois que
o motor persistente subir, verificando se o MEC continua desimpedido.

