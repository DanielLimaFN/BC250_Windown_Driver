#ifndef BC250_AUDIT_H
#define BC250_AUDIT_H

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "../../inc/bc250dbg_abi.h"
#include "../lib/bc250lib.h"

/* Auditoria completa: valida o que ja foi conquistado e levanta o estado de
 * que a proxima etapa precisa. Devolve 0 se nao houve nenhuma falha. */
int Bc250RunAudit(BC250_CTX *ctx);

/* Bateria de validacao da SDMA0: tamanhos, regioes, wraparound do anel e
 * sequencias. Devolve 0 se nao houve falha. */
int Bc250RunStress(BC250_CTX *ctx);

#endif /* BC250_AUDIT_H */
