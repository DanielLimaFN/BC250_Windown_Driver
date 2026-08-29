# Handoff — BC-250 Display 3.24.0.0

## Estado atual

O driver ativo é `bc250mcdm` `3.24.0.0`, em Código 0, com imagem visível em
`2560x1080@60`. O primeiro gate físico de apresentação foi aprovado:

- `SchemaVersion=96`, `SkipHwBringup=1`;
- VidPN suportado, Commit status 0 e Present status 0;
- prova persistente `DmmVsyncCount=7`;
- watchdog de binding desarmado por confirmação visual.

Evidência: `docs/evidence/2026-08-28_23-56-14-DESKTOP-HQAG6T4-mcdm-display-binding`.

## Regra crítica

Enquanto o 3.24 estiver instalado, **não gerar outro pacote em
`driver/wdk/package/output_active`**. O rollback online compara o SYS e INF
instalados com o manifesto desse diretório. Primeiro concluir os gates do 3.24
ou executar rollback controlado para BasicDisplay; só então criar novo
candidato.

`StartDevice` permanece display-only: não reativar GCVM/RLC/PFP/firmware ou
qualquer teste destrutivo nesse caminho. A DPC de VSync não grava no Registro;
a prova é persistida apenas por um Present bem-sucedido em `PASSIVE_LEVEL`.

## Próximos gates

1. Carga interativa do desktop por 10 minutos, seguida de captura read-only.
2. Armar o watchdog de reboot 3.24 como Administrador:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\arm-bc250mcdm-reboot-watchdog.ps1 -Execute -IAcceptPersistentRebootGuard
   ```

   O armador só movimenta o guard 3.23 anterior se ele estiver comprovadamente
   `DISARMED`; a movimentação é para quarentena recuperável.
3. Após autorização explícita, reboot manual. Com imagem visível, confirmar
   usando o comando emitido pelo armador. O guard exige também
   `DmmVsyncCount >= 1`.
4. Se qualquer tela preta ocorrer, não confirmar; aguardar o watchdog. Se não
   recuperar, usar Modo de Segurança e `C:\BC250_RECOVERY.cmd`.

## Limites da conclusão

Display inicial e VSync foram validados no silício. Compute, aceleração,
energia/suspensão, mudança de modo e estabilidade prolongada ainda não foram.
Veredito: **NÃO VALIDADO EM HARDWARE**.
