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

/* Cliente e fontes do IH (irqsrcs_dcn_1_0.h, soc15_ih_clientid.h). */
#define BC250_IH_CLIENTID_DCE                    0x04u
#define BC250_IH_SRCID_DCN_OTG0_VSTARTUP         0x3Cu
#define BC250_IH_SRCID_DCN_HUBP0_FLIP            0x4Fu

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

/* Converte a forma do WDDM para ARGB 64x64 (pitch 256 B). Cor: copia ARGB.
 * Monocromatico (AND depois XOR, 1 bpp): AND=1/XOR=0 transparente, AND=0
 * preto/branco pelo XOR, AND=1/XOR=1 (inverter) vira branco opaco (o modo
 * de inversao nao existe em ARGB; declarado). 0 = recusado. */
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

#ifdef __cplusplus
}
#endif

#endif /* BC250_DCN_EXT_CONTRACT_H */
