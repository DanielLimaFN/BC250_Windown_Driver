/* ===========================================================================
 * bc250_dcn_ext_contract.h -- DCN.2 (doc 713): o resto do DCN sobre o GOP
 *
 * Parte pura e testavel offline de cinco entregas da P9 (ampliada pelo Daniel
 * em 12/10/2026): timing real do OTG no VidPn, VSYNC e flip pela interrupcao
 * do DCN, CRC do OTG, cursor por hardware no HUBP0/DPP0, e o lado seguro do
 * conector (EDID real por AUX, HPD so' leitura, apagar/acender o OTG).
 *
 * PROVENIENCIA (Lei 7, P1), arquivos em inc/linux_ref/dcn/:
 *   dcn_2_0_3_offset.h / dcn_2_0_3_sh_mask.h -- o dcn201_resource.c do amdgpu
 *     (Cyan Skillfish) inclui exatamente estes dois; todos com BASE_IDX 2;
 *     byte = (DMU_BASE.seg[2] 0x34C0 + mm) * 4.
 *   irq_service_dcn201.c + irqsrcs_dcn_1_0.h -- VBLANK1 = OTG0 VSTARTUP
 *     (src 0x3C, enable VSTARTUP_INT_EN, ack VSTARTUP_EVENT_CLEAR em
 *     OTG_GLOBAL_SYNC_STATUS) e PFLIP1 = HUBP0 flip (src 0x4F, enable
 *     SURFACE_FLIP_INT_MASK, ack SURFACE_FLIP_CLEAR); cliente IH DCE = 0x04
 *     (inc/linux_ref/soc15_ih_clientid.h).
 *   dcn10_optc.c -- optc1_configure_crc/optc1_get_crc, optc1_program_timing
 *     (blank_start = total - front_porch; blank_end = blank_start - active;
 *     POL 1 = sincronismo negativo), optc1_blank_crtc/unblank_crtc.
 *   dcn20_hubp.c / dcn20_dpp.c / dcn10_dpp.c / dcn10_hwseq.c -- cursor:
 *     hubp2_cursor_set_attributes/position, dpp2_set_cursor_attributes,
 *     dpp1_set_cursor_position, escala 0x3C00 (1.0 em fp16).
 *   dce_aux.c / dc_ddc_types.h -- transacao AUX por software (acquire,
 *     submit_channel_request, get_channel_status, read_channel_reply).
 * =========================================================================== */
#ifndef BC250_DCN_EXT_CONTRACT_H
#define BC250_DCN_EXT_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#ifndef BC250_DCN_EXTRA_TYPES_DEFINED
#define BC250_DCN_EXTRA_TYPES_DEFINED
typedef UINT16 uint16_t;
typedef UINT8  uint8_t;
typedef BOOLEAN bool;
#define true 1
#define false 0
#endif
#ifndef BC250_DCN2_INT_TYPES_DEFINED
#define BC250_DCN2_INT_TYPES_DEFINED
typedef INT32 int32_t;
typedef INT64 int64_t;
#endif
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_DCN_BYTE(mm) ((0x34C0u + (uint32_t)(mm)) * 4u)

/* OTG0 (mm de dcn_2_0_3_offset.h) */
#define BC250_DCN2_REG_OTG0_H_TOTAL              BC250_DCN_BYTE(0x1B2Au)
#define BC250_DCN2_REG_OTG0_H_BLANK_START_END    BC250_DCN_BYTE(0x1B2Bu)
#define BC250_DCN2_REG_OTG0_H_SYNC_A             BC250_DCN_BYTE(0x1B2Cu)
#define BC250_DCN2_REG_OTG0_H_SYNC_A_CNTL        BC250_DCN_BYTE(0x1B2Du)
#define BC250_DCN2_REG_OTG0_V_TOTAL              BC250_DCN_BYTE(0x1B2Fu)
/* doc 727 (Corrida 279): so' leitura. optc1_program_timing (dcn10_optc.c:223-229)
 * escreve OTG_V_TOTAL e, na MESMA chamada, MIN e MAX via set_vtotal_min_max --
 * "In case of V_TOTAL_CONTROL is on, make sure OTG_V_TOTAL_MAX e MIN sao
 * iguais a V_TOTAL". O D19 nunca tocou nem leu estes tres; se o GOP deixou
 * V_TOTAL_CONTROL com MIN_SEL/MAX_SEL ligado, o V_TOTAL "normal" que
 * escrevemos pode nao ser o que realmente governa a varredura. mm de
 * dcn_2_0_3_offset.h (0x1b30/0x1b31/0x1b33), BASE_IDX 2, igual ao V_TOTAL. */
