/* thermal.c — watchdog termico.
 *
 * Postura deste modulo: um watchdog que confia num sensor nao calibrado e
 * PIOR que nenhum watchdog, porque produz falsa seguranca. As sondas SMN de
 * temperatura da BC-250 estao documentadas no projeto anterior como "raw
 * probes" — o endereco e conhecido, a escala nao. Entao:
 *
 *   - Trusted comeca em 0;
 *   - com Trusted == 0 o driver RECUSA qualquer mensagem SMU de risco alto
 *     (as que mexem em clock/tensao). Falha fechada;
 *   - o operador calibra comparando com o Linux (sensors / umr) e declara a
 *     escala por IOCTL_BC250DBG_THERMAL_SET. Isso fica registrado no log.
 *
 * O watchdog roda em thread propria em PASSIVE_LEVEL porque ler o sensor
 * envolve acesso SMN e espera — nada disso pode acontecer em DPC.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bc250dbg.h"

#define BC250_THERMAL_PERIOD_MS   500u
#define BC250_THERMAL_DEFAULT_C   85u

static VOID BcpThermalThread(PVOID Context)
{
    LARGE_INTEGER timeout;

    UNREFERENCED_PARAMETER(Context);

    timeout.QuadPart = -((LONGLONG)BC250_THERMAL_PERIOD_MS * 10000);

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_THERMAL,
              "watchdog iniciado (limite %u C)", (ULONG)g_Dev.ThermalLimitC);

    for (;;) {
        NTSTATUS st = KeWaitForSingleObject(&g_Dev.ThermalStopEvent, Executive,
                                            KernelMode, FALSE, &timeout);
        if (st == STATUS_SUCCESS || g_Dev.ThermalStopping) {
            break;
        }
        BcThermalSample();
    }

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_THERMAL, "watchdog encerrado");
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS BcThermalStart(VOID)
{
    HANDLE h = NULL;
    NTSTATUS st;

    g_Dev.ThermalSource        = 1;        /* junction, o mais conservador */
    g_Dev.ThermalScaleNum      = 1000;     /* raw tratado como C inteiro   */
    g_Dev.ThermalScaleDen      = 1;
    g_Dev.ThermalOffsetMilliC  = 0;
    InterlockedExchange(&g_Dev.ThermalTrusted, 0);
    InterlockedExchange(&g_Dev.ThermalTripped, 0);
    InterlockedExchange(&g_Dev.ThermalLimitC, BC250_THERMAL_DEFAULT_C);
    InterlockedExchange(&g_Dev.ThermalStopping, 0);

    KeInitializeEvent(&g_Dev.ThermalStopEvent, NotificationEvent, FALSE);

    st = PsCreateSystemThread(&h, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                              BcpThermalThread, NULL);
    if (!NT_SUCCESS(st)) {
        return st;
    }

    st = ObReferenceObjectByHandle(h, THREAD_ALL_ACCESS, NULL, KernelMode,
                                   (PVOID *)&g_Dev.ThermalThread, NULL);
    ZwClose(h);
    if (!NT_SUCCESS(st)) {
        g_Dev.ThermalThread = NULL;
    }
    return STATUS_SUCCESS;
}

VOID BcThermalStop(VOID)
{
    if (g_Dev.ThermalThread == NULL) {
        return;
    }

    InterlockedExchange(&g_Dev.ThermalStopping, 1);
    KeSetEvent(&g_Dev.ThermalStopEvent, IO_NO_INCREMENT, FALSE);

    KeWaitForSingleObject(g_Dev.ThermalThread, Executive, KernelMode,
                          FALSE, NULL);
    ObDereferenceObject(g_Dev.ThermalThread);
    g_Dev.ThermalThread = NULL;
}

static ULONG BcpRawForSource(VOID)
{
    switch (g_Dev.ThermalSource) {
    case 0:  return g_Dev.RawEdge;
    case 2:  return g_Dev.RawMem;
    default: return g_Dev.RawJunction;
    }
}

