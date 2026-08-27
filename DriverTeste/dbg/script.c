/* script.c — motor de experimentos.
 *
 * Um experimento e uma lista de operacoes montada no user mode (o parser de
 * texto .bcs vive la, longe do kernel) e executada aqui em UMA chamada. O
 * ponto de executar tudo dentro do KMD e o timing: entre duas escritas de
 * registrador nao entra transicao de modo, nem escalonamento de IOCTL, nem
 * formatacao de log em user mode.
 *
 * Esta e tambem a ferramenta de replay do roadmap: um trace de umr do Linux
 * vira uma lista de ops, roda aqui, e o diff sai pronto — cada op registra o
 * valor lido de volta no log ring.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bc250dbg.h"

static UINT64 BcpNowUs(VOID)
{
    LARGE_INTEGER qpc = KeQueryPerformanceCounter(NULL);
    LONGLONG freq = g_Dev.QpcFrequency.QuadPart;

    if (freq <= 0) {
        return 0;
    }
    return (UINT64)((qpc.QuadPart * 1000000LL) / freq);
}

static VOID BcpStallUs(ULONG Us)
{
    if (Us == 0) {
        return;
    }
    if (Us < 50) {
        KeStallExecutionProcessor(Us);
    } else {
        LARGE_INTEGER t;
        t.QuadPart = -((LONGLONG)Us * 10);   /* us -> unidades de 100 ns */
        KeDelayExecutionThread(KernelMode, FALSE, &t);
    }
}

/* Devolve TRUE se o script deve parar agora. */
static BOOLEAN BcpShouldAbort(VOID)
{
    if (g_Dev.ScriptAbort) {
        return TRUE;
    }
    if (g_Dev.ThermalTrusted && g_Dev.ThermalTripped) {
        return TRUE;
    }
    return FALSE;
}

static VOID BcpRunOne(const BC250DBG_OP *Op, BC250DBG_OP_RESULT *Res,
                      ULONG ThermalLimitC)
{
    ULONG status = BC250DBG_OPS_OK;
    ULONG value = 0;

    Res->Status = BC250DBG_OPS_OK;
    Res->Value = 0;

    switch (Op->Op) {

    case BC250DBG_OP_REG_R:
        value = BcRegRead((ULONG)Op->A, &status);
        Res->Value = value;
        Res->Status = status;
        break;

    case BC250DBG_OP_REG_W:
        value = BcRegWrite((ULONG)Op->A, (ULONG)Op->B, &status);
        Res->Value = value;      /* readback, nao o valor escrito */
        Res->Status = status;
        break;

    case BC250DBG_OP_SMN_R:
        value = BcSmnRead((ULONG)Op->A, &status);
        Res->Value = value;
        Res->Status = status;
        break;

    case BC250DBG_OP_SMN_W:
        value = BcSmnWrite((ULONG)Op->A, (ULONG)Op->B, &status);
        Res->Value = value;
        Res->Status = status;
        break;

    case BC250DBG_OP_SMU: {
        ULONG smuStatus = 0, smuResult = 0;
        NTSTATUS st = BcSmuMsg((ULONG)Op->A, (ULONG)Op->B, (ULONG)Op->C,
                               (ULONG)Op->D, 0, &smuStatus, &smuResult);
        Res->Value = ((UINT64)smuStatus << 32) | smuResult;
        if (st == STATUS_ACCESS_DENIED || st == STATUS_DEVICE_POWER_FAILURE) {
            Res->Status = BC250DBG_OPS_DENIED;
        } else if (!NT_SUCCESS(st)) {
            Res->Status = BC250DBG_OPS_TIMEOUT;
        } else if (smuStatus != BC250_SMU_OK) {
            Res->Status = BC250DBG_OPS_MISMATCH;
        }
        break;
    }

    case BC250DBG_OP_DELAY_US:
        BcpStallUs((ULONG)Op->A);
        break;

    case BC250DBG_OP_POLL_REG:
    case BC250DBG_OP_POLL_SMN: {
        UINT64 deadline = BcpNowUs() + (Op->D ? Op->D : 100000ull);
        BOOLEAN hit = FALSE;

        for (;;) {
            if (Op->Op == BC250DBG_OP_POLL_REG) {
                value = BcRegReadRaw((ULONG)Op->A);
            } else {
                value = BcSmnReadRaw((ULONG)Op->A);
            }
            if ((value & (ULONG)Op->B) == (ULONG)Op->C) {
                hit = TRUE;
                break;
            }
            if (BcpNowUs() >= deadline || BcpShouldAbort()) {
                break;
            }
            BcpStallUs(20);
        }
        Res->Value = value;
        Res->Status = hit ? BC250DBG_OPS_OK : BC250DBG_OPS_TIMEOUT;
        break;
    }

    case BC250DBG_OP_EXPECT_REG:
    case BC250DBG_OP_EXPECT_SMN:
        if (Op->Op == BC250DBG_OP_EXPECT_REG) {
            value = BcRegRead((ULONG)Op->A, &status);
        } else {
            value = BcSmnRead((ULONG)Op->A, &status);
        }
        Res->Value = value;
        if (status != BC250DBG_OPS_OK) {
            Res->Status = status;
        } else {
            Res->Status = ((value & (ULONG)Op->B) == (ULONG)Op->C)
                        ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
        }
        break;

    case BC250DBG_OP_MARK:
        Res->Value = Op->A;
        break;

    case BC250DBG_OP_THERMAL_CHK:
        if (!BcThermalOk(ThermalLimitC)) {
            Res->Status = BC250DBG_OPS_ABORTED;
        }
        Res->Value = (UINT64)(LONG64)g_Dev.ThermalMilliC;
        break;

    default:
        Res->Status = BC250DBG_OPS_BADOP;
        break;
    }
}

