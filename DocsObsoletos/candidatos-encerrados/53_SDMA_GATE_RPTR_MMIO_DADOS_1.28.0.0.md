# Gate RPTR MMIO, fence e dados - pacote 1.28.0.0

Data: 2026-08-26.

Status: **SDMA VALIDADA NESTA OPERACAO FISICA; PM4/COMPUTE NAO EXECUTADOS**.

O 1.28 conclui o ring somente quando `RPTR MMIO == WPTR == 68` e a fence GPU
sai do poison. O RPTR writeback continua capturado, mas nao bloqueia a prova
quando esta atrasado no inicio do pacote final, comportamento observado tanto
no 1.27 quanto no laboratorio legacy funcional.

Depois desse gate, o driver invalida HDP e compara os quatro dwords do
`WRITE_LINEAR` com o vetor conhecido. Sucesso exige simultaneamente:

- firmware e F32 ativos;
- RPTR MMIO igual ao WPTR;
- fence escrita pela GPU;
- quatro dwords corretos, zero divergencias;
- rollback integral de registradores e das cinco paginas.

Build completo, contratos offline, assinatura e auditoria: PASS.

- source SYS: `18D70D908ABA5442DB9A308AEA534148195ED90AFE6246C8ACA6B63BB8D2CF17`;
- signed SYS: `E16EC1EDEDC4C34250F58E873AA84F5D931A4C98C52CD03A973DA0257580E2AA`;
- INF: `1DEE0C8C99E3A3670C93BFB2AB4E9CC70B247C3BCE90D200D96F526B6E70BA3B`;
- CAT: `B2CC0268FD988B0533B29AB744A7FA9AC2561EF619999C29F40CF08A75F9B731`.

**NAO VALIDADO EM HARDWARE**

## Resultado fisico aprovado

Evidencia: `docs/evidence/2026-08-26_18-15-42-DESKTOP-HQAG6T4-mcdm-replace`.

```text
firmware PSP/F32:       status 0, 14 transicoes de PC
ring:                   status 0
RPTR MMIO/WB/WPTR:      68 / 52 / 68
fence GPU:              43DAFFFE -> BC250123
resultado WRITE_LINEAR: 4 dwords, 0 mismatch
polls:                  1
rollback reg/mem:       1 / 1
```

O RPTR MMIO alcancou o WPTR, a fence saiu do poison por escrita da GPU e os
quatro dwords produzidos pela SDMA coincidiram com o vetor conhecido da CPU.
O RPTR writeback atrasado em 52 foi preservado como diagnostico e corresponde
ao comportamento ja medido no laboratorio legacy.

Veredito restrito: **SDMA VALIDADA NESTA OPERACAO FISICA**.

PM4, MEC, shaders, Vulkan e LLM continuam **NAO VALIDADOS EM HARDWARE**.
