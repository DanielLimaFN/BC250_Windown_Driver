# SDMA0 F32 ativa - pacote 1.22.0.0

Data: 2026-08-26.

Status: **F32 SDMA0 ATIVA APROVADA EM HARDWARE; ACELERACAO NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   91%
Miniport MCDM funcional            54%
Memoria/GPUVA WDDM                 32%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

Depois de o PSP aceitar o firmware SDMA0, o KMD executa uma transacao limitada
da F32:

```text
PREFLIGHT -> SNAPSHOT -> CLOCK/CNTL -> HALT/RESET/RUN
          -> READBACK -> 16 amostras do PC -> ROLLBACK
```

Registradores usados, todos classificados P2 no catalogo:

- `SDMA0_CLK_CTRL` (`0x49EC`);
- `SDMA0_CNTL` (`0x49F0`);
- `SDMA0_F32_CNTL` (`0x4A28`);
- `SDMA0_STATUS2_REG` (`0x4A60`, PC nos bits 15:2).

O gate falha fechado se qualquer readback divergir, se o PC nao mudar entre as
16 amostras ou se CLK/CNTL/F32 nao voltarem exatamente ao snapshot. O teste nao
programa ring e nao submete COPY/WRITE; portanto nao prova aceleracao.

Build completo, teste dedicado da maquina de estados, testes de integracao,
assinatura e auditoria do pacote: PASS.

- source SYS: `5C3BAEABF687302908B9FBC20FBF52AE1CC82540FCEDFC8D05A14F02434B91D3`;
- signed SYS: `D7E14819CEA61F4529C42A1E95D78E9C6E31BB83D3AA20800B4D445A1CA6CEBA`;
- INF: `72CAE5E0F83C6C74A94FE1F38648A74C6054A1B4268C61965A3CAE29FD0326F0`;
- CAT: `60EC2D2AD598A860CD4B8A9942428E71FA30FA9D7AF0CB464E21C434FB19CCF5`.

## Resultado fisico

Evidencia:
`docs\evidence\2026-08-26_13-30-47-DESKTOP-HQAG6T4-mcdm-replace`.

```text
PSP LOAD_IP_FW:       status=0, response=0, fence=1/1
Checksum SDMA:        AB6305C6
F32 status:           0
PC primeiro/ultimo:   385 -> 932
Transicoes do PC:     12
Apply readback:       1
Rollback integral:    1
PhysicalReady:        1
```

As transicoes do PC provam que a F32 executou firmware no silicio. Como nenhum
ring SDMA recebeu trabalho, ainda nao ha prova de copia, fence da engine ou
aceleracao.

**NAO VALIDADO EM HARDWARE**
