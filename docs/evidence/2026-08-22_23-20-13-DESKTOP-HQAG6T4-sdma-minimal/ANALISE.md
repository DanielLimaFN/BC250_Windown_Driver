# Análise do primeiro WRITE_LINEAR com fence física

O ensaio carregou `cyan_skillfish2_sdma.bin` pelo PSP (ucode `0x34`, SHA-256
`15d0d362...f25e9e95`). O PSP respondeu status zero e a fence de transporte foi
alcançada. O F32 saiu de HALT, apresentou checksum `0x55B182E3` e percorreu
`0x0142..0x03E0`, com 17 endereços distintos em 24 amostras.

No ring SDMA0 em `GPU 0xF40FF00000`, um `WRITE_LINEAR` de quatro dwords avançou
RPTR de `0` para `0x44`, igual ao WPTR. A fence reservada em VRAM
`0x0FF03000` mudou de poison `0x43DAFFFE` para `0xBC250001`, valor que só
constava no pacote enviado à engine. Os quatro dwords de destino lidos pelo BAR0
coincidiram com o esperado e depois foram restaurados aos valores originais.
`STATUS_REG` mudou de `0x46DEE557` para `0x46DEED57` durante a operação.

O gate global falhou no teardown. O rollback restaurou ring/base/polling, mas o
WPTR ficou em `0x44`; o IOCTL retornou divergência. A causa no código era a
restauração de um WPTR menor sem armar `MINOR_PTR_UPDATE`, requisito já observado
na inicialização. A correção foi aplicada após a captura e o novo driver, SHA-256
`8003ccac...392d8aa`, foi instalado em `readonly`. Ela ainda precisa ser
confirmada após reboot limpo e repetição do mesmo ensaio.

O firmware SDMA continuou ativo até o próximo reboot. Não houve amostragem
sincronizada de clock/potência/temperatura nem readback físico conclusivo de
GFXOFF; `ActiveWgp=0` não substitui essa prova. Portanto, apesar da evidência
forte de execução do comando SDMA, o veredito formal da sessão permanece:

**NÃO VALIDADO EM HARDWARE**
