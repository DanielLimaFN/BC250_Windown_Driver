/* Included in bc250_d3d_umd.c. A new immutable set per draw. The bridge
 * retires all objects only after a successful fence wait -- with DX.49 that is
 * the flush of the whole draw batch, so every set of the batch stays alive
 * until the batch completes. On timeout they stay owned by the bridge until
 * device teardown. */
static const char *Bc250UmdVkImageLayoutName(VkImageLayout layout)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
    case VK_IMAGE_LAYOUT_GENERAL: return "GENERAL";
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return "COLOR_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return "DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL: return "DEPTH_STENCIL_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return "SHADER_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC_OPTIMAL";
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return "TRANSFER_DST_OPTIMAL";
    case VK_IMAGE_LAYOUT_PREINITIALIZED: return "PREINITIALIZED";
#ifdef VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return "PRESENT_SRC_KHR";
#endif
    default: return "DESCONHECIDO";
    }
}

/* Definida mais abaixo em bc250_d3d_umd.c, que inclui este arquivo. */
static UINT Bc250DxgiFormatBytesPerPixel(DXGI_FORMAT Format);

static uint64_t Bc250UmdDescNowMicros(void)
{
    LARGE_INTEGER now, freq;
    if (!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) return 0u;
    return (uint64_t)(now.QuadPart / freq.QuadPart) * 1000000ull +
           (uint64_t)((now.QuadPart % freq.QuadPart) * 1000000ll / freq.QuadPart);
}

/*
 * T4.3 (30/09) -- carga de superficie ABERTA (OpenResource) fora do registro
 * do draw.
 *
 * MEDIDO na Corrida 182: dwm.exe (pid 1288) morreu com c0000005 em
 * bc250umd.dll+0x8376, que resolve para o laco de RestoreImages de
 * Bc250UmdBindDrawDescriptors (bytes do ponto conferidos entre o binario
 * empacotado e um build com simbolos). A ultima linha do log era
 * "Superficie compartilhada carregada via SDMA GPU". Os caminhos abaixo
 * drenam o lote (Bc250UmdVkDrainPending / FlushCommands), e o
 * FlushCommands bem-sucedido chama Bc250UmdVkReleaseDescriptorBatches, que
 * libera TODOS os batches -- inclusive o que Bc250UmdBindDrawDescriptors
 * estava preenchendo. E o ultimo flush deixa RecordingActive=FALSE, entao o
 * resto do draw era gravado num command buffer fora de gravacao.
 *
 * Por isso a carga roda ANTES: sem batch alocado, com o render pass ja
 * fechado pelo chamador, e o chamador re-obtem o command buffer depois.
 * O corpo e' o mesmo das T4.0/T4.1 (tres camadas, cinza da T3.8 mantido).
 */
