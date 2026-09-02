# Resultado físico 3.59 — RPTR 4 e gate de shader ausente

**Data:** 31/08/2026 08:14 (UTC-3)  
**Pacote:** `3.59.0.0`  
**SYS SHA-256:** `C7A5B48352B27D6D32607A53275295AFE9C944EA38117E064FA99874DAE762C9`  
**Evidência:** `docs/evidence/2026-08-31_08-14-41-DESKTOP-HQAG6T4-p2-multi-submission/p2-multi-submission-acceptance.json`  
**Veredito:** **NÃO VALIDADO EM HARDWARE**

## Resultado

- vídeo confirmado, `bc250mcdm` Código 0 e watchdog desarmado;
- descritor real `C0023F00 00A96000 000000F4 0080002D`;
- zero mismatch nos 12 DWORDs do anel primário;
- `WPTR=204`, `RPTR MMIO=4`, `RPTR report=0`;
- zero aposentadorias e 32 divergências em fences/resultados;
- `CP_HQD_IB_CONTROL=0x84300000` e header MEC2 `0xC0037602`, indicando
  entrada no IB e parada durante `SET_SH_REG`.

O Event ID 27 do `Microsoft-Windows-Kernel-Boot` registrou tipo de boot
`0x0`; portanto esta execução não foi retomada por Fast Startup.

## Comparação com o 3.54 vencedor

A captura vencedora continha `ShaderPhysicalReady=1`,
`ShaderRptrAfter=ShaderWptrAfter=45`, fence física e máscaras WGP/CU validadas
antes da P1 persistente. No 3.59, os campos `Shader*` estão ausentes. O fluxo
atual tratava `PersistentSubmission` e a transação one-shot de shader como
ramos mutuamente exclusivos.

O candidato 3.60 restaura o gate físico de shader antes da ativação
persistente e impede P2 quando a P1 da mesma sessão falhar.