#define BC250_DCN2_REG_OTG0_V_TOTAL_MIN          BC250_DCN_BYTE(0x1B30u)
#define BC250_DCN2_REG_OTG0_V_TOTAL_MAX          BC250_DCN_BYTE(0x1B31u)
#define BC250_DCN2_REG_OTG0_V_TOTAL_CONTROL      BC250_DCN_BYTE(0x1B33u)
/* dcn_2_0_3_sh_mask.h: OTG0_OTG_V_TOTAL_CONTROL. */
#define BC250_DCN2_V_TOTAL_MIN_SEL               0x00000001u
#define BC250_DCN2_V_TOTAL_MAX_SEL               0x00000002u
#define BC250_DCN2_VTOTAL_MID_REPLACING_MAX_EN   0x00000004u
#define BC250_DCN2_VTOTAL_MID_REPLACING_MIN_EN   0x00000008u
#define BC250_DCN2_FORCE_LOCK_ON_EVENT           0x00000010u
#define BC250_DCN2_REG_OTG0_V_BLANK_START_END    BC250_DCN_BYTE(0x1B36u)
#define BC250_DCN2_REG_OTG0_V_SYNC_A             BC250_DCN_BYTE(0x1B37u)
#define BC250_DCN2_REG_OTG0_V_SYNC_A_CNTL        BC250_DCN_BYTE(0x1B38u)
#define BC250_DCN2_REG_OTG0_BLANK_CONTROL        BC250_DCN_BYTE(0x1B42u)
#define BC250_DCN2_REG_OTG0_STATUS_POSITION      BC250_DCN_BYTE(0x1B4Au)
#define BC250_DCN2_REG_OTG0_CRC_CNTL             BC250_DCN_BYTE(0x1B68u)
#define BC250_DCN2_REG_OTG0_CRC0_WINDOWA_X       BC250_DCN_BYTE(0x1B6Au)
#define BC250_DCN2_REG_OTG0_CRC0_WINDOWA_Y       BC250_DCN_BYTE(0x1B6Bu)
#define BC250_DCN2_REG_OTG0_CRC0_WINDOWB_X       BC250_DCN_BYTE(0x1B6Cu)
#define BC250_DCN2_REG_OTG0_CRC0_WINDOWB_Y       BC250_DCN_BYTE(0x1B6Du)
#define BC250_DCN2_REG_OTG0_CRC0_DATA_RG         BC250_DCN_BYTE(0x1B6Eu)
#define BC250_DCN2_REG_OTG0_CRC0_DATA_B          BC250_DCN_BYTE(0x1B6Fu)
#define BC250_DCN2_REG_OTG0_GLOBAL_SYNC_STATUS   BC250_DCN_BYTE(0x1B8Au)
/* DCN.3 (doc 714): VLINE0, VSTARTUP_PARAM (so' leitura) e FMT0 (so' leitura). */
#define BC250_DCN2_REG_OTG0_VINT0_POSITION       BC250_DCN_BYTE(0x1B62u)
#define BC250_DCN2_REG_OTG0_VINT0_CONTROL        BC250_DCN_BYTE(0x1B63u)
#define BC250_DCN2_REG_OTG0_VSTARTUP_PARAM       BC250_DCN_BYTE(0x1B87u)
#define BC250_DCN2_REG_FMT0_CONTROL              BC250_DCN_BYTE(0x1840u)
#define BC250_DCN2_REG_FMT0_BIT_DEPTH_CONTROL    BC250_DCN_BYTE(0x1841u)
/* D06 (doc 716 par.5.1, doc 718, SO' LEITURA): roteamento das interrupcoes do
 * cliente DCE. mm de dcn_2_0_0_offset.h (o dcn_2_0_3_offset.h nao traz o
 * bloco dce_dc_dmu_ihc_dispdec; mesmo BASE_IDX 2, mesma formula de byte). */
#define BC250_DCN2_REG_OTG0_INTERRUPT_DEST       BC250_DCN_BYTE(0x0154u)
#define BC250_DCN2_REG_DCHUB_INTERRUPT_DEST2     BC250_DCN_BYTE(0x014Fu)
/* D12 (doc 716 par.5.2, doc 718): pad DDC1/AUX0 em modo hardware/AUX
 * (dal_ddc_open, hw_gpio.c + hw_ddc.c). mm de dcn_2_0_3_offset.h (existe
 * tambem no 2.0.0, mesmo valor). */