static void Bc250UmdMaterializeOpenedSrvImage(BC250_UMD_DEVICE *device,
    BC250_UMD_SRV *srv, BC250_UMD_RESOURCE *resource)
{
    if (resource->hKMResource.handle != 0 &&
        device->VkBridgeInitialized && device->VkBridge.DeviceActive &&
        srv->Desc.ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D) {
        D3D10DDIARG_CREATERESOURCE createArgs;
        UINT width = (resource->Width > 0u) ? resource->Width : 1u;
        UINT height = (resource->Height > 0u) ? resource->Height : 1u;
        DXGI_FORMAT fmt = srv->Desc.Format ? srv->Desc.Format :
            (resource->Format ? resource->Format : DXGI_FORMAT_B8G8R8A8_UNORM);
        UINT bpp = 4u;
        UINT pitch = (resource->RowPitch >= width * bpp) ? resource->RowPitch : (width * bpp);
        SIZE_T totalBytes = (resource->TotalByteSize >= (SIZE_T)pitch * height) ?
            resource->TotalByteSize : ((SIZE_T)pitch * height);
        BOOL uploaded = FALSE;
        memset(&createArgs, 0, sizeof(createArgs));
        createArgs.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
        createArgs.BindFlags = D3D10_DDI_BIND_SHADER_RESOURCE;
        createArgs.Usage = D3D10_DDI_USAGE_DEFAULT;
        resource->Width = width;
        resource->Height = height;
        resource->RowPitch = pitch;
        resource->Format = fmt;
        resource->MipLevels = 1u;
        resource->ArraySize = 1u;
        resource->SampleCount = 1u;
        resource->TotalByteSize = (UINT)totalBytes;
        resource->VkResourceCreated = FALSE;
        if (Bc250UmdVkCreateResource(device, resource, &createArgs) && resource->VkMemAliased) {
            /* Frente B: a VkImage le a propria alocacao aberta; nada a copiar.
             * Se a adocao falhar, a imagem fica sem conteudo e o descritor
             * recusa com motivo nomeado -- nunca cinza inventado sobre ela. */
            (void)Bc250UmdVkAdoptAliasedContent(device, resource);
        } else if (resource->VkResourceCreated) {
            /* 1. Tenta ler pixels da alocacao compartilhada via pfnLockCb */
            if (device->WinsysInitialized && resource->Allocation.KernelAllocation != 0 &&
                totalBytes > 0 && totalBytes <= (32u * 1024u * 1024u)) {
                void *pTemp = malloc(totalBytes);
                if (pTemp != NULL) {
                    if (Bc250WinsysReadAllocation(&device->Winsys, &resource->Allocation,
                            pTemp, (UINT)totalBytes) == BC250_WINSYS_OK) {
                        if (Bc250UmdVkUploadImageData(device, resource, pTemp, pitch, totalBytes)) {
                            uploaded = TRUE;
                            Bc250UmdTrace("[VK_RES] Superficie compartilhada REAL carregada via Lock: %ux%u pitch=%u bytes=%llu hKM=%u",
                                width, height, pitch, (unsigned long long)totalBytes,
                                (unsigned)resource->hKMResource.handle);
                        }
                    }
                    free(pTemp);
                }
            }
            /* 2. Se o Lock nao foi possivel mas ha GPUVA de origem e destino na staging memory, tenta SDMA Blt */
            if (!uploaded && resource->Allocation.GpuVirtualAddress != 0 &&
                device->VkBridge.pfnGetMemoryGpuVa != NULL &&
                totalBytes > 0 && totalBytes <= (32u * 1024u * 1024u)) {
                BOOL grew = FALSE;
                uint64_t allocUs = 0u;
                if (Bc250UmdVkEnsureUploadStaging(&device->VkBridge, (VkDeviceSize)totalBytes, &grew, &allocUs)) {
                    (void)Bc250UmdVkDrainPending(&device->VkBridge, "SDMA Staging Blt");
                    uint64_t stagingGpuVa = device->VkBridge.pfnGetMemoryGpuVa(device->VkBridge.UploadStagingMem);
                    if (stagingGpuVa != 0) {
                        HRESULT bltHr = Bc250UmdBridgeGpuBlt(device,
                            resource->Allocation.GpuVirtualAddress, pitch,
                            stagingGpuVa, pitch, width, height);
                        if (SUCCEEDED(bltHr)) {
                            if (Bc250UmdVkCopyStagingBufferToImage(device, resource, 0, pitch, totalBytes)) {
                                uploaded = TRUE;
                                Bc250UmdTrace("[VK_RES] Superficie compartilhada carregada via SDMA GPU: %ux%u pitch=%u hKM=%u",
                                    width, height, pitch, (unsigned)resource->hKMResource.handle);
                            }
                        }
                    }
                }
            }
            /* 3. Fallback gracioso com a geometria real preenchida com o cinza do tema */
            if (!uploaded) {
                if (totalBytes > 0 && totalBytes <= (16u * 1024u * 1024u)) {
                    void *pFallback = malloc(totalBytes);
                    if (pFallback != NULL) {
                        uint32_t *pPix = (uint32_t *)pFallback;
                        SIZE_T numPix = totalBytes / sizeof(uint32_t);
                        SIZE_T pi;
                        for (pi = 0; pi < numPix; ++pi) {
                            pPix[pi] = 0xFF202020u;
                        }
                        (void)Bc250UmdVkUploadImageData(device, resource, pFallback, pitch, totalBytes);
                        free(pFallback);
                    } else {
                        static const uint32_t s_dummyPixel = 0xFF202020u;
                        (void)Bc250UmdVkUploadImageData(device, resource, &s_dummyPixel, 4u, 4u);
                    }
                } else {
                    static const uint32_t s_dummyPixel = 0xFF202020u;
                    (void)Bc250UmdVkUploadImageData(device, resource, &s_dummyPixel, 4u, 4u);
                }
                Bc250UmdTrace("[VK_RES] Fallback %ux%u (0xFF202020) criado para recurso aberto hKM=%u (res=%p)",
                    width, height, (unsigned)resource->hKMResource.handle, (void*)resource);
            }
        }
    }
}

/*
 * T4.6 (30/09) -- RECOPIA da superficie aberta, no maximo uma vez por quadro.
 *
 * MEDIDO na Corrida 185 (cruzando o log do dwm.exe 1280 com o do
 * StartMenuExperienceHost 7016 pelo token): a superficie 640x640
 * 0x524553001B68000E foi copiada pelo DWM as 09:47:12.591 e o menu so' desenhou
 * nela de 09:47:15.408 a 09:48:07.975 -- 564 de 564 draws DEPOIS da unica copia.
 * O mesmo nas seguintes (88/92, 376/382, 232/238). O menu escreve a alocacao
 * compartilhada a cada Present dele; o DWM copiava uma vez e compunha a foto.
 *
 * Passo intermediario, declarado: o destino final e' a VkImage ALIASAR a
 * alocacao aberta (como o Linux, que importa o BO e le a mesma memoria). Ate
 * la, a VkImage e' recarregada pelo mesmo caminho SDMA da camada 2 no primeiro
 * uso de cada quadro do DWM (DxgiPresentCount). Roda na etapa da T4.3, antes do
 * batch de descritores existir.
 */
