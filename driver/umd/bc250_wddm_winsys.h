#ifndef BC250_WDDM_WINSYS_H
#define BC250_WDDM_WINSYS_H

#pragma warning(push)
#pragma warning(disable: 4201)
#include <windows.h>
#include <d3d9types.h>
#include <d3dumddi.h>
#pragma warning(pop)

#include "../core/bc250_allocation_abi_contract.h"
#include "../core/bc250_adapter_info_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_WINSYS_NODE_SDMA 0u
#define BC250_WINSYS_NODE_COMPUTE 1u

typedef enum BC250_WINSYS_STATUS {
    BC250_WINSYS_OK = 0,
    BC250_WINSYS_BAD_ARGUMENT,
    BC250_WINSYS_MISSING_CALLBACK,
    BC250_WINSYS_STATE_CONFLICT,
    BC250_WINSYS_CALLBACK_FAILED,
    BC250_WINSYS_BAD_CALLBACK_OUTPUT
} BC250_WINSYS_STATUS;

typedef struct BC250_WINSYS_CONTEXT {
    HANDLE RuntimeDevice;
    const D3DDDI_DEVICECALLBACKS *Callbacks;
    HANDLE KernelContext;
    /*
     * CONTEXTO DE APRESENTACAO (13/09, corrida 17:18:35) -- SEPARADO DO DE TRABALHO.
     *
     * Medido: 84 de 84 pfnPresentCb devolveram E_FAIL (0x80004005) no MESMO
     * milissegundo da chamada -- rejeicao sincrona de validacao, nao falha de
     * submissao -- com a primaria ja corretamente declarada
     * (SrcDeclaredPrimary=1, SrcRTResource nao-nulo). A hipotese da primaria
     * como causa suficiente esta REFUTADA.
     *
     * O discriminante que sobrou: os UNICOS contextos que o dxgkrnl usou para
     * apresentar nessa corrida estao no Node 0 --
     *   Ctx 0x04: Node=0 Flags=0x05 (SystemContext + VirtualAddressing)
     *   Ctx 0x07: Node=0 Flags=0x06 (GdiContext + VirtualAddressing)  <- fez os 6
     * enquanto TODO contexto que a nossa UMD cria e' Node=1 Flags=0x04
     * (VirtualAddressing apenas) e NUNCA apresentou.
     *
     * Correcao de enquadramento registrada: nao e' "node 1 e' compute". O
     * GetNodeMetadata da propria corrida declara Node 1 = DXGK_ENGINE_TYPE_3D (1)
     * e Node 0 = DXGK_ENGINE_TYPE_COPY (6). O que difere e' o no, nao o tipo de
     * engine -- e SystemContext/GdiContext sao decididos no kernel, fora do
     * alcance da UMD (D3DDDI_CREATECONTEXTFLAGS nao os expoe, d3dukmdt.h:791).
     *
     * Logo a UNICA variavel sob nosso controle e' NodeOrdinal, e este contexto
     * existe para mover exatamente essa uma variavel. O contexto de trabalho
     * continua no Node 1; o de apresentacao nasce no no de paginacao.
     *
     * NAO VALIDADO EM HARDWARE. Que o no do contexto seja a causa do E_FAIL e' H:
     * 0x80004005 e' generico e a corrida nao o determinou.
     */
    HANDLE PresentContext;
    UINT PresentNodeOrdinal;
    BOOL PresentContextCreated;
    D3DKMT_HANDLE PagingQueue;
    D3DKMT_HANDLE PagingSyncObject;
    volatile UINT64 *PagingFenceCpuVa;
    UINT NodeOrdinal;
    UINT ActiveGpuVaMappings;
    HRESULT LastCallbackHr;
    BOOL Initialized;
    BOOL ContextCreated;
    BOOL PagingQueueCreated;
} BC250_WINSYS_CONTEXT;

typedef struct BC250_WINSYS_ALLOCATION {
    D3DKMT_HANDLE KernelAllocation;
    D3DGPU_VIRTUAL_ADDRESS GpuVirtualAddress;
    uint64_t Token;
    uint64_t ByteSize;
    uint64_t Alignment;
    UINT64 PagingFenceValue;
    /*
     * Guardados na criacao para que a desalocacao seja SIMETRICA: alocacao
     * criada com hResource precisa ser devolvida com hResource, senao o objeto
     * de recurso do kernel fica orfao.
     */
    HANDLE RuntimeResource;
    D3DKMT_HANDLE KernelResource;
    BOOL DeclaredPrimary;
    BOOL Created;
    BOOL OwnsGpuVaMapping;
    BOOL Resident;
    BOOL ResidencyRequestInvalid; /* callback accepted without a usable fence */
    BOOL ResidencyPending; /* request accepted; paging fence not yet completed */
} BC250_WINSYS_ALLOCATION;

