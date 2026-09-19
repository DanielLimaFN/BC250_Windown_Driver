# Handoff 196 — acionador físico protegido do tripé Node 1 da UMD

**Data:** 08/09/2026  
**Trabalho:** implementação e validação offline  
**Corridas físicas:** 0  
**Veredito:** **NÃO VALIDADO EM HARDWARE**

## Entrega

O teste `B7_UMD_NODE1_TRIPOD` agora possui acionador e coletor físicos, mas eles não foram
executados. A autorização exige conjuntamente:

- `HKLM\\SOFTWARE\\BC250\\Umd\\Node1TripodArmed == 1`;
- token de 128 bits hexadecimal no registro;
- o mesmo token no ambiente exclusivo do processo;
- nome exato `bc250-umd-node1-tripod.exe`.

`dwm.exe` é recusado mesmo com o registro armado. O disparo é single-shot por processo. Ao
final, inclusive em erro, o runner remove o token do ambiente e desarma o registro.

## Comportamento fail-closed

O modo de prova publica a tabela apenas para alcançar `CreateDevice`, executa a transação
do doc 195 e **sempre devolve `E_NOTIMPL`**. Sucesso do device D3D é tratado pelo harness
como falha de segurança. `BC250_D3D10_11_NATIVE_READY` permanece 0.

O harness aceita a perna UMD somente quando o log do próprio PID contém:

```text
NODE1_TRIPOD_RESULT: hr=0x00000000
Poison=1 Submitted=1 Fence=1 Data=1 Cleanup=1
```

O runner combina isso com telemetria KMD antes/depois: submit aceito, retirement, zero
rejeições e `P1LastWptrMmioReadback == P1LastRptrMmio`.

## Arquivos

- `tools/bc250-umd-node1-tripod.c` — executável D3D10.1 de nome fixo;
- `tools/arm-umd-node1-tripod.ps1` — inspect/arm/disarm, sem iniciar harness;
- `tools/test-b7-umd-node1-tripod.ps1` — dry-run por default e execução transacional;
- `tools/audit-umd-node1-submission.py` — gate estrutural e mutações;
- `driver/umd/bc250_umd_caps_probe.c` — política pura de autorização;
- `driver/umd/bc250_d3d_umd.c` — acionamento single-shot e resultado estruturado no log.

## Verificações offline

| Verificação | Resultado |
|---|---:|
| `driver\\umd\\build-umd.bat` | PASS; harness físico apenas compilado |
| política de autorização | PASS, incluindo DWM/token inválido/desarmado |
| harness sem token | recusado antes de enumerar DXGI, exit 2 |
| `test-b7-umd-node1-tripod.ps1` sem `-Execute` | DRY-RUN PASS; registro e GPU intocados |
| parse dos dois scripts PowerShell | PASS |
| auditoria semântica da UMD | PASS |

## Bloqueador medido antes da corrida

O `output_lab.manifest.json` existente é da versão 3.81.1 e anterior a esta UMD. O runner
compara o SHA-256 de `C:\\Windows\\System32\\bc250umd.dll` com o build auditado e recusará
a execução enquanto divergirem. Reconstruir/assinar/instalar um pacote de bring-up é uma
decisão destrutiva do dono do projeto e não foi feita nesta sessão.

Depois de existir um candidato novo, ainda valem os preflights do runner: Código 0,
watchdog `ARMED`, processo exato do watchdog vivo, telemetria completa e aceite explícito.
Teto: uma corrida. Evidência em
`docs/evidence/<data>_B7_UMD_NODE1_TRIPOD/B7_UMD_NODE1_TRIPOD_<hora>/`.

Até a corrida: **NÃO VALIDADO EM HARDWARE**.
