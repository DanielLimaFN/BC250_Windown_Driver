# Post-display ownership fisico MCDM 1.1.0.0

Data: 2026-08-25.

## Veredito

**CAPTURA FISICA DO SCANOUT APROVADA. PM4/COMPUTE NAO EXECUTADOS.**

O pacote 1.1.0.0 foi publicado como `oem19.inf` pelo ciclo direto de replace.
O binding NewDev retornou sucesso, sem reboot, e `StartDevice` terminou
deliberadamente em Codigo 43 por `RuntimeMissing=1`.

## Evidencia do silicio e do PnP

```text
PCI config bytes                 64
BARs descobertos/resolvidos      3 / 3
VRAM total                       512 MiB
CPU-visible / ReBAR atribuido    256 MiB / 256 MiB
Topologia MMIO                   8 leituras, 2 passagens estaveis
Discovery TMR                    NOT_REPORTED

AcquirePostDisplayOwnership2     SUCCESS
Framebuffer state                InitializedByFirmware
Target ID                        0xFFFFFFFF
Resolucao                        2560 x 1080
Pitch                            10240 bytes
Formato inferido pelo contrato   4 bytes por pixel
Endereco fisico                  0x00000000C0000000
Comprimento                      11059200 bytes
Offset no BAR VRAM               0
```

O contrato validou geometria, overflow e inclusao integral no unico BAR VRAM
resolvido pelo PnP antes de produzir a reserva `SCANOUT`, CPU-visible e
persistente.

## Alteracoes do pacote

- `BC250_RESOLVED_BAR_RESOURCE` agora fica preservado no contexto PnP;
- o miniport ativo chama a variante 2 quando presente e nao a chama no driver
  readonly;
- schema de telemetria passou para 8;
- endereco fisico e offset sao registrados em metades low/high de 32 bits;
- o retorno original `STATUS_NOT_SUPPORTED` continua preservado;
- pacote versionado como 1.1.0.0 e assinado sem erros ou avisos de Inf2Cat.

## Limites

Esta prova transfere e identifica o framebuffer herdado. Nao demonstra que o
restante da VRAM esta livre e nao identifica PSP TMR, page tables, rings,
firmware ou outras reservas. Nenhum comando PM4 foi submetido e nenhuma
aceleracao foi validada.

Evidencia bruta:

`docs\evidence\2026-08-25_19-29-47-DESKTOP-HQAG6T4-mcdm-replace`

**NAO VALIDADO EM HARDWARE**
