/* ===========================================================================
 * bc250_vcn_probe.h — Sonda e Ativacao Experimental do VCN 2.0 (Video Core Next)
 *
 * Target: AMD BC-250 APU (1002:13FE, GC 10.1.3 / Cyan Skillfish 2 / Oberon)
 * Microcodigo: ps5_vcn.bin (VCN 2.0.3, proveniente dos patches do PS5 Linux)
 *
 * Acionado via tunable 'VcnProbe':
 *   0 = Inativo (producao padrao, fail-closed)
 *   1 = Leitura passiva de status e registradores UVD/VCN
 *   2 = Sequencia completa de ungate, reset PS5 e boot de firmware (ps5_vcn.bin)
 * =========================================================================== */

#ifndef BC250_VCN_PROBE_H
#define BC250_VCN_PROBE_H

#include <ntddk.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Executa a sonda/ativacao do VCN 2.0 utilizando o BAR5 MMIO e a VRAM/DRAM fisica.
 * Nao-bloqueante e fail-closed: falhas de hardware nao impedem o StartDevice.
 */
NTSTATUS Bc250VcnProbeExecute(
    _In_ PHYSICAL_ADDRESS MmioPhysical,
    _In_opt_ void *Context,
    _In_ ULONG Mode);

void Bc250VcnProbeTeardown(void);

#ifdef __cplusplus
}
#endif

#endif /* BC250_VCN_PROBE_H */
