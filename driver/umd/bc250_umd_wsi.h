/*
 * WSI.1/WSI.2 (docs 757 e 759) -- interface privada entre a WSI Win32 da RADV
 * da BC-250 e a UMD D3D11 (bc250umd.dll), no MESMO processo.
 *
 * Um app Vulkan comum (vkcube baixado, jogo) apresenta numa janela: a WSI da
 * RADV cria um device D3D11 na BC-250 e uma swapchain DXGI (modelo blt) na
 * janela, e passa pelo DWM como qualquer app D3D11.
 *
 * WSI.1: textura compartilhada por imagem, CopyResource no D3D11 e Present;
 *        a CPU esperava a GPU duas vezes por quadro.
 * WSI.2: a RADV do app copia direto para a alocacao WDDM do back buffer (pela
 *        GPUVA, que e' do processo, como na Frente B do doc 547) e a ordem
 *        entre o app e o Present sai de duas fences monitoradas criadas por
 *        esta UMD e importadas pela RADV:
 *          "render pronto"  -- o blit do app sinaliza; o contexto de
 *                              apresentacao espera, pela GPU, antes do Present;
 *          "present pronto" -- o contexto de apresentacao sinaliza depois do
 *                              Present; o blit seguinte do app espera.
 *        Nenhuma espera da CPU por quadro.
 *
 * A captura de GPUVA e' por thread: BeginCapture arma, o primeiro recurso
 * criado depois na mesma thread e' gravado, EndCapture devolve e desarma.
 *
 * A copia espelho do lado da Mesa esta em
 * mesa-radv-wddm/src/vulkan/wsi/bc250_umd_wsi.h; os dois tem de ficar
 * identicos (layout e versao).
 */
#ifndef BC250_UMD_WSI_H
#define BC250_UMD_WSI_H

#include <windows.h>

#define BC250_UMD_WSI_VERSION 2u
#define BC250_UMD_WSI_MAX_FENCES 4u

typedef struct BC250_UMD_WSI_SURFACE {
    UINT32 Size;            /* sizeof(BC250_UMD_WSI_SURFACE), preenchido por quem pede */
    UINT32 Version;         /* BC250_UMD_WSI_VERSION, preenchido pela UMD */
    UINT32 Width;
    UINT32 Height;
    UINT32 RowPitch;        /* bytes por linha da alocacao WDDM (layout linear) */
    UINT32 DxgiFormat;
    UINT32 GpuAliased;      /* 1 = a UMD desenha na propria alocacao (VkImage linear sobre ela) */
    UINT32 Reserved;
    UINT64 GpuVirtualAddress;
    UINT64 ByteSize;
    void  *UmdResource;     /* opaco: MarkWritten / ArmPresent */
    void  *UmdDevice;       /* opaco (v2): CreateSharedFence / ArmPresent */
} BC250_UMD_WSI_SURFACE;

typedef BOOL (WINAPI *PFN_BC250_UMD_WSI_BEGIN_CAPTURE)(void);
typedef BOOL (WINAPI *PFN_BC250_UMD_WSI_END_CAPTURE)(BC250_UMD_WSI_SURFACE *Surface);
typedef BOOL (WINAPI *PFN_BC250_UMD_WSI_MARK_WRITTEN)(void *UmdResource);
/* v2: fence monitorada compartilhada do device; devolve um NT handle novo
 * (o chamador fica dono dele) e o indice da fence neste device. */
typedef BOOL (WINAPI *PFN_BC250_UMD_WSI_CREATE_SHARED_FENCE)(void *UmdDevice,
    HANDLE *NtHandle, UINT32 *FenceIndex);
/* v2: o PROXIMO Present deste device com UmdResource como origem espera, pela
 * GPU, RenderFence >= RenderValue, apresenta a alocacao como esta (sem copia
 * da UMD) e sinaliza PresentFence = PresentValue depois. */
typedef BOOL (WINAPI *PFN_BC250_UMD_WSI_ARM_PRESENT)(void *UmdDevice, void *UmdResource,
    UINT32 RenderFence, UINT64 RenderValue, UINT32 PresentFence, UINT64 PresentValue);

#define BC250_UMD_WSI_BEGIN_CAPTURE_NAME       "Bc250UmdWsiBeginCapture"
#define BC250_UMD_WSI_END_CAPTURE_NAME         "Bc250UmdWsiEndCapture"
#define BC250_UMD_WSI_MARK_WRITTEN_NAME        "Bc250UmdWsiMarkWritten"
#define BC250_UMD_WSI_CREATE_SHARED_FENCE_NAME "Bc250UmdWsiCreateSharedFence"
#define BC250_UMD_WSI_ARM_PRESENT_NAME         "Bc250UmdWsiArmPresent"

#endif /* BC250_UMD_WSI_H */
