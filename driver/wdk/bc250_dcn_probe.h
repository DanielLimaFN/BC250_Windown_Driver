/* ===========================================================================
 * bc250_dcn_probe.h — Integração de Sonda DCN 2.0.1 em Modo Kernel (WDK)
 *
 * Target: AMD BC-250 APU (1002:13FE, GC 10.1.3 / Cyan Skillfish 2, DCN 2.0.1)
 *
 * Executa as Baterias A, B e C durante o StartDevice do KMD.
 * Todas as operações são puramente não-intrusivas (somente leitura e DP AUX).
 * =========================================================================== */

#ifndef BC250_DCN_PROBE_H
#define BC250_DCN_PROBE_H

#include <ntddk.h>
#include "bc250_dcn_probe_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Executa a bateria de diagnóstico do DCN 2.0.1 utilizando a BAR5 mapeada.
 * Registra os resultados no log do display via Bc250DisplayLog.
 * Falha de leitura do DCN é tratada como fail-closed e não impede o StartDevice.
 */
NTSTATUS Bc250DcnProbeExecute(_In_ PHYSICAL_ADDRESS MmioPhysical);

/* Corrida 251 (doc 664): foto do clock gating por bloco e refresh real, so' leitura. */
NTSTATUS Bc250BootProbeCgAndRefresh(_In_ PHYSICAL_ADDRESS MmioPhysical, _In_ BOOLEAN DoCgPhoto);

#ifdef __cplusplus
}
#endif

#endif /* BC250_DCN_PROBE_H */