typedef struct BC250_WINSYS_MONITORED_FENCE {
    D3DKMT_HANDLE SyncObject;
    volatile UINT64 *CpuVirtualAddress;
    D3DGPU_VIRTUAL_ADDRESS GpuVirtualAddress;
    UINT64 InitialValue;
    BOOL Created;
} BC250_WINSYS_MONITORED_FENCE;

BC250_WINSYS_STATUS Bc250WinsysInitialize(
    BC250_WINSYS_CONTEXT *Context,
    HANDLE RuntimeDevice,
    const D3DDDI_DEVICECALLBACKS *Callbacks);

BC250_WINSYS_STATUS Bc250WinsysCreateVirtualContext(
    BC250_WINSYS_CONTEXT *Context,
    UINT NodeOrdinal);

BC250_WINSYS_STATUS Bc250WinsysDestroyVirtualContext(
    BC250_WINSYS_CONTEXT *Context);

/*
 * Contexto dedicado a apresentacao. Ver o bloco de proveniencia em
 * BC250_WINSYS_CONTEXT.PresentContext. Nao substitui o contexto de trabalho:
 * os dois coexistem, e so' o de apresentacao vai ao pfnPresentCb.
 */
BC250_WINSYS_STATUS Bc250WinsysCreatePresentContext(
    BC250_WINSYS_CONTEXT *Context,
    UINT NodeOrdinal);

BC250_WINSYS_STATUS Bc250WinsysDestroyPresentContext(
    BC250_WINSYS_CONTEXT *Context);

BC250_WINSYS_STATUS Bc250WinsysCreatePagingQueue(
    BC250_WINSYS_CONTEXT *Context);

BC250_WINSYS_STATUS Bc250WinsysDestroyPagingQueue(
    BC250_WINSYS_CONTEXT *Context);

/*
 * VINCULO DE RECURSO (13/09) -- CORRIGE UMA DIVERGENCIA DE CONTRATO ENTRE UMD E KMD.
 *
 * Medido na corrida 16:28:27 (pacote 3.172.0.0): TODA alocacao originada desta
 * winsys chegou ao KMD com `DXGKARG_CREATEALLOCATION.Flags.Resource == 0` e caiu
 * no ramo "[CreateAllocation] Non-resource allocation". O unico
 * "Primary token confirmed" da corrida inteira veio do caminho de display no
 * bind (a alocacao padrao do dxgkrnl), nunca do dwm.exe.
 *
 * CAUSA: Bc250WinsysAllocate zerava D3DDDICB_ALLOCATE e nunca atribuia
 * `hResource`, `D3DDDI_ALLOCATIONINFO2.Flags.Primary` nem `VidPnSourceId`. O
 * dxgkrnl so' liga `Flags.Resource` quando a UMD passa `hResource`, e o ramo do
 * KMD que marca primaria (bc250_active_miniport.c) esta atras justamente desse
 * bit -- logo era INALCANCAVEL para qualquer alocacao nossa. A "primariedade"
 * existia apenas no nosso token privado, que o KMD nem chegava a consultar.
 *
 * CONSEQUENCIA MEDIDA: as 90 chamadas de DXGI Present do dwm.exe pediam FLIP
 * (`DXGI_DDI_PRESENT_FLAGS.Flip`, dxgiddi.h:71) sobre alocacao que o kernel
 * havia classificado como nao-primaria. Bc250DxgkDdiPresent NAO foi chamada uma
 * unica vez na corrida, e o dwm.exe entrou em laco de device-lost a ~1 Hz.
 *
 * Que a ausencia de Flags.Primary/hResource seja a causa do Present falhar e' H,
 * nao P0: o hr do pfnPresentCb nunca foi registrado (defeito de instrumentacao
 * corrigido na mesma faixa). NAO VALIDADO EM HARDWARE.
 */
typedef struct BC250_WINSYS_RESOURCE_BINDING {
    /*
     * Handle de recurso do RUNTIME D3D (hRTResource da DDI CreateResource).
     * E' o valor que D3DDDICB_ALLOCATE.hResource espera -- nao e' o handle
     * privado do driver nem o hKMResource devolvido pelo kernel.
     */
    HANDLE RuntimeResource;
    UINT   VidPnSourceId;
    BOOL   Primary;
    /*
     * F2 (13/09): geometria da superficie, levada ate o KMD pela ABI de
     * alocacao. Sem ela o KMD nao consegue responder DxgkDdiDescribeAllocation
     * nem preencher DXGK_ALLOCATIONINFO.Pitch -- os dois ficavam em branco.
     *
     * `HasGeometry` FALSE e' legitimo para command buffer, staging e scratch.
     * Nesse caso os campos sao ignorados e o blob sai sem a flag.
     */
    BOOL   HasGeometry;
    BC250_ALLOCATION_GEOMETRY Geometry;
} BC250_WINSYS_RESOURCE_BINDING;

