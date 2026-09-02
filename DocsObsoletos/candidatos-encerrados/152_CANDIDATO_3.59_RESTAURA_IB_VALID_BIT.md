# Candidato 3.59 — restauração comprovada do `INDIRECT_BUFFER_VALID`

**Data:** 31/08/2026  
**Pacote:** `3.59.0.0`  
**Schema de telemetria:** `127`  
**Veredito atual:** **NÃO VALIDADO EM HARDWARE**

## Evidência que motivou a correção

O candidato 3.58 restaurou o caminho `PACKET3_INDIRECT_BUFFER`, mas a prova
física terminou com `WPTR=204`, `RPTR MMIO=0`, zero aposentadorias e 32
divergências. A captura do primeiro descritor mostrou:

```text
DWORD0 = 0xC0023F00
DWORD1 = 0x00A96000
DWORD2 = 0x000000F4
DWORD3 = 0x0000002D
```

A evidência vencedora do 3.54 contém exatamente o mesmo descritor, exceto pelo
controle `DWORD3=0x0080002D`. Nela, o silício drenou `RPTR==WPTR==12`, gravou
as duas fences e produziu o resultado correto em um poll.

A causa da regressão era objetiva:
`BC250_PM4_IB_VALID_BIT` estava definido como `0u`, apesar de o builder fazer
OR com essa constante. Os testes também usavam a mesma macro na expectativa,
permitindo que produtor e teste concordassem no valor inválido.

## Alterações

- `BC250_PM4_IB_VALID_BIT` restaurado para `(1u << 23)`;
- contratos passaram a exigir os literais independentes `0x00800000`,
  `0x00800008` e `0x00800400`;
- auditoria dos 12 DWORDs do self-test voltou a comparar o anel primário com
  `INDIRECT_BUFFER + RELEASE_MEM`, em vez de compará-lo com o conteúdo do IB;
- pacote elevado para `3.59.0.0`, mantendo o Schema 127.

## Gate físico

O próximo ensaio exige boot frio e o ciclo completo:

```text
[PREFLIGHT] -> [SNAPSHOT] -> [APPLY] -> [READBACK] -> [PROVA NO SILÍCIO] -> [ROLLBACK/TEARDOWN]
```

Até que a BC-250 produza 16 aposentadorias, `RPTR == WPTR`, fences físicas e
zero divergências de dados, o candidato permanece **NÃO VALIDADO EM HARDWARE**.
