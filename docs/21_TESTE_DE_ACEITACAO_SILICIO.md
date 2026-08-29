# Teste de aceitação integral no silício

O comando `tools\test-silicon-full-acceptance.ps1` é um gate fail-closed para
impedir que o serviço de runtime seja usado como prova enquanto tiver caminhos
sintéticos. Ele deve ser executado antes de qualquer teste live:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test-silicon-full-acceptance.ps1
```

Para conferir também a telemetria do miniport já instalada, use
`-RequireLiveEvidence`. O teste não escreve MMIO e não inicia o driver.

O gate só pode passar quando o backend de compute substituir todos os itens
abaixo por operações reais no hardware:

1. alocação em VRAM/GPUVA real, sem `g_VramPool` do host;
2. cópia submetida ao ring SDMA físico, com `RPTR == WPTR`, fence da GPU e
   comparação integral de dados;
3. dispatch por MQD/HQD/MEC, com PM4 e shader que escrevem um buffer de saída;
4. readback de RPTR/WPTR, fence e resultado a partir do silício — nunca valores
   retornados como constantes pelo IOCTL;
5. amostras de GRBM, clock, potência/temperatura e GFXOFF antes, durante e após
   a carga; e
6. teardown que restaura cada snapshot e falha fechado se o readback divergir.

Enquanto o teste bloquear, o único veredito permitido é:

`NÃO VALIDADO EM HARDWARE`
