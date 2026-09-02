# Ring SDMA0 owned com COPY e fence - pacote 1.23.0.0

Data: 2026-08-26.

Status: **ENSAIO FISICO BLOQUEADO NO DECODE DO COPY; ROLLBACK APROVADO; ACELERACAO NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   91%
Miniport MCDM funcional            54%
Memoria/GPUVA WDDM                 32%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

O pacote cria o primeiro trabalho real da SDMA dentro do miniport. Ring e
workspace sao duas paginas CPU-visible obtidas do allocator owned. Seus
enderecos MC sao derivados de `VramMcStart` e dos offsets dos tokens, nunca de
uma constante de VRAM.

O workspace contem writeback do RPTR, shadow do WPTR, fence, origem e destino.
O ring recebe:

```text
COPY_LINEAR 256 bytes
GCR (writeback/invalidate L0/L1/L2)
FENCE 0xBC250123
WPTR final: 64 bytes
```

Gate fisico obrigatorio:

- `RPTR == WPTR == 64`;
- fence muda de poison `0x43DAFFFE` para `0xBC250123`;
- 64 dwords do destino iguais a origem e zero divergencias;
- configuracao do ring conferida por readback;
- registradores e as duas paginas restaurados ao snapshot.

O schema 31 tambem registra STATUS e PC proximos ao kick, polls, enderecos MC e
resultado do teardown. Falhar qualquer parte mantem `PhysicalReady=0`.

Build completo, testes de packet builder/dispatcher/submit/preemption, integracao,
assinatura e auditoria do pacote: PASS.

- source SYS: `A037F2E031B10D494B621BB79A0DFD43E60595375FAAD3393773935BFAEE6027`;
- signed SYS: `DC0767830BA07D04EEBCD9FD654E66697BE00262591E557FC680CD981B5A2FC8`;
- INF: `84802F0AD87D827BCAD7EDE1E6CBF3B2599CA7A0BA1903891C53098E4D02B5B0`;
- CAT: `57E69C46E34A5BE3651DB57ECB6C31FF5985583A166F76D46A3F54FB64265BED`.

## Resultado fisico

Evidencia:
`docs\evidence\2026-08-26_17-47-29-DESKTOP-HQAG6T4-mcdm-replace`.

```text
SdmaRingWorkStatus: 10 (TIMEOUT)
RPTR/WPTR:          0 / 64
Fence:              43DAFFFE -> 43DAFFFE
Polls:              1000
PC F32:             ativo
STATUS_REG:          mudou durante o kick
Rollback regs/mem:  1 / 1
```

A comparacao com o pacote Navi10 aprovado no driver de laboratorio encontrou a
causa: o builder colocava `src_lo` no dword reservado para endian e deixava o
zero reservado no final. A engine recebeu enderecos deslocados e nao consumiu
o pacote. O 1.24 corrige a ordem para `header, count, endian=0, src, dst`.

**NAO VALIDADO EM HARDWARE**
