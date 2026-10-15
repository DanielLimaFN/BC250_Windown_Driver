/* ===========================================================================
 * bc250_dcn_flip_contract.h -- DCN.1 (doc 710): flip pelo HUBP0 do DCN 2.0.1
 *
 * Parte pura e testavel offline do flip de scanout por hardware. O driver le o
 * estado que o UEFI GOP deixou no HUBP0/OTG0, decide se o flip e' seguro e, a
 * cada SetVidPnSourceAddress, aponta o HUBP0 para a alocacao primaria em vez
 * de copiar 11 MB pelo SDMA0.
 *
 * PROVENIENCIA (Lei 7, P1): offsets e mascaras de
 *   drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_0_3_offset.h
 *   drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_0_3_sh_mask.h
 * (copias em inc/linux_ref/dcn/). Correcao de 12/10 (doc 713): o doc 710
 * citava dcn_2_0_1, que nao existe; os 12 valores ja eram os do 2_0_3.
 * (o dcn201_resource.c do amdgpu inclui exatamente esses dois para a Cyan
 * Skillfish), todos com BASE_IDX 2; byte = (DMU_BASE.seg[2] 0x34C0 + mm) * 4,
 * DMU_BASE de inc/linux_ref/cyan_skillfish_ip_offset.h.
 * Sequencia de flip de hubp2_program_surface_flip_and_addr e criterio de flip
 * pendente de hubp2_is_flip_pending (dcn20_hubp.c): SURFACE_FLIP_TYPE em
 * DCSURF_FLIP_CONTROL, endereco ALTO e depois o BAIXO (a escrita do baixo
 * dispara o latch), pendente = SURFACE_FLIP_PENDING ou EARLIEST_INUSE != pedido.
 * =========================================================================== */
#ifndef BC250_DCN_FLIP_CONTRACT_H
#define BC250_DCN_FLIP_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes na BAR de registradores: (0x34C0 + mm) * 4. */
#define BC250_DCN_FLIP_REG_HUBP0_SURFACE_CONFIG       0x0000EA94u /* mm 0x05e5 */
#define BC250_DCN_FLIP_REG_HUBP0_PRI_VIEWPORT_DIM     0x0000EAA8u /* mm 0x05ea */
#define BC250_DCN_FLIP_REG_HUBPREQ0_SURFACE_PITCH     0x0000EB1Cu /* mm 0x0607 */
#define BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR      0x0000EB28u /* mm 0x060a */
#define BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR_HI   0x0000EB2Cu /* mm 0x060b */
#define BC250_DCN_FLIP_REG_HUBPREQ0_SURFACE_CONTROL   0x0000EB68u /* mm 0x061a */
#define BC250_DCN_FLIP_REG_HUBPREQ0_FLIP_CONTROL      0x0000EB6Cu /* mm 0x061b */
#define BC250_DCN_FLIP_REG_HUBPREQ0_EARLIEST_INUSE    0x0000EB94u /* mm 0x0625 */
#define BC250_DCN_FLIP_REG_HUBPREQ0_EARLIEST_INUSE_HI 0x0000EB98u /* mm 0x0626 */
#define BC250_DCN_FLIP_REG_OTG0_CONTROL               0x00014004u /* mm 0x1b41 */
#define BC250_DCN_FLIP_REG_OTG0_STATUS_FRAME_COUNT    0x00014030u /* mm 0x1b4c */
#define BC250_DCN_FLIP_REG_OTG0_MASTER_UPDATE_LOCK    0x0001412Cu /* mm 0x1b8b */

/* Janela mapeada pelo driver: [0xD800, 0x17800) cobre HUBP0/HUBPREQ0, cursor,
 * DPP0, OTG0, HPD0/1 e DP_AUX0/1 (desde a DCN.2, doc 713, ate 0x150C8) e,
 * ampliada para a 274 (doc 716 par.5.3, doc 718): abaixo, OTG0_INTERRUPT_DEST/
 * DCHUB_INTERRUPT_DEST2 (D06, 0xD83C/0xD850); acima, DC_GPIO_DDC1_MASK
 * (D12, 0x17640). */