#define BC250_DCN2_REG_GPIO_DDC1_MASK            BC250_DCN_BYTE(0x28D0u)
/* HUBPREQ0 / CURSOR0_0 (HUBP0) / CNVC_CUR0 (DPP0) */
#define BC250_DCN2_REG_HUBPREQ0_FLIP_INTERRUPT   BC250_DCN_BYTE(0x0620u)
#define BC250_DCN2_REG_HUBPREQ0_CURSOR_SETTINGS  BC250_DCN_BYTE(0x065Eu)
#define BC250_DCN2_REG_CURSOR0_CONTROL           BC250_DCN_BYTE(0x0678u)
#define BC250_DCN2_REG_CURSOR0_SURFACE_ADDRESS   BC250_DCN_BYTE(0x0679u)
#define BC250_DCN2_REG_CURSOR0_SURFACE_ADDR_HIGH BC250_DCN_BYTE(0x067Au)
#define BC250_DCN2_REG_CURSOR0_SIZE              BC250_DCN_BYTE(0x067Bu)
#define BC250_DCN2_REG_CURSOR0_POSITION          BC250_DCN_BYTE(0x067Cu)
#define BC250_DCN2_REG_CURSOR0_HOT_SPOT          BC250_DCN_BYTE(0x067Du)
#define BC250_DCN2_REG_CURSOR0_DST_OFFSET        BC250_DCN_BYTE(0x067Fu)
#define BC250_DCN2_REG_CNVC_CUR0_CONTROL         BC250_DCN_BYTE(0x0CE0u)
#define BC250_DCN2_REG_CNVC_CUR0_COLOR0          BC250_DCN_BYTE(0x0CE1u)
#define BC250_DCN2_REG_CNVC_CUR0_COLOR1          BC250_DCN_BYTE(0x0CE2u)
#define BC250_DCN2_REG_CNVC_CUR0_FP_SCALE_BIAS   BC250_DCN_BYTE(0x0CE3u)
/* D23: Color Management / Gamma LUT (DPP0 / CM0) */
#define BC250_DCN2_REG_CM0_BLNDGAM_CONTROL       BC250_DCN_BYTE(0x0D63u)
#define BC250_DCN2_REG_CM0_BLNDGAM_LUT_INDEX     BC250_DCN_BYTE(0x0D64u)
#define BC250_DCN2_REG_CM0_BLNDGAM_LUT_DATA      BC250_DCN_BYTE(0x0D65u)
/* HPD0/1 (passo 8 mm) e DP_AUX0/1 (passo 0x1C mm): o DCN 2.0.3 tem dois. */
#define BC250_DCN2_HPD_COUNT                     2u
#define BC250_DCN2_REG_HPD_INT_STATUS(i)         BC250_DCN_BYTE(0x1F14u + 8u * (uint32_t)(i))
#define BC250_DCN2_AUX_COUNT                     2u
#define BC250_DCN2_REG_AUX_CONTROL(i)            BC250_DCN_BYTE(0x1F50u + 0x1Cu * (uint32_t)(i))
#define BC250_DCN2_REG_AUX_SW_CONTROL(i)         BC250_DCN_BYTE(0x1F51u + 0x1Cu * (uint32_t)(i))
#define BC250_DCN2_REG_AUX_ARB_CONTROL(i)        BC250_DCN_BYTE(0x1F52u + 0x1Cu * (uint32_t)(i))
#define BC250_DCN2_REG_AUX_INTERRUPT_CONTROL(i)  BC250_DCN_BYTE(0x1F53u + 0x1Cu * (uint32_t)(i))
#define BC250_DCN2_REG_AUX_SW_STATUS(i)          BC250_DCN_BYTE(0x1F54u + 0x1Cu * (uint32_t)(i))
#define BC250_DCN2_REG_AUX_SW_DATA(i)            BC250_DCN_BYTE(0x1F56u + 0x1Cu * (uint32_t)(i))
#define BC250_DCN2_REG_AUX_LS_STATUS(i)          BC250_DCN_BYTE(0x1F55u + 0x1Cu * (uint32_t)(i))
#define BC250_DCN2_REG_AUX_DPHY_TX_CONTROL(i)    BC250_DCN_BYTE(0x1F59u + 0x1Cu * (uint32_t)(i))
#define BC250_DCN2_REG_AUX_DPHY_RX_CONTROL0(i)   BC250_DCN_BYTE(0x1F5Au + 0x1Cu * (uint32_t)(i))
#define BC250_DCN2_REG_AUX_DPHY_RX_CONTROL1(i)   BC250_DCN_BYTE(0x1F5Bu + 0x1Cu * (uint32_t)(i))

