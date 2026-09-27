#ifndef BC250_FAULT_LATCH_CONTRACT_H
#define BC250_FAULT_LATCH_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =========================================================================
 * DRENO DO LATCH DE FAULT DA GCVM
 *
 * POR QUE EXISTE (medido na Corrida 55, P0):
 *
 *   LATCH_CNTL=0x3FFFFFFC -> Clear=0 AceitaAtualizacoes=0
 *
 * O bit 1 de GCVM_L2_PROTECTION_FAULT_CNTL
 * (ALLOW_SUBSEQUENT_PROTECTION_FAULT_STATUS_ADDR_UPDATES, sh_mask 12092) esta
 * em ZERO. O par STATUS/ADDR trava no PRIMEIRO fault e nao acompanha os
 * seguintes ate alguem escrever o bit 0
 * (CLEAR_PROTECTION_FAULT_STATUS_ADDR, sh_mask 12091). E o bit 0 tambem esta
 * em zero: nunca limpamos.
 *
 * Consequencia medida: das 6 TDRs da Corrida 55, apenas 2 tiveram fault
 * latchado, e o primeiro veio bit a bit IDENTICO ao da Corrida 54
 * (Status=0x0014115D). Os dois traziam MoreFaults=1 -- havia mais faults, e
 * nunca vimos nenhum deles.
 *
 * O QUE ESTE CONTRATO FAZ:
 *
 * Le o fault latchado, pulsa o CLEAR, le de novo, e repete ate o STATUS sair
 * zero ou ate o teto. Assim cada TDR passa a ver o PRIMEIRO fault da SUA
 * janela em vez de um residuo antigo, e a fila que MoreFaults anunciava
 * finalmente aparece.
 *
 * POR QUE NAO LIGAR ALLOW_SUBSEQUENT EM VEZ DISSO:
 *
 * Com ALLOW=1 o par passaria a mostrar o fault MAIS RECENTE, sobrescrevendo o
 * primeiro. O primeiro e' o causalmente interessante; os seguintes tendem a ser
 * cascata. Drenar preserva a ordem e ainda entrega a fila inteira. Ligar
 * ALLOW tambem alteraria de forma persistente uma configuracao que o POST
 * deixou, o que exigiria criterio proprio.
 *
 * CICLO DE VIDA (Lei 5): SNAPSHOT do CNTL -> pulsos de CLEAR -> RESTAURA o
 * CNTL exatamente como estava -> READBACK que prova a restauracao. Se a
 * restauracao nao conferir, o evidence diz, e quem chama decide.
 *
 * ARMADILHA TRATADA: se o CLEAR nao funcionar, o mesmo Status apareceria de
 * novo a cada volta e o dreno relataria N faults que sao UM SO'. O laco para
 * quando a leitura repete a anterior e marca ClearIneficaz. Contar repeticao
 * como fault novo seria a sexta ocorrencia de "o gate carimbava o falso
 * sucesso" neste projeto.
 * =========================================================================
 */

#define BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL      0x0000A120u
#define BC250_REG_GCVM_L2_PROTECTION_FAULT_STATUS    0x0000A130u
#define BC250_REG_GCVM_L2_PROTECTION_FAULT_ADDR_LO32 0x0000A134u
#define BC250_REG_GCVM_L2_PROTECTION_FAULT_ADDR_HI32 0x0000A138u

/* sh_mask 12091/12092 */
#define BC250_FAULT_CNTL_CLEAR_STATUS_ADDR_BIT       0x00000001u
#define BC250_FAULT_CNTL_ALLOW_SUBSEQUENT_BIT        0x00000002u

/* Teto do dreno. Nao e' estimativa de fila: e' limite de tempo dentro do TDR. */
#define BC250_FAULT_LATCH_MAX_DRAIN 8u

typedef enum BC250_FAULT_LATCH_STATUS {
    BC250_FAULT_LATCH_OK = 0,
    BC250_FAULT_LATCH_BAD_ARGUMENT,
    BC250_FAULT_LATCH_CNTL_UNREADABLE,
    BC250_FAULT_LATCH_RESTORE_FAILED
} BC250_FAULT_LATCH_STATUS;

typedef struct BC250_FAULT_LATCH_ENTRY {
    uint32_t Status;
    uint32_t AddrLo;
    uint32_t AddrHi;
} BC250_FAULT_LATCH_ENTRY;

typedef struct BC250_FAULT_LATCH_IO {
    void *Context;
    uint32_t (*Read32)(void *Context, uint32_t ByteOffset, uint32_t *Value);
    uint32_t (*Write32)(void *Context, uint32_t ByteOffset, uint32_t Value);
} BC250_FAULT_LATCH_IO;

typedef struct BC250_FAULT_LATCH_EVIDENCE {
    uint32_t CntlBefore;
    uint32_t CntlReadback;
    uint32_t RestorePassed;
    uint32_t Drained;          /* faults DISTINTOS observados */
    uint32_t ClearAcked;       /* o STATUS saiu zero depois de um CLEAR */
    uint32_t ClearIneficaz;    /* a leitura repetiu a anterior: o CLEAR nao pegou */
    uint32_t HitCeiling;       /* parou no teto, e pode haver mais na fila */
    uint32_t MoreFaultsSeen;   /* algum Status trouxe MORE_FAULTS=1 */
    BC250_FAULT_LATCH_ENTRY Entries[BC250_FAULT_LATCH_MAX_DRAIN];
} BC250_FAULT_LATCH_EVIDENCE;

/*
 * Le e drena o latch. SEMPRE restaura o CNTL ao valor de entrada, inclusive
 * nos caminhos de falha. Devolve RESTORE_FAILED se o readback divergir --
 * deixar o CNTL alterado seria pior que nao ter drenado.
 */
BC250_FAULT_LATCH_STATUS Bc250FaultLatchDrain(
    const BC250_FAULT_LATCH_IO *Io,
    BC250_FAULT_LATCH_EVIDENCE *Evidence);

#ifdef __cplusplus
}
#endif

#endif