NTSTATUS BcScriptRun(const BC250DBG_SCRIPT_IN *In, const BC250DBG_OP *Ops,
                     BC250DBG_SCRIPT_OUT *Out, BC250DBG_OP_RESULT *Results,
                     ULONG MaxResults)
{
    ULONG i, executed = 0, failures = 0;
    ULONG fatal = BC250DBG_OPS_OK;
    UINT64 start;

    if (In->OpCount == 0 || In->OpCount > MaxResults) {
        return STATUS_INVALID_PARAMETER;
    }

    KeWaitForSingleObject(&g_Dev.ScriptMutex, Executive, KernelMode,
                          FALSE, NULL);
    InterlockedExchange(&g_Dev.ScriptAbort, 0);

    start = BcpNowUs();

    BcLogEvent(BC250DBG_LVL_INFO, BC250DBG_SUB_SCRIPT, BC250DBG_EV_SCRIPT_BEGIN,
               In->OpCount, In->ThermalLimitC, 0, 0);
    /* O nome vem do user mode e ja veio validado como ASCII terminado. */
    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_SCRIPT, "script: %s", In->Name);

    for (i = 0; i < In->OpCount; i++) {
        UINT64 t0, t1;

        if (Ops[i].Op == BC250DBG_OP_END) {
            break;
        }

        if (BcpShouldAbort()) {
            Results[i].Status = BC250DBG_OPS_ABORTED;
            Results[i].Value = 0;
            Results[i].ElapsedUs = 0;
            executed++;
            failures++;
            fatal = BC250DBG_OPS_ABORTED;
            break;
        }

        t0 = BcpNowUs();
        BcpRunOne(&Ops[i], &Results[i], In->ThermalLimitC);
        t1 = BcpNowUs();
        Results[i].ElapsedUs = t1 - t0;
        Results[i].Reserved = 0;
        executed++;

        if ((Ops[i].Flags & BC250DBG_OPF_NOLOG) == 0) {
            BcLogEvent(Results[i].Status == BC250DBG_OPS_OK
                         ? BC250DBG_LVL_INFO : BC250DBG_LVL_WARN,
                       BC250DBG_SUB_SCRIPT, BC250DBG_EV_SCRIPT_OP,
                       i, Ops[i].Op, Results[i].Value, Results[i].Status);
        }

        if (Results[i].Status != BC250DBG_OPS_OK) {
            failures++;
            if ((Ops[i].Flags & BC250DBG_OPF_CONTINUE) == 0) {
                fatal = Results[i].Status;
                break;
            }
        }
    }

    Out->Size      = sizeof(*Out);
    Out->OpCount   = executed;
    Out->Failures  = failures;
    Out->Status    = fatal;
    Out->ElapsedUs = BcpNowUs() - start;

    BcLogEvent(BC250DBG_LVL_INFO, BC250DBG_SUB_SCRIPT, BC250DBG_EV_SCRIPT_END,
               executed, failures, fatal, Out->ElapsedUs);

    KeReleaseMutex(&g_Dev.ScriptMutex, FALSE);
    return STATUS_SUCCESS;
}