/* Campos (dcn_2_0_3_sh_mask.h). */
#define BC250_DCN2_TOTAL_MASK                    0x00007FFFu
#define BC250_DCN2_START_MASK                    0x00007FFFu
#define BC250_DCN2_END_MASK                      0x7FFF0000u
#define BC250_DCN2_END_SHIFT                     16u
#define BC250_DCN2_SYNC_POL_MASK                 0x00000001u
#define BC250_DCN2_VERT_COUNT_MASK               0x00007FFFu
#define BC250_DCN2_VSTARTUP_INT_EN               0x00000001u
#define BC250_DCN2_VSTARTUP_EVENT_OCCURRED       0x00000004u
#define BC250_DCN2_VSTARTUP_EVENT_CLEAR          0x00000010u
#define BC250_DCN2_VSTARTUP_INT_STATUS           0x00000008u
#define BC250_DCN2_VUPDATE_NO_LOCK_INT_EN        0x00001000u
#define BC250_DCN2_VUPDATE_NO_LOCK_INT_STATUS    0x00008000u
#define BC250_DCN2_VUPDATE_NO_LOCK_EVENT_CLEAR   0x00010000u
#define BC250_DCN2_VINT0_LINE_START_MASK         0x00007FFFu
#define BC250_DCN2_VINT0_LINE_END_MASK           0x7FFF0000u
#define BC250_DCN2_VINT0_LINE_END_SHIFT          16u
#define BC250_DCN2_VINT0_INT_ENABLE              0x00000100u
#define BC250_DCN2_VINT0_INT_STATUS              0x00010000u
#define BC250_DCN2_VINT0_CLEAR                   0x00100000u
#define BC250_DCN2_VSTARTUP_START_MASK           0x000003FFu
#define BC250_DCN2_SURFACE_FLIP_INT_MASK         0x00000001u
#define BC250_DCN2_SURFACE_FLIP_CLEAR            0x00000100u
#define BC250_DCN2_SURFACE_FLIP_OCCURRED         0x00010000u
#define BC250_DCN2_CRC_EN                        0x00000001u
#define BC250_DCN2_CRC_CONT_EN                   0x00000010u
#define BC250_DCN2_CRC0_SELECT_MASK              0x00700000u
#define BC250_DCN2_CRC_R_MASK                    0x0000FFFFu
#define BC250_DCN2_CRC_G_MASK                    0xFFFF0000u
#define BC250_DCN2_CRC_B_MASK                    0x0000FFFFu
#define BC250_DCN2_BLANK_CURRENT_STATE           0x00000001u
#define BC250_DCN2_BLANK_DATA_EN                 0x00000100u
#define BC250_DCN2_BLANK_DE_MODE                 0x00010000u
#define BC250_DCN2_CURSOR_ENABLE                 0x00000001u
#define BC250_DCN2_CURSOR_2X_MAGNIFY             0x00000010u
#define BC250_DCN2_CURSOR_MODE_MASK              0x00000700u
#define BC250_DCN2_CURSOR_MODE_SHIFT             8u
#define BC250_DCN2_CURSOR_PITCH_MASK             0x00030000u
#define BC250_DCN2_CURSOR_PITCH_SHIFT            16u
#define BC250_DCN2_CURSOR_LPC_MASK               0x1F000000u
#define BC250_DCN2_CURSOR_LPC_SHIFT              24u
#define BC250_DCN2_CURSOR_HEIGHT_MASK            0x000001FFu
#define BC250_DCN2_CURSOR_WIDTH_SHIFT            16u
#define BC250_DCN2_CURSOR_Y_MASK                 0x00003FFFu
#define BC250_DCN2_CURSOR_X_SHIFT                16u
#define BC250_DCN2_CURSOR_X_MASK                 0x3FFF0000u
#define BC250_DCN2_CURSOR_HOT_Y_MASK             0x000000FFu
#define BC250_DCN2_CURSOR_HOT_X_SHIFT            16u
#define BC250_DCN2_CURSOR0_DST_Y_OFFSET_MASK     0x000000FFu
#define BC250_DCN2_CURSOR0_CHUNK_HDL_ADJUST_MASK 0x00000300u
#define BC250_DCN2_CURSOR0_CHUNK_HDL_ADJUST_SHIFT 8u
#define BC250_DCN2_CUR0_ENABLE                   0x00000001u
#define BC250_DCN2_CUR0_EXPANSION_MODE           0x00000002u
#define BC250_DCN2_CUR0_ROM_EN                   0x00000008u
#define BC250_DCN2_CUR0_MODE_MASK                0x00000070u
#define BC250_DCN2_CUR0_MODE_SHIFT               4u
#define BC250_DCN2_CUR0_FP_SCALE_ONE             0x00003C00u
#define BC250_DCN2_HPD_SENSE                     0x00000002u
#define BC250_DCN2_AUX_EN                        0x00000001u
#define BC250_DCN2_AUX_HPD_SEL_MASK              0x00700000u
#define BC250_DCN2_AUX_HPD_SEL_SHIFT             20u
#define BC250_DCN2_AUX_LS_READ_EN                0x00000100u
#define BC250_DCN2_AUX_RX_RECEIVE_WINDOW_MASK    0x00000700u
#define BC250_DCN2_AUX_SW_GO                     0x00000001u
#define BC250_DCN2_AUX_SW_START_DELAY_MASK       0x000000F0u
#define BC250_DCN2_AUX_SW_WR_BYTES_MASK          0x001F0000u
#define BC250_DCN2_AUX_SW_WR_BYTES_SHIFT         16u
#define BC250_DCN2_AUX_REG_RW_CNTL_STATUS_MASK   0x0000000Cu
#define BC250_DCN2_AUX_REG_RW_CNTL_STATUS_SHIFT  2u
#define BC250_DCN2_AUX_SW_USE_AUX_REG_REQ        0x00010000u
#define BC250_DCN2_AUX_SW_DONE_USING_AUX_REG     0x00020000u
#define BC250_DCN2_AUX_SW_DONE_ACK               0x00000002u
#define BC250_DCN2_AUX_SW_DONE                   0x00000001u
#define BC250_DCN2_AUX_SW_RX_TIMEOUT_STATE_MASK  0x00000070u
#define BC250_DCN2_AUX_SW_RX_TIMEOUT             0x00000080u
#define BC250_DCN2_AUX_SW_HPD_DISCON             0x00000200u
#define BC250_DCN2_AUX_SW_RX_INVALID_STOP        0x00004000u
#define BC250_DCN2_AUX_SW_RX_RECV_NO_DET         0x00100000u
#define BC250_DCN2_AUX_SW_RX_RECV_INVALID_H      0x00400000u
#define BC250_DCN2_AUX_SW_RX_RECV_INVALID_L      0x00800000u
#define BC250_DCN2_AUX_SW_REPLY_BYTE_COUNT_MASK  0x1F000000u
#define BC250_DCN2_AUX_SW_REPLY_BYTE_COUNT_SHIFT 24u
#define BC250_DCN2_AUX_SW_DATA_RW                0x00000001u
#define BC250_DCN2_AUX_SW_DATA_MASK              0x0000FF00u
#define BC250_DCN2_AUX_SW_DATA_SHIFT             8u
#define BC250_DCN2_AUX_SW_INDEX_MASK             0x001F0000u
#define BC250_DCN2_AUX_SW_AUTOINC_DISABLE        0x80000000u
#define BC250_DCN2_SW_CAN_ACCESS_AUX             1u
#define BC250_DCN2_DMCU_CAN_ACCESS_AUX           2u
/* D06 (dcn_2_0_0_sh_mask.h): OTG0_IHC_OTG_VERTICAL_INTERRUPT0_DEST bit 9,
 * OTG0_IHC_VSTARTUP_INTERRUPT_DEST bit 16, OTG0_IHC_V_UPDATE_NO_LOCK_
 * INTERRUPT_DEST bit 19; DCHUB_INTERRUPT_DEST2__HUBP0_IHC_FLIP_INTERRUPT_DEST
 * bit 0. A semantica do bit (0 ou 1 = roteado ao host) e' H: o header nao
 * documenta, e so' se decide por comparacao com a do flip, que sabemos
 * chegar ao IH (D05). */
