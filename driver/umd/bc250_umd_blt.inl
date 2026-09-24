/* Linear, same-format DXGI Blt. Completion is written by GPU, never CPU.
 * No rendering capability is implied by this restricted transfer backend. */
#define BC250_BLT_WORDS 32768u

/*
 * A maquinaria de contexto do blt (paging queue, contexto virtual, anel de
 * slots de command buffer e fence monitorada) foi REMOVIDA em 13/09.
 *
 * Ela existia para submeter a composicao como contexto no Node 0 -- o no de
 * paginacao. Medicao da corrida 08:12: 953 buffers de paginacao executaram
 * sem UM TDR, e as 5 submissoes de contexto desta UMD produziram 5 TDRs com
 * a engine OCIOSA e o anel DRENADO. O caminho passou a ser o do KMD, que fez
 * 584 blts consecutivos sem TDR em 12/09 13:01.
 *
 * O codigo removido esta no historico do git; nao fica dead code aqui.
 */

/*
 * Blt de composicao executado PELO KMD, via D3DKMTEscape.
 *
 * Nao cria contexto, nao submete no no de paginacao e nao gera pacote para o
 * escalonador -- e' exatamente por isso que existe. A prova de execucao no
 * silicio continua sendo a do KMD (RPTR==WPTR e fence sobre poison dentro de
 * Bc250SdmaPresentBlt); esta funcao nao inventa nenhuma.
 */
static HRESULT Bc250UmdBltViaKmd(BC250_UMD_DEVICE *d, BC250_UMD_RESOURCE *dst,
    BC250_UMD_RESOURCE *src, const DXGI_DDI_ARG_BLT *blt)
{
    BC250_ESCAPE_COMPOSITE_BLT payload;
    D3DDDICB_ESCAPE args;
    HRESULT hr;

    if (d == NULL) {
        Bc250UmdTrace("BltViaKmd RECUSADO: device UMD ausente");
        return E_INVALIDARG;
    }
    if (!d->WinsysInitialized || d->pAdapter == NULL ||
        d->pAdapter->hRTAdapter.handle == NULL ||
        d->Winsys.RuntimeDevice == NULL || d->Winsys.Callbacks == NULL ||
        d->Winsys.Callbacks->pfnEscapeCb == NULL) {
        Bc250UmdTrace("BltViaKmd RECUSADO: contrato de escape incompleto "
            "(wsInit=%d adapter=%p hAdapter=%p hDevice=%p callbacks=%p hasEscape=%d)",
            d->WinsysInitialized, (void *)d->pAdapter,
            d->pAdapter != NULL ? (void *)d->pAdapter->hRTAdapter.handle : NULL,
            (void *)d->Winsys.RuntimeDevice, (void *)d->Winsys.Callbacks,
            d->Winsys.Callbacks != NULL &&
                d->Winsys.Callbacks->pfnEscapeCb != NULL);
        return E_NOTIMPL;
    }
    if (src == NULL || dst == NULL || blt == NULL || src->Width == 0u || src->Height == 0u) {
        Bc250UmdTrace("BltViaKmd RECUSADO: geometria ausente");
        return E_INVALIDARG;
    }
    /*
     * DX.49: o SDMA do KMD le a memoria fora da fila Vulkan. Draws gravados no
     * lote e ainda nao submetidos precisam terminar antes, senao a composicao
     * copiaria o quadro anterior.
     */
    if (d->VkBridgeInitialized &&
        Bc250UmdVkDrainPending(&d->VkBridge, "composicao pelo KMD (BltViaKmd)") != VK_SUCCESS) {
        Bc250UmdTrace("BltViaKmd RECUSADO: dreno do lote Vulkan falhou; a origem nao tem conclusao provada");
        return E_FAIL;
    }

    memset(&payload, 0, sizeof(payload));
    payload.Opcode = BC250_ESCAPE_OP_COMPOSITE_BLT;
    payload.SourceGpuVa = src->Allocation.GpuVirtualAddress;
    payload.DestGpuVa = dst->Allocation.GpuVirtualAddress;
    payload.Width = src->Width;
    payload.Height = src->Height;
    payload.SourcePitch = src->RowPitch;
    payload.DestPitch = dst->RowPitch;
    payload.DestX = blt->DstLeft;
    payload.DestY = blt->DstTop;

    memset(&args, 0, sizeof(args));
    args.hDevice = d->Winsys.RuntimeDevice;
    args.hContext = NULL;
    args.pPrivateDriverData = &payload;
    args.PrivateDriverDataSize = (UINT)sizeof(payload);
    /*
     * O handler dirige o SDMA, portanto o contrato D3DDDI_ESCAPEFLAGS exige
     * HardwareAccess. As corridas 12:09 (flag=1) e 12:21 (flag=0) tiveram a
     * mesma assinatura ENTER sem LEAVE; elas refutam a flag como causa. Nas
     * duas, o primeiro argumento do callback era o hDevice, embora
     * PFND3DDDI_ESCAPECB exija hAdapter. Esta chamada usa os dois handles nos
     * campos definidos pelo WDK. Correcao ainda NAO VALIDADA EM HARDWARE.
     */
    args.Flags.HardwareAccess = 1u;

    /*
     * Log maximo: o hr desta chamada e' o unico dado que nomeia a recusa, e
     * ele se perdeu em quatro corridas. Tudo o que entra na decisao sai aqui,
     * inclusive o sizeof -- se o KMD imprimir um DataSize diferente, a ABI
     * entre os dois lados esta divergente e os dois numeros estarao no log.
     */
    Bc250UmdTrace("BltViaKmd ENTER RECT 1:1: %ux%u srcPitch=%u dstPitch=%u DstXY=%u,%u SrcVa=0x%llX DstVa=0x%llX "
        "Opcode=0x%08X sizeof=%u hAdapter=%p hDevice=%p hContext=%p Flags=0x%08X",
        src->Width, src->Height, src->RowPitch, dst->RowPitch,
        blt->DstLeft, blt->DstTop,
        (unsigned long long)payload.SourceGpuVa,
        (unsigned long long)payload.DestGpuVa,
        (unsigned)payload.Opcode, (unsigned)sizeof(payload),
        (void *)d->pAdapter->hRTAdapter.handle,
        (void *)d->Winsys.RuntimeDevice, (void *)args.hContext,
        (unsigned)args.Flags.Value);
    hr = d->Winsys.Callbacks->pfnEscapeCb(
        d->pAdapter->hRTAdapter.handle, &args);
    Bc250UmdTrace("BltViaKmd LEAVE: hr=0x%08X ResultCode=0x%08X (hr!=0 significa que o "
        "dxgkrnl ou o KMD recusaram; compare com [Bc250Escape] no log do KMD)",
        (unsigned)hr, payload.ResultCode);

    if (FAILED(hr)) return hr;
    if (payload.ResultCode != BC250_PRESENT_BLT_STATUS_SUCCESS) return E_FAIL;
    ++d->BltSequence;
    return S_OK;
}

