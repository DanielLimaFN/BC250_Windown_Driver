# Resultado físico 3.58 — `INDIRECT_BUFFER` sem VALID

**Data:** 31/08/2026 08:01 (UTC-3)  
**Pacote:** `3.58.0.0`  
**SYS SHA-256:** `DA57513410A72A5ED046B1C7D5FDAD2C793044DACDDF4BEA9733E56DD27B7208`  
**Evidência:** `docs/evidence/2026-08-31_08-01-00-DESKTOP-HQAG6T4-p2-multi-submission/p2-multi-submission-acceptance.json`  
**Veredito:** **NÃO VALIDADO EM HARDWARE**

## Resultado observado

- dispositivo `bc250mcdm` em Código 0, vídeo confirmado pelo operador;
- watchdog confirmado e desarmado corretamente;
- 16 submissões aceitas e zero aposentadas;
- `WPTR=204`, `RPTR MMIO=0`, `RPTR report=0`;
- 32 divergências entre resultados/fences e seus valores esperados;
- doorbell canônica `BAR2+0x110`, uma escrita de 64 bits, sem falha;
- `CP_HQD_PQ_DOORBELL_CONTROL=0x40000110` antes e depois;
- `TriadPassed=0`, `PhysicalReady=0`.

## Causa-raiz comprovada

O primeiro descritor real no anel foi:

```text
0xC0023F00 0x00A96000 0x000000F4 0x0000002D
```

O candidato 3.54, validado fisicamente, usou:

```text
0xC0023F00 0x00A96000 0x000000F4 0x0080002D
```

O código-fonte atual revelou `BC250_PM4_IB_VALID_BIT=0u`. Assim, o 3.58
restaurou o tipo correto de submissão, mas removeu do descritor exatamente o
bit 23 que permitiu a vitória física do 3.54. Os contratos não detectaram a
regressão porque a expectativa reutilizava a mesma macro zerada.

## Encaminhamento

O candidato 3.59 restaura `(1u << 23)` e passa a conferir os DWORDs produzidos
contra literais independentes. O 3.58 deve ser desmontado e qualquer novo
ensaio deve começar por boot frio.