#define BC250_DCN2_OTG0_VLINE0_DEST_MASK          0x00000200u
#define BC250_DCN2_OTG0_VSTARTUP_DEST_MASK        0x00010000u
#define BC250_DCN2_OTG0_VUPDATE_NO_LOCK_DEST_MASK 0x00080000u
#define BC250_DCN2_DCHUB_HUBP0_FLIP_DEST_MASK     0x00000001u
/* D12 (dcn_2_0_3_sh_mask.h DC_GPIO_DDC1_MASK): DC_GPIO_DDC1CLK_MASK bit 0,
 * DC_GPIO_DDC1DATA_MASK bit 8, AUX_PAD1_MODE bit 16. */
#define BC250_DCN2_GPIO_DDC1CLK_MASK              0x00000001u
#define BC250_DCN2_GPIO_DDC1DATA_MASK             0x00000100u
#define BC250_DCN2_GPIO_AUX_PAD1_MODE              0x00010000u
/* C09 (doc 765): a segunda metade do hw_ddc.c set_config(MODE_AUX):
 * DC_GPIO_AUX_CTRL_5.DDC_PAD1_I2CMODE (bit 12) a 0. mmDC_GPIO_AUX_CTRL_5 =
 * 0x291D, BASE_IDX 2 (dcn_2_0_3_offset.h); mascara 0x00001000
 * (dcn_2_0_3_sh_mask.h). Rastro E03 do amdgpu nesta placa: 0x0003F55A ->
 * 0x0003E55A, e DDC1_MASK em 0xCF411010. */
#define BC250_DCN2_REG_GPIO_AUX_CTRL_5            BC250_DCN_BYTE(0x291Du)
#define BC250_DCN2_GPIO_DDC_PAD1_I2CMODE           0x00001000u

/* Cliente e fontes do IH (irqsrcs_dcn_1_0.h, soc15_ih_clientid.h). */
#define BC250_IH_CLIENTID_DCE                    0x04u
#define BC250_IH_SRCID_DCN_OTG0_VSTARTUP         0x3Cu
#define BC250_IH_SRCID_DCN_HUBP0_FLIP            0x4Fu
/* DCN.3 (doc 714), irq_service_dcn201.c: VUPDATE1 = OTG0 VUPDATE sem lock
 * (0x57); DC1_VLINE0 = OTG1_VERTICAL_INTERRUPT0 (0x1E, ext_id/src_data[0] 6). */
#define BC250_IH_SRCID_DCN_OTG0_VUPDATE_NO_LOCK  0x57u
#define BC250_IH_SRCID_DCN_OTG0_VLINE0           0x1Eu
#define BC250_IH_CTXID_DCN_OTG0_VLINE0           6u

/* Classifica uma entrada do IH do cliente DCE (to_dal_irq_source_dcn201). */
#define BC250_DCN_IRQ_NONE      0u
#define BC250_DCN_IRQ_VSTARTUP  1u
#define BC250_DCN_IRQ_VUPDATE   2u
#define BC250_DCN_IRQ_VLINE0    3u
#define BC250_DCN_IRQ_FLIP      4u
uint32_t Bc250DcnIrqClassify(uint32_t SrcId, uint32_t ExtId);

