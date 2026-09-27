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

static uint64_t Bc250UmdDescNowMicros(void)
{
    LARGE_INTEGER now, freq;
    if (!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) return 0u;
    return (uint64_t)(now.QuadPart / freq.QuadPart) * 1000000ull +
           (uint64_t)((now.QuadPart % freq.QuadPart) * 1000000ll / freq.QuadPart);
}

static BOOL Bc250UmdBindDrawDescriptors(BC250_UMD_DEVICE *device,
    VkPipelineLayout layout, VkCommandBuffer cmd, BOOL *hasDescriptors)
{
    BC250_UMD_VK_BRIDGE *bridge = &device->VkBridge;
    BC250_PSO_ENTRY *entry = NULL;
    BC250_VK_DESCRIPTOR_BATCH *batch;
    VkDescriptorPoolSize sizes[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0}, {VK_DESCRIPTOR_TYPE_SAMPLER, 0}};
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
             * Escopo declarado do upload: subrecurso 0. View que peca mip ou
             * camada alem disso leria area que ninguem carregou. Render target
             * nao passa por aqui porque seu conteudo cobre o alvo inteiro.
             */
            if (resource->VkLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                (srv->Desc.Tex2D.MostDetailedMip != 0 || srv->Desc.Tex2D.FirstArraySlice != 0)) {
                refusedReason = "SRV_ALEM_DO_SUBRECURSO_CARREGADO"; goto refused;
            }
            for (uint32_t rt = 0; rt < device->PipelineState.NumBoundRTVs; ++rt) {
                BC250_UMD_RTV *rtv = (BC250_UMD_RTV *)device->PipelineState.BoundRTVs[rt].pDrvPrivate;
                if (rtv && rtv->pResource == resource) { refusedReason = "SRV_EM_FEEDBACK_LOOP_COM_RTV"; goto refused; } /* no feedback loop */
            }
            view.image = resource->VkImg;
            view.viewType = resource->ArraySize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            view.format = Bc250UmdVkFormatFromDxgi(srv->Desc.Format);
            /* DX.24: X8 amostra alpha=1 e A8 amostra (0,0,0,a), como no D3D. */
            view.components = Bc250UmdVkSrvComponentMapping(srv->Desc.Format);
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.baseMipLevel = srv->Desc.Tex2D.MostDetailedMip;
            view.subresourceRange.levelCount = srv->Desc.Tex2D.MipLevels;
            view.subresourceRange.baseArrayLayer = srv->Desc.Tex2D.FirstArraySlice;
            view.subresourceRange.layerCount = srv->Desc.Tex2D.ArraySize;
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
        VkDescriptorPoolSize compact[3];
        for (i = 0; i < 3; ++i) if (sizes[i].descriptorCount) compact[poolInfo.poolSizeCount++] = sizes[i];
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
        Bc250UmdTrace("[VK_DESCRIPTOR] BOUND: bindings=%u (CB=%u SRV=%u SAMPLER=%u) [DX54_DESC] Pool=%s DescUs=%llu",
            entry->BindingCount, sizes[0].descriptorCount, sizes[1].descriptorCount, sizes[2].descriptorCount,
            poolKind, (unsigned long long)descMicros);
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
