# BC-250 Display 3.22.0.0: tela preta, rollback e correção VidPN

Data: 28/29 de agosto de 2026  
Dispositivo: `PCI\VEN_1002&DEV_13FE`  
Veredito: **NÃO VALIDADO EM HARDWARE**

## Resultado físico do candidato 3.21.0.0

O binding controlado de `oem19.inf` foi aceito por
`UpdateDriverForPlugAndPlayDevicesW` e o miniport permaneceu temporariamente
em `bc250mcdm`, Código 0. A imagem, porém, ficou preta durante todo o ensaio.
Não houve confirmação visual válida.

O watchdog expirou e concluiu o rollback online às `23:01:08`, removendo
somente `oem19.inf` e restaurando `BasicDisplay`, Código 0. As evidências são:

- `docs/evidence/2026-08-28_22-59-01-DESKTOP-HQAG6T4-mcdm-display-binding`;
- `docs/evidence/2026-08-28_23-01-03-DESKTOP-HQAG6T4-mcdm-online-rollback`.

## Estado transacional observado

```text
[PREFLIGHT PASS]
  -> [WATCHDOG ARMADO]
  -> [BINDING API PASS]
  -> [bc250mcdm / Código 0]
  -> [TELA PRETA]
  -> [SEM CONFIRMAÇÃO VÁLIDA]
  -> [ROLLBACK PASS]
  -> [BasicDisplay / Código 0]
```

O snapshot intermediário comprovou:

- aquisição do GOP em `2560x1080`, pitch `10240`;
- framebuffer físico `0xC0000000`, comprimento `11059200` bytes;
- `DmmDisplayMapped=1`;
- serviço `bc250mcdm` em execução e dispositivo Código 0;
- consulta VidPN alcançada;
- nenhuma prova de `CommitVidPn`, VSync efetivo ou `PresentDisplayOnly`.

Código 0, sozinho, não provou um pipeline de apresentação funcional.

## Causa de software tratada no candidato 3.22.0.0

O validador 3.21 exigia que o stride da superfície de origem fosse exatamente
igual ao pitch físico do GOP. Esse contrato era contraditório com
`PresentDisplayOnly`, que já recebe e usa um pitch de origem independente.

O 3.22 agora:

- aceita stride de origem maior ou igual a `width * 4`;
- aceita fontes `A8R8G8B8` e `X8R8G8B8`;
- mantém o destino físico limitado ao pitch e à geometria do GOP;
- força uma cópia integral no primeiro present após commit/visibilidade;
- publica EDID, modo de target e modo de monitor com os mesmos blankings;
- para `2560x1080 @ 60 Hz`, publica total `2720x1125`, HSync `67.5 kHz`
  e pixel rate `183600000`;
- responde a `StatusRotation` com ângulo zero e sucesso.

## Telemetria do pipeline de display

O `SchemaVersion=95` adiciona provas para:

- resultado e contagem de `IsSupportedVidPn`;
- resultado, caminho e suporte de `CommitVidPn`;
- chamadas de `ControlInterrupt`;
- entrada, status e pitch do primeiro `PresentDisplayOnly`;
- contador de VSync observado no primeiro present.

A telemetria persistente é condicionada a `PASSIVE_LEVEL`. O DPC e o callback
sincronizado de VSync não escrevem no Registro. O status de present é
persistido apenas no primeiro frame, nunca a 60 Hz.

O watchdog só pode ser desarmado quando, além da confirmação visual explícita,
o dispositivo estiver em `bc250mcdm` Código 0, `SkipHwBringup=1` e a cadeia
VidPN/commit/present estiver comprovada pela telemetria do schema 95.

## Validação offline

- `driver/wdk/build-contract.bat`: PASS;
- `display_vidpn_test.exe`: PASS, incluindo stride `12288` com GOP pitch
  `10240` e rejeição de stride `4096`;
- auditoria de segurança: PASS;
- build, assinatura e Inf2Cat: PASS;
- auditoria do pacote: PASS (integridade somente).