/* optc1_setup_vertical_interrupt0: linha de inicio e fim (fim = inicio + 1). */
uint32_t Bc250DcnVline0Position(uint32_t StartLine, uint32_t VTotal);

/* dcn10_aux_initialize (dcn10_link_encoder.c): AUX_HPD_SEL = hpd, LS_READ_EN 0;
 * e AUX_DPHY_RX_CONTROL0.AUX_RX_RECEIVE_WINDOW = 0. */
uint32_t Bc250DcnAuxInitControl(uint32_t ReadControl, uint32_t HpdSel);
uint32_t Bc250DcnAuxInitRxControl0(uint32_t ReadRxControl0);

/* Acoes AUX (dc_ddc_types.h). */
#define BC250_AUX_ACTION_I2C_WRITE               0x00u
#define BC250_AUX_ACTION_I2C_READ                0x10u
#define BC250_AUX_ACTION_I2C_WRITE_MOT           0x40u
#define BC250_AUX_ACTION_I2C_READ_MOT            0x50u
#define BC250_AUX_ACTION_DP_READ                 0x90u
#define BC250_EDID_I2C_ADDRESS                   0x50u

/* Lei 6: (lido & ~mask) | (valor & mask). */
uint32_t Bc250DcnField(uint32_t Read, uint32_t Mask, uint32_t Value);

/* ---- 1. Timing real do OTG ------------------------------------------------ */
typedef struct BC250_DCN_TIMING {
    uint32_t HActive, HTotal, HFrontPorch, HSyncWidth;
    uint32_t VActive, VTotal, VFrontPorch, VSyncWidth;
    uint32_t HSyncPositive, VSyncPositive;
    uint32_t RefreshMilliHz;      /* 0 = ainda nao medido */
    uint64_t PixelRateHz;         /* 0 = ainda nao medido */
} BC250_DCN_TIMING;

#define BC250_DCN_TIMING_OK               0u
#define BC250_DCN_TIMING_REFUSE_TOTAL     0x01u /* total <= ativo */
#define BC250_DCN_TIMING_REFUSE_ACTIVE    0x02u /* ativo do OTG != modo do GOP */
#define BC250_DCN_TIMING_REFUSE_SYNC      0x04u /* sync fora do blank */
#define BC250_DCN_TIMING_REFUSE_REFRESH   0x08u /* medida fora de [24, 240] Hz */

/* Decodifica os registradores do OTG (inverso de optc1_program_timing) e
 * confere contra o modo do GOP. Devolve bitmask de recusa (0 = ok). */
uint32_t Bc250DcnTimingDecode(
    uint32_t HTotalReg, uint32_t HBlankStartEnd, uint32_t HSyncA, uint32_t HSyncCntl,
    uint32_t VTotalReg, uint32_t VBlankStartEnd, uint32_t VSyncA, uint32_t VSyncCntl,
    uint32_t ExpectedWidth, uint32_t ExpectedHeight, BC250_DCN_TIMING *Out);

/* Refresh medido: linhas varridas no intervalo (quadros * VTotal + delta da
 * posicao vertical) sobre o tempo, em unidades de 100 ns. */
uint32_t Bc250DcnRefreshFromScan(uint32_t Frames, int32_t VertDelta, uint32_t VTotal,
    uint64_t Elapsed100ns);

/* Fecha o timing com o refresh medido (pixel rate = refresh * HTotal * VTotal). */
uint32_t Bc250DcnTimingApplyRefresh(BC250_DCN_TIMING *Timing, uint32_t RefreshMilliHz);

/* ---- 6. EDID ---------------------------------------------------------------- */
/* Codifica o DTD (18 bytes) do timing; o tamanho fisico vai em mm. */
void Bc250DcnEncodeDtd(const BC250_DCN_TIMING *Timing, uint32_t HSizeMm, uint32_t VSizeMm,
    uint8_t Dtd[18]);
/* Decodifica um DTD (0 = descritor que nao e' timing). */
int Bc250DcnDecodeDtd(const uint8_t Dtd[18], BC250_DCN_TIMING *Out);
int Bc250DcnEdidBlockValid(const uint8_t Block[128]);
/* Indice (0..3) do DTD do bloco base igual ao timing do OTG (ativo, totais e
 * clock de pixel a 0,5%); -1 se nenhum. */
int Bc250DcnEdidFindTiming(const uint8_t Block[128], const BC250_DCN_TIMING *Timing);