static void Bc250UmdRefreshOpenedSrvImage(BC250_UMD_DEVICE *device, BC250_UMD_RESOURCE *resource)
{
    static volatile LONG s_refreshLog = 0;
    UINT width = resource->Width;
    UINT height = resource->Height;
    UINT pitch = resource->RowPitch;
    SIZE_T totalBytes = (SIZE_T)pitch * height;
    BOOL grew = FALSE;
    uint64_t allocUs = 0u;
    uint64_t stagingGpuVa;
    HRESULT bltHr = E_FAIL;
    BOOL copied = FALSE;
    LONG n;

    resource->SharedRefreshPresent = device->DxgiPresentCount;
    if (width == 0u || height == 0u || pitch < width * 4u ||
        resource->Allocation.GpuVirtualAddress == 0u ||
        device->VkBridge.pfnGetMemoryGpuVa == NULL ||
        totalBytes > (32u * 1024u * 1024u)) {
        return;
    }
    if (!Bc250UmdVkEnsureUploadStaging(&device->VkBridge, (VkDeviceSize)totalBytes, &grew, &allocUs)) return;
    (void)Bc250UmdVkDrainPending(&device->VkBridge, "recopia de superficie aberta");
    stagingGpuVa = device->VkBridge.pfnGetMemoryGpuVa(device->VkBridge.UploadStagingMem);
    if (stagingGpuVa != 0u) {
        bltHr = Bc250UmdBridgeGpuBlt(device, resource->Allocation.GpuVirtualAddress, pitch,
            stagingGpuVa, pitch, width, height);
        if (SUCCEEDED(bltHr)) {
            copied = Bc250UmdVkCopyStagingBufferToImage(device, resource, 0, pitch, totalBytes);
        }
    }
    n = InterlockedIncrement(&s_refreshLog);
    if (n <= 16 || (n % 64) == 0 || !copied) {
        Bc250UmdTrace("[VK_RES] T4.6 RECOPIA de superficie aberta #%ld: %ux%u hKM=%u quadro=%llu blt=0x%08X copia=%d",
            n, width, height, (unsigned)resource->hKMResource.handle,
            (unsigned long long)device->DxgiPresentCount, (unsigned)bltHr, (int)copied);
    }
}

/* Percorre SO' os bindings de imagem da pipeline deste draw. */
static void Bc250UmdMaterializeDrawSrvs(BC250_UMD_DEVICE *device, VkPipelineLayout layout)
{
    BC250_PSO_ENTRY *entry = NULL;
    uint32_t i;
    for (i = 0; i < BC250_PSO_CACHE_CAPACITY; ++i)
        if (device->PipelineCache.Entries[i].InUse && device->PipelineCache.Entries[i].PipelineLayout == layout)
            entry = &device->PipelineCache.Entries[i];
    if (!entry) return;
    for (i = 0; i < entry->BindingCount; ++i) {
        const VkDescriptorSetLayoutBinding *binding = &entry->Bindings[i];
        uint32_t stage = binding->binding / BC250_DESCRIPTOR_STAGE_STRIDE;
        uint32_t reg = binding->binding % BC250_DESCRIPTOR_STAGE_STRIDE;
        BC250_UMD_SRV *srv;
        BC250_UMD_RESOURCE *resource;
        if (binding->descriptorType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) continue;
        if (stage >= BC250_DESCRIPTOR_STAGE_COUNT) continue;
        if (reg < BC250_DESCRIPTOR_SRV_BASE || reg >= BC250_DESCRIPTOR_SRV_BASE + BC250_DESCRIPTOR_SRV_COUNT) continue;
        reg -= BC250_DESCRIPTOR_SRV_BASE;
        srv = (BC250_UMD_SRV *)(stage == 0 ? device->PipelineState.VsShaderResources[reg].pDrvPrivate :
            device->PipelineState.PsShaderResources[reg].pDrvPrivate);
        if (!srv || !srv->Created || !(resource = srv->pResource)) continue;
        if (!resource->VkImg) {
            Bc250UmdMaterializeOpenedSrvImage(device, srv, resource);
            resource->SharedRefreshPresent = device->DxgiPresentCount;
        } else if (resource->OpenedShared && !resource->VkMemAliased &&
                   srv->Desc.ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
                   resource->SharedRefreshPresent != device->DxgiPresentCount) {
            Bc250UmdRefreshOpenedSrvImage(device, resource);
        }
    }
}

