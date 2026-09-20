# 301 — Corrida 43 — restart GFX falhou fechado e revelou `CP_RB0_BASE` sem `>> 8`

> **Data/hora:** 20/09/2026, bind iniciado às 10:05:31.  
> **Candidato:** lab `3.201.1.0`, `DISPLAY_RENDER_LAB`, WDDM 2.7, schema 135.  
> **Evidência:** `docs/evidence/2026-09-20_10-05-31-DESKTOP-HQAG6T4-lab-binding/`.  
> **Desfecho:** `VIDEO_TDR_FAILURE (0x116)` e reboot.  
> **Veredito:** **FALHA do ensaio; avanço diagnóstico e de honestidade do contrato**.

## 1. Houve avanço?

**Sim, diagnóstico; não houve avanço funcional.** Comparada à Corrida 42, a Corrida 43 não
declarou falsamente que o CP_RB0 estava recuperado. O novo pacote executável de prova ficou
em `RPTR=0/WPTR=12`, preservou o poison e reprovou o restart. O callback devolveu
`STATUS_DEVICE_NOT_READY`, exatamente como o critério prévio exigia para a falha fechada.

O Windows ainda transformou essa falha honesta do reset em `0x116`; portanto isso não é
recuperação bem-sucedida e não fecha o item de TDR.

## 2. Sucessos medidos

- `StartDevice` autorizado e driver `3.201.1.0` efetivamente carregado.
- Tripé de boot SDMA passou: `WPTR=68`, `RPTR=68`, fence `0x1001`, dados `4/4`.
- O preâmbulo gráfico executou antes da carga: `WPTR=20`, `RPTR=20`, fence `1`,
  `DrenoOk=1`; o CP_RB0 ficou online com ring em `0xF40FF70000`.
- Houve um `CommitVidPn`, três `SetVidPnSourceAddress` e atividade de VSync até pelo menos
  a amostra 540. Isso prova atividade do caminho, não pixel correto.
- O log contém 78 aceitações `IB EXECUTADO (INDIRECT)` no Node 0 e 70 vereditos assíncronos
  `TRIPE_PROVADO` antes do crash; nenhum veredito SDMA de timeout/falha foi registrado.
- No TDR, o SDMA foi reiniciado e provado fisicamente: `SYNC_NOP TRIPOD PASS`,
  `RPTR=WPTR=40`, fence `0x1053` em 2 polls.
- A reconciliação nova funcionou: `[TDR_FENCE_RECONCILE] PASS: Abortadas=1, Committed=1,
  Reserved=0, NotificadasComoConcluidas=0`.
- O fusível anti-loop disparou e recusou o segundo `StartDevice` (`State=2`, `Boots=2`).
- O binder tentou arquivar o PDB privado antes do bind, mas a auditoria posterior mostrou
  que o arquivo de destino ficou corrompido; a atribuição foi recuperada abaixo.

## 3. Falhas medidas

O primeiro submit GFX desta corrida foi aceito com `DmaVA=0x244FE4000`, 664 dwords e VMID 7.
Ele avançou somente até `RPTR_Reg=24`, `RPTR_Wb=20`, com `WPTR=32`. A fence de submissão
anterior apareceu escrita, mas a perna de dreno não convergiu. A sonda do scheduler mediu
`Responsive=0`.

No quiesce:

- `GRBM_STATUS=0xA0403028`: `GUI_ACTIVE=1`, `CP_BUSY=1`;
- `CP_BUSY_STAT=0`: nenhuma espera nomeada pelos campos lidos;
- `SQ_DEBUG_STS_GLOBAL=0`: nenhuma onda visível;
- `IB1 Base=0x244FE4000`, `RestamDW=0`;
- nenhuma falha de GFXHUB foi registrada.

O reset físico foi disparado, mas a prova pós-reset falhou:

