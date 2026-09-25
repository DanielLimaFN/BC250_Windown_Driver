import sys

def update_umd():
    with open('driver/umd/bc250_d3d_umd.c', 'r', encoding='utf-8') as f:
        text = f.read()

    target1 = """        presentHr = device->pDXGICallbacks->pfnPresentCb(
            device->hRTDevice.handle, &presentCb);

        Bc250UmdTrace("DXGI Present LEAVE cb: hr=0x%08X (hr!=0 significa que o "
            "dxgkrnl RECUSOU a apresentacao antes de chamar DxgkDdiPresent; "
            "compare com [Present #] no log do KMD)",
            (unsigned)presentHr);
        return presentHr;"""

    replacement1 = """        LARGE_INTEGER qpcStart, qpcEnd, qpcFreq;
        ULONGLONG cbElapsedUs = 0ull;
        static volatile LONG s_UmdPresentCbOrdinal = 0;
        LONG cbOrdinal = InterlockedIncrement(&s_UmdPresentCbOrdinal);

        if (QueryPerformanceFrequency(&qpcFreq) && qpcFreq.QuadPart > 0) {
            QueryPerformanceCounter(&qpcStart);
        } else {
            qpcStart.QuadPart = 0;
            qpcFreq.QuadPart = 1;
        }

        presentHr = device->pDXGICallbacks->pfnPresentCb(
            device->hRTDevice.handle, &presentCb);

        QueryPerformanceCounter(&qpcEnd);
        if (qpcFreq.QuadPart > 0 && qpcEnd.QuadPart >= qpcStart.QuadPart) {
            cbElapsedUs = (ULONGLONG)((qpcEnd.QuadPart - qpcStart.QuadPart) * 1000000ull / (ULONGLONG)qpcFreq.QuadPart);
        }

        if (!srcIsPrimary || cbOrdinal <= 16L || (cbOrdinal % 32L) == 0L || FAILED(presentHr)) {
            Bc250UmdTrace("[DX74_PRESENT_UMD_CB] n=%ld StartQpc=%llu EndQpc=%llu ElapsedUs=%llu hr=0x%08X",
                cbOrdinal, (unsigned long long)qpcStart.QuadPart, (unsigned long long)qpcEnd.QuadPart,
                (unsigned long long)cbElapsedUs, (unsigned)presentHr);
        }

        Bc250UmdTrace("DXGI Present LEAVE cb: hr=0x%08X (hr!=0 significa que o "
            "dxgkrnl RECUSOU a apresentacao antes de chamar DxgkDdiPresent; "
            "compare com [Present #] no log do KMD)",
            (unsigned)presentHr);
        return presentHr;"""

    target2 = """        presentHr = device->pDXGICallbacks->pfnPresentCb(
            device->hRTDevice.handle, &presentCb);

        Bc250UmdTrace("DXGI Present1 LEAVE cb: hr=0x%08X", (unsigned)presentHr);
        return presentHr;"""

    replacement2 = """        LARGE_INTEGER qpcStart, qpcEnd, qpcFreq;
        ULONGLONG cbElapsedUs = 0ull;
        static volatile LONG s_UmdPresent1CbOrdinal = 0;
        LONG cbOrdinal = InterlockedIncrement(&s_UmdPresent1CbOrdinal);

        if (QueryPerformanceFrequency(&qpcFreq) && qpcFreq.QuadPart > 0) {
            QueryPerformanceCounter(&qpcStart);
        } else {
            qpcStart.QuadPart = 0;
            qpcFreq.QuadPart = 1;
        }

        presentHr = device->pDXGICallbacks->pfnPresentCb(
            device->hRTDevice.handle, &presentCb);

        QueryPerformanceCounter(&qpcEnd);
        if (qpcFreq.QuadPart > 0 && qpcEnd.QuadPart >= qpcStart.QuadPart) {
            cbElapsedUs = (ULONGLONG)((qpcEnd.QuadPart - qpcStart.QuadPart) * 1000000ull / (ULONGLONG)qpcFreq.QuadPart);
        }

        if (!srcIsPrimary || cbOrdinal <= 16L || (cbOrdinal % 32L) == 0L || FAILED(presentHr)) {
            Bc250UmdTrace("[DX74_PRESENT1_UMD_CB] n=%ld StartQpc=%llu EndQpc=%llu ElapsedUs=%llu hr=0x%08X",
                cbOrdinal, (unsigned long long)qpcStart.QuadPart, (unsigned long long)qpcEnd.QuadPart,
                (unsigned long long)cbElapsedUs, (unsigned)presentHr);
        }

        Bc250UmdTrace("DXGI Present1 LEAVE cb: hr=0x%08X", (unsigned)presentHr);
        return presentHr;"""

    crlf = '\r\n' in text
    newline = '\r\n' if crlf else '\n'

    target1_norm = target1.replace('\r\n', '\n').replace('\n', newline)
    replacement1_norm = replacement1.replace('\r\n', '\n').replace('\n', newline)
    target2_norm = target2.replace('\r\n', '\n').replace('\n', newline)
    replacement2_norm = replacement2.replace('\r\n', '\n').replace('\n', newline)

    assert target1_norm in text, 'target1 not found in bc250_d3d_umd.c'
    assert target2_norm in text, 'target2 not found in bc250_d3d_umd.c'

    text = text.replace(target1_norm, replacement1_norm, 1)
    text = text.replace(target2_norm, replacement2_norm, 1)

    with open('driver/umd/bc250_d3d_umd.c', 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    print('bc250_d3d_umd.c updated.')

def update_kmd_display():
    with open('driver/wdk/bc250_display_ddi.c', 'r', encoding='utf-8') as f:
        text = f.read()

    target = """        if (pPresent->Flags.Blt || count <= 16L || (count % 32L) == 0L ||
            !NT_SUCCESS(presentStatus)) {
            Bc250DisplayLog("[DX72_PRESENT_KMD_TEMPO] n=%ld hContext=%p Blt=%u Flip=%u Status=0x%08X ElapsedUs=%llu\\n",
                count, hContext, (unsigned)pPresent->Flags.Blt,
                (unsigned)pPresent->Flags.Flip, (unsigned)presentStatus,
                (unsigned long long)elapsedUs);
        }"""

    replacement = """        if (pPresent->Flags.Blt || count <= 16L || (count % 32L) == 0L ||
            !NT_SUCCESS(presentStatus)) {
            Bc250DisplayLog("[DX74_PRESENT_KMD_TEMPO] n=%ld hContext=%p Blt=%u Flip=%u Status=0x%08X StartQpc=%llu EndQpc=%llu ElapsedUs=%llu\\n",
                count, hContext, (unsigned)pPresent->Flags.Blt,
                (unsigned)pPresent->Flags.Flip, (unsigned)presentStatus,
                (unsigned long long)presentTimerStart.QuadPart,
                (unsigned long long)presentTimerEnd.QuadPart,
                (unsigned long long)elapsedUs);
        }"""

    crlf = '\r\n' in text
    newline = '\r\n' if crlf else '\n'

    target_norm = target.replace('\r\n', '\n').replace('\n', newline)
    replacement_norm = replacement.replace('\r\n', '\n').replace('\n', newline)

    assert target_norm in text, 'target not found in bc250_display_ddi.c'
    text = text.replace(target_norm, replacement_norm, 1)

    with open('driver/wdk/bc250_display_ddi.c', 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    print('bc250_display_ddi.c updated.')

def update_kmd_miniport():
    with open('driver/wdk/bc250_active_miniport.c', 'r', encoding='utf-8') as f:
        text = f.read()

    target = """        if (context->GfxWaveProbePresent == 1u && context->GfxWaveProbe == 1u)
            Bc250DisplayLog("BC250_DIAG schema=1 level=INFO event=KMD_SDMA_SUBMIT ctx=%p node=0 dxg_fence=%llu hw_fence=%u vmid=%u path=%s accepted=1 va=0x%llX bytes=%u\\n",
                pSubmitCommand->hContext,
                (unsigned long long)pSubmitCommand->SubmissionFenceId,
                assignedHwFence, targetVmid, Bc250SubmitPathName(execPath),
                (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
                pSubmitCommand->DmaBufferSize);"""

    replacement = """        if (context->GfxWaveProbePresent == 1u && context->GfxWaveProbe == 1u) {
            LARGE_INTEGER submitQpc = KeQueryPerformanceCounter(NULL);
            Bc250DisplayLog("BC250_DIAG schema=1 level=INFO event=KMD_SDMA_SUBMIT ctx=%p node=0 dxg_fence=%llu hw_fence=%u vmid=%u path=%s accepted=1 va=0x%llX bytes=%u qpc=%llu\\n",
                pSubmitCommand->hContext,
                (unsigned long long)pSubmitCommand->SubmissionFenceId,
                assignedHwFence, targetVmid, Bc250SubmitPathName(execPath),
                (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
                pSubmitCommand->DmaBufferSize,
                (unsigned long long)submitQpc.QuadPart);
        }"""

    crlf = '\r\n' in text
    newline = '\r\n' if crlf else '\n'

    target_norm = target.replace('\r\n', '\n').replace('\n', newline)
    replacement_norm = replacement.replace('\r\n', '\n').replace('\n', newline)

    assert target_norm in text, 'target not found in bc250_active_miniport.c'
    text = text.replace(target_norm, replacement_norm, 1)

    with open('driver/wdk/bc250_active_miniport.c', 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    print('bc250_active_miniport.c updated.')

if __name__ == '__main__':
    update_umd()
    update_kmd_display()
    update_kmd_miniport()
    print('All DX.74 updates completed.')