#define BC250_DCN_FLIP_WINDOW_BASE  0x0000D800u
#define BC250_DCN_FLIP_WINDOW_BYTES 0x0000A000u

/* Mascaras (dcn_2_0_3_sh_mask.h). */
#define BC250_DCN_SURFACE_PIXEL_FORMAT_MASK          0x0000007Fu
#define BC250_DCN_PRI_VIEWPORT_WIDTH_MASK            0x00003FFFu
#define BC250_DCN_PRI_VIEWPORT_HEIGHT_MASK           0x3FFF0000u
#define BC250_DCN_PRI_VIEWPORT_HEIGHT_SHIFT          16u
#define BC250_DCN_SURFACE_PITCH_MASK                 0x00003FFFu
#define BC250_DCN_PRIMARY_ADDR_HIGH_MASK             0x0000FFFFu
#define BC250_DCN_PRIMARY_SURFACE_DCC_EN_MASK        0x00000002u
#define BC250_DCN_SURFACE_UPDATE_LOCK_MASK           0x00000001u
#define BC250_DCN_SURFACE_FLIP_TYPE_MASK             0x00000002u
#define BC250_DCN_SURFACE_FLIP_PENDING_MASK          0x00000100u
#define BC250_DCN_OTG_MASTER_EN_MASK                 0x00000001u
#define BC250_DCN_OTG_MASTER_UPDATE_LOCK_MASK        0x00000001u
#define BC250_DCN_FRAME_COUNT_MASK                   0x00FFFFFFu

/* O DCN le a superficie em blocos; o upstream exige 256 bytes. */
#define BC250_DCN_SURFACE_ADDRESS_ALIGN 256u

/* Motivos de recusa (bitmask; 0 = elegivel). */
#define BC250_DCN_FLIP_REFUSE_OTG_OFF          0x0001u
#define BC250_DCN_FLIP_REFUSE_VIEWPORT         0x0002u
#define BC250_DCN_FLIP_REFUSE_PITCH            0x0004u
#define BC250_DCN_FLIP_REFUSE_DCC              0x0008u
#define BC250_DCN_FLIP_REFUSE_SURFACE_LOCK     0x0010u
#define BC250_DCN_FLIP_REFUSE_MASTER_LOCK      0x0020u
#define BC250_DCN_FLIP_REFUSE_GOP_ADDRESS      0x0040u
#define BC250_DCN_FLIP_REFUSE_DEAD_BLOCK       0x0080u
#define BC250_DCN_FLIP_REFUSE_MODE             0x0100u

/* Estado lido do HUBP0/OTG0 como o GOP deixou (D2, so' leitura). */
typedef struct BC250_DCN_GOP_STATE {
    uint32_t SurfaceConfig;
    uint32_t ViewportDim;
    uint32_t SurfacePitch;
    uint32_t PrimaryAddrLo;
    uint32_t PrimaryAddrHi;
    uint32_t SurfaceControl;
    uint32_t FlipControl;
    uint32_t OtgControl;
    uint32_t MasterUpdateLock;
} BC250_DCN_GOP_STATE;

typedef struct BC250_DCN_FLIP_PLAN {
    uint64_t GopAddress;      /* endereco do HUBP0 que corresponde ao scanout do GOP */
    uint32_t Width;
    uint32_t Height;
    uint32_t PitchPixels;
    uint32_t PixelFormat;
    uint32_t BaseFlipControl; /* valor do GOP, preservado fora de SURFACE_FLIP_TYPE */
} BC250_DCN_FLIP_PLAN;

/* D2: decide se o pipeline do GOP serve para flip sem reprogramar formato,
 * pitch, viewport nem timing. Devolve 0 (elegivel) ou a mascara de recusas. */