VOID BcThermalSample(VOID)
{
    ULONG raw;
    LONG milliC;

    if (g_Dev.Mmio == NULL) {
        return;
    }

    g_Dev.RawEdge     = BcSmnReadRaw(BC250_SMN_TEMP_EDGE);
    g_Dev.RawJunction = BcSmnReadRaw(BC250_SMN_TEMP_JUNCTION);
    g_Dev.RawMem      = BcSmnReadRaw(BC250_SMN_TEMP_MEM);
    g_Dev.FanRpm      = BcSmnReadRaw(BC250_SMN_FAN_RPM);
    g_Dev.FanPwm      = BcSmnReadRaw(BC250_SMN_FAN_PWM);

    raw = BcpRawForSource();

    /* 0xFFFFFFFF = sonda ausente/inacessivel. Nunca converter isso. */
    if (raw == 0xFFFFFFFFu) {
        InterlockedExchange(&g_Dev.ThermalMilliC, 0);
        if (g_Dev.ThermalTrusted) {
            InterlockedExchange(&g_Dev.ThermalTrusted, 0);
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_THERMAL,
                      "sonda sumiu; confianca revogada");
        }
        return;
    }

    if (g_Dev.ThermalScaleDen == 0) {
        g_Dev.ThermalScaleDen = 1;
    }
    milliC = (LONG)(((LONGLONG)raw * g_Dev.ThermalScaleNum) /
                    g_Dev.ThermalScaleDen) + g_Dev.ThermalOffsetMilliC;

    InterlockedExchange(&g_Dev.ThermalMilliC, milliC);

    BcLogEvent(BC250DBG_LVL_TRACE, BC250DBG_SUB_THERMAL,
               BC250DBG_EV_THERMAL_SAMPLE,
               g_Dev.RawEdge, g_Dev.RawJunction, g_Dev.RawMem,
               (UINT64)(LONG64)milliC);

    if (!g_Dev.ThermalTrusted) {
        return;   /* sem calibracao nao existe decisao a tomar */
    }

    if (milliC >= (LONG)g_Dev.ThermalLimitC * 1000) {
        if (InterlockedExchange(&g_Dev.ThermalTripped, 1) == 0) {
            ULONG status, result;

            BcLogEvent(BC250DBG_LVL_ERROR, BC250DBG_SUB_THERMAL,
                       BC250DBG_EV_THERMAL_TRIP,
                       (UINT64)(LONG64)milliC, g_Dev.ThermalLimitC, 0, 0);

            /* 1) derruba qualquer script em andamento */
            InterlockedExchange(&g_Dev.ScriptAbort, 1);

            /* 2) desarma o driver: nada mais escreve ate re-armar a mao */
            InterlockedExchange(&g_Dev.ArmLevel, BC250DBG_ARM_READONLY);

            /* 3) solta clock e tensao forcados (mensagens classificadas
             *    como seguras — nao dependem de armamento) */
            (VOID)BcSmuMsg(0, 0x3A, 0, 0, 200, &status, &result);
            (VOID)BcSmuMsg(0, 0x3C, 0, 0, 200, &status, &result);

            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_THERMAL,
                      "TRIP %ld mC: driver desarmado", milliC);
        }
    } else if (milliC < ((LONG)g_Dev.ThermalLimitC - 5) * 1000) {
        /* histerese de 5 C para nao oscilar */
        if (InterlockedExchange(&g_Dev.ThermalTripped, 0) == 1) {
            BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_THERMAL,
                      "temperatura normalizada (%ld mC)", milliC);
        }
    }
}

VOID BcThermalGet(BC250DBG_THERMAL *Out)
{
    RtlZeroMemory(Out, sizeof(*Out));
    Out->Size          = sizeof(*Out);
    Out->RawEdge       = g_Dev.RawEdge;
    Out->RawJunction   = g_Dev.RawJunction;
    Out->RawMem        = g_Dev.RawMem;
    Out->FanRpm        = g_Dev.FanRpm;
    Out->FanPwm        = g_Dev.FanPwm;
    Out->MilliC        = (LONG)g_Dev.ThermalMilliC;
    Out->Trusted       = (ULONG)g_Dev.ThermalTrusted;
    Out->LimitC        = (ULONG)g_Dev.ThermalLimitC;
    Out->Tripped       = (ULONG)g_Dev.ThermalTripped;
    Out->ScaleNum      = (ULONG)g_Dev.ThermalScaleNum;
    Out->ScaleDen      = (ULONG)g_Dev.ThermalScaleDen;
    Out->OffsetMilliC  = g_Dev.ThermalOffsetMilliC;
    Out->Source        = g_Dev.ThermalSource;
}

NTSTATUS BcThermalSet(const BC250DBG_THERMAL_SET *In)
{
    if (In->ScaleDen == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if (In->LimitC < 40 || In->LimitC > 105) {
        return STATUS_INVALID_PARAMETER;
    }
    if (In->Source > 2) {
        return STATUS_INVALID_PARAMETER;
    }

    g_Dev.ThermalSource       = In->Source;
    g_Dev.ThermalScaleNum     = (LONG)In->ScaleNum;
    g_Dev.ThermalScaleDen     = (LONG)In->ScaleDen;
    g_Dev.ThermalOffsetMilliC = In->OffsetMilliC;
    InterlockedExchange(&g_Dev.ThermalLimitC, (LONG)In->LimitC);
    InterlockedExchange(&g_Dev.ThermalTrusted, In->Trusted ? 1 : 0);

    BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_THERMAL,
              "calib src=%u %u/%u lim=%uC trust=%u",
              In->Source, In->ScaleNum, In->ScaleDen, In->LimitC, In->Trusted);

    BcThermalSample();
    if (g_Dev.ThermalMilliC < (LONG)In->LimitC * 1000) {
        InterlockedExchange(&g_Dev.ThermalTripped, 0);
        InterlockedExchange(&g_Dev.ScriptAbort, 0);
    }
    return STATUS_SUCCESS;
}

BOOLEAN BcThermalOk(ULONG LimitOverrideC)
{
    LONG limit;

    /* Sem calibracao nao ha permissao para operacao de risco. */
    if (!g_Dev.ThermalTrusted) {
        return FALSE;
    }
    if (g_Dev.ThermalTripped) {
        return FALSE;
    }

    limit = LimitOverrideC ? (LONG)LimitOverrideC : g_Dev.ThermalLimitC;
    return (g_Dev.ThermalMilliC < limit * 1000);
}