static BOOL Bc250UmdBindDrawDescriptors(BC250_UMD_DEVICE *device,
    VkPipelineLayout layout, VkCommandBuffer cmd, BOOL *hasDescriptors)
{
    BC250_UMD_VK_BRIDGE *bridge = &device->VkBridge;
    BC250_PSO_ENTRY *entry = NULL;
    BC250_VK_DESCRIPTOR_BATCH *batch;
    VkDescriptorPoolSize sizes[4] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0}, {VK_DESCRIPTOR_TYPE_SAMPLER, 0},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 0}};
    VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    VkDescriptorSetAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    VkWriteDescriptorSet writes[BC250_DESCRIPTOR_MAX_BINDINGS];
    VkDescriptorBufferInfo buffers[BC250_DESCRIPTOR_MAX_BINDINGS];
    VkDescriptorImageInfo images[BC250_DESCRIPTOR_MAX_BINDINGS];
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint64_t descStart = Bc250UmdDescNowMicros();
    const char *poolKind = "POR_DRAW";
    const char *refusedReason = "MOTIVO_NAO_REGISTRADO";
    BC250_UMD_RESOURCE *refusedSrvResource = NULL;
    BOOL refusedIsSrv = FALSE;
    uint32_t i;
    *hasDescriptors = FALSE;
    device->LastVkDrawFailureReason = NULL;
    for (i = 0; i < BC250_PSO_CACHE_CAPACITY; ++i)
        if (device->PipelineCache.Entries[i].InUse && device->PipelineCache.Entries[i].PipelineLayout == layout)
            entry = &device->PipelineCache.Entries[i];
    if (!entry) { device->LastVkDrawFailureReason = "PIPELINE_SEM_ENTRADA_DE_DESCRITOR"; return FALSE; }
    if (!entry->BindingCount) return TRUE;
    if (device->InRenderPass) { device->LastVkDrawFailureReason = "DESCRITOR_DENTRO_DE_RENDERPASS"; return FALSE; }
    if (bridge->SubmissionFailed) { device->LastVkDrawFailureReason = "SUBMISSAO_VULKAN_JA_FALHOU"; return FALSE; }
    batch = (BC250_VK_DESCRIPTOR_BATCH *)calloc(1, sizeof(*batch));
    if (!batch) { device->LastVkDrawFailureReason = "SEM_MEMORIA_PARA_DESCRITOR"; return FALSE; }
    batch->Next = bridge->DescriptorBatches;
    bridge->DescriptorBatches = batch;
    memset(writes, 0, sizeof(writes));
    memset(buffers, 0, sizeof(buffers));
    memset(images, 0, sizeof(images));
    for (i = 0; i < entry->BindingCount; ++i) {
        const VkDescriptorSetLayoutBinding *binding = &entry->Bindings[i];
        uint32_t stage = binding->binding / BC250_DESCRIPTOR_STAGE_STRIDE;
        uint32_t reg = binding->binding % BC250_DESCRIPTOR_STAGE_STRIDE;
        if (stage >= BC250_DESCRIPTOR_STAGE_COUNT || binding->descriptorCount != 1 ||
            binding->stageFlags != (VkShaderStageFlags)(stage == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT)) { refusedReason = "STAGE_OU_CONTAGEM_INVALIDA"; goto refused; }
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstBinding = binding->binding;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = binding->descriptorType;
        if (binding->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            BC250_UMD_RESOURCE *resource;
            if (reg >= BC250_DESCRIPTOR_CB_COUNT) { refusedReason = "CB_REGISTRADOR_FORA_DE_FAIXA"; goto refused; }
            resource = (BC250_UMD_RESOURCE *)(stage == 0 ? device->PipelineState.VsConstantBuffers[reg].pDrvPrivate :
                device->PipelineState.PsConstantBuffers[reg].pDrvPrivate);
            if (!resource || !resource->VkBuf || !resource->Width || resource->Width > 65536u ||
                resource->Width > resource->TotalByteSize || !resource->pCpuShadow) { refusedReason = "CB_RECURSO_AUSENTE_OU_INVALIDO"; goto refused; }
            if (device->DrawUsesRing) {
                /* DX.49: faixa propria no anel; o draw seguinte do lote nao
                 * reescreve o CB que este ainda vai ler. */
                VkBuffer ringBuf = VK_NULL_HANDLE;
                VkDeviceSize ringOff = 0;
                if (!Bc250UmdVkDrawRingPut(bridge, resource->pCpuShadow, resource->Width, &ringBuf, &ringOff)) {
                    refusedReason = "CB_ANEL_SEM_ESPACO"; goto refused;
                }
                buffers[i].buffer = ringBuf;
                buffers[i].offset = ringOff;
            } else {
                if (!Bc250UmdVkUploadBufferData(device, resource, resource->pCpuShadow, resource->Width)) { refusedReason = "CB_UPLOAD_FALHOU"; goto refused; }
                buffers[i].buffer = resource->VkBuf;
            }
            buffers[i].range = resource->Width;
            writes[i].pBufferInfo = &buffers[i];
            sizes[0].descriptorCount++;
        } else if (binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
            BC250_UMD_SRV *srv;
            BC250_UMD_RESOURCE *resource;
            VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            refusedIsSrv = TRUE;
            if (reg < BC250_DESCRIPTOR_SRV_BASE || reg >= BC250_DESCRIPTOR_SRV_BASE + BC250_DESCRIPTOR_SRV_COUNT) { refusedReason = "SRV_REGISTRADOR_FORA_DE_FAIXA"; goto refused; }
            reg -= BC250_DESCRIPTOR_SRV_BASE;
            srv = (BC250_UMD_SRV *)(stage == 0 ? device->PipelineState.VsShaderResources[reg].pDrvPrivate :
                device->PipelineState.PsShaderResources[reg].pDrvPrivate);
            /*
             * SETE CONDICOES, SETE NOMES. Dividido apos a Corrida 39.
             *
             * MEDIDO em 19/09 16:20:14: o dwm.exe foi recusado com
             *   motivo=SRV_AUSENTE_OU_SEM_IMAGEM
             * e esse nome agregava as sete checagens abaixo. Foram nomeadas 17
             * recusas naquela faixa e justamente a que disparou ficou grossa --
             * a corrida disse que parou aqui, nao disse por que. Cada linha
             * agora responde uma pergunta diferente, e cada uma manda a
             * investigacao para um lugar diferente.
             */
            if (!srv) { refusedReason = "SRV_SLOT_VAZIO"; goto refused; }
            if (!srv->Created) { refusedReason = "SRV_NAO_CRIADA"; goto refused; }
            if (!(resource = srv->pResource)) { refusedReason = "SRV_SEM_RECURSO_ASSOCIADO"; goto refused; }
            refusedSrvResource = resource;
            /*
             * T4.3: a carga de superficie aberta SAIU daqui. Ela drena o lote, e o
             * FlushCommands libera TODOS os batches -- inclusive o `batch` desta
             * funcao -- e deixa o command buffer fora de gravacao. Medido na
             * Corrida 182: c0000005 em bc250umd.dll+0x8376 = linha do laco de
             * RestoreImages abaixo. Quem carrega agora e' Bc250UmdMaterializeDrawSrvs,
             * ANTES do batch existir e antes do command buffer ser (re)obtido.
             */
            if (!resource->VkImg) { refusedReason = "SRV_RECURSO_SEM_VKIMAGE"; goto refused; }
            if (srv->Desc.ResourceDimension != D3D10DDIRESOURCE_TEXTURE2D) {
                refusedReason = "SRV_DIMENSAO_NAO_E_TEXTURE2D"; goto refused;
            }
            if (resource->SampleCount > 1) { refusedReason = "SRV_MULTISAMPLE_NAO_SUPORTADA"; goto refused; }
            if (resource->VkLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
                refusedReason = "SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA"; goto refused;
            }
            /*
             * CONTEUDO, DERIVADO DO LAYOUT -- nao de um campo paralelo.
             *
             * Layout valido nao e o mesmo que conteudo: uma imagem apenas
             * transicionada continua com memoria nao inicializada, e amostra-la
             * produziria cor a partir de lixo. Só dois layouts provam escrita:
             *   SHADER_READ_ONLY_OPTIMAL -- posto por Bc250UmdVkUploadImageData
             *                               DEPOIS da fence da copia;
             *   COLOR_ATTACHMENT_OPTIMAL -- posto por quem desenhou no alvo.
             * Qualquer outro e recusa nomeada.
             */
            if (resource->VkLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                resource->VkLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                refusedReason = "SRV_SEM_CONTEUDO_NUNCA_ESCRITO"; goto refused;
            }
            /*
             * T3.8: Em vez de recusar a SRV quando o aplicativo pede MostDetailedMip
             * ou FirstArraySlice especifico (o que fazia o Direct2D abortar o render
             * do menu Iniciar com SRV_ALEM_DO_SUBRECURSO_CARREGADO), grampeamos as
             * faixas solicitadas dentro dos limites reais do recurso alocado.
             */
            uint32_t reqMip = srv->Desc.Tex2D.MostDetailedMip;
            uint32_t reqMipCount = srv->Desc.Tex2D.MipLevels;
            uint32_t reqSlice = srv->Desc.Tex2D.FirstArraySlice;
            uint32_t reqSliceCount = srv->Desc.Tex2D.ArraySize;

            if (reqMip >= resource->MipLevels) reqMip = 0u;
            if (reqMipCount == 0u || reqMipCount > resource->MipLevels - reqMip) reqMipCount = resource->MipLevels - reqMip;
            if (reqSlice >= resource->ArraySize) reqSlice = 0u;
            if (reqSliceCount == 0u || reqSliceCount > resource->ArraySize - reqSlice) reqSliceCount = resource->ArraySize - reqSlice;

            for (uint32_t rt = 0; rt < device->PipelineState.NumBoundRTVs; ++rt) {
                BC250_UMD_RTV *rtv = (BC250_UMD_RTV *)device->PipelineState.BoundRTVs[rt].pDrvPrivate;
                if (rtv && rtv->pResource == resource) { refusedReason = "SRV_EM_FEEDBACK_LOOP_COM_RTV"; goto refused; } /* no feedback loop */
            }
            view.image = resource->VkImg;
            view.viewType = resource->ArraySize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            view.format = Bc250UmdVkFormatFromDxgi(srv->Desc.Format);
            if (view.format == VK_FORMAT_UNDEFINED) {
                view.format = Bc250UmdVkFormatFromDxgi(resource->Format);
            }
            /* DX.24: X8 amostra alpha=1 e A8 amostra (0,0,0,a), como no D3D. */
            view.components = Bc250UmdVkSrvComponentMapping(srv->Desc.Format);
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.baseMipLevel = reqMip;
            view.subresourceRange.levelCount = reqMipCount;
            view.subresourceRange.baseArrayLayer = reqSlice;
            view.subresourceRange.layerCount = reqSliceCount;
            if (view.format == VK_FORMAT_UNDEFINED || !view.subresourceRange.levelCount || !view.subresourceRange.layerCount ||
                view.subresourceRange.baseMipLevel >= resource->MipLevels ||
                view.subresourceRange.levelCount > resource->MipLevels - view.subresourceRange.baseMipLevel ||
                view.subresourceRange.baseArrayLayer >= resource->ArraySize ||
                view.subresourceRange.layerCount > resource->ArraySize - view.subresourceRange.baseArrayLayer) { refusedReason = "SRV_FORMATO_OU_SUBRECURSO_INVALIDO"; goto refused; }
            if (bridge->vk.vkCreateImageView(bridge->Device, &view, NULL, &batch->Views[i]) != VK_SUCCESS) { refusedReason = "SRV_CREATE_IMAGE_VIEW_FALHOU"; goto refused; }
            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = resource->VkLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = resource->VkImg;
            /* Track the whole image, never claim untransitioned subresources. */
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = resource->MipLevels;
            barrier.subresourceRange.layerCount = resource->ArraySize;
            BOOL alreadyTransitioned = FALSE;
            for (uint32_t prior = 0; prior < i; ++prior)
                if (batch->RestoreImages[prior].image == resource->VkImg) alreadyTransitioned = TRUE;
            if (!alreadyTransitioned) {
                bridge->vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, NULL, 0, NULL, 1, &barrier);
                batch->RestoreImages[i] = barrier;
                batch->RestoreImages[i].oldLayout = barrier.newLayout;
                batch->RestoreImages[i].newLayout = barrier.oldLayout;
                batch->RestoreImages[i].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                batch->RestoreImages[i].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            }
            images[i].imageView = batch->Views[i];
            images[i].imageLayout = barrier.newLayout;
            writes[i].pImageInfo = &images[i];
            sizes[1].descriptorCount++;
        } else if (binding->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) {
            /*
             * SHADER.2 (Corrida 161): Buffer<T> em t#. O PS do menu Iniciar le
             * t0 como buffer tipado (SRV Dim=1, Format=62). Escopo declarado:
             * o conteudo sai do shadow de CPU, como no CB fora do anel; buffer
             * escrito so' pela GPU (CopyResource, stream output) nao esta
             * coberto e o shadow estaria velho -- declarado, nao testado.
             */
            BC250_UMD_SRV *srv;
            BC250_UMD_RESOURCE *resource;
            VkBufferViewCreateInfo view = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
            UINT bpp;
            uint64_t first, count;
            refusedIsSrv = TRUE;
            if (reg < BC250_DESCRIPTOR_SRV_BASE || reg >= BC250_DESCRIPTOR_SRV_BASE + BC250_DESCRIPTOR_SRV_COUNT) { refusedReason = "SRV_REGISTRADOR_FORA_DE_FAIXA"; goto refused; }
            reg -= BC250_DESCRIPTOR_SRV_BASE;
            srv = (BC250_UMD_SRV *)(stage == 0 ? device->PipelineState.VsShaderResources[reg].pDrvPrivate :
                device->PipelineState.PsShaderResources[reg].pDrvPrivate);
            if (!srv) { refusedReason = "SRV_SLOT_VAZIO"; goto refused; }
            if (!srv->Created) { refusedReason = "SRV_NAO_CRIADA"; goto refused; }
            if (!(resource = srv->pResource)) { refusedReason = "SRV_SEM_RECURSO_ASSOCIADO"; goto refused; }
            refusedSrvResource = resource;
            if (srv->Desc.ResourceDimension != D3D10DDIRESOURCE_BUFFER) { refusedReason = "SRV_TEXEL_DIMENSAO_NAO_E_BUFFER"; goto refused; }
            if (!resource->VkBuf) { refusedReason = "SRV_BUFFER_SEM_VKBUFFER"; goto refused; }
            view.format = Bc250UmdVkFormatFromDxgi(srv->Desc.Format);
            bpp = Bc250DxgiFormatBytesPerPixel(srv->Desc.Format);
            if (view.format == VK_FORMAT_UNDEFINED || bpp == 0u) { refusedReason = "SRV_BUFFER_FORMATO_NAO_SUPORTADO"; goto refused; }
            first = (uint64_t)srv->Desc.Buffer.FirstElement * bpp;
            count = (uint64_t)srv->Desc.Buffer.NumElements * bpp;
            if (!count || first + count > resource->Width) { refusedReason = "SRV_BUFFER_FAIXA_FORA_DO_RECURSO"; goto refused; }
            /* minTexelBufferOffsetAlignment do RADV e' 4; sem o limite em maos,
             * 16 cobre qualquer implementacao conhecida. Deslocado = recusa. */
            if (first % 16u) { refusedReason = "SRV_BUFFER_DESLOCAMENTO_DESALINHADO"; goto refused; }
            if (!resource->pCpuShadow) { refusedReason = "SRV_BUFFER_SEM_SHADOW_DE_CPU"; goto refused; }
            if (!Bc250UmdVkUploadBufferData(device, resource, resource->pCpuShadow, resource->Width)) { refusedReason = "SRV_BUFFER_UPLOAD_FALHOU"; goto refused; }
            view.buffer = resource->VkBuf;
            view.offset = first;
            view.range = count;
            if (bridge->vk.vkCreateBufferView(bridge->Device, &view, NULL, &batch->BufferViews[i]) != VK_SUCCESS) { refusedReason = "SRV_CREATE_BUFFER_VIEW_FALHOU"; goto refused; }
            writes[i].pTexelBufferView = &batch->BufferViews[i];
            sizes[3].descriptorCount++;
        } else if (binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) {
            BC250_UMD_SAMPLER *sampler;
            VkSamplerCreateInfo info;
            VkSampler cached = VK_NULL_HANDLE;
            uint32_t filter;
            /* DX.54: o cache compara a struct inteira; o padding tem de ser zero. */
            memset(&info, 0, sizeof(info));
            info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            if (reg < BC250_DESCRIPTOR_SAMPLER_BASE || reg >= BC250_DESCRIPTOR_SAMPLER_BASE + BC250_DESCRIPTOR_SAMPLER_COUNT) { refusedReason = "SAMPLER_REGISTRADOR_FORA_DE_FAIXA"; goto refused; }
            reg -= BC250_DESCRIPTOR_SAMPLER_BASE;
            sampler = (BC250_UMD_SAMPLER *)(stage == 0 ? device->PipelineState.VsSamplers[reg].pDrvPrivate : device->PipelineState.PsSamplers[reg].pDrvPrivate);
            if (!sampler || !sampler->Created) { refusedReason = "SAMPLER_AUSENTE"; goto refused; }
            filter = (uint32_t)sampler->Desc.Filter;
            /* Anisotropy requires an enabled device feature; do not silently downgrade. */
            if ((filter & ~0x95u) || (filter & 0x40u) || sampler->Desc.AddressU < 1 || sampler->Desc.AddressU > 3 ||
                sampler->Desc.AddressV < 1 || sampler->Desc.AddressV > 3 || sampler->Desc.AddressW < 1 || sampler->Desc.AddressW > 3) { refusedReason = "SAMPLER_MODO_NAO_SUPORTADO"; goto refused; }
            info.magFilter = (filter & 4u) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            info.minFilter = (filter & 16u) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            info.mipmapMode = (filter & 1u) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
            info.addressModeU = (VkSamplerAddressMode)(sampler->Desc.AddressU - 1);
            info.addressModeV = (VkSamplerAddressMode)(sampler->Desc.AddressV - 1);
            info.addressModeW = (VkSamplerAddressMode)(sampler->Desc.AddressW - 1);
            info.mipLodBias = sampler->Desc.MipLODBias;
            info.minLod = sampler->Desc.MinLOD;
            info.maxLod = sampler->Desc.MaxLOD;
            info.compareEnable = (filter & 0x80u) != 0;
            if (sampler->Desc.ComparisonFunc < 1 || sampler->Desc.ComparisonFunc > 8) { refusedReason = "SAMPLER_COMPARISON_INVALIDA"; goto refused; }
            info.compareOp = (VkCompareOp)(sampler->Desc.ComparisonFunc - 1);
            if (s_Bc250Dx54PersistentDescriptors)
                cached = Bc250UmdVkCachedSampler(bridge, &info);
            if (cached != VK_NULL_HANDLE) {
                /* Do cache: vive ate o shutdown, nunca entra no lote que e' destruido. */
                images[i].sampler = cached;
            } else {
                if (bridge->vk.vkCreateSampler(bridge->Device, &info, NULL, &batch->Samplers[i]) != VK_SUCCESS) { refusedReason = "SAMPLER_CREATE_FALHOU"; goto refused; }
                images[i].sampler = batch->Samplers[i];
            }
            writes[i].pImageInfo = &images[i];
            sizes[2].descriptorCount++;
        } else { refusedReason = "TIPO_DE_DESCRITOR_NAO_SUPORTADO"; goto refused; }
    }
    /*
     * DX.54: conjunto do pool persistente (zerado so' na fence do lote). Sem
     * pool ou com o pool esgotado, o draw usa o pool proprio de antes -- e isso
     * e' contado e dito no log, nunca silencioso.
     */
    if (s_Bc250Dx54PersistentDescriptors) {
        set = Bc250UmdVkAllocPersistentSet(bridge, entry->DescriptorSetLayout);
        if (set != VK_NULL_HANDLE) poolKind = "PERSISTENTE";
    }
    if (set == VK_NULL_HANDLE) {
        poolInfo.maxSets = 1;
        VkDescriptorPoolSize compact[4];
        for (i = 0; i < 4; ++i) if (sizes[i].descriptorCount) compact[poolInfo.poolSizeCount++] = sizes[i];
        poolInfo.pPoolSizes = compact;
        if (bridge->vk.vkCreateDescriptorPool(bridge->Device, &poolInfo, NULL, &batch->Pool) != VK_SUCCESS) { refusedReason = "DESCRIPTOR_POOL_FALHOU"; goto refused; }
        allocInfo.descriptorPool = batch->Pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &entry->DescriptorSetLayout;
        if (bridge->vk.vkAllocateDescriptorSets(bridge->Device, &allocInfo, &set) != VK_SUCCESS) { refusedReason = "ALOCACAO_DO_CONJUNTO_FALHOU"; goto refused; }
        if (s_Bc250Dx54PersistentDescriptors) {
            bridge->DescriptorFallbackPools++;
            if (bridge->DescriptorFallbackPools <= 8u || (bridge->DescriptorFallbackPools % 256u) == 0u)
                Bc250UmdTrace("[DX54_DESC] POOL POR DRAW (fallback #%llu): pool persistente %s, conjuntos no lote=%u",
                    (unsigned long long)bridge->DescriptorFallbackPools,
                    bridge->PersistentPoolFailed ? "RECUSADO" : "esgotado ou alocacao falhou",
                    bridge->PersistentPoolSets);
        }
    }
    for (i = 0; i < entry->BindingCount; ++i) writes[i].dstSet = set;
    bridge->vk.vkUpdateDescriptorSets(bridge->Device, entry->BindingCount, writes, 0, NULL);
    bridge->vk.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
    *hasDescriptors = TRUE;
    {
        uint64_t descMicros = Bc250UmdDescNowMicros() - descStart;
        bridge->DescriptorMicrosInBatch += descMicros;
        Bc250UmdTrace("[VK_DESCRIPTOR] BOUND: bindings=%u (CB=%u SRV=%u SAMPLER=%u SRV_BUFFER=%u) [DX54_DESC] Pool=%s DescUs=%llu",
            entry->BindingCount, sizes[0].descriptorCount, sizes[1].descriptorCount, sizes[2].descriptorCount,
            sizes[3].descriptorCount, poolKind, (unsigned long long)descMicros);
    }
    return TRUE;