/*
 * Binding == NULL preserva o comportamento historico (alocacao avulsa, sem
 * recurso e sem primaria): e' o caso legitimo de command buffer, staging de
 * prova e scratch, que nunca sao apresentados.
 */
BC250_WINSYS_STATUS Bc250WinsysAllocateForResource(
    BC250_WINSYS_CONTEXT *Context,
    uint64_t Token,
    uint64_t ByteSize,
    uint64_t Alignment,
    const BC250_WINSYS_RESOURCE_BINDING *Binding,
    BC250_WINSYS_ALLOCATION *Allocation);

BC250_WINSYS_STATUS Bc250WinsysAllocate(
    BC250_WINSYS_CONTEXT *Context,
    uint64_t Token,
    uint64_t ByteSize,
    uint64_t Alignment,
    BC250_WINSYS_ALLOCATION *Allocation);

BC250_WINSYS_STATUS Bc250WinsysDeallocate(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation);

BC250_WINSYS_STATUS Bc250WinsysMakeResident(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation);

BC250_WINSYS_STATUS Bc250WinsysEvict(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation);

BOOL Bc250WinsysAllocationIsPrimary(
    const BC250_WINSYS_ALLOCATION *Allocation);

BC250_WINSYS_STATUS Bc250WinsysMapAllocation(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation,
    BOOL Executable);

BC250_WINSYS_STATUS Bc250WinsysWaitPaging(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation);

BC250_WINSYS_STATUS Bc250WinsysSubmitGpuVa(
    BC250_WINSYS_CONTEXT *Context,
    D3DGPU_VIRTUAL_ADDRESS CommandGpuVa,
    UINT CommandLength);

/*
 * Submit only through the context that was created for ExpectedNodeOrdinal.
 * This prevents a caller from labelling work as Node 1 while broadcasting it
 * through a Node 0 (or uncreated) context.
 */
BC250_WINSYS_STATUS Bc250WinsysSubmitGpuVaForNode(
    BC250_WINSYS_CONTEXT *Context,
    UINT ExpectedNodeOrdinal,
    D3DGPU_VIRTUAL_ADDRESS CommandGpuVa,
    UINT CommandLength);

/*
 * Copy CPU-built command bytes into the actual WDDM allocation by using the
 * runtime Lock/Unlock callbacks. A detached CPU shadow is never executable.
 */
BC250_WINSYS_STATUS Bc250WinsysUploadAllocation(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation,
    const void *Source,
    UINT ByteCount);

/* Read bytes back from the real WDDM allocation. This is deliberately a
 * separate operation from Upload so a proof cannot compare two CPU shadows. */
BC250_WINSYS_STATUS Bc250WinsysReadAllocation(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation,
    void *Destination,
    UINT ByteCount);

BC250_WINSYS_STATUS Bc250WinsysCreateMonitoredFence(
    BC250_WINSYS_CONTEXT *Context,
    UINT64 InitialValue,
    BC250_WINSYS_MONITORED_FENCE *Fence);

BC250_WINSYS_STATUS Bc250WinsysDestroyMonitoredFence(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_MONITORED_FENCE *Fence);

BC250_WINSYS_STATUS Bc250WinsysSignalFenceFromCpu(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_MONITORED_FENCE *Fence,
    UINT64 FenceValue);

/* Sinaliza a fence monitorada PELO CONTRATO: o runtime enfileira o sinal no
 * contexto e o KMD o materializa em Bc250McdmBuildSignalMonitoredFence.
 * Sem isto o dxgkrnl nao observa o sinal e o wait nunca e' satisfeito. */
BC250_WINSYS_STATUS Bc250WinsysSignalMonitoredFenceFromGpu(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_MONITORED_FENCE *Fence,
    UINT64 FenceValue);

BC250_WINSYS_STATUS Bc250WinsysWaitForFenceFromCpu(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_MONITORED_FENCE *Fence,
    UINT64 TargetFenceValue);

const char *Bc250WinsysStatusName(BC250_WINSYS_STATUS Status);

/*
 * Sink unico de trace da UMD: carimba hora/pid/imagem e anexa em
 * C:\Windows\Temp\BC250_UMD_LOG_<pid>.txt. Usado pela winsys e por
 * Bc250UmdTrace, para que exista UM formato e UM arquivo por processo.
 * Sem isto as linhas da winsys morriam em stderr, que o dwm.exe nao tem.
 */
void Bc250WinsysTraceRaw(const char *Message);

#ifdef __cplusplus
}
#endif

#endif