```text
[TDR_GFX_RESTART_PROOF] FALHA: RPTR=0 WPTR=12
FenceObs=0x43DAFFFE FenceEsp=0x00000003 Polls=2000
[TDR_GFX_RESTART] FALHA: registradores rearmados, mas o CP_RB0 nao executou NOP+RELEASE_MEM
[McdmRestartFromTimeout] ... STATUS_DEVICE_NOT_READY
```

O evento 1001 confirma:

```text
0x00000116
Arg1=ffffc1818f0cf010
Arg2=fffff8028a0cfb80
Arg3=ffffffffc00000a3
Arg4=0000000000000004
```

`Arg3=0xC00000A3` é o mesmo `STATUS_DEVICE_NOT_READY` devolvido pelo restart. O
`C:\Windows\MEMORY.DMP` novo tem 1.156.075.908 bytes e timestamp 10:06:45.

### Atribuição final do dump

O relatório CDB foi arquivado com SHA-256 `7B629120...5805C19`. Ele fixou o RVA do Arg2 em
`bc250mcdm+0x1FB80`, mas revelou que o PDB arquivado estava corrompido: hash real
`CCDA116B...E959F5`, contra `8376AA54...4542` declarado; `symchk` recusou o arquivo.

Uma reconstrução offline do estado 3.201 produziu `.text` e `.pdata` byte a byte idênticas
ao SYS instalado exato (`A76A3DA6...E514`). O PDB válido dessa reconstrução resolve
`RVA 0x1FB80` como `Bc250McdmRestartFromTimeout` (`ResetFromTimeout` começa em `0x1FB20`;
a função seguinte em `0x1FBE0`). O Arg2 é a owner tag da DDI de restart, não uma exceção
dentro dela; o Arg3 é sua recusa `STATUS_DEVICE_NOT_READY`. Não há bucket de AV, IRQL,
verifier ou corrupção. Detalhes em `memory-dmp-symbol-attribution.txt` na evidência.

## 4. Causa encontrada no nosso código

O caminho normal e a documentação P1 já codificavam `CP_RB0_BASE = RingGpuVa >> 8`. O
restart, porém, tinha uma segunda implementação divergente:

```c
BASE_LO = (uint32_t)(RingGpuVa & 0xFFFFFFFF);
BASE_HI = (uint32_t)((RingGpuVa >> 32) & 0xFFFF);
```

Para o ring medido em `0xF40FF70000`, o formato correto é `HI:LO = 0:F40FF700`. O restart
escrevia `F4:0FF70000`, que decodifica para uma base 256 vezes maior. Isso explica diretamente
por que, depois do reset, o CP não consumiu sequer o primeiro dword (`RPTR=0`) e a fence ficou
no poison. A Corrida 42 só conferia readbacks estruturais e não podia revelar esse erro.

A correção passou a usar `Bc250GfxRingEncodeBaseLo/Hi` como fonte única nos caminhos ONLINE e
restart. O teste cobre simultaneamente LO e HI com `0x000123456789AB00`, e a auditoria proíbe
o endereço bruto no restart. **Essa correção é offline e ainda não foi validada no silício.**

## 5. Recuperação e lacunas da coleta

Após o reboot, a BC-250 está em `CM_PROB_FAILED_POST_START`, serviço `bc250mcdm` parado e
fusível `TRIPPED`. Isso é contenção, não sucesso do nosso display. A recuperação visual deve
ser atribuída ao BasicDisplay somente após a checagem do roteiro elevado.

O reboot interrompeu o binder e deixou três arquivos pré-alocados inteiramente com zeros:
`display-log-this-bind.txt`, `prebind-policy.json` e `safe-bind-watchdog-config.json`.
O log cumulativo preservou a prova. A cauda foi recuperada em
`bc250_display_log-tail-recovered.txt`: 173.347 bytes, SHA-256
`36B67CA1552BCECDAE304549BA1C68D993F61259D9DEC931536B68BBF5FB0536`.

Continuam **NÃO VALIDADOS EM HARDWARE**: restart GFX corrigido, submissão GFX posterior ao
reset, recuperação sem BugCheck, execução pós-reset do MEC, desktop visível, frame/pixel
correto e jogos.
