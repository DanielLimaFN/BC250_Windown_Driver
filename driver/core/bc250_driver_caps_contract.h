#ifndef BC250_DRIVER_CAPS_CONTRACT_H
#define BC250_DRIVER_CAPS_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A1 (06/09) -- fonte unica de verdade dos caps de tipo de driver.
 *
 * MOTIVO: existiam dois produtores de DXGK_DRIVERCAPS no mesmo binario --
 * Bc250DxgkDdiQueryAdapterInfoDisplay (caminho KMDOD) e Bc250McdmQueryAdapterInfo
 * (caminho WDDM sob RenderNode=1) -- e eles divergiam. O caminho KMDOD escrevia
 * SupportNonVGA=TRUE; o caminho WDDM zerava a estrutura e nunca escrevia o campo.
 * A divergencia so foi descoberta por leitura, nunca por teste.
 *
 * Este contrato nao inventa politica: cada campo abaixo cita a exigencia lida em
 * referencias/windows-driver-docs/windows-driver-docs-pr/display/ e diz por que o
 * valor e' esse. Onde a exigencia da Microsoft e' condicional, a condicao esta
 * escrita. Onde nos deliberadamente NAO declaramos um cap obrigatorio, a divida
 * esta declarada aqui em vez de escondida no meio do handler.
 */

typedef enum BC250_WDDM_DRIVER_TYPE {
    /* DxgkInitializeDisplayOnlyDriver -- perfil KMDOD, RenderNode=0. */
    BC250_WDDM_DRIVER_DISPLAY_ONLY = 0,
    /* DxgkInitialize -- display+render, RenderNode=1. */
    BC250_WDDM_DRIVER_FULL_GRAPHICS = 1
} BC250_WDDM_DRIVER_TYPE;

typedef enum BC250_DRIVER_CAPS_STATUS {
    BC250_DRIVER_CAPS_OK = 0,
    BC250_DRIVER_CAPS_BAD_ARGUMENT,
    BC250_DRIVER_CAPS_INVALID_TYPE
} BC250_DRIVER_CAPS_STATUS;

typedef struct BC250_DRIVER_CAPS_POLICY {
    /*
     * wddm-driver-and-feature-caps.md: M para Full Graphics e Display Only.
     * plug-and-play--pnp--start-and-stop-cases.md: exige o comportamento de
     * start/stop de PnP, que o driver implementa (StopDevice/RemoveDevice
     * exercitados no silicio). Declarar TRUE e' honesto nos dois perfis.
     */
    uint32_t SupportNonVGA;

    /*
     * wddm-driver-and-feature-caps.md marca M, mas
     * optimized-screen-rotation-support.md condiciona em texto expresso:
     * "required on all WDDM 1.2 drivers THAT SUPPORT ROTATED MODES".
     * A BC-250 v1 herda o modo do UEFI GOP e publica somente
     * RotationSupport.Identity/Offset0 -- nao ha modo rotacionado. Declarar
     * TRUE seria cap sem backend (Lei 4). Fica FALSE por correcao, nao por
     * omissao.
     */
    uint32_t SupportSmoothRotation;

    /*
     * direct-flip-of-video-memory.md marca "Driver implementation -- Full
     * graphics: Mandatory", e a DDI exige trocar a superficie de origem em
     * DxgkDdiSetVidPnSourceAddress. Isso exige reprogramar o endereco de
     * scanout do display controller -- escrita em DCN, que a decisao de
     * arquitetura vigente proibe fora da P9. Enquanto o display v1 e' blt para
     * o scanout herdado do GOP, DirectFlip NAO tem backend, e declara-lo seria
     * fake. DIVIDA DECLARADA: nao-conformidade conhecida, reaberta na P9 junto
     * com o flip de PRIMARY_SURFACE_ADDRESS.
     */
    uint32_t SupportDirectFlip;

    /*
     * wddm-driver-and-feature-caps.md: M para Full Graphics e Render Only, NA
     * para Display Only. O caminho WDDM ja declarava TRUE.
     * ATENCAO: o item 25 (TDR) e' o gate de saida da P6 e ainda NAO esta
     * provado no silicio -- este cap declara a intencao do contrato per-engine,
     * e nao substitui a prova.
     */
    uint32_t SupportPerEngineTDR;

    /*
     * wddm-driver-and-feature-caps.md: DXGK_FLIPCAPS.FlipOnVSyncMmIo e' M para
     * Full Graphics e Render Only, NA para Display Only.
     */
    uint32_t FlipOnVSyncMmIo;
} BC250_DRIVER_CAPS_POLICY;

/*
 * Preenche a politica de caps do tipo de driver informado. Nao toca em
 * DXGK_DRIVERCAPS: quem aplica e' o handler, para que este contrato continue
 * compilavel e testavel fora do kernel.
 */
BC250_DRIVER_CAPS_STATUS Bc250GetDriverCapsPolicy(
    BC250_WDDM_DRIVER_TYPE DriverType,
    BC250_DRIVER_CAPS_POLICY *Policy);

/*
 * Divida declarada: campos que a documentacao marca como obrigatorios para o
 * tipo de driver e que NAO declaramos, com o motivo. Serve para a auditoria e
 * para a BASE_DE_AVANCO nao perderem o rastro.
 */
uint32_t Bc250GetDriverCapsDeclaredDebtCount(BC250_WDDM_DRIVER_TYPE DriverType);

#ifdef __cplusplus
}
#endif

#endif /* BC250_DRIVER_CAPS_CONTRACT_H */
