# Candidato 3.29 — inicialização explícita do fetcher EOP

Data local: 30/08/2026  
Pacote: 3.29.0.0 / SchemaVersion 100  
Veredito: **NÃO VALIDADO EM HARDWARE**

## Hipótese derivada da tentativa 3.28

O 3.28 comprovou a tradução `GPUVA 0x10000 -> ring físico`, releu os 64
dwords do ring sem divergência e publicou `WPTR=40`, mas terminou com
`RPTR=0`, poison intacto e nenhum fault. A comparação com a sequência KFD de
carregamento de HQD mostrou uma etapa ausente no nosso ciclo: antes de escrever
`CP_HQD_ACTIVE`, o helper `kgd_gfx_v9_hqd_load` escreve
`CP_HQD_EOP_RPTR.INIT_FETCHER=1`.

Fonte primária usada para a sequência:
`drivers/gpu/drm/amd/amdgpu/amdgpu_amdkfd_gfx_v9.c`, árvore oficial Linux.
O offset e as máscaras GC10.1 foram reconciliados com os headers locais:

```text
mmCP_HQD_EOP_RPTR                         0x1FD1
BAR5 byte                                 0x0C8C4
CP_HQD_EOP_RPTR__RPTR_MASK                0x00001FFF
CP_HQD_EOP_RPTR__RESET_FETCHER_MASK       0x10000000
CP_HQD_EOP_RPTR__DEQUEUE_PEND_MASK        0x20000000
CP_HQD_EOP_RPTR__RPTR_EQ_CSMD_WPTR_MASK   0x40000000
CP_HQD_EOP_RPTR__INIT_FETCHER_MASK        0x80000000
```

O endereço foi incorporado primeiro ao catálogo de registradores, mantendo a
regra de não escrever MMIO não catalogado.

## Máquina transacional implementada

```text
[PREFLIGHT]
  lê CP_HQD_EOP_RPTR e exige HQD inativo
       |
[SNAPSHOT]
  preserva EopRptrBefore
       |
[APPLY/READBACK]
  escreve 0x80000000 antes de CP_HQD_ACTIVE
  aceita auto-clear de INIT_FETCHER e o status RPTR_EQ_CSMD_WPTR
  rejeita RPTR != 0, RESET_FETCHER ou DEQUEUE_PEND
       |
[PROVA NO SILÍCIO]
  critérios existentes: ring relido, RPTR==WPTR, resultado byte a byte,
  fence física posterior e poison removido
       |
[ROLLBACK]
  fila inativa -> restaura EOP_RPTR exatamente -> readback exato
```

Telemetria nova:

- `HqdActiveEopRptrBefore`
- `HqdActiveEopRptrApplied`
- `HqdActiveEopRptrReadback`
- `HqdActiveEopRptrRestored`
- `HqdActiveEopRptrRestorePassed`

O teste de aceitação Schema 100 exige `Applied=0x80000000`, ausência dos bits
proibidos no readback, `Restored==Before` e `RestorePassed=1`. Nada nessa
instrumentação promove avanço de ponteiro, fence ou resultado por CPU.

## Validação offline concluída

| Gate | Resultado |
|---|---|
| Compilação completa de contratos e drivers | PASS |
| Teste de HQD com aplicação e rollback de EOP_RPTR | PASS |
| Teste da allowlist MMIO com escrita/readback/restore | PASS |
| Dispatch observável offline e negativos | PASS |
| Parse dos scripts Schema 100 | PASS |
| Build do SYS de laboratório | PASS |
| Assinatura SYS/CAT e Inf2Cat | PASS, zero erros e zero avisos |
| Auditoria read-only do pacote | PASS |
| Binding/execução física 3.29 | **NÃO EXECUTADO** |

Artefato preparado:

```text
DriverVer             08/30/2026,3.29.0.0
Schema                 100
Package SYS SHA-256    20B38038B8C89F857FACBCB4076A4F0E83B1FE9534A38396DCE6E53D0439A2FB
Package INF SHA-256    AA59F0F0CB94BFBAD466C64E8AB2100F798320A3C9449DF512B44BA846EBB37C
Package CAT SHA-256    1F096A0037967C1222E2369EED15E8292907E1DEBC1EDDCE965CC2B49803ED82
```

## Condição para o próximo ensaio

O dispositivo está em `BasicDisplay / Código 0`, mas ainda existe um SYS antigo
residual em `C:\Windows\System32\drivers\bc250mcdm.sys` (187680 bytes), diferente
do pacote 3.29. Deve-se reiniciar manualmente e executar a quarentena/preflight
antes do binding. O agente não reiniciou, não instalou e não tocou novamente no
hardware neste boot.

O 3.29 só será aprovado se, no mesmo ciclo físico, houver:

1. `HqdActiveEopRptrApplied=0x80000000` e rollback EOP aprovado;
2. `RPTR == WPTR == ShaderDispatchDwords`;
3. resultado sair de `0x43DAFFFE` para `0xBC250C01`, byte a byte;
4. fence sair de `0xA55A0FF0` para `0xBC250D15`, depois do dispatch;
5. rollback de registradores e memória aprovado.

Até lá: **NÃO VALIDADO EM HARDWARE**.