/* ---- 4. Cursor --------------------------------------------------------------- */
#define BC250_DCN_CURSOR_MAX                  64u
#define BC250_DCN_CURSOR_BYTES                (BC250_DCN_CURSOR_MAX * BC250_DCN_CURSOR_MAX * 4u)
#define BC250_DCN_CURSOR_MODE_UN_PREMULT      3u   /* CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA */
#define BC250_DCN_CURSOR_PITCH_64             0u   /* CURSOR_PITCH_64_PIXELS */
#define BC250_DCN_CURSOR_LPC_8                3u   /* CURSOR_LINE_PER_CHUNK_8 (33..64 px) */
#define BC250_DCN_CURSOR_LPC_16               4u   /* CURSOR_LINE_PER_CHUNK_16 (1..32 px) */
#define BC250_DCN_POINTER_MONOCHROME          0x1u
#define BC250_DCN_POINTER_COLOR               0x2u
#define BC250_DCN_POINTER_MASKED_COLOR        0x4u

/* Resultado da conversao. */
#define BC250_DCN_CURSOR_REFUSED              0   /* forma invalida ou grande demais */
#define BC250_DCN_CURSOR_OK                   1
#define BC250_DCN_CURSOR_NEEDS_XOR            2   /* tem pixel de inversao: fica no software */

/* Converte a forma do WDDM para ARGB 64x64 (pitch 256 B), DCN.3 (doc 714).
 * Layout (DXGKARG_SETPOINTERSHAPE no WDK, e VBoxMPWddm.cpp, que le o XOR em
 * pPixels + Height * Pitch):
 *   Cor: ARGB 32 bpp, Width x Height.
 *   Monocromatico: mascara AND 1 bpp Width x Height, depois XOR do mesmo
 *     tamanho (2*Height linhas). AND=1/XOR=0 transparente; AND=0 preto ou
 *     branco pelo XOR; AND=1/XOR=1 inverte a tela.
 *   Cor mascarada: ARGB com a mascara no alfa (so' 0 ou 0xFF). 0 = o RGB
 *     substitui a tela (opaco); 0xFF = XOR do RGB com a tela (RGB 0 =
 *     transparente).
 * A inversao (XOR com a tela) nao existe no modo ARGB do cursor; o modo
 * COLOR_1BIT_AND/MONO do DCN nao tem layout de bits no upstream ("todo:
 * clarify" em dpp2_set_cursor_attributes). Forma com inversao devolve
 * NEEDS_XOR e o Windows desenha por software, sem aproximar. */
int Bc250DcnCursorConvert(uint32_t Flags, uint32_t Width, uint32_t Height, uint32_t Pitch,
    const uint8_t *Pixels, uint32_t *Dst64x64);

typedef struct BC250_DCN_CURSOR_POS {
    uint32_t Enable;
    uint32_t PositionReg;   /* CURSOR_POSITION */
    uint32_t HotSpotReg;    /* CURSOR_HOT_SPOT (usado como recorte na borda) */
} BC250_DCN_CURSOR_POS;

/* X,Y = pixel do canto superior esquerdo (DXGKARG_SETPOINTERPOSITION).
 * Fora da tela pela esquerda/topo vira posicao 0 com recorte no hot spot,
 * como o amdgpu_dm; invisivel ou fora da tela desliga. */
void Bc250DcnCursorPosition(int32_t X, int32_t Y, uint32_t Visible, uint32_t Width,
    uint32_t Height, uint32_t ScreenW, uint32_t ScreenH, BC250_DCN_CURSOR_POS *Out);

uint32_t Bc250DcnCursorControl(uint32_t Read, uint32_t Width, uint32_t Enable);

/* ---- 2/3. Interrupcao e CRC -------------------------------------------------- */
/* Janela A e B = quadro ativo inteiro, selecao UNION_WINDOW_A_B (0). */
uint32_t Bc250DcnCrcWindowX(uint32_t Width);
uint32_t Bc250DcnCrcWindowY(uint32_t Height);
uint32_t Bc250DcnCrcCntlEnable(uint32_t Read);

/* ---- 6. AUX ------------------------------------------------------------------- */
/* Cabecalho de uma requisicao (dce_aux.c): 3 bytes de acao+endereco e, com
 * Length>0, um quarto byte com Length-1. Devolve quantos bytes escrever. */
uint32_t Bc250DcnAuxComposeHeader(uint32_t Action, uint32_t Address, uint32_t Length,
    uint8_t Out[4]);

#define BC250_AUX_REPLY_ACK       0u
#define BC250_AUX_REPLY_NACK      1u
#define BC250_AUX_REPLY_DEFER     2u
#define BC250_AUX_REPLY_I2C_NACK  3u
#define BC250_AUX_REPLY_I2C_DEFER 4u
#define BC250_AUX_REPLY_INVALID   5u
/* Primeiro byte da resposta, ja deslocado (>> 4) como em read_channel_reply. */
uint32_t Bc250DcnAuxDecodeReply(uint32_t ReplyNibble);

/* ---- 7. D06: roteamento das interrupcoes (so' leitura) ----------------------- */
typedef struct BC250_DCN_IRQ_DEST {
    uint32_t VstartupDest;       /* OTG0_INTERRUPT_DEST bit 16 */
    uint32_t VupdateNoLockDest;  /* OTG0_INTERRUPT_DEST bit 19 */
    uint32_t Vline0Dest;         /* OTG0_INTERRUPT_DEST bit 9 */
    uint32_t FlipDest;           /* DCHUB_INTERRUPT_DEST2 bit 0 (HUBP0) */
} BC250_DCN_IRQ_DEST;

