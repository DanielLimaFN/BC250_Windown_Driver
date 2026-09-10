/* Linear, same-format DXGI Blt. Completion is written by GPU, never CPU.
 * No rendering capability is implied by this restricted transfer backend. */
#define BC250_BLT_WORDS 32768u

static void Bc250UmdBltTeardown(BC250_UMD_DEVICE *d)
{
    /* Destroy the executing context before releasing memory it may reference. */
    if (d->BltWinsys.ContextCreated &&
        Bc250WinsysDestroyVirtualContext(&d->BltWinsys) != BC250_WINSYS_OK) {
        Bc250UmdTrace("Blt teardown: context destruction failed; allocations retained");
        return;
    }
    if (d->BltFence.Created)
        (void)Bc250WinsysDestroyMonitoredFence(&d->BltWinsys, &d->BltFence);
    if (d->BltCommand.Created)
        (void)Bc250WinsysDeallocate(&d->BltWinsys, &d->BltCommand);
    if (d->BltWinsys.PagingQueueCreated)
        (void)Bc250WinsysDestroyPagingQueue(&d->BltWinsys);
    free(d->BltWords);
    d->BltWords = NULL;
}

static HRESULT Bc250UmdBltInit(BC250_UMD_DEVICE *d)
{
    if (d->BltPending) return E_FAIL;
    if (d->BltWords) return S_OK;
    if (!d->WinsysInitialized) return E_FAIL;
    if (Bc250WinsysInitialize(&d->BltWinsys, d->Winsys.RuntimeDevice,
            d->Winsys.Callbacks) != BC250_WINSYS_OK ||
        Bc250WinsysCreatePagingQueue(&d->BltWinsys) != BC250_WINSYS_OK ||
        Bc250WinsysCreateVirtualContext(&d->BltWinsys, 0u) != BC250_WINSYS_OK ||
        Bc250WinsysAllocate(&d->BltWinsys, Bc250UmdNextProofToken(),
            BC250_BLT_WORDS * 4u, 4096u, &d->BltCommand) != BC250_WINSYS_OK ||
        Bc250WinsysMapAllocation(&d->BltWinsys, &d->BltCommand, TRUE) != BC250_WINSYS_OK ||
        Bc250WinsysMakeResident(&d->BltWinsys, &d->BltCommand) != BC250_WINSYS_OK ||
        Bc250WinsysWaitPaging(&d->BltWinsys, &d->BltCommand) != BC250_WINSYS_OK ||
        Bc250WinsysCreateMonitoredFence(&d->BltWinsys, 0u, &d->BltFence) != BC250_WINSYS_OK)
        goto fail;
    d->BltWords = (uint32_t *)calloc(BC250_BLT_WORDS, sizeof(uint32_t));
    if (!d->BltWords) goto fail;
    return S_OK;
fail:
    Bc250UmdBltTeardown(d);
    return E_FAIL;
}

static HRESULT Bc250UmdBltGpu(BC250_UMD_DEVICE *d, BC250_UMD_RESOURCE *dst,
    BC250_UMD_RESOURCE *src, const DXGI_DDI_ARG_BLT *a)
{
    uint64_t rowBytes, dstOffset, srcEnd, dstEnd;
    uint32_t y, cursor = 0u;
    HRESULT hr;
    /* Shared/imported and multisample layouts have no proven linear contract. */
    if (src->ResourceDimension != D3D10DDIRESOURCE_TEXTURE2D ||
        dst->ResourceDimension != D3D10DDIRESOURCE_TEXTURE2D ||
        src->SampleCount != 1u || dst->SampleCount != 1u ||
        src->MipLevels != 1u || dst->MipLevels != 1u ||
        src->ArraySize != 1u || dst->ArraySize != 1u ||
        src->MiscFlags || dst->MiscFlags || src->Format != dst->Format ||
        (src->Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
         src->Format != DXGI_FORMAT_B8G8R8A8_UNORM) ||
        a->SrcSubresource || a->DstSubresource || (a->Flags.Value & ~8u) ||
        a->Rotate > DXGI_DDI_MODE_ROTATION_IDENTITY)
        return E_NOTIMPL;
    if (!src->Width || !src->Height || a->DstRight > dst->Width ||
        a->DstBottom > dst->Height || a->DstLeft >= a->DstRight ||
        a->DstTop >= a->DstBottom || a->DstRight - a->DstLeft != src->Width ||
        a->DstBottom - a->DstTop != src->Height)
        return E_INVALIDARG;
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
         dst->Allocation.GpuVirtualAddress < src->Allocation.GpuVirtualAddress + srcEnd))
        return E_INVALIDARG;
    /* Node1 retirement and cross-engine ownership are still unproven. Do not
     * race a producer merely because its CPU command buffer is empty. */
    if (d->NativeNode1Ready || d->Node1CmdBuffer.Initialized)
        return E_NOTIMPL;
    hr = Bc250UmdBltInit(d);
    if (FAILED(hr)) return hr;
    if (d->BltSequence == 0xffffffffu) return E_FAIL;
    if (Bc250WinsysWaitPaging(&d->Winsys, &src->Allocation) != BC250_WINSYS_OK ||
        Bc250WinsysWaitPaging(&d->Winsys, &dst->Allocation) != BC250_WINSYS_OK)
        return E_FAIL;
    if (Bc250SdmaEmitGcr(d->BltWords, BC250_BLT_WORDS, &cursor) != BC250_SDMA_OK)
        return E_FAIL;
    for (y = 0; y < src->Height; ++y) {
        if (Bc250SdmaEmitCopyLinear(d->BltWords, BC250_BLT_WORDS, &cursor,
            src->Allocation.GpuVirtualAddress + (uint64_t)y * src->RowPitch,
            dst->Allocation.GpuVirtualAddress + dstOffset + (uint64_t)y * dst->RowPitch,
            (uint32_t)rowBytes) != BC250_SDMA_OK) return E_FAIL;
    }
    ++d->BltSequence;
    if (Bc250SdmaEmitGcr(d->BltWords, BC250_BLT_WORDS, &cursor) != BC250_SDMA_OK ||
        Bc250SdmaEmitFence(d->BltWords, BC250_BLT_WORDS, &cursor,
            d->BltFence.GpuVirtualAddress, d->BltSequence) != BC250_SDMA_OK)
        return E_FAIL;
    while (cursor & 7u) d->BltWords[cursor++] = BC250_SDMA_OP_NOP;
    if (Bc250WinsysUploadAllocation(&d->BltWinsys, &d->BltCommand,
            d->BltWords, cursor * 4u) != BC250_WINSYS_OK)
        return E_FAIL;
    d->BltPending = TRUE;
    if (Bc250WinsysSubmitGpuVaForNode(&d->BltWinsys, 0u,
            d->BltCommand.GpuVirtualAddress, cursor * 4u) != BC250_WINSYS_OK ||
        Bc250WinsysWaitForFenceFromCpu(&d->BltWinsys, &d->BltFence,
            d->BltSequence) != BC250_WINSYS_OK ||
        !d->BltFence.CpuVirtualAddress || *d->BltFence.CpuVirtualAddress != d->BltSequence)
        return E_FAIL;
    d->BltPending = FALSE;
    return S_OK;
}