uint32_t Bc250DcnFlipEvaluateGop(
    const BC250_DCN_GOP_STATE *State,
    uint32_t ModeWidth, uint32_t ModeHeight, uint32_t ModePitchBytes,
    BC250_DCN_FLIP_PLAN *OutPlan);

/* D3: endereco do HUBP0 para uma superficie em offset de VRAM, pela mesma
 * relacao linear do GOP (endereco do GOP <-> offset do scanout). Falha fechada
 * fora da VRAM mapeada, sem alinhamento ou acima de 48 bits. */
int Bc250DcnFlipAddressForVramOffset(
    uint64_t GopAddress, uint64_t ScanoutVramOffset,
    uint64_t SourceVramOffset, uint64_t SurfaceBytes, uint64_t VramBytes,
    uint64_t *OutAddress);

/* D3: valor de DCSURF_FLIP_CONTROL (Lei 6: so' SURFACE_FLIP_TYPE muda). */
uint32_t Bc250DcnFlipControlValue(uint32_t BaseFlipControl, int Immediate);

/* D4: o flip pedido ainda nao foi latchado? (hubp2_is_flip_pending) */
int Bc250DcnFlipIsPending(
    uint32_t FlipControl, uint32_t EarliestInUseLo, uint32_t EarliestInUseHi,
    uint64_t RequestedAddress);

/* D4, contabilidade (Corrida 269: 881 flips, 866 latchados, 15 sem destino).
 * Os flips sao numerados 1..N. Quando o latch do flip LatchedSeq e' visto e o
 * ultimo ja resolvido era ResolvedSeq, os flips entre os dois foram trocados por
 * um mais novo antes de latchar: devolve quantos. Cada flip cai em um balde so':
 * latchado + substituido + pendente no teardown == pedidos. */
uint32_t Bc250DcnFlipSupersededBetween(uint32_t ResolvedSeq, uint32_t LatchedSeq);

/* Medicao da Corrida 270 (doc 711): quadros do OTG entre dois valores do
 * contador de 24 bits (com volta), balde do latch em quadros (0,1,2,3+) e em
 * tempo (<16,7 ms, <33,4 ms, <50 ms, mais), e refresh medido em mHz. */
uint32_t Bc250DcnFrameDelta(uint32_t FromFrame, uint32_t ToFrame);
uint32_t Bc250DcnLatchFrameBucket(uint32_t Frames);
uint32_t Bc250DcnLatchUsBucket(uint32_t Microseconds);
uint32_t Bc250DcnRefreshMilliHz(uint32_t Frames, uint64_t ElapsedMicroseconds);

/* Completar o flip pela propria interrupcao (doc 716 par.6.1, doc 717): o
 * flip do HUBP0 (fonte 0x4F) so' enfileira o DPC de VSYNC (Bc250DisplayDcnIsr)
 * e nao marca "VSYNC pendente" -- so' VSTARTUP/VUPDATE/VLINE0 marcam, e esses
 * tres nunca chegaram ao IH (D06, doc 715). Por isso um DPC disparado so' por
 * um flip pode nao ter nada para notificar ao DXGK; isso NAO significa que o
 * latch nao deva ser conferido naquela chamada -- so' significa que nao ha
 * VSYNC novo. Esta decisao e' pura, sem registrador: o sinalizador consumido
 * do ISR (0 ou 1, por InterlockedExchange) e se a MESMA chamada do DPC
 * resolveu um latch novo (Bc250DcnFlipPollLatch devolveu TRUE). Devolve 1 so'
 * quando os dois sao verdadeiros, isto e', quando a propria interrupcao do
 * flip -- nao o tique periodico de 4 ms do timer -- e' testemunha desta
 * resolucao. */
int Bc250DcnFlipLatchIsViaIrq(int FlipIrqDpcPendingConsumed, int LatchDetectedThisCall);

#ifdef __cplusplus
}
#endif

#endif /* BC250_DCN_FLIP_CONTRACT_H */