/* Pura: so' decodifica os quatro bits dos dois registradores lidos (doc 716
 * par.5.1, doc 718). Nao decide nada por si; serve para comparar a rota do
 * flip (chega ao IH, D05) contra as tres que nao chegam (D06, doc 715). */
void Bc250DcnIrqDestDecode(uint32_t OtgInterruptDest, uint32_t DchubInterruptDest2,
    BC250_DCN_IRQ_DEST *Out);

/* ---- 8. D12: pad DDC1/AUX0 em modo hardware/AUX ------------------------------ */
/* Pura (Lei 6): zera DC_GPIO_DDC1CLK_MASK/DC_GPIO_DDC1DATA_MASK (pino
 * dirigido por hardware, nao mascarado por software) e liga AUX_PAD1_MODE
 * (pad em modo AUX, nao DDC/I2C), preservando o resto do registrador -- como
 * dal_ddc_open(GPIO_MODE_HARDWARE, MODE_AUX) (hw_gpio.c dal_hw_gpio_config_mode
 * caso GPIO_MODE_HARDWARE para os dois pinos + hw_ddc.c set_config caso
 * GPIO_DDC_CONFIG_TYPE_MODE_AUX). Os blocos dc_gpio_aux_ctrl_5/phy_aux_cntl
 * de hw_ddc.c nao existem no DCN 2.x (guardados por ponteiro nulo no
 * upstream) e nao tem equivalente aqui (Q, nao inventar). */
uint32_t Bc250DcnAuxPadHardwareModeValue(uint32_t Read);
/* C09 (doc 765): DC_GPIO_AUX_CTRL_5 com DDC_PAD1_I2CMODE a 0, resto preservado. */
uint32_t Bc250DcnAuxPadI2cModeOffValue(uint32_t Read);

/* ---- 9. D19: troca de refresh no mesmo modo (so' V_TOTAL) ------------------- */
/* Motivos de recusa (bitmask; 0 = elegivel). */
#define BC250_DCN_VTOTAL_REFUSE_RANGE          0x01u /* refresh pedido fora de [24, 240] Hz */
#define BC250_DCN_VTOTAL_REFUSE_NOT_LOWER      0x02u /* so' suporta diminuir o refresh (aumentar V_TOTAL) */
#define BC250_DCN_VTOTAL_REFUSE_OVERFLOW       0x04u /* V_TOTAL novo nao cabe em 15 bits (BC250_DCN2_TOTAL_MASK) */
#define BC250_DCN_VTOTAL_REFUSE_INPUT          0x08u /* VTotal/refresh atuais invalidos (zero) */

/* optc1_program_timing: blank_start = total - front_porch, independente de
 * V_TOTAL; o sync (OTG0_V_SYNC_A) e' um registrador proprio, tambem
 * independente de V_TOTAL. Logo mudar SO' V_TOTAL so' estica (ou encolhe) o
 * trecho entre blank_start e o total -- nunca move blank nem sync. Esta
 * funcao so' permite DIMINUIR o refresh (aumentar V_TOTAL): diminuir V_TOTAL
 * poderia encolher esse trecho a ponto de invadir o sync, e essa direcao
 * fica fora do escopo (doc 716 par.6.4, proposto para a 275). Pura: so'
 * aritmetica, sem registrador. */
uint32_t Bc250DcnComputeVTotalForRefresh(
    uint32_t CurrentVTotal, uint32_t CurrentRefreshMilliHz,
    uint32_t DesiredRefreshMilliHz, uint32_t *OutNewVTotal);

/* ---- 10. D19b: duracao real da janela com V_TOTAL alterado (doc 723/724) --- */
/* Corrida 275: operador relatou tela cheia de artefatos no bind, nao mapeada
 * no criterio da 275. O proprio Bc250Dcn2VTotalProve mede duas vezes
 * (Bc250Dcn2MeasureTiming, ~200 ms de KeDelayExecutionThread cada) com o
 * V_TOTAL alterado entre a 1a e a 2a medida -- ou seja, ~400 ms de dwell time
 * com o monitor sincronizado num V_TOTAL diferente do GOP, antes de
 * devolver. Isso e' candidato forte (H, nao P0 ainda) para a duracao
 * perceptivel do artefato relatado. Esta funcao pura so' calcula o elapsed em
 * ms entre dois QPC, para logar a duracao real (nao so' a ordem das linhas)
 * e permitir correlacionar com o relato do operador na proxima corrida.
 * Fail-safe: frequencia <=0 ou Now antes de Start devolve 0 (nunca negativo
 * nem divide por zero). */
uint64_t Bc250DcnElapsedMs(int64_t StartQpc, int64_t NowQpc, int64_t FreqQpc);

#ifdef __cplusplus
}
#endif

#endif /* BC250_DCN_EXT_CONTRACT_H */