Pacote 3.22.0.0:

- source SYS: `BCB344DF5DEE860BEB9D37F8B7DF4C08040023140B5962EB67649DEE0EE7FA69`;
- signed SYS: `FB0C0F8D5ABB008FDB0F156F770554326DA551A9D92C98120CD73662ED194C66`;
- INF: `A860BB6FE90967E2B7292D60EFE5F38A0C73B88332FDF1C1A2EF51F3B4C7913F`;
- CAT: `229D775BA7E7449A4056856252F64550960BEB2E43AF2B493BA77A784C3A21BF`.

## Próximo gate

O próximo ensaio deve repetir o ciclo stage sem binding, dry-run de binding e
binding com watchdog. O sucesso mínimo exige simultaneamente:

1. imagem visível durante toda a transição;
2. `bc250mcdm`, Código 0;
3. `DmmIsSupportedResult=1`;
4. `DmmCommitStatus=0`, caminho presente e suportado;
5. `DmmPresentEntered=1` e `DmmPresentStatus=0`;
6. confirmação visual explícita aceita pelo gate do schema 95.

Até essa prova física existir: **NÃO VALIDADO EM HARDWARE**.

## Resultado físico do 3.22 e sucessor 3.23

O segundo ensaio do 3.22 preservou a imagem durante toda a janela de 120
segundos e comprovou a cadeia de apresentação:

- `DmmIsSupportedCount=22`, status 0, suportado;
- `DmmCommitCount=1`, caminho presente e suportado, status 0;
- `DmmControlInterruptCount=18`;
- primeiro present com pitch `10240`, status 0;
- `DmmVsyncCount=6` no primeiro present;
- `bc250mcdm`, Código 0.

A confirmação foi recusada exclusivamente porque `SkipHwBringup` não havia
sido persistido no ramo protegido por compilação. O watchdog permaneceu armado
e restaurou `BasicDisplay`, Código 0, com sucesso. Evidências:

- `docs/evidence/2026-08-28_23-17-45-DESKTOP-HQAG6T4-mcdm-display-binding`;
- `docs/evidence/2026-08-28_23-19-47-DESKTOP-HQAG6T4-mcdm-online-rollback`.

O candidato 3.23.0.0 mantém o mesmo `SchemaVersion=95` e passa a registrar
explicitamente `SkipHwBringup=1` dentro do próprio ramo display-only compilado,
antes de `StartDevice` retornar sucesso. Não se trata de um tunable nem de
evidência fabricada: o valor descreve a exclusão em tempo de compilação de todo
o bloco destrutivo de bring-up.

## Resultado físico do 3.23.0.0

O ensaio iniciado às `23:23:04` passou pelo gate de display:

- binding via `UpdateDriverForPlugAndPlayDevicesW`: sucesso, sem reboot;
- dispositivo `bc250mcdm`, classe Display, Código 0;
- imagem visível durante a transição e após o binding;
- `SkipHwBringup=1` e `SchemaVersion=95` comprovados;
- `DmmIsSupportedCount=23` no gate, com resultado suportado;
- `DmmCommitCount=1`, caminho presente/suportado e status 0;
- primeiro `PresentDisplayOnly` alcançado com status 0;
- VSync observado antes do primeiro present (`DmmVsyncCount=6`);
- confirmação visual explícita aceita e watchdog desarmado às `23:23:23`.

Uma fotografia read-only 45 segundos depois da confirmação registrou:

- driver `3.23.0.0` em `2560x1080@60`;
- serviço `bc250mcdm` ainda em execução;
- dispositivo ainda em Código 0;
- `DmmIsSupportedCount=26` e `DmmControlInterruptCount=41`, mostrando
  atividade posterior ao gate;
- nenhum warning/erro relevante de Display, PnP, WHEA, BugCheck ou dxgkrnl no
  log do Sistema desde o binding.

Evidência primária:

- `docs/evidence/2026-08-28_23-23-04-DESKTOP-HQAG6T4-mcdm-display-binding`;
- `watchdog-disarm.json` registra a confirmação aceita;
- `post-confirm-soak-45s.json` registra a fotografia posterior.

Este resultado valida apenas o smoke test físico do caminho de display. Ainda
faltam soak prolongado, carga real do desktop, mudança de modo, desligar/ligar
o monitor, suspensão/retomada, reboot e testes de aceleração/compute. Até que
esses gates sejam concluídos: **NÃO VALIDADO EM HARDWARE**.

### Gate de carga interativa do desktop

Após o smoke test, o operador confirmou que o display 3.23 permaneceu estável
durante uso do desktop com carga interativa. A captura read-only feita às
`23:30:11` confirmou:

- dispositivo Display em Código 0 e serviço `bc250mcdm` em execução;
- versão `3.23.0.0`, modo `2560x1080@60`;
- hash do SYS instalado
  `21EFF7319A13282FD30BD254803BD9D3642397918CFA227A62C2DBB31FAADC6A`;
- `DmmIsSupportedCount=31`, resultado suportado;
- `DmmCommitCount=1` e `DmmCommitStatus=0`;
- `DmmPresentEntered=1` e `DmmPresentStatus=0`;
- `DmmControlInterruptCount=81`;
- nenhum warning/erro relevante no log do Sistema desde o binding.

Evidência: `desktop-load-final.json`, no diretório do ensaio 3.23. Este gate
aprova a carga interativa observada, mas não substitui os testes de reboot,
energia, mudança de modo, duração prolongada ou aceleração. O campo
`hardware_validated` permanece `false` deliberadamente.

### Watchdog persistente para o gate de reboot

Às `23:38:22`, o guard do primeiro reboot do candidato 3.23 foi armado sem
reiniciar a máquina. A tarefa `BC250-RebootGuard-3.23.0.0` foi registrada como:

- estado `Ready` e habilitada;
- trigger de boot;
- principal `SYSTEM` (`S-1-5-18`) com nível mais alto;
- timeout de confirmação visual de 300 segundos no próximo boot;
- candidato fixado em `3.23.0.0`, pacote `oem19.inf` e hashes esperados do
  INF, SYS, recovery e script de rollback;
- tentativa de rollback limitada a uma única execução;
- nenhuma chamada de reboot presente no armamento.

O guard compara o identificador do boot atual ao boot de armamento, impedindo
confirmação antecipada. No próximo boot, a confirmação exige Código 0, versão e
hash autorizados, `SkipHwBringup=1`, nova entrada de `StartDevice` e a cadeia
VidPN/Commit/Present. Sem confirmação, ele tenta o rollback online exato; uma
falha não é repetida indefinidamente e encaminha para recovery em Modo de
Segurança.

Evidência:
`docs/evidence/2026-08-28_23-38-22-DESKTOP-HQAG6T4-mcdm-reboot-watchdog`.
O arquivo `scheduled-task-after-arm.json` registra também
`reboot_executed=false`. O reboot continua pendente de autorização explícita.

### Resultado do primeiro reboot do 3.23

O Windows foi reiniciado manualmente com o guard armado. A imagem voltou e a
confirmação visual pós-reboot foi aceita dentro da janela de 300 segundos. A
auditoria final comprovou:

- identificador de boot diferente do boot de armamento;
- watchdog em `DISARMED`, associado ao boot novo;
- tarefa persistente removida pelo próprio watchdog após a confirmação;
- `bc250mcdm` `3.23.0.0`, Código 0, em `2560x1080@60`;
- SYS instalado com o hash autorizado
  `21EFF7319A13282FD30BD254803BD9D3642397918CFA227A62C2DBB31FAADC6A`;
- `StartDeviceEntered=1`, `SkipHwBringup=1`, schema 95;
- VidPN suportado, commit com status 0 e present com status 0;
- nenhum warning/erro relevante de display, dxgkrnl, WHEA ou BugCheck no boot.

Evidência: `post-reboot-final-audit.json`, no diretório do guard.