static HRESULT Bc250UmdBltGpu(BC250_UMD_DEVICE *d, BC250_UMD_RESOURCE *dst,
    BC250_UMD_RESOURCE *src, const DXGI_DDI_ARG_BLT *a)
{
    uint64_t rowBytes, dstOffset, srcEnd, dstEnd;
    /* Shared/imported and multisample layouts have no proven linear contract. */
#define BC250_ALLOWED_BLT_MISC_FLAGS ( \
    0x00000001u /* AUTO_GEN_MIP_MAP */ | \
    0x00000002u /* D3D10_DDI_RESOURCE_MISC_SHARED */ | \
    0x00000008u /* D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT */ | \
    0x00000020u /* GDI_COMPATIBLE / RAW_VIEWS */ | \
    0x00000100u /* SHARED_KEYEDMUTEX */ | \
    0x00000200u /* GDI_COMPATIBLE (reserved) */ | \
    0x00000400u /* REMOTE */ | \
    0x00000800u /* RESTRICTED_CONTENT */ | \
    0x00001000u /* RESTRICT_SHARED_RESOURCE_DRIVER */ | \
    0x00002000u /* CROSS_ADAPTER */ | \
    0x00010000u /* HW_PROTECTED */ | \
    0x00020000u /* D3DWDDM2_0DDI_RESOURCE_MISC_DISPLAYABLE_SURFACE */ | \
    0x01000000u /* PHYSICALLY_CONTIGUOUS */)
    if (src->ResourceDimension != D3D10DDIRESOURCE_TEXTURE2D ||
        dst->ResourceDimension != D3D10DDIRESOURCE_TEXTURE2D ||
        src->SampleCount != 1u || dst->SampleCount != 1u ||
        src->MipLevels != 1u || dst->MipLevels != 1u ||
        src->ArraySize != 1u || dst->ArraySize != 1u ||
        ((src->MiscFlags & ~BC250_ALLOWED_BLT_MISC_FLAGS) != 0) ||
        ((dst->MiscFlags & ~BC250_ALLOWED_BLT_MISC_FLAGS) != 0) ||
        src->Format != dst->Format ||
        (src->Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
         src->Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
         src->Format != DXGI_FORMAT_B8G8R8X8_UNORM &&
         src->Format != 29u /* R8G8B8A8_UNORM_SRGB */ &&
         src->Format != 91u /* B8G8R8A8_UNORM_SRGB */ &&
         src->Format != 93u /* B8G8R8X8_UNORM_SRGB */) ||
        a->SrcSubresource || a->DstSubresource || (a->Flags.Value & ~8u) ||
        a->Rotate > DXGI_DDI_MODE_ROTATION_IDENTITY) {
        Bc250UmdTrace("BltGpu REPROVOU (NOTIMPL): dim=%u/%u samples=%u/%u mips=%u/%u arr=%u/%u misc=0x%X/0x%X fmt=%u/%u rot=%u",
            src->ResourceDimension, dst->ResourceDimension,
            src->SampleCount, dst->SampleCount, src->MipLevels, dst->MipLevels,
            src->ArraySize, dst->ArraySize, src->MiscFlags, dst->MiscFlags,
            src->Format, dst->Format, (UINT)a->Rotate);
        return E_NOTIMPL;
    }
    if (!src->Width || !src->Height || a->DstRight > dst->Width ||
        a->DstBottom > dst->Height || a->DstLeft >= a->DstRight ||
        a->DstTop >= a->DstBottom || a->DstRight - a->DstLeft != src->Width ||
        a->DstBottom - a->DstTop != src->Height) {
        Bc250UmdTrace("BltGpu REPROVOU (INVALIDARG rect): src %ux%u, dst %ux%u, DstBox [%u,%u..%u,%u]",
            src->Width, src->Height, dst->Width, dst->Height,
            a->DstLeft, a->DstTop, a->DstRight, a->DstBottom);
        return E_INVALIDARG;
    }
    rowBytes = (uint64_t)src->Width * 4u;
    dstOffset = (uint64_t)a->DstTop * dst->RowPitch + (uint64_t)a->DstLeft * 4u;
    srcEnd = (uint64_t)(src->Height - 1u) * src->RowPitch + rowBytes;
    dstEnd = dstOffset + (uint64_t)(src->Height - 1u) * dst->RowPitch + rowBytes;
    if (rowBytes > src->RowPitch || (uint64_t)dst->Width * 4u > dst->RowPitch ||
        rowBytes > 0x400000u || (uint64_t)src->Height * 7u + 32u >= BC250_BLT_WORDS ||
        srcEnd > src->Allocation.ByteSize || srcEnd > src->TotalByteSize ||
        dstEnd > dst->Allocation.ByteSize || dstEnd > dst->TotalByteSize ||
        !src->Allocation.Created || !dst->Allocation.Created ||
        !src->Allocation.Resident || !dst->Allocation.Resident ||
        !src->Allocation.GpuVirtualAddress || !dst->Allocation.GpuVirtualAddress ||
        src->Allocation.GpuVirtualAddress >= (1ull << 34) - srcEnd ||
        dst->Allocation.GpuVirtualAddress >= (1ull << 34) - dstEnd ||
        (src->Allocation.GpuVirtualAddress < dst->Allocation.GpuVirtualAddress + dstEnd &&
         dst->Allocation.GpuVirtualAddress < src->Allocation.GpuVirtualAddress + srcEnd)) {
        Bc250UmdTrace("BltGpu REPROVOU (INVALIDARG alloc/pitch): sPitch=%u dPitch=%u sAlloc=%d dAlloc=%d sRes=%d dRes=%d sVA=0x%llX dVA=0x%llX",
            src->RowPitch, dst->RowPitch,
            src->Allocation.Created, dst->Allocation.Created,
            src->Allocation.Resident, dst->Allocation.Resident,
            (unsigned long long)src->Allocation.GpuVirtualAddress,
            (unsigned long long)dst->Allocation.GpuVirtualAddress);
        return E_INVALIDARG;
    }
    /* Node1 retirement and cross-engine ownership: se o produtor Node 1
     * possuir comandos montados pendentes de flush, recusamos o Blt
     * concorrente para evitar condicoes de corrida cross-engine. */
    if (d->Node1CmdBuffer.Initialized && d->Node1CmdBuffer.CursorDwords > 0u)
        return E_NOTIMPL;
    /*
     * CAMINHO DO KMD (13/09) -- a composicao NAO submete contexto no no de
     * paginacao.
     *
     * Medicao da corrida 08:12: 953 buffers de paginacao executaram sem UM
     * TDR; as 5 submissoes de CONTEXTO desta UMD no Node 0 produziram 5 TDRs,
     * sempre com a engine OCIOSA e o anel DRENADO, e BugCheck 0x116. O caminho
     * de blt do KMD fez 584 blts consecutivos sem TDR em 12/09 13:01: ele nao
     * cria pacote de contexto e nao passa pelo escalonador.
     *
     * Elegibilidade DECLARADA: copia 1:1, sem scaling/blend/rasterizacao. O
     * escape leva os dois pitches e o deslocamento; o KMD escolhe COPY_LINEAR
     * para o caso contiguo ou COPY_LINEAR_SUB_WIND para o retangular.
     */
    return Bc250UmdBltViaKmd(d, dst, src, a);
}