refused:
    /* Commands may already reference image transitions. Keep ownership through
     * teardown and prevent a partial recording from being submitted. */
    bridge->SubmissionFailed = TRUE;
    device->LastVkDrawFailureReason = refusedReason;
    if (refusedIsSrv) {
        Bc250UmdTrace("[VK_DESCRIPTOR] RECUSADO: binding index=%u motivo=%s "
            "SRV_RESOURCE=%p WxH=%ux%u Format=%u BindFlags=0x%08X Usage=%u "
            "TextureWrites=%u VkLayout=%s -- recurso e condicao nomeados",
            i, refusedReason, (void *)refusedSrvResource,
            refusedSrvResource ? refusedSrvResource->Width : 0u,
            refusedSrvResource ? refusedSrvResource->Height : 0u,
            refusedSrvResource ? (uint32_t)refusedSrvResource->Format : 0u,
            refusedSrvResource ? refusedSrvResource->BindFlags : 0u,
            refusedSrvResource ? refusedSrvResource->Usage : 0u,
            refusedSrvResource ? refusedSrvResource->TextureWriteDdiCount : 0u,
            Bc250UmdVkImageLayoutName(refusedSrvResource ?
                refusedSrvResource->VkLayout : VK_IMAGE_LAYOUT_UNDEFINED));
    } else {
        Bc250UmdTrace("[VK_DESCRIPTOR] RECUSADO: binding index=%u motivo=%s -- uma condicao NOMEADA, nao um agregado; ver bc250_umd_descriptors.c", i, refusedReason);
    }
    return FALSE;
}

static void Bc250UmdRestoreDescriptorImages(BC250_UMD_VK_BRIDGE *bridge, VkCommandBuffer cmd)
{
    BC250_VK_DESCRIPTOR_BATCH *batch = bridge->DescriptorBatches;
    if (!batch) return;
    for (uint32_t i = 0; i < BC250_DESCRIPTOR_MAX_BINDINGS; ++i)
        if (batch->RestoreImages[i].image)
            bridge->vk.vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &batch->RestoreImages[i]);
}