O valor persistido `DmmVsyncCount=0` não prova falha de VSync: a implementação
grava esse campo apenas no primeiro `PresentDisplayOnly`, e neste boot o
primeiro present ocorreu antes do primeiro tick. Como o valor nunca é atualizado
depois, o 3.23 não oferece prova persistente pós-reboot de que o callback de
VSync avançou. O reboot de display está aprovado, mas esta lacuna de
observabilidade deve ser corrigida antes dos gates de energia. O veredito global
permanece **NÃO VALIDADO EM HARDWARE**.

## Candidato 3.24.0.0 — prova tardia de VSync

O 3.24 corrige a lacuna de observabilidade encontrada no primeiro reboot sem
alterar a origem do VSync ou introduzir escrita de Registro na DPC:

- `DxgkCbNotifyInterrupt` continua exclusivamente dentro do callback passado a
  `DxgkCbSynchronizeExecution`;
- a DPC continua apenas sincronizando/notificando e incrementando o contador em
  memória;
- o primeiro Present não persiste mais `DmmVsyncCount=0`;
- depois de uma notificação real (`VsyncCount > 0`), o primeiro Present que
  termina com sucesso publica o contador em `PASSIVE_LEVEL`;
- `VsyncProofRecorded` usa claim atômico e impede escrita repetida por frame;
- os gates visual e pós-reboot aguardam no máximo 20 segundos e recusam
  confirmação se `DmmVsyncCount >= 1` não existir;
- o schema de telemetria foi elevado para 96.

O teste offline cobre a ordem observada no hardware: primeiro Present com
contador zero, VSync posterior e novo Present. Também comprova que a prova é
reivindicada apenas uma vez. A gravação foi posicionada depois da barreira de
memória e da liberação do acesso ao framebuffer, portanto descreve um Present
concluído, não apenas uma entrada na DDI.

Pacote `3.24.0.0`:

- source SYS: `F0BADF74A447C955408EF4B7A7B6AC99DA1F2F3AA4FB29690869A3B2781A4469`;
- signed SYS: `0D265F1F5574FCB87484575A391D0BD5AB517D1B1B1FF339B31CF08AA594E6D5`;
- INF: `DC300493B0553744813C4B42832708C3AF8E880B4F0C4A2D9B71BA75FA64909B`;
- CAT: `E2EA192176744033F6614EA5028B5FEA8040A2E005CF94B83EDDFE073EAC45AE`.

Build de contratos, testes de display, auditoria de segurança, assinatura,
Inf2Cat e auditoria de pacote: PASS. O host permaneceu em BasicDisplay, Código
0, sem stage ou binding. **NÃO VALIDADO EM HARDWARE**.

### Resultado físico inicial do 3.24

O pacote foi estagiado como `oem19.inf` e vinculado com watchdog de 120
segundos. A imagem permaneceu visível; a confirmação explícita foi aceita e o
watchdog foi desarmado. A evidência do binding registra:

- `bc250mcdm`, Código 0, versão `3.24.0.0`, em `2560x1080@60`;
- `SchemaVersion=96` e `SkipHwBringup=1`;
- VidPN suportado (`DmmIsSupportedResult=1`) e commit com status 0;
- primeiro Present com pitch 10240 e status 0;
- `DmmVsyncCount=7`, persistido somente após uma notificação VSync real e um
  Present posterior concluído;
- `DmmControlInterruptCount=22` no momento da confirmação;
- binding API bem-sucedida, sem reboot requerido;
- estado final do watchdog `DISARMED`.

Evidência primária:
`docs/evidence/2026-08-28_23-56-14-DESKTOP-HQAG6T4-mcdm-display-binding`.

Este resultado fecha a lacuna observada no 3.23 para o primeiro gate de
display. Ainda faltam carga interativa prolongada e reboot do próprio 3.24;
compute, aceleração e estabilidade de produção continuam sem validação física.
**NÃO VALIDADO EM HARDWARE**.
