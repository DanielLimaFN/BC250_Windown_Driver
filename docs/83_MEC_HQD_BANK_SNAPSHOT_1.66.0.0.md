# MEC/HQD — prova de banco e snapshot físico 1.66.0.0

## Resultado

O candidato 1.66 executou no BC-250 a máquina transacional:

`PREFLIGHT → SNAPSHOT → APPLY POISON INERTE → READBACK CRUZADO → SNAPSHOT HQD 2x → ROLLBACK/TEARDOWN`

A fila permaneceu inativa antes de qualquer escrita. O único campo HQD alterado
foi `CP_HQD_QUEUE_PRIORITY`, limitado pela máscara `0xF`; nenhum endereço de
MQD/ring, cursor, doorbell ou bit `ACTIVE` foi programado.

Telemetria física:

- banco original: seletor observado `0`, `ACTIVE=0`, prioridade `0`;
- banco candidato MEC1/P0/Q0: `ACTIVE=0`, prioridade `0`;
- poison: prioridade `1`, readback `1`;
- leitura cruzada no banco 0: `0`;
- leitura cruzada no banco candidato: `1`;
- prioridade candidata restaurada para `0` e banco 0 permaneceu em `0`;
- seletor final restaurado para `0`;
- 47 leituras e 7 escritas; poison, isolamento, captura, rollback e prontidão
  física iguais a `1`.

O snapshot HQD selecionado foi idêntico em duas passagens:

| Campo | Valor |
|---|---:|
| `CP_MQD_BASE_ADDR[_HI]` | `0x0000000000000000` |
| `CP_HQD_ACTIVE` | `0x00000000` |
| `CP_HQD_VMID` | `0x00000000` |
| `CP_HQD_PQ_BASE[_HI]` | `0x0000000000000000` |
| `CP_HQD_PQ_RPTR` | `0` |
| `CP_HQD_PQ_WPTR_LO/HI` | `0/0` |
| `CP_HQD_PQ_DOORBELL_CONTROL` | `0x00000000` |
| `CP_HQD_PQ_CONTROL` | `0x00308509` |

## Conclusão técnica

Esta prova resolve o resultado não-ecoante da 1.64: `GRBM_GFX_CNTL` não retorna
o seletor escrito nesta unidade, mas a independência física do campo de
prioridade demonstra que a escrita seleciona efetivamente bancos distintos.
O banco MEC1/P0/Q0 estava limpo, inativo e estável, portanto existe um baseline
confiável para uma futura transação de programação MQD.

Isso **não** prova execução compute: `CP_HQD_ACTIVE` não foi ativado, nenhum
doorbell foi tocado, nenhum PM4 foi consumido e nenhuma fence compute foi
gravada.

## Pacote e evidência

- DriverVer: `08/27/2026,1.66.0.0`;
- SchemaVersion: `72`;
- source SYS: `30529250629F8AB6C853380CBFEBEF2C65C4BC3790035E4373BB8FAC4789F6D1`;
- signed SYS: `4A0E20535D96C27FDDE611CC5479EB32474C0AC0F2CD8D3BA34C52E1C7BB17D3`;
- INF: `99EBB8A9059DEA04B8A4C8DCB154225C47A1D513FCD17660922B769F54C1DF72`;
- CAT: `808F0AFCF240ABEA12CB5487E9F05C792B939FB3EEE97C4E1CAD7A43F889E985`;
- binding: `oem19.inf`, serviço `bc250mcdm`, Código 43 fail-closed;
- auditoria: `BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS`;
- evidência: `docs/evidence/2026-08-27_15-00-07-DESKTOP-HQAG6T4-mcdm-replace`.

## Próximo gate

Construir primeiro uma transação MQD/HQD reversível, ainda sem doorbell:

1. reservar ring, MQD, RPTR/WPTR writeback, EOP e fence em memória MC plana;
2. aplicar MQD e registradores HQD com `ACTIVE=0`, usando snapshot e RMW;
3. fazer readback completo e conferir bases, VMID, cursores e controles;
4. restaurar o banco e a memória, provando que nenhuma execução ocorreu;
5. somente em candidato posterior, ativar a HQD e publicar o WPTR/doorbell para
   buscar o tripé PM4 físico.

**NÃO VALIDADO EM HARDWARE.**
