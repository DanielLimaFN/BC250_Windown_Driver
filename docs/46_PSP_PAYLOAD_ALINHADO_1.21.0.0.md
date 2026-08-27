# PSP payload SDMA0 alinhado - pacote 1.21.0.0

Data: 2026-08-26.

Status: **PSP LOAD_IP_FW SDMA0 APROVADO EM HARDWARE; ACELERACAO NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   88%
Miniport MCDM funcional            50%
Memoria/GPUVA WDDM                 32%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

O arquivo completo de 33.792 bytes e lido e validado pelo SHA-256
`15D0...9E95`. Depois, os 33.536 bytes a partir de `0x100` sao movidos para o
inicio da alocacao contigua. O KMD valida novamente o payload pelo SHA-256
`ACBA51D3D734637F5ACBBABF3A7DA5CECDC4909628D79A40F29F5A39EB1338FE`.

Assim, o PSP recebe exatamente:

```text
PA: alinhada a 4096 bytes
tamanho: 33536 bytes
primeiro byte: inicio real do microcodigo, sem header AMD
tipo: 9 (SDMA0)
```

As alocacoes agora aceitam o mesmo limite fisico de 40 bits usado pelo driver
legacy, e todos os campos high do mailbox/frame/comando sao preenchidos. O
schema 29 registra as PAs completas de ring, command, fence e payload.

Build completo e testes: PASS.

- source SYS: `E34AF69DA1B9078A50215F24ED660D789CBA0C8575197F518E74463F78487EFD`;
- signed SYS: `824A296F74300C3C5EAAEFCD88CA38AB1C1E868690B4DBB3B8749B1B607F474A`;
- INF: `47DB51F31E69CAC61F07CD5C674402F468153398CCEAC340257FAF8EA19221E0`;
- CAT: `4F561032332EAA702AEB6D1EA448F54DAE697A63BFF4373F497A3C16DF79F806`.

## Resultado fisico

Evidencia:
`docs\evidence\2026-08-26_13-21-21-DESKTOP-HQAG6T4-mcdm-replace`.

```text
PspLoadFwStatus:          0
FirmwareBytes/Type:       33536 / 9 (SDMA0)
Fence esperado/observado: 1 / 1
Fence polls:              3
ResponseStatus:           0
Checksum SDMA:            55B182E3
Destroy/restore:          1 / 1
PhysicalReady:            1
```

O PSP aceitou o payload alinhado e o checksum fisico coincide com o ensaio
legacy. Isso fecha apenas transporte, autenticacao, carga e teardown do
firmware SDMA. Ainda faltam PC ativo, ring, RPTR/WPTR, fence escrita pela SDMA e
comparacao do resultado.

**NAO VALIDADO EM HARDWARE**
