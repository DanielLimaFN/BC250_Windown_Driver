# Inventário bruto de endereços encontrados nos fontes antigos

> Gerado mecanicamente por `tools/generate_register_ledger.py`. Este arquivo é um índice arqueológico, não uma fonte da verdade. Definições podem ser máscaras, IDs, offsets relativos, endereços de outra geração, código morto ou valores já revogados. Consulte `03_CATALOGO_DE_REGISTRADORES.md` antes de usar qualquer item.

Arquivos-fonte examinados: **324**. Definições distintas: **951**. Literais passados diretamente a acessos de registro: **73**.

## Definições com aparência de registro/endereço

| Símbolo | Expressão histórica | Proveniência |
|---|---|---|
| `AMDBC250_IH_CLIENTID_DCE` | `0x08` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:249` |
| `AMDBC250_IH_CLIENTID_GFX` | `0x0A` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:245` |
| `AMDBC250_IH_CLIENTID_SDMA0` | `0x12` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:246` |
| `AMDBC250_IH_CLIENTID_SDMA1` | `0x13` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:247` |
| `AMDBC250_IH_CLIENTID_VMC` | `0x11` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:248` |
| `AMDBC250_INIT_FLAG_NBIO_MAP` | `0x00000001` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_ioctl.h:477`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/dcn-active-scanout.c:17`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/dcn-live-counter.c:19`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/dcn-live-scan.c:21`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/dcn-targeted-probe.c:36`<br>+4 ocorrência(s) |
| `AMDBC250_MP1_BASE` | `0x16000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_kmd.h:53` |
| `AMDBC250_REG_ASIC_REVISION` | `0x00000E0C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:96` |
| `AMDBC250_REG_ASIC_REVISION` | `0x00000E14` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:193` |
| `AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG` | `(AMDBC250_GC_BASE + 0x000089BC)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:511` |
| `AMDBC250_REG_CC_GC_SHADER_RATE_CONFIG` | `(AMDBC250_GC_BASE + 0x00002010)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:512` |
| `AMDBC250_REG_CHIP_FAMILY` | `0x00000E00` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:93` |
| `AMDBC250_REG_CHIP_FAMILY` | `0x00000E10` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:192` |
| `AMDBC250_REG_CHIP_REVISION` | `0x00000E04` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:94` |
| `AMDBC250_REG_COMPUTE_DIM_X` | `(AMDBC250_GC_BASE + 0x00006E84)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:332` |
| `AMDBC250_REG_COMPUTE_DIM_Y` | `(AMDBC250_GC_BASE + 0x00006E88)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:333` |
| `AMDBC250_REG_COMPUTE_DIM_Z` | `(AMDBC250_GC_BASE + 0x00006E8C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:334` |
| `AMDBC250_REG_COMPUTE_DISPATCH_INITIATOR` | `(AMDBC250_GC_BASE + 0x00006E80)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:331` |
| `AMDBC250_REG_COMPUTE_NUM_THREAD_X` | `(AMDBC250_GC_BASE + 0x00006E9C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:338` |
| `AMDBC250_REG_COMPUTE_NUM_THREAD_Y` | `(AMDBC250_GC_BASE + 0x00006EA0)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:339` |
| `AMDBC250_REG_COMPUTE_NUM_THREAD_Z` | `(AMDBC250_GC_BASE + 0x00006EA4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:340` |
| `AMDBC250_REG_COMPUTE_PGM_HI` | `(AMDBC250_GC_BASE + 0x00006EB4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:342` |
| `AMDBC250_REG_COMPUTE_PGM_LO` | `(AMDBC250_GC_BASE + 0x00006EB0)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:341` |
| `AMDBC250_REG_COMPUTE_START_X` | `(AMDBC250_GC_BASE + 0x00006E90)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:335` |
| `AMDBC250_REG_COMPUTE_START_Y` | `(AMDBC250_GC_BASE + 0x00006E94)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:336` |
| `AMDBC250_REG_COMPUTE_START_Z` | `(AMDBC250_GC_BASE + 0x00006E98)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:337` |
| `AMDBC250_REG_COMPUTE_STATIC_THREAD_MGMT_SE0` | `(AMDBC250_GC_BASE + 0x00006ED8)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:348` |
| `AMDBC250_REG_CP_CE_IC_BASE_CNTL` | `(AMDBC250_GC_BASE + 0x00016128)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:259` |
| `AMDBC250_REG_CP_CE_IC_BASE_HI` | `(AMDBC250_GC_BASE + 0x00016124)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:261` |
| `AMDBC250_REG_CP_CE_IC_BASE_LO` | `(AMDBC250_GC_BASE + 0x00016120)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:260` |
| `AMDBC250_REG_CP_COMPUTE_RING0_BASE_LO` | `(AMDBC250_GC_BASE + 0x0000C900)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:308` |
| `AMDBC250_REG_CP_COMPUTE_RING0_CNTL` | `(AMDBC250_GC_BASE + 0x0000C908)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:309` |
| `AMDBC250_REG_CP_COMPUTE_RING0_RPTR` | `(AMDBC250_GC_BASE + 0x0000C90C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:310` |
| `AMDBC250_REG_CP_COMPUTE_RING0_WPTR` | `(AMDBC250_GC_BASE + 0x0000C918)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:311` |
| `AMDBC250_REG_CP_GFX_RING0_BASE_HI` | `(AMDBC250_GC_BASE + 0x0000C804)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:286` |
| `AMDBC250_REG_CP_GFX_RING0_BASE_LO` | `(AMDBC250_GC_BASE + 0x0000C800)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:285` |
| `AMDBC250_REG_CP_GFX_RING0_CNTL` | `(AMDBC250_GC_BASE + 0x0000C808)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:287` |
| `AMDBC250_REG_CP_GFX_RING0_DOORBELL` | `(AMDBC250_GC_BASE + 0x0000C820)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:293` |
| `AMDBC250_REG_CP_GFX_RING0_RPTR` | `(AMDBC250_GC_BASE + 0x0000C80C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:288` |
| `AMDBC250_REG_CP_GFX_RING0_RPTR_ADDR_HI` | `(AMDBC250_GC_BASE + 0x0000C814)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:290` |
| `AMDBC250_REG_CP_GFX_RING0_RPTR_ADDR_LO` | `(AMDBC250_GC_BASE + 0x0000C810)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:289` |
| `AMDBC250_REG_CP_GFX_RING0_WPTR` | `(AMDBC250_GC_BASE + 0x0000C818)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:291` |
| `AMDBC250_REG_CP_GFX_RING0_WPTR_POLL` | `(AMDBC250_GC_BASE + 0x0000C81C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:292` |
| `AMDBC250_REG_CP_HQD_ACTIVE` | `(AMDBC250_GC_BASE + 0x00007EAC)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:365` |
| `AMDBC250_REG_CP_HQD_ACTIVE` | `0x000080D8` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:106` |
| `AMDBC250_REG_CP_HQD_DEQUEUE_REQUEST` | `(AMDBC250_GC_BASE + 0x00007F5C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:386` |
| `AMDBC250_REG_CP_HQD_EOP_BASE_ADDR` | `(AMDBC250_GC_BASE + 0x00007E8C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:387` |
| `AMDBC250_REG_CP_HQD_EOP_BASE_ADDR_HI` | `(AMDBC250_GC_BASE + 0x00007E94)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:388` |
| `AMDBC250_REG_CP_HQD_EOP_CONTROL` | `(AMDBC250_GC_BASE + 0x00007E98)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:389` |
| `AMDBC250_REG_CP_HQD_EOP_RPTR` | `(AMDBC250_GC_BASE + 0x00007E9C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:390` |
| `AMDBC250_REG_CP_HQD_EOP_WPTR` | `(AMDBC250_GC_BASE + 0x00007EA0)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:391` |
| `AMDBC250_REG_CP_HQD_PERSISTENT_STATE` | `(AMDBC250_GC_BASE + 0x00007EB4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:367` |
| `AMDBC250_REG_CP_HQD_PIPE_PRIORITY` | `(AMDBC250_GC_BASE + 0x00007EB8)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:368` |
| `AMDBC250_REG_CP_HQD_PQ_BASE_HI` | `(AMDBC250_GC_BASE + 0x00007EC8)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:372` |
| `AMDBC250_REG_CP_HQD_PQ_BASE_LO` | `(AMDBC250_GC_BASE + 0x00007EC4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:371` |
| `AMDBC250_REG_CP_HQD_PQ_CONTROL` | `(AMDBC250_GC_BASE + 0x00007EE8)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:385` |
| `AMDBC250_REG_CP_HQD_PQ_DOORBELL_CONTROL` | `(AMDBC250_GC_BASE + 0x00007EE0)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:379` |
| `AMDBC250_REG_CP_HQD_PQ_RPTR` | `(AMDBC250_GC_BASE + 0x00007ECC)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:373` |
| `AMDBC250_REG_CP_HQD_PQ_RPTR_REPORT_ADDR` | `(AMDBC250_GC_BASE + 0x00007EDC)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:374` |
| `AMDBC250_REG_CP_HQD_PQ_RPTR_REPORT_ADDR_HI` | `(AMDBC250_GC_BASE + 0x00007EE0)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:375` |
| `AMDBC250_REG_CP_HQD_PQ_WPTR_HI` | `(AMDBC250_GC_BASE + 0x00007F80)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:393` |
| `AMDBC250_REG_CP_HQD_PQ_WPTR_LO` | `(AMDBC250_GC_BASE + 0x00007F7C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:392` |
| `AMDBC250_REG_CP_HQD_PQ_WPTR_POLL_ADDR` | `(AMDBC250_GC_BASE + 0x00007ED8)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:376` |
| `AMDBC250_REG_CP_HQD_PQ_WPTR_POLL_ADDR_HI` | `(AMDBC250_GC_BASE + 0x00007EDC)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:377` |
| `AMDBC250_REG_CP_HQD_PQ_WPTR_POLL_CNTL` | `(AMDBC250_GC_BASE + 0x00007ED8)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:378` |
| `AMDBC250_REG_CP_HQD_QUANTUM` | `(AMDBC250_GC_BASE + 0x00007EC0)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:370` |
| `AMDBC250_REG_CP_HQD_QUEUE_PRIORITY` | `(AMDBC250_GC_BASE + 0x00007EBC)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:369` |
| `AMDBC250_REG_CP_HQD_VMID` | `(AMDBC250_GC_BASE + 0x00007EB0)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:366` |
| `AMDBC250_REG_CP_HQD_VMID` | `0x000080DC` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:107` |
| `AMDBC250_REG_CP_HYP_CE_UCODE_ADDR` | `(AMDBC250_GC_BASE + 0x00016060)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:250` |
| `AMDBC250_REG_CP_HYP_CE_UCODE_DATA` | `(AMDBC250_GC_BASE + 0x00016064)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:251` |
| `AMDBC250_REG_CP_HYP_ME_UCODE_ADDR` | `(AMDBC250_GC_BASE + 0x00016058)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:248` |
| `AMDBC250_REG_CP_HYP_ME_UCODE_DATA` | `(AMDBC250_GC_BASE + 0x0001605C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:249` |
| `AMDBC250_REG_CP_HYP_PFP_UCODE_ADDR` | `(AMDBC250_GC_BASE + 0x00016050)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:246` |
| `AMDBC250_REG_CP_HYP_PFP_UCODE_DATA` | `(AMDBC250_GC_BASE + 0x00016054)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:247` |
| `AMDBC250_REG_CP_KIQ_ACTIVE` | `(AMDBC250_GC_BASE + 0x0000CE20)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:535` |
| `AMDBC250_REG_CP_KIQ_BASE_HI` | `(AMDBC250_GC_BASE + 0x0000CE04)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:528` |
| `AMDBC250_REG_CP_KIQ_BASE_LO` | `(AMDBC250_GC_BASE + 0x0000CE00)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:527` |
| `AMDBC250_REG_CP_KIQ_CNTL` | `(AMDBC250_GC_BASE + 0x0000CE08)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:529` |
| `AMDBC250_REG_CP_KIQ_DOORBELL` | `(AMDBC250_GC_BASE + 0x0000CE14)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:532` |
| `AMDBC250_REG_CP_KIQ_PQ_CTL` | `(AMDBC250_GC_BASE + 0x0000CE10)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:531` |
| `AMDBC250_REG_CP_KIQ_RPTR` | `(AMDBC250_GC_BASE + 0x0000CE0C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:530` |
| `AMDBC250_REG_CP_KIQ_VMID` | `(AMDBC250_GC_BASE + 0x0000CE1C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:534` |
| `AMDBC250_REG_CP_KIQ_WPTR` | `(AMDBC250_GC_BASE + 0x0000CE18)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:533` |
| `AMDBC250_REG_CP_MEC_CNTL` | `0x000080E0` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:105` |
| `AMDBC250_REG_CP_MEC_CNTL` | `0x0000C0E0` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:221` |
| `AMDBC250_REG_CP_MEC_CNTL_GC` | `(AMDBC250_GC_BASE + 0x000038B4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:481` |
| `AMDBC250_REG_CP_MEC_IC_BASE_CNTL` | `(AMDBC250_GC_BASE + 0x00016138)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:264` |
| `AMDBC250_REG_CP_MEC_IC_BASE_HI` | `(AMDBC250_GC_BASE + 0x00016134)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:263` |
| `AMDBC250_REG_CP_MEC_IC_BASE_LO` | `(AMDBC250_GC_BASE + 0x00016130)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:262` |
| `AMDBC250_REG_CP_MEC_STATUS` | `0x0000C0E4` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:222` |
| `AMDBC250_REG_CP_ME_CNTL` | `(AMDBC250_GC_BASE + 0x00003814)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:215` |
| `AMDBC250_REG_CP_ME_CNTL` | `0x00008080` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:99` |
| `AMDBC250_REG_CP_ME_IC_BASE_CNTL` | `(AMDBC250_GC_BASE + 0x00016118)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:256` |
| `AMDBC250_REG_CP_ME_IC_BASE_HI` | `(AMDBC250_GC_BASE + 0x00016114)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:258` |
| `AMDBC250_REG_CP_ME_IC_BASE_LO` | `(AMDBC250_GC_BASE + 0x00016110)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:257` |
| `AMDBC250_REG_CP_ME_RAM_DATA` | `0x000080B4` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:104` |
| `AMDBC250_REG_CP_ME_RAM_RADDR` | `0x000080AC` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:102` |
| `AMDBC250_REG_CP_ME_RAM_WADDR` | `0x000080B0` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:103` |
| `AMDBC250_REG_CP_ME_STATUS` | `0x0000C064` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:216` |
| `AMDBC250_REG_CP_ME_UCODE_ADDR` | `0x0000C0B0` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:219` |
| `AMDBC250_REG_CP_ME_UCODE_DATA` | `0x0000C0B4` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:220` |
| `AMDBC250_REG_CP_MQD_BASE_ADDR` | `(AMDBC250_GC_BASE + 0x00007EA4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:363` |
| `AMDBC250_REG_CP_MQD_BASE_ADDR_HI` | `(AMDBC250_GC_BASE + 0x00007EA8)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:364` |
| `AMDBC250_REG_CP_PFP_IC_BASE_CNTL` | `(AMDBC250_GC_BASE + 0x00016108)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:253` |
| `AMDBC250_REG_CP_PFP_IC_BASE_HI` | `(AMDBC250_GC_BASE + 0x00016104)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:255` |
| `AMDBC250_REG_CP_PFP_IC_BASE_LO` | `(AMDBC250_GC_BASE + 0x00016100)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:254` |
| `AMDBC250_REG_CP_PFP_UCODE_ADDR` | `0x000080A0` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:100` |
| `AMDBC250_REG_CP_PFP_UCODE_ADDR` | `0x0000C0A0` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:217` |
| `AMDBC250_REG_CP_PFP_UCODE_DATA` | `0x000080A4` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:101` |
| `AMDBC250_REG_CP_PFP_UCODE_DATA` | `0x0000C0A4` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:218` |
| `AMDBC250_REG_CP_RB0_BASE` | `(AMDBC250_GC_BASE + 0x00007780)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:296` |
| `AMDBC250_REG_CP_RB0_BASE` | `0x0000C100` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:110` |
| `AMDBC250_REG_CP_RB0_BASE_HI` | `(AMDBC250_GC_BASE + 0x00007944)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:297` |
| `AMDBC250_REG_CP_RB0_BASE_HI` | `0x0000C104` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:111` |
| `AMDBC250_REG_CP_RB0_CNTL` | `(AMDBC250_GC_BASE + 0x00007784)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:298` |
| `AMDBC250_REG_CP_RB0_CNTL` | `0x0000C108` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:112` |
| `AMDBC250_REG_CP_RB0_RPTR` | `(AMDBC250_GC_BASE + 0x00003D80)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:299` |
| `AMDBC250_REG_CP_RB0_RPTR` | `0x0000C10C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:113` |
| `AMDBC250_REG_CP_RB0_RPTR_ADDR` | `(AMDBC250_GC_BASE + 0x0000778C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:300` |
| `AMDBC250_REG_CP_RB0_RPTR_ADDR_HI` | `(AMDBC250_GC_BASE + 0x00007790)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:301` |
| `AMDBC250_REG_CP_RB0_WPTR` | `(AMDBC250_GC_BASE + 0x000077D0)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:302` |
| `AMDBC250_REG_CP_RB0_WPTR` | `0x0000C114` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:114` |
| `AMDBC250_REG_CP_RB0_WPTR_HI` | `(AMDBC250_GC_BASE + 0x000077D4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:303` |
| `AMDBC250_REG_CP_RB0_WPTR_HI` | `0x0000C118` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:115` |
| `AMDBC250_REG_CP_RB_DOORBELL_CTL` | `0x0000C148` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:117` |
| `AMDBC250_REG_CP_RB_VMID` | `0x0000C140` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:116` |
| `AMDBC250_REG_CP_RB_WPTR_DELAY` | `(AMDBC250_GC_BASE + 0x00003D84)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:304` |
| `AMDBC250_REG_CP_RB_WPTR_POLL_CNTL` | `(AMDBC250_GC_BASE + 0x00003D88)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:305` |
| `AMDBC250_REG_CRTC0_CONTROL` | `0x00006080` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:149` |
| `AMDBC250_REG_CRTC0_H_BLANK` | `0x0000608C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:152` |
| `AMDBC250_REG_CRTC0_H_SYNC` | `0x00006094` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:154` |
| `AMDBC250_REG_CRTC0_H_TOTAL` | `0x00006084` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:150` |
| `AMDBC250_REG_CRTC0_STATUS` | `0x0000609C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:156` |
| `AMDBC250_REG_CRTC0_V_BLANK` | `0x00006090` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:153` |
| `AMDBC250_REG_CRTC0_V_SYNC` | `0x00006098` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:155` |
| `AMDBC250_REG_CRTC0_V_TOTAL` | `0x00006088` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:151` |
| `AMDBC250_REG_DMCUB_INBOX0_RPTR` | `(AMDBC250_DCN_BASE + 0x327D * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:679` |
| `AMDBC250_REG_DMCUB_INBOX0_SIZE` | `(AMDBC250_DCN_BASE + 0x327B * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:677` |
| `AMDBC250_REG_DMCUB_INBOX0_WPTR` | `(AMDBC250_DCN_BASE + 0x327C * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:678` |
| `AMDBC250_REG_DMCUB_REGION0_OFFSET` | `(AMDBC250_DCN_BASE + 0x3238 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:673` |
| `AMDBC250_REG_DMCUB_REGION0_OFFSET_HIGH` | `(AMDBC250_DCN_BASE + 0x3239 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:674` |
| `AMDBC250_REG_DMCUB_REGION1_OFFSET` | `(AMDBC250_DCN_BASE + 0x323A * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:675` |
| `AMDBC250_REG_DMCUB_REGION1_OFFSET_HIGH` | `(AMDBC250_DCN_BASE + 0x323B * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:676` |
| `AMDBC250_REG_DMCUB_SCRATCH0` | `(AMDBC250_DCN_BASE + 0x328D * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:680` |
| `AMDBC250_REG_DMCUB_SCRATCH1` | `(AMDBC250_DCN_BASE + 0x328E * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:681` |
| `AMDBC250_REG_DP_AUX0_AUX_CNTL` | `0x00004800` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:319` |
| `AMDBC250_REG_DP_AUX0_AUX_LS_DATA` | `0x00004808` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:321` |
| `AMDBC250_REG_DP_AUX0_AUX_SW_DATA` | `0x00004804` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:320` |
| `AMDBC250_REG_DP_AUX0_AUX_SW_STATUS` | `0x0000480C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:322` |
| `AMDBC250_REG_GB_ADDR_CONFIG` | `0x0000263C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:170` |
| `AMDBC250_REG_GB_ADDR_CONFIG` | `0x000061D8` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:713` |
| `AMDBC250_REG_GB_ADDR_CONFIG_READ` | `0x000061DC` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:714` |
| `AMDBC250_REG_GB_ADDR_CONFIG_READ` | `0x00009800` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:171` |
| `AMDBC250_REG_GCVM_CONTEXT0_CNTL` | `0x00000B460` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:582` |
| `AMDBC250_REG_GCVM_CONTEXT0_PT_BASE_HI` | `0x000006C90` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:584` |
| `AMDBC250_REG_GCVM_CONTEXT0_PT_BASE_LO` | `0x000006C8C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:583` |
| `AMDBC250_REG_GCVM_CTX0_CFG_0` | `0x00000B4C0` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:593` |
| `AMDBC250_REG_GCVM_CTX0_CFG_5` | `0x00000B4D4` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:594` |
| `AMDBC250_REG_GCVM_CTX0_TLB_ENTRY_0` | `0x00000B408` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:589` |
| `AMDBC250_REG_GCVM_CTX0_TLB_ENTRY_19` | `0x00000B454` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:590` |
| `AMDBC250_REG_GCVM_INVALIDATE_ENG0_ACK` | `0x000006C10` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:601` |
| `AMDBC250_REG_GCVM_INVALIDATE_ENG0_REQ` | `0x000006C0C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:600` |
| `AMDBC250_REG_GCVM_L2_CNTL` | `0x00000B360` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:577` |
| `AMDBC250_REG_GCVM_L2_CNTL2` | `0x00000B364` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:578` |
| `AMDBC250_REG_GCVM_L2_CNTL3` | `0x00000B368` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:579` |
| `AMDBC250_REG_GCVM_L2_CNTL4` | `0x00000B36C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:580` |
| `AMDBC250_REG_GC_USER_PRIM_CONFIG` | `0x00009B7C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:168` |
| `AMDBC250_REG_GC_USER_RB_BACKEND_DISABLE` | `0x00009B80` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:169` |
| `AMDBC250_REG_GRBM_GFX_INDEX` | `(AMDBC250_GC_BASE + 0x00002270)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:401` |
| `AMDBC250_REG_GRBM_SOFT_RESET` | `(AMDBC250_GC_BASE + 0x00002018)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:493` |
| `AMDBC250_REG_GRBM_STATUS` | `(AMDBC250_GC_BASE + 0x00002000)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:491` |
| `AMDBC250_REG_GRBM_STATUS2` | `(AMDBC250_GC_BASE + 0x0000200C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:492` |
| `AMDBC250_REG_HDP_DEBUG0` | `0x000012B0` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:621` |
| `AMDBC250_REG_HDP_MEM_COHERENCY_FLUSH_CNTL` | `0x000012A0` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:620` |
| `AMDBC250_REG_HDP_NONSURFACE_BASE` | `0x000012C8` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:624` |
| `AMDBC250_REG_HDP_NONSURFACE_INFO` | `0x000012C0` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:622` |
| `AMDBC250_REG_HDP_NONSURFACE_SIZE` | `0x000012C4` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:623` |
| `AMDBC250_REG_HUBP0_DCHUBP_CNTL` | `(AMDBC250_DCN_BASE + 0x05F3 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:645` |
| `AMDBC250_REG_HUBP0_DCSURF_ADDR_CONFIG` | `(AMDBC250_DCN_BASE + 0x05E6 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:641` |
| `AMDBC250_REG_HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION` | `(AMDBC250_DCN_BASE + 0x05EA * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:644` |
| `AMDBC250_REG_HUBP0_DCSURF_PRI_VIEWPORT_START` | `(AMDBC250_DCN_BASE + 0x05E9 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:643` |
| `AMDBC250_REG_HUBP0_DCSURF_SURFACE_CONFIG` | `(AMDBC250_DCN_BASE + 0x05E5 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:640` |
| `AMDBC250_REG_HUBP0_DCSURF_TILING_CONFIG` | `(AMDBC250_DCN_BASE + 0x05E7 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:642` |
| `AMDBC250_REG_HUBPREQ0_DCSURF_FLIP_CONTROL` | `(AMDBC250_DCN_BASE + 0x061B * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:652` |
| `AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS` | `(AMDBC250_DCN_BASE + 0x060A * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:649` |
| `AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH` | `(AMDBC250_DCN_BASE + 0x060B * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:650` |
| `AMDBC250_REG_HUBPREQ0_DCSURF_SURFACE_CONTROL` | `(AMDBC250_DCN_BASE + 0x061A * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:651` |
| `AMDBC250_REG_HUBPREQ0_DCSURF_SURFACE_INUSE` | `(AMDBC250_DCN_BASE + 0x0621 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:653` |
| `AMDBC250_REG_HUBPREQ0_DCSURF_SURFACE_INUSE_HIGH` | `(AMDBC250_DCN_BASE + 0x0622 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:654` |
| `AMDBC250_REG_HUBPREQ0_DCSURF_SURFACE_PITCH` | `(AMDBC250_DCN_BASE + 0x0607 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:648` |
| `AMDBC250_REG_HW_ID` | `0x00000E08` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:190`<br>`referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:95` |
| `AMDBC250_REG_HW_ID2` | `0x00000E0C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:191` |
| `AMDBC250_REG_IH_CNTL` | `0x00003820` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:144` |
| `AMDBC250_REG_IH_CNTL` | `0x00004580` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:548` |
| `AMDBC250_REG_IH_CNTL2` | `0x00003824` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:145` |
| `AMDBC250_REG_IH_CNTL2` | `0x00004584` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:549` |
| `AMDBC250_REG_IH_DOORBELL_RPTR` | `0x00003818` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:143` |
| `AMDBC250_REG_IH_RB_BASE` | `0x00003800` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:138` |
| `AMDBC250_REG_IH_RB_BASE_HI` | `0x00003804` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:139` |
| `AMDBC250_REG_IH_RB_BASE_HI` | `0x00004488` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:543` |
| `AMDBC250_REG_IH_RB_BASE_LO` | `0x00004484` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:542` |
| `AMDBC250_REG_IH_RB_CNTL` | `0x00003808` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:140` |
| `AMDBC250_REG_IH_RB_CNTL` | `0x00004480` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:544` |
| `AMDBC250_REG_IH_RB_RPTR` | `0x00003810` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:141` |
| `AMDBC250_REG_IH_RB_RPTR` | `0x0000448C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:545` |
| `AMDBC250_REG_IH_RB_WPTR` | `0x00003814` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:142` |
| `AMDBC250_REG_IH_RB_WPTR` | `0x00004490` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:546` |
| `AMDBC250_REG_IH_STATUS` | `0x00003830` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:146` |
| `AMDBC250_REG_IH_STATUS` | `0x00004588` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:550` |
| `AMDBC250_REG_IH_WPTR_POLL_ADDR_LO` | `0x00004498` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:547` |
| `AMDBC250_REG_MC_VM_AGP_BASE` | `0x00009524` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:121` |
| `AMDBC250_REG_MC_VM_AGP_BASE` | `0x00009528` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:560` |
| `AMDBC250_REG_MC_VM_AGP_BOT` | `0x0000952C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:123` |
| `AMDBC250_REG_MC_VM_AGP_BOT` | `0x00009530` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:562` |
| `AMDBC250_REG_MC_VM_AGP_CNTL` | `0x00009534` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:563` |
| `AMDBC250_REG_MC_VM_AGP_TOP` | `0x00009528` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:122` |
| `AMDBC250_REG_MC_VM_AGP_TOP` | `0x0000952C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:561` |
| `AMDBC250_REG_MC_VM_FB_LOCATION` | `0x00009520` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:120` |
| `AMDBC250_REG_MC_VM_FB_LOCATION_BASE` | `0x00009520` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:558` |
| `AMDBC250_REG_MC_VM_FB_LOCATION_TOP` | `0x00009524` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:559` |
| `AMDBC250_REG_MC_VM_FB_OFFSET` | `0x00000000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:557` |
| `AMDBC250_REG_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR` | `0x00009548` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:566` |
| `AMDBC250_REG_MC_VM_SYSTEM_APERTURE_HIGH` | `0x00009544` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:125` |
| `AMDBC250_REG_MC_VM_SYSTEM_APERTURE_HIGH_ADDR` | `0x00009544` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:565` |
| `AMDBC250_REG_MC_VM_SYSTEM_APERTURE_LOW` | `0x00009540` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:124` |
| `AMDBC250_REG_MC_VM_SYSTEM_APERTURE_LOW_ADDR` | `0x00009540` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:564` |
| `AMDBC250_REG_MEC_ME1_CNTL` | `0x00007A00` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:488` |
| `AMDBC250_REG_MMHUB_VM_CONTEXT0_CNTL` | `0x00001A00` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:615` |
| `AMDBC250_REG_MMHUB_VM_PT_BASE_LO` | `0x00001A04` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:616` |
| `AMDBC250_REG_MP1_SMN_C2PMSG_33` | `0x00016984` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:692` |
| `AMDBC250_REG_MP1_SMN_C2PMSG_66` | `0x00016104` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:161` |
| `AMDBC250_REG_MP1_SMN_C2PMSG_66` | `0x00016A08` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:693` |
| `AMDBC250_REG_MP1_SMN_C2PMSG_82` | `0x00016148` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:162` |
| `AMDBC250_REG_MP1_SMN_C2PMSG_82` | `0x00016A48` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:694` |
| `AMDBC250_REG_MP1_SMN_C2PMSG_83` | `0x00016A4C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:695` |
| `AMDBC250_REG_MP1_SMN_C2PMSG_90` | `0x00016168` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:163` |
| `AMDBC250_REG_MP1_SMN_C2PMSG_90` | `0x00016A68` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:696` |
| `AMDBC250_REG_MP1_SMN_P2CMSG_1` | `0x00016204` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:164` |
| `AMDBC250_REG_MP1_SMN_P2CMSG_33` | `0x00016284` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:165` |
| `AMDBC250_REG_OTG0_OTG_CONTROL` | `(AMDBC250_DCN_BASE + 0x1B41 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:665` |
| `AMDBC250_REG_OTG0_OTG_H_BLANK_START_END` | `(AMDBC250_DCN_BASE + 0x1B2B * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:660` |
| `AMDBC250_REG_OTG0_OTG_H_SYNC_A` | `(AMDBC250_DCN_BASE + 0x1B2C * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:661` |
| `AMDBC250_REG_OTG0_OTG_H_TOTAL` | `(AMDBC250_DCN_BASE + 0x1B2A * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:659` |
| `AMDBC250_REG_OTG0_OTG_MASTER_UPDATE_LOCK` | `(AMDBC250_DCN_BASE + 0x1B8B * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:669` |
| `AMDBC250_REG_OTG0_OTG_STATUS` | `(AMDBC250_DCN_BASE + 0x1B49 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:666` |
| `AMDBC250_REG_OTG0_OTG_STATUS_FRAME_COUNT` | `(AMDBC250_DCN_BASE + 0x1B4C * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:668` |
| `AMDBC250_REG_OTG0_OTG_STATUS_POSITION` | `(AMDBC250_DCN_BASE + 0x1B4A * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:667` |
| `AMDBC250_REG_OTG0_OTG_V_BLANK_START_END` | `(AMDBC250_DCN_BASE + 0x1B36 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:663` |
| `AMDBC250_REG_OTG0_OTG_V_SYNC_A` | `(AMDBC250_DCN_BASE + 0x1B37 * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:664` |
| `AMDBC250_REG_OTG0_OTG_V_TOTAL` | `(AMDBC250_DCN_BASE + 0x1B2F * 4)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:662` |
| `AMDBC250_REG_RLC_CP_SCHEDULERS` | `(0x0000ECA8)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:469` |
| `AMDBC250_REG_RLC_CP_SCHEDULERS_LEGACY` | `(AMDBC250_GC_BASE_SEG1 + 0x00004CA1)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:470` |
| `AMDBC250_REG_RLC_PG_ALWAYS_ON_WGP_MASK` | `(AMDBC250_GC_BASE + 0x00002B04)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:516` |
| `AMDBC250_REG_RT_ACCEL_CNTL` | `0x0000D000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:851` |
| `AMDBC250_REG_RT_ACCEL_STATUS` | `0x0000D004` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:852` |
| `AMDBC250_REG_RT_BVH_ADDR_HI` | `0x0000D00C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:854` |
| `AMDBC250_REG_RT_BVH_ADDR_LO` | `0x0000D008` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:853` |
| `AMDBC250_REG_RT_RAY_ADDR_HI` | `0x0000D014` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:856` |
| `AMDBC250_REG_RT_RAY_ADDR_LO` | `0x0000D010` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:855` |
| `AMDBC250_REG_SCRATCH_REG0` | `(AMDBC250_GC_BASE + 0x00002074)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:196` |
| `AMDBC250_REG_SCRATCH_REG0` | `0x00008500` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:87` |
| `AMDBC250_REG_SCRATCH_REG1` | `(AMDBC250_GC_BASE + 0x00002078)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:197` |
| `AMDBC250_REG_SCRATCH_REG1` | `0x00008504` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:88` |
| `AMDBC250_REG_SCRATCH_REG2` | `(AMDBC250_GC_BASE + 0x0000207C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:198` |
| `AMDBC250_REG_SCRATCH_REG2` | `0x00008508` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:89` |
| `AMDBC250_REG_SCRATCH_REG3` | `(AMDBC250_GC_BASE + 0x00002080)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:199` |
| `AMDBC250_REG_SCRATCH_REG3` | `0x0000850C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:90` |
| `AMDBC250_REG_SCRATCH_REG4` | `(AMDBC250_GC_BASE + 0x00002084)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:200` |
| `AMDBC250_REG_SCRATCH_REG5` | `(AMDBC250_GC_BASE + 0x00002088)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:201` |
| `AMDBC250_REG_SCRATCH_REG6` | `(AMDBC250_GC_BASE + 0x0000208C)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:202` |
| `AMDBC250_REG_SCRATCH_REG7` | `(AMDBC250_GC_BASE + 0x00002090)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:203` |
| `AMDBC250_REG_SDMA0_CNTL` | `0x0000122C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:295` |
| `AMDBC250_REG_SDMA0_CNTL` | `0x0000E018` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:832` |
| `AMDBC250_REG_SDMA0_F32_CNTL` | `0x00001214` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:294` |
| `AMDBC250_REG_SDMA0_GFX_DOORBELL` | `0x000012A0` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:293` |
| `AMDBC250_REG_SDMA0_GFX_RB_BASE` | `0x00001260` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:288` |
| `AMDBC250_REG_SDMA0_GFX_RB_BASE_HI` | `0x00001264` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:289` |
| `AMDBC250_REG_SDMA0_GFX_RB_BASE_HI` | `0x0000E004` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:827` |
| `AMDBC250_REG_SDMA0_GFX_RB_BASE_LO` | `0x0000E000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:826` |
| `AMDBC250_REG_SDMA0_GFX_RB_CNTL` | `0x00001268` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:290` |
| `AMDBC250_REG_SDMA0_GFX_RB_CNTL` | `0x0000E008` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:828` |
| `AMDBC250_REG_SDMA0_GFX_RB_RPTR` | `0x00001270` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:291` |
| `AMDBC250_REG_SDMA0_GFX_RB_RPTR` | `0x0000E00C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:829` |
| `AMDBC250_REG_SDMA0_GFX_RB_WPTR` | `0x00001278` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:292` |
| `AMDBC250_REG_SDMA0_GFX_RB_WPTR` | `0x0000E010` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:830` |
| `AMDBC250_REG_SDMA0_GFX_RB_WPTR_POLL` | `0x0000E014` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:831` |
| `AMDBC250_REG_SDMA0_STATUS_REG` | `0x00001234` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:296` |
| `AMDBC250_REG_SMC_IND_DATA` | `0x00000204` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:160` |
| `AMDBC250_REG_SMC_IND_INDEX` | `0x00000200` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:159` |
| `AMDBC250_REG_SPI_PG_ENABLE_STATIC_WGP_MASK` | `(AMDBC250_GC_BASE + 0x000049DC)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:515` |
| `AMDBC250_REG_THM_CURRENT_TEMP` | `0x00008008` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:706` |
| `AMDBC250_REG_THM_THERMAL_CTRL` | `0x00008000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:705` |
| `AMDBC250_REG_THM_THERMAL_INT_ENA` | `0x00008050` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:707` |
| `AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32` | `0x00009B04` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:129` |
| `AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32` | `0x00009B00` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:128` |
| `AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32` | `0x00009B14` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:133` |
| `AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32` | `0x00009B10` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:132` |
| `AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32` | `0x00009B0C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:131` |
| `AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32` | `0x00009B08` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:130` |
| `AMDBC250_REG_VM_INVALIDATE_ENG0_ACK` | `0x00009B80` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:135` |
| `AMDBC250_REG_VM_INVALIDATE_ENG0_REQ` | `0x00009B40` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:134` |
| `AMDBC250_RLC_CP_SCHEDULERS_ENABLE` | `0x80` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:471` |
| `AMDBC250_THM_BASE` | `0x08000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_kmd.h:54` |
| `A_CC_ARRAY_CONFIG` | `0x09C1Cu` | `referencias/BC250-V2/wddm_hw.c:1269`<br>`referencias/wddm/wddm_hw.c:1424` |
| `A_CC_GC_SHADER_ARRAY_CONFIG` | `SOC15_GC0(0x100Fu)` | `referencias/BC250-V3/wddm_hw.c:128` |
| `A_COMPUTE_DIM_X` | `0x080E4u` | `referencias/BC250-V2/wddm_hw.c:1250`<br>`referencias/wddm/wddm_hw.c:1405` |
| `A_COMPUTE_DIM_X` | `SOC15_GC0(0x1BA1u)` | `referencias/BC250-V3/wddm_hw.c:1497` |
| `A_COMPUTE_DIM_Y` | `0x080E8u` | `referencias/BC250-V2/wddm_hw.c:1251`<br>`referencias/wddm/wddm_hw.c:1406` |
| `A_COMPUTE_DIM_Y` | `SOC15_GC0(0x1BA2u)` | `referencias/BC250-V3/wddm_hw.c:1498` |
| `A_COMPUTE_DIM_Z` | `0x080ECu` | `referencias/BC250-V2/wddm_hw.c:1252`<br>`referencias/wddm/wddm_hw.c:1407` |
| `A_COMPUTE_DIM_Z` | `SOC15_GC0(0x1BA3u)` | `referencias/BC250-V3/wddm_hw.c:1499` |
| `A_COMPUTE_DISPATCH_INITIATOR` | `0x080E0u` | `referencias/BC250-V2/wddm_hw.c:1249`<br>`referencias/wddm/wddm_hw.c:1404` |
| `A_COMPUTE_DISPATCH_INITIATOR` | `SOC15_GC0(0x1BA0u)` | `referencias/BC250-V3/wddm_hw.c:1496` |
| `A_COMPUTE_NUM_THREAD_X` | `0x080FCu` | `referencias/BC250-V2/wddm_hw.c:1256`<br>`referencias/wddm/wddm_hw.c:1411` |
| `A_COMPUTE_NUM_THREAD_X` | `SOC15_GC0(0x1BA7u)` | `referencias/BC250-V3/wddm_hw.c:1503` |
| `A_COMPUTE_NUM_THREAD_Y` | `0x08100u` | `referencias/BC250-V2/wddm_hw.c:1257`<br>`referencias/wddm/wddm_hw.c:1412` |
| `A_COMPUTE_NUM_THREAD_Y` | `SOC15_GC0(0x1BA8u)` | `referencias/BC250-V3/wddm_hw.c:1504` |
| `A_COMPUTE_NUM_THREAD_Z` | `0x08104u` | `referencias/BC250-V2/wddm_hw.c:1258`<br>`referencias/wddm/wddm_hw.c:1413` |
| `A_COMPUTE_NUM_THREAD_Z` | `SOC15_GC0(0x1BA9u)` | `referencias/BC250-V3/wddm_hw.c:1505` |
| `A_COMPUTE_PGM_HI` | `0x08114u` | `referencias/BC250-V2/wddm_hw.c:1260`<br>`referencias/wddm/wddm_hw.c:1415` |
| `A_COMPUTE_PGM_HI` | `SOC15_GC0(0x1BADu)` | `referencias/BC250-V3/wddm_hw.c:1507` |
| `A_COMPUTE_PGM_LO` | `0x08110u` | `referencias/BC250-V2/wddm_hw.c:1259`<br>`referencias/wddm/wddm_hw.c:1414` |
| `A_COMPUTE_PGM_LO` | `SOC15_GC0(0x1BACu)` | `referencias/BC250-V3/wddm_hw.c:1506` |
| `A_COMPUTE_PGM_RSRC1` | `0x08128u` | `referencias/BC250-V2/wddm_hw.c:1261`<br>`referencias/wddm/wddm_hw.c:1416` |
| `A_COMPUTE_PGM_RSRC1` | `SOC15_GC0(0x1BB2u)` | `referencias/BC250-V3/wddm_hw.c:1508` |
| `A_COMPUTE_PGM_RSRC2` | `0x0812Cu` | `referencias/BC250-V2/wddm_hw.c:1262`<br>`referencias/wddm/wddm_hw.c:1417` |
| `A_COMPUTE_PGM_RSRC2` | `SOC15_GC0(0x1BB3u)` | `referencias/BC250-V3/wddm_hw.c:1509` |
| `A_COMPUTE_RESOURCE_LIMITS` | `SOC15_GC0(0x1BB5u)` | `referencias/BC250-V3/wddm_hw.c:1510` |
| `A_COMPUTE_START_X` | `0x080F0u` | `referencias/BC250-V2/wddm_hw.c:1253`<br>`referencias/wddm/wddm_hw.c:1408` |
| `A_COMPUTE_START_X` | `SOC15_GC0(0x1BA4u)` | `referencias/BC250-V3/wddm_hw.c:1500` |
| `A_COMPUTE_START_Y` | `0x080F4u` | `referencias/BC250-V2/wddm_hw.c:1254`<br>`referencias/wddm/wddm_hw.c:1409` |
| `A_COMPUTE_START_Y` | `SOC15_GC0(0x1BA5u)` | `referencias/BC250-V3/wddm_hw.c:1501` |
| `A_COMPUTE_START_Z` | `0x080F8u` | `referencias/BC250-V2/wddm_hw.c:1255`<br>`referencias/wddm/wddm_hw.c:1410` |
| `A_COMPUTE_START_Z` | `SOC15_GC0(0x1BA6u)` | `referencias/BC250-V3/wddm_hw.c:1502` |
| `A_COMPUTE_STATIC_THREAD_MGMT_SE0` | `0x08138u` | `referencias/BC250-V2/wddm_hw.c:1263`<br>`referencias/wddm/wddm_hw.c:1418` |
| `A_COMPUTE_STATIC_THREAD_MGMT_SE0` | `SOC15_GC0(0x1BB6u)` | `referencias/BC250-V3/wddm_hw.c:1511` |
| `A_COMPUTE_STATIC_THREAD_MGMT_SE1` | `0x0813Cu` | `referencias/BC250-V2/wddm_hw.c:1264`<br>`referencias/wddm/wddm_hw.c:1419` |
| `A_COMPUTE_STATIC_THREAD_MGMT_SE1` | `SOC15_GC0(0x1BB7u)` | `referencias/BC250-V3/wddm_hw.c:1512` |
| `A_COMPUTE_TMPRING_SIZE` | `0x08140u` | `referencias/BC250-V2/wddm_hw.c:1265`<br>`referencias/wddm/wddm_hw.c:1420` |
| `A_COMPUTE_TMPRING_SIZE` | `SOC15_GC0(0x1BB8u)` | `referencias/BC250-V3/wddm_hw.c:1513` |
| `A_COMPUTE_USER_DATA_0` | `0x081E0u` | `referencias/BC250-V2/wddm_hw.c:1266`<br>`referencias/wddm/wddm_hw.c:1421` |
| `A_COMPUTE_USER_DATA_0` | `SOC15_GC0(0x1BE0u)` | `referencias/BC250-V3/wddm_hw.c:1514` |
| `A_CP_CE_IC_BASE_CNTL` | `0x17388u` | `referencias/wddm/wddm_hw.c:161` |
| `A_CP_CE_IC_BASE_HI` | `0x17384u` | `referencias/wddm/wddm_hw.c:160` |
| `A_CP_CE_IC_BASE_LO` | `0x17380u` | `referencias/wddm/wddm_hw.c:159` |
| `A_CP_DEVICE_ID` | `0x0C684u` | `referencias/wddm/wddm_hw.c:197` |
| `A_CP_HYP_CE_UCODE_ADDR` | `0x172C0u` | `referencias/wddm/wddm_hw.c:171` |
| `A_CP_HYP_CE_UCODE_DATA` | `0x172C4u` | `referencias/wddm/wddm_hw.c:172` |
| `A_CP_HYP_ME_UCODE_ADDR` | `0x172B8u` | `referencias/wddm/wddm_hw.c:169` |
| `A_CP_HYP_ME_UCODE_DATA` | `0x172BCu` | `referencias/wddm/wddm_hw.c:170` |
| `A_CP_HYP_PFP_UCODE_ADDR` | `0x172B0u` | `referencias/wddm/wddm_hw.c:167` |
| `A_CP_HYP_PFP_UCODE_DATA` | `0x172B4u` | `referencias/wddm/wddm_hw.c:168` |
| `A_CP_MEC_IC_BASE_CNTL` | `0x17398u` | `referencias/wddm/wddm_hw.c:165` |
| `A_CP_MEC_IC_BASE_HI` | `0x17394u` | `referencias/wddm/wddm_hw.c:164` |
| `A_CP_MEC_IC_BASE_LO` | `0x17390u` | `referencias/wddm/wddm_hw.c:163` |
| `A_CP_ME_CNTL` | `0x086D8u` | `dbg/hw.c:1899`<br>`referencias/BC250-V2/wddm_hw.c:117`<br>`referencias/BC250-V3/wddm_hw.c:76`<br>`referencias/wddm/wddm_hw.c:127` |
| `A_CP_ME_IC_BASE_CNTL` | `0x17378u` | `referencias/wddm/wddm_hw.c:157` |
| `A_CP_ME_IC_BASE_HI` | `0x17374u` | `referencias/wddm/wddm_hw.c:156` |
| `A_CP_ME_IC_BASE_LO` | `0x17370u` | `referencias/wddm/wddm_hw.c:155` |
| `A_CP_PFP_IC_BASE_CNTL` | `0x17368u` | `referencias/wddm/wddm_hw.c:153` |
| `A_CP_PFP_IC_BASE_HI` | `0x17364u` | `referencias/wddm/wddm_hw.c:152` |
| `A_CP_PFP_IC_BASE_LO` | `0x17360u` | `referencias/wddm/wddm_hw.c:151` |
| `A_CP_RB0_BASE` | `0x0C100u` | `dbg/hw.c:1901`<br>`referencias/BC250-V2/wddm_hw.c:120`<br>`referencias/BC250-V3/wddm_hw.c:79` |
| `A_CP_RB0_BASE` | `0x0DA60u` | `referencias/wddm/wddm_hw.c:128` |
| `A_CP_RB0_BASE_HI` | `0x0C2C4u` | `dbg/hw.c:1907`<br>`referencias/BC250-V2/wddm_hw.c:126`<br>`referencias/BC250-V3/wddm_hw.c:85` |
| `A_CP_RB0_BASE_HI` | `0x0DA64u` | `referencias/wddm/wddm_hw.c:129` |
| `A_CP_RB0_CNTL` | `0x0C104u` | `dbg/hw.c:1902`<br>`referencias/BC250-V2/wddm_hw.c:121`<br>`referencias/BC250-V3/wddm_hw.c:80` |
| `A_CP_RB0_CNTL` | `0x0DA68u` | `referencias/wddm/wddm_hw.c:130` |
| `A_CP_RB0_RPTR` | `0x08700u` | `dbg/hw.c:1900`<br>`referencias/BC250-V2/wddm_hw.c:118`<br>`referencias/BC250-V3/wddm.h:634`<br>`referencias/BC250-V3/wddm_hw.c:77` |
| `A_CP_RB0_RPTR` | `0x0DA6Cu` | `referencias/wddm/wddm_hw.c:131` |
| `A_CP_RB0_RPTR_ADDR` | `0x0C10Cu` | `dbg/hw.c:1903`<br>`referencias/BC250-V2/wddm_hw.c:122`<br>`referencias/BC250-V3/wddm_hw.c:81`<br>`referencias/wddm/wddm_hw.c:136` |
| `A_CP_RB0_RPTR_ADDR_HI` | `0x0C110u` | `dbg/hw.c:1904`<br>`referencias/BC250-V2/wddm_hw.c:123`<br>`referencias/BC250-V3/wddm_hw.c:82`<br>`referencias/wddm/wddm_hw.c:137` |
| `A_CP_RB0_WPTR` | `0x0C150u` | `dbg/hw.c:1905`<br>`referencias/BC250-V2/wddm_hw.c:124`<br>`referencias/BC250-V3/wddm_hw.c:83` |
| `A_CP_RB0_WPTR` | `0x0DA78u` | `referencias/wddm/wddm_hw.c:133` |
| `A_CP_RB0_WPTR_HI` | `0x0C154u` | `dbg/hw.c:1906`<br>`referencias/BC250-V2/wddm_hw.c:125`<br>`referencias/BC250-V3/wddm_hw.c:84` |
| `A_CP_RB0_WPTR_HI` | `0x0DA74u` | `referencias/wddm/wddm_hw.c:132` |
| `A_CP_RB_ACTIVE` | `0x0DA7Cu` | `referencias/wddm/wddm_hw.c:134` |
| `A_CP_RB_DOORBELL_CONTROL` | `0x0C3B4u` | `dbg/hw.c:1910`<br>`referencias/BC250-V2/wddm_hw.c:129`<br>`referencias/BC250-V3/wddm_hw.c:88`<br>`referencias/wddm/wddm_hw.c:140` |
| `A_CP_RB_WPTR_POLL_ADDR_HI` | `0x0C3B0u` | `dbg/hw.c:1909`<br>`referencias/BC250-V2/wddm_hw.c:128`<br>`referencias/BC250-V3/wddm_hw.c:87`<br>`referencias/wddm/wddm_hw.c:139` |
| `A_CP_RB_WPTR_POLL_ADDR_LO` | `0x0C3ACu` | `dbg/hw.c:1908`<br>`referencias/BC250-V2/wddm_hw.c:127`<br>`referencias/BC250-V3/wddm_hw.c:86`<br>`referencias/wddm/wddm_hw.c:138` |
| `A_CP_RB_WPTR_POLL_CNTL` | `0x08708u` | `referencias/BC250-V2/wddm_hw.c:119`<br>`referencias/BC250-V3/wddm_hw.c:78`<br>`referencias/wddm/wddm_hw.c:135` |
| `A_CP_STAT` | `0x08680u` | `referencias/BC250-V3/wddm.h:643` |
| `A_GCMC_VM_FB_LOCATION_BASE` | `0x0A600` | `dbg/hw.c:818` |
| `A_GCMC_VM_FB_LOCATION_TOP` | `0x0A604` | `dbg/hw.c:819` |
| `A_GCMC_VM_FB_OFFSET` | `0x0A5AC` | `dbg/hw.c:817`<br>`dbg/psp.c:410` |
| `A_GCMC_VM_FB_OFFSET` | `0x0A5ACu` | `referencias/BC250-V2/wddm_hw.c:101`<br>`referencias/BC250-V3/wddm_hw.c:60`<br>`referencias/wddm/wddm_hw.c:101` |
| `A_GCMC_VM_MX_L1_TLB_CNTL` | `0x0A61C` | `dbg/hw.c:822` |
| `A_GCMC_VM_MX_L1_TLB_CNTL` | `0x0A61Cu` | `referencias/wddm/wddm_hw.c:104` |
| `A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB` | `0x0A5B0` | `dbg/hw.c:823` |
| `A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB` | `0x0A5B0u` | `referencias/wddm/wddm_hw.c:105` |
| `A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB` | `0x0A5B4` | `dbg/hw.c:824` |
| `A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB` | `0x0A5B4u` | `referencias/wddm/wddm_hw.c:106` |
| `A_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR` | `0x0A618` | `dbg/hw.c:821` |
| `A_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR` | `0x0A618u` | `referencias/wddm/wddm_hw.c:103` |
| `A_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR` | `0x0A614` | `dbg/hw.c:820` |
| `A_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR` | `0x0A614u` | `referencias/wddm/wddm_hw.c:102` |
| `A_GCVM_CONTEXT0_CNTL` | `0x0A200` | `dbg/hw.c:810` |
| `A_GCVM_CONTEXT0_CNTL` | `0x0A200u` | `referencias/BC250-V2/wddm_hw.c:107`<br>`referencias/BC250-V3/wddm_hw.c:66`<br>`referencias/wddm/wddm_hw.c:117` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI` | `0x0A3B0` | `dbg/hw.c:812` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI` | `0x0A3B0u` | `referencias/BC250-V2/wddm_hw.c:109`<br>`referencias/BC250-V3/wddm_hw.c:68`<br>`referencias/wddm/wddm_hw.c:119` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO` | `0x0A3AC` | `dbg/hw.c:811` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO` | `0x0A3ACu` | `referencias/BC250-V2/wddm_hw.c:108`<br>`referencias/BC250-V3/wddm_hw.c:67`<br>`referencias/wddm/wddm_hw.c:118` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI` | `0x0A4B0` | `dbg/hw.c:816` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI` | `0x0A4B0u` | `referencias/BC250-V2/wddm_hw.c:113`<br>`referencias/BC250-V3/wddm_hw.c:72`<br>`referencias/wddm/wddm_hw.c:123` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO` | `0x0A4AC` | `dbg/hw.c:815` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO` | `0x0A4ACu` | `referencias/BC250-V2/wddm_hw.c:112`<br>`referencias/BC250-V3/wddm_hw.c:71`<br>`referencias/wddm/wddm_hw.c:122` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI` | `0x0A430` | `dbg/hw.c:814` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI` | `0x0A430u` | `referencias/BC250-V2/wddm_hw.c:111`<br>`referencias/BC250-V3/wddm_hw.c:70`<br>`referencias/wddm/wddm_hw.c:121` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO` | `0x0A42C` | `dbg/hw.c:813` |
| `A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO` | `0x0A42Cu` | `referencias/BC250-V2/wddm_hw.c:110`<br>`referencias/BC250-V3/wddm_hw.c:69`<br>`referencias/wddm/wddm_hw.c:120` |
| `A_GCVM_INVALIDATE_ENG0_ACK` | `0x0A2D4u` | `referencias/BC250-V2/wddm_hw.c:115`<br>`referencias/BC250-V3/wddm_hw.c:74`<br>`referencias/wddm/wddm_hw.c:125` |
| `A_GCVM_INVALIDATE_ENG0_REQ` | `0x0A28Cu` | `referencias/BC250-V2/wddm_hw.c:114`<br>`referencias/BC250-V3/wddm_hw.c:73`<br>`referencias/wddm/wddm_hw.c:124` |
| `A_GCVM_INVALIDATE_ENG17_ACK` | `0x0A318` | `dbg/hw.c:832` |
| `A_GCVM_INVALIDATE_ENG17_REQ` | `0x0A2D0` | `dbg/hw.c:831` |
| `A_GCVM_INVALIDATE_ENG17_SEM` | `0x0A288` | `dbg/hw.c:830` |
| `A_GCVM_L2_CNTL` | `0x0A100` | `dbg/hw.c:804` |
| `A_GCVM_L2_CNTL` | `0x0A100u` | `referencias/BC250-V2/wddm_hw.c:102`<br>`referencias/BC250-V3/wddm_hw.c:61`<br>`referencias/wddm/wddm_hw.c:112` |
| `A_GCVM_L2_CNTL2` | `0x0A104` | `dbg/hw.c:805` |
| `A_GCVM_L2_CNTL2` | `0x0A104u` | `referencias/BC250-V2/wddm_hw.c:103`<br>`referencias/BC250-V3/wddm_hw.c:62`<br>`referencias/wddm/wddm_hw.c:113` |
| `A_GCVM_L2_CNTL3` | `0x0A108` | `dbg/hw.c:806` |
| `A_GCVM_L2_CNTL3` | `0x0A108u` | `referencias/BC250-V2/wddm_hw.c:104`<br>`referencias/BC250-V3/wddm_hw.c:63`<br>`referencias/wddm/wddm_hw.c:114` |
| `A_GCVM_L2_CNTL4` | `0x0A160` | `dbg/hw.c:807` |
| `A_GCVM_L2_CNTL4` | `0x0A160u` | `referencias/BC250-V2/wddm_hw.c:105`<br>`referencias/BC250-V3/wddm_hw.c:64`<br>`referencias/wddm/wddm_hw.c:115` |
| `A_GCVM_L2_CNTL5` | `0x0A184` | `dbg/hw.c:808` |
| `A_GCVM_L2_CNTL5` | `0x0A184u` | `referencias/BC250-V2/wddm_hw.c:106`<br>`referencias/BC250-V3/wddm_hw.c:65`<br>`referencias/wddm/wddm_hw.c:116` |
| `A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI` | `0x0A154` | `dbg/hw.c:828` |
| `A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI` | `0x0A154u` | `referencias/wddm/wddm_hw.c:110` |
| `A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO` | `0x0A150` | `dbg/hw.c:827` |
| `A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO` | `0x0A150u` | `referencias/wddm/wddm_hw.c:109` |
| `A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI` | `0x0A14C` | `dbg/hw.c:826` |
| `A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI` | `0x0A14Cu` | `referencias/wddm/wddm_hw.c:108` |
| `A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO` | `0x0A148` | `dbg/hw.c:825` |
| `A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO` | `0x0A148u` | `referencias/wddm/wddm_hw.c:107` |
| `A_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO` | `0x0A158` | `dbg/hw.c:829` |
| `A_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO` | `0x0A158u` | `referencias/wddm/wddm_hw.c:111` |
| `A_GCVM_L2_PROTECTION_FAULT_STATUS` | `0x0A130` | `dbg/hw.c:809` |
| `A_GRBM_GFX_CNTL` | `0x04968u` | `referencias/BC250-V2/wddm_hw.c:1247`<br>`referencias/wddm/wddm_hw.c:1402` |
| `A_GRBM_GFX_CNTL` | `SOC15_GC0(0x0DC2u)` | `referencias/BC250-V3/wddm_hw.c:1494` |
| `A_GRBM_GFX_INDEX` | `0x034D0u` | `referencias/BC250-V2/wddm_hw.c:1246`<br>`referencias/wddm/wddm_hw.c:1401` |
| `A_GRBM_GFX_INDEX` | `SOC15_GC1(0x2200u)` | `referencias/BC250-V3/wddm_hw.c:126` |
| `A_GRBM_STATUS` | `0x03260u` | `referencias/BC250-V2/wddm_hw.c:1244`<br>`referencias/wddm/wddm_hw.c:1399` |
| `A_GRBM_STATUS` | `0x08010u` | `referencias/BC250-V3/wddm.h:639` |
| `A_GRBM_STATUS2` | `0x0326Cu` | `referencias/BC250-V2/wddm_hw.c:1245`<br>`referencias/wddm/wddm_hw.c:1400` |
| `A_GRBM_STATUS2` | `SOC15_GC0(0x0DA2u)` | `referencias/BC250-V3/wddm_hw.c:1493` |
| `A_HDP_MEM_COHERENCY_FLUSH_CNTL` | `0x04A80u` | `referencias/BC250-V2/wddm_hw.c:135`<br>`referencias/BC250-V3/wddm_hw.c:144`<br>`referencias/wddm/wddm_hw.c:146` |
| `A_HDP_MISC_CNTL` | `0x03FCCu` | `referencias/BC250-V2/wddm_hw.c:137`<br>`referencias/BC250-V3/wddm_hw.c:146`<br>`referencias/wddm/wddm_hw.c:148` |
| `A_HDP_READ_CACHE_INVALIDATE` | `0x03FC4u` | `referencias/BC250-V2/wddm_hw.c:136`<br>`referencias/BC250-V3/wddm_hw.c:145`<br>`referencias/wddm/wddm_hw.c:147` |
| `A_LEGACY_BAD_CC_SHADER_ARRAY` | `0x09C1Cu` | `referencias/BC250-V3/wddm_hw.c:139` |
| `A_LEGACY_BAD_COMPUTE_DISPATCH` | `0x080E0u` | `referencias/BC250-V3/wddm_hw.c:141` |
| `A_LEGACY_BAD_CP_STAT` | `0x08684u` | `referencias/BC250-V3/wddm_hw.c:137` |
| `A_LEGACY_BAD_GRBM_STATUS` | `0x03040u` | `referencias/BC250-V3/wddm_hw.c:136` |
| `A_LEGACY_BAD_SCRATCH` | `0x032D4u` | `referencias/BC250-V3/wddm_hw.c:140` |
| `A_LEGACY_BAD_SPI_PG_WGP` | `0x05C3Cu` | `referencias/BC250-V3/wddm_hw.c:138` |
| `A_MMIO_DATA` | `0x00004u` | `referencias/BC250-V2/wddm_hw.c:29`<br>`referencias/BC250-V3/wddm_hw.c:32`<br>`referencias/wddm/wddm_hw.c:29` |
| `A_MMIO_INDEX` | `0x00000u` | `referencias/BC250-V2/wddm_hw.c:28`<br>`referencias/BC250-V3/wddm_hw.c:31`<br>`referencias/wddm/wddm_hw.c:28` |
| `A_PA_SC_LINE_STIPPLE_STATE` | `0x30A04u` | `referencias/BC250-V2/wddm_hw.c:132`<br>`referencias/BC250-V3/wddm_hw.c:91`<br>`referencias/wddm/wddm_hw.c:143` |
| `A_PSP_C2PMSG_64` | `(MP0_BASE + 0x200u)` | `referencias/BC250-V2/wddm_hw.c:32`<br>`referencias/BC250-V3/wddm_hw.c:35`<br>`referencias/wddm/wddm_hw.c:32` |
| `A_PSP_C2PMSG_67` | `(MP0_BASE + 0x20Cu)` | `referencias/BC250-V2/wddm_hw.c:33`<br>`referencias/BC250-V3/wddm_hw.c:36`<br>`referencias/wddm/wddm_hw.c:33` |
| `A_PSP_C2PMSG_69` | `(MP0_BASE + 0x214u)` | `referencias/BC250-V2/wddm_hw.c:34`<br>`referencias/BC250-V3/wddm_hw.c:37`<br>`referencias/wddm/wddm_hw.c:34` |
| `A_PSP_C2PMSG_70` | `(MP0_BASE + 0x218u)` | `referencias/BC250-V2/wddm_hw.c:35`<br>`referencias/BC250-V3/wddm_hw.c:38`<br>`referencias/wddm/wddm_hw.c:35` |
| `A_PSP_C2PMSG_71` | `(MP0_BASE + 0x21Cu)` | `referencias/BC250-V2/wddm_hw.c:36`<br>`referencias/BC250-V3/wddm_hw.c:39`<br>`referencias/wddm/wddm_hw.c:36` |
| `A_PSP_C2PMSG_81` | `(MP0_BASE + 0x244u)` | `referencias/BC250-V2/wddm_hw.c:37`<br>`referencias/BC250-V3/wddm_hw.c:40`<br>`referencias/wddm/wddm_hw.c:37` |
| `A_RLC_PG_ALWAYS_ON_WGP_MASK` | `SOC15_GC1(0x4C53u)` | `referencias/BC250-V3/wddm_hw.c:129` |
| `A_RSMU_DATA` | `0x0003Cu` | `referencias/BC250-V2/wddm_hw.c:182`<br>`referencias/BC250-V3/wddm_hw.c:176`<br>`referencias/wddm/wddm_hw.c:219` |
| `A_RSMU_INDEX` | `0x00038u` | `referencias/BC250-V2/wddm_hw.c:181`<br>`referencias/BC250-V3/wddm_hw.c:175`<br>`referencias/wddm/wddm_hw.c:218` |
| `A_SCRATCH_REG0` | `SOC15_GC1(0x2040u)` | `referencias/BC250-V3/wddm_hw.c:130` |
| `A_SCRATCH_REG1` | `SOC15_GC1(0x2041u)` | `referencias/BC250-V3/wddm_hw.c:131` |
| `A_SDMA0_CHICKEN_BITS` | `0x049F4` | `dbg/hw.c:753` |
| `A_SDMA0_CHICKEN_BITS` | `0x049F4u` | `referencias/BC250-V2/wddm_hw.c:828`<br>`referencias/BC250-V3/wddm_hw.c:1027`<br>`referencias/wddm/wddm_hw.c:983` |
| `A_SDMA0_CLK_CTRL` | `0x049EC` | `dbg/hw.c:758` |
| `A_SDMA0_CLK_CTRL` | `0x049ECu` | `referencias/BC250-V2/wddm_hw.c:826`<br>`referencias/BC250-V3/wddm_hw.c:1025`<br>`referencias/wddm/wddm_hw.c:981` |
| `A_SDMA0_CNTL` | `0x049F0` | `dbg/hw.c:752` |
| `A_SDMA0_CNTL` | `0x049F0u` | `referencias/BC250-V2/wddm_hw.c:827`<br>`referencias/BC250-V3/wddm_hw.c:1026`<br>`referencias/wddm/wddm_hw.c:982` |
| `A_SDMA0_F32_CNTL` | `0x04A28` | `dbg/hw.c:760` |
| `A_SDMA0_F32_CNTL` | `0x04A28u` | `referencias/BC250-V2/wddm_hw.c:139`<br>`referencias/BC250-V3/wddm_hw.c:148`<br>`referencias/wddm/wddm_hw.c:174` |
| `A_SDMA0_GB_ADDR_CONFIG` | `0x049F8` | `dbg/hw.c:754` |
| `A_SDMA0_GB_ADDR_CONFIG` | `0x049F8u` | `referencias/BC250-V2/wddm_hw.c:829`<br>`referencias/BC250-V3/wddm_hw.c:1028`<br>`referencias/wddm/wddm_hw.c:984` |
| `A_SDMA0_GB_ADDR_CONFIG_READ` | `0x049FC` | `dbg/hw.c:755` |
| `A_SDMA0_GB_ADDR_CONFIG_READ` | `0x049FCu` | `referencias/BC250-V2/wddm_hw.c:830`<br>`referencias/BC250-V3/wddm_hw.c:1029`<br>`referencias/wddm/wddm_hw.c:985` |
| `A_SDMA0_GFX_DOORBELL` | `0x04BC8` | `dbg/hw.c:774` |
| `A_SDMA0_GFX_DOORBELL` | `0x04BC8u` | `referencias/BC250-V2/wddm_hw.c:846`<br>`referencias/BC250-V3/wddm_hw.c:1045`<br>`referencias/wddm/wddm_hw.c:1001` |
| `A_SDMA0_GFX_DOORBELL_OFFSET` | `0x04C2C` | `dbg/hw.c:775` |
| `A_SDMA0_GFX_IB_CNTL` | `0x04BA8` | `dbg/hw.c:773` |
| `A_SDMA0_GFX_IB_CNTL` | `0x04BA8u` | `referencias/BC250-V2/wddm_hw.c:845`<br>`referencias/BC250-V3/wddm_hw.c:1044`<br>`referencias/wddm/wddm_hw.c:1000` |
| `A_SDMA0_GFX_MINOR_PTR_UPDATE` | `0x04BD0u` | `referencias/BC250-V2/wddm_hw.c:847`<br>`referencias/BC250-V3/wddm_hw.c:1046`<br>`referencias/wddm/wddm_hw.c:1002` |
| `A_SDMA0_GFX_MINOR_PTR_UPDATE` | `0x04C54` | `dbg/hw.c:776` |
| `A_SDMA0_GFX_POLL_ADDR_HI` | `0x04C48u` | `referencias/BC250-V2/wddm_hw.c:848`<br>`referencias/BC250-V3/wddm_hw.c:1047`<br>`referencias/wddm/wddm_hw.c:1003` |
| `A_SDMA0_GFX_POLL_ADDR_LO` | `0x04C4Cu` | `referencias/BC250-V2/wddm_hw.c:849`<br>`referencias/BC250-V3/wddm_hw.c:1048`<br>`referencias/wddm/wddm_hw.c:1004` |
| `A_SDMA0_GFX_RB_BASE` | `0x04B84` | `dbg/hw.c:764` |
| `A_SDMA0_GFX_RB_BASE` | `0x04B84u` | `referencias/BC250-V2/wddm_hw.c:836`<br>`referencias/BC250-V3/wddm_hw.c:1035`<br>`referencias/wddm/wddm_hw.c:991` |
| `A_SDMA0_GFX_RB_BASE_HI` | `0x04B88` | `dbg/hw.c:765` |
| `A_SDMA0_GFX_RB_BASE_HI` | `0x04B88u` | `referencias/BC250-V2/wddm_hw.c:837`<br>`referencias/BC250-V3/wddm_hw.c:1036`<br>`referencias/wddm/wddm_hw.c:992` |
| `A_SDMA0_GFX_RB_CNTL` | `0x04B80` | `dbg/hw.c:763` |
| `A_SDMA0_GFX_RB_CNTL` | `0x04B80u` | `referencias/BC250-V2/wddm_hw.c:835`<br>`referencias/BC250-V3/wddm_hw.c:1034`<br>`referencias/wddm/wddm_hw.c:990` |
| `A_SDMA0_GFX_RB_RPTR` | `0x04B8C` | `dbg/hw.c:766` |
| `A_SDMA0_GFX_RB_RPTR` | `0x04B8Cu` | `referencias/BC250-V2/wddm_hw.c:838`<br>`referencias/BC250-V3/wddm.h:646`<br>`referencias/BC250-V3/wddm_hw.c:1037`<br>`referencias/wddm/wddm_hw.c:993` |
| `A_SDMA0_GFX_RB_RPTR_ADDR_HI` | `0x04BA0` | `dbg/hw.c:771` |
| `A_SDMA0_GFX_RB_RPTR_ADDR_HI` | `0x04BA0u` | `referencias/BC250-V2/wddm_hw.c:843`<br>`referencias/BC250-V3/wddm_hw.c:1042`<br>`referencias/wddm/wddm_hw.c:998` |
| `A_SDMA0_GFX_RB_RPTR_ADDR_LO` | `0x04BA4` | `dbg/hw.c:772` |
| `A_SDMA0_GFX_RB_RPTR_ADDR_LO` | `0x04BA4u` | `referencias/BC250-V2/wddm_hw.c:844`<br>`referencias/BC250-V3/wddm_hw.c:1043`<br>`referencias/wddm/wddm_hw.c:999` |
| `A_SDMA0_GFX_RB_RPTR_HI` | `0x04B90` | `dbg/hw.c:767` |
| `A_SDMA0_GFX_RB_RPTR_HI` | `0x04B90u` | `referencias/BC250-V2/wddm_hw.c:839`<br>`referencias/BC250-V3/wddm_hw.c:1038`<br>`referencias/wddm/wddm_hw.c:994` |
| `A_SDMA0_GFX_RB_WPTR` | `0x04B94` | `dbg/hw.c:768` |
| `A_SDMA0_GFX_RB_WPTR` | `0x04B94u` | `referencias/BC250-V2/wddm_hw.c:840`<br>`referencias/BC250-V3/wddm_hw.c:1039`<br>`referencias/wddm/wddm_hw.c:995` |
| `A_SDMA0_GFX_RB_WPTR_HI` | `0x04B98` | `dbg/hw.c:769` |
| `A_SDMA0_GFX_RB_WPTR_HI` | `0x04B98u` | `referencias/BC250-V2/wddm_hw.c:841`<br>`referencias/BC250-V3/wddm_hw.c:1040`<br>`referencias/wddm/wddm_hw.c:996` |
| `A_SDMA0_GFX_RB_WPTR_POLL_ADDR_HI` | `0x04C48` | `dbg/hw.c:777` |
| `A_SDMA0_GFX_RB_WPTR_POLL_ADDR_LO` | `0x04C4C` | `dbg/hw.c:778` |
| `A_SDMA0_GFX_RB_WPTR_POLL_CNTL` | `0x04B9C` | `dbg/hw.c:770` |
| `A_SDMA0_GFX_RB_WPTR_POLL_CNTL` | `0x04B9Cu` | `referencias/BC250-V2/wddm_hw.c:842`<br>`referencias/BC250-V3/wddm_hw.c:1041`<br>`referencias/wddm/wddm_hw.c:997` |
| `A_SDMA0_SEM_WAIT_FAIL_TIMER` | `0x04A04u` | `referencias/BC250-V2/wddm_hw.c:831`<br>`referencias/BC250-V3/wddm_hw.c:1030`<br>`referencias/wddm/wddm_hw.c:986` |
| `A_SDMA0_SEM_WAIT_FAIL_TIMER_CNTL` | `0x04A04` | `dbg/hw.c:751` |
| `A_SDMA0_STATUS2` | `0x04A60u` | `referencias/BC250-V2/wddm_hw.c:832`<br>`referencias/BC250-V3/wddm_hw.c:1031`<br>`referencias/wddm/wddm_hw.c:987` |
| `A_SDMA0_STATUS2_REG` | `0x04A60` | `dbg/hw.c:757` |
| `A_SDMA0_STATUS_REG` | `0x04A14` | `dbg/hw.c:756` |
| `A_SDMA0_UCODE_ADDR` | `0x3E200` | `dbg/hw.c:779` |
| `A_SDMA0_UCODE_CHECKSUM` | `0x04A24` | `dbg/hw.c:759` |
| `A_SDMA0_UCODE_DATA` | `0x3E204` | `dbg/hw.c:780` |
| `A_SDMA0_UTCL1_CNTL` | `0x04A70` | `dbg/hw.c:761` |
| `A_SDMA0_UTCL1_CNTL` | `0x04A70u` | `referencias/BC250-V2/wddm_hw.c:833`<br>`referencias/BC250-V3/wddm_hw.c:1032`<br>`referencias/wddm/wddm_hw.c:988` |
| `A_SDMA0_UTCL1_PAGE` | `0x04AA0` | `dbg/hw.c:762` |
| `A_SDMA0_UTCL1_PAGE` | `0x04AA0u` | `referencias/BC250-V2/wddm_hw.c:834`<br>`referencias/BC250-V3/wddm_hw.c:1033`<br>`referencias/wddm/wddm_hw.c:989` |
| `A_SPI_PG_ENABLE_STATIC_WGP_MASK` | `SOC15_GC0(0x1277u)` | `referencias/BC250-V3/wddm_hw.c:127` |
| `A_SPI_PG_MASK` | `0x034FCu` | `referencias/BC250-V2/wddm_hw.c:1268`<br>`referencias/wddm/wddm_hw.c:1423` |
| `A_VGT_NUM_INSTANCES` | `0x30934u` | `referencias/BC250-V2/wddm_hw.c:131`<br>`referencias/BC250-V3/wddm_hw.c:90`<br>`referencias/wddm/wddm_hw.c:142` |
| `BC250_BAR0_VRAM_SIZE` | `0x10000000ULL` | `referencias/BC250-V2/wddm.h:35`<br>`referencias/BC250-V3/wddm.h:36`<br>`referencias/wddm/wddm.h:37` |
| `BC250_BAR5_MMIO_SIZE` | `0x00080000ULL` | `referencias/BC250-V2/wddm.h:34`<br>`referencias/BC250-V3/wddm.h:35`<br>`referencias/wddm/wddm.h:36` |
| `BC250_BAR5_PHYSICAL_BASE` | `0xFE800000ULL` | `referencias/AMD-BC-250-Windows-Driver/src/amdbc250_reg/amdbc250_reg.c:12` |
| `BC250_BAR5_SIZE` | `0x80000` | `referencias/AMD-BC-250-Windows-Driver/src/amdbc250_reg/amdbc250_reg.c:13` |
| `BC250_ESCAPE_CMD_COMPUTE_DISPATCH` | `0xBD07u` | `referencias/BC250-V2/tests/test_phase1_hardware_bringup.c:93`<br>`referencias/BC250-V2/tests/test_phase5_compute.c:95`<br>`referencias/BC250-V2/wddm.h:105`<br>`referencias/BC250-V3/CentralDeTestes/src/test_framework.h:82`<br>`referencias/BC250-V3/tests/test_common.h:66`<br>+3 ocorrência(s) |
| `BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS` | `0xBD06u` | `referencias/BC250-V2/adl/atiadlxx.c:46`<br>`referencias/BC250-V2/tests/test_phase1_hardware_bringup.c:92`<br>`referencias/BC250-V2/tests/test_phase2_radv_escape.c:75`<br>`referencias/BC250-V2/tests/test_phase3_vram_allocator.c:68`<br>`referencias/BC250-V2/tests/test_phase5_compute.c:94`<br>+8 ocorrência(s) |
| `BC250_ESCAPE_SDMA_BENCHMARK` | `0xBD05u` | `referencias/BC250-V2/tests/test_phase5_compute.c:93`<br>`referencias/BC250-V2/wddm.h:83`<br>`referencias/BC250-V3/CentralDeTestes/src/test_framework.h:80`<br>`referencias/BC250-V3/tests/test_common.h:63`<br>`referencias/BC250-V3/tools/common.h:62`<br>+2 ocorrência(s) |
| `BC250_ESCAPE_SDMA_COPY` | `0xBD04u` | `referencias/BC250-V2/tests/test_phase5_compute.c:92`<br>`referencias/BC250-V2/wddm.h:82`<br>`referencias/BC250-V3/CentralDeTestes/src/test_framework.h:79`<br>`referencias/BC250-V3/tests/test_common.h:62`<br>`referencias/BC250-V3/tools/common.h:61`<br>+2 ocorrência(s) |
| `BC250_ESCAPE_SDMA_FILL` | `0xBD0Cu` | `referencias/BC250-V3/tests/test_common.h:64`<br>`referencias/BC250-V3/wddm.h:134` |
| `BC250_ESCAPE_SMN_READ` | `0xBD20u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:87`<br>`referencias/BC250-V3/tools/common.h:69`<br>`referencias/BC250-V3/wddm.h:267` |
| `BC250_ESCAPE_SMN_WRITE` | `0xBD21u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:88`<br>`referencias/BC250-V3/tools/common.h:70`<br>`referencias/BC250-V3/wddm.h:268` |
| `BC250_ESC_COMPUTE_DISPATCH` | `0xBD07u` | `referencias/BC250-V2/umd/bc250_vulkan_icd.c:111` |
| `BC250_ESC_QUERY_COMPUTE_CAPS` | `0xBD06u` | `referencias/BC250-V2/umd/bc250_vulkan_icd.c:110` |
| `BC250_REG_CC_ARRAY` | `0x089BCu` | `dbg/hw.c:40` |
| `BC250_REG_CC_ARRAY_LEGACY` | `0x03264u` | `dbg/hw.c:43` |
| `BC250_REG_GPU_ID` | `0x0000u` | `dbg/hw.c:24` |
| `BC250_REG_GRBM_STATUS` | `0x08010u` | `dbg/hw.c:39` |
| `BC250_REG_GRBM_STATUS_LEGACY` | `0x03260u` | `dbg/hw.c:42` |
| `BC250_REG_SCRATCH0` | `0x30100u` | `dbg/hw.c:38` |
| `BC250_REG_SCRATCH0_LEGACY` | `0x032D4u` | `dbg/hw.c:41` |
| `BC250_SDMA_GCR_CNTL` | `0x0000C3A1u` | `dbg/hw.c:1339` |
| `BIOS_GCVM_CONTEXT0_CNTL` | `0x0B460` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7019` |
| `BIOS_GCVM_CONTEXT0_PT_BASE_LO` | `0x6C8C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7020` |
| `BIOS_SCRATCH_OFF` | `0x32D4` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7012` |
| `C2PMSG_101_OFF` | `0x0294` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe-esc.c:88`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe.c:59` |
| `C2PMSG_35` | `0x1056C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-mailbox-scan.c:13`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/setup-tmr-test.c:10` |
| `C2PMSG_35_OFF` | `0x018C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe-esc.c:79`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe.c:50` |
| `C2PMSG_35_OFF` | `0x5818C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_psp_fw_load.c:22` |
| `C2PMSG_36` | `0x10570` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-mailbox-scan.c:14`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/setup-tmr-test.c:11` |
| `C2PMSG_36_OFF` | `0x0190` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe-esc.c:80`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe.c:51` |
| `C2PMSG_36_OFF` | `0x58190` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_psp_fw_load.c:23` |
| `C2PMSG_37` | `0x10574` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-mailbox-scan.c:15`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/setup-tmr-test.c:12` |
| `C2PMSG_37_OFF` | `0x0194` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe-esc.c:81`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe.c:52` |
| `C2PMSG_37_OFF` | `0x58194` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_psp_fw_load.c:24` |
| `C2PMSG_528` | `0x03B10528` | `referencias/AMD-BC-250-Windows-Driver/test-tools/governor-full-test.c:16` |
| `C2PMSG_564` | `0x03B10564` | `referencias/AMD-BC-250-Windows-Driver/test-tools/governor-full-test.c:18` |
| `C2PMSG_64` | `(MP0_BASE + 0x200)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-create-v2.c:49` |
| `C2PMSG_64` | `0x105E0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/setup-tmr-test.c:13` |
| `C2PMSG_64` | `0x1A350` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-create-test.c:16` |
| `C2PMSG_64` | `0x58200` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-submit-test.c:54` |
| `C2PMSG_64_OFF` | `0x0200` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe-esc.c:82`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe.c:53` |
| `C2PMSG_66` | `0x03B10A08` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-smn-test.c:31`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/bc250ctl.c:53`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/c2pmsg-probe.c:11`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/governor-full-test.c:11`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/smnu-dpm-test.c:11`<br>+2 ocorrência(s) |
| `C2PMSG_67` | `(MP0_BASE + 0x20C)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-create-v2.c:50` |
| `C2PMSG_67` | `0x105EC` | `referencias/AMD-BC-250-Windows-Driver/test-tools/setup-tmr-test.c:14` |
| `C2PMSG_67` | `0x5820C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-submit-test.c:55` |
| `C2PMSG_67_OFF` | `0x020C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe-esc.c:83`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe.c:54` |
| `C2PMSG_69` | `(MP0_BASE + 0x214)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-create-v2.c:51` |
| `C2PMSG_69` | `0x105F4` | `referencias/AMD-BC-250-Windows-Driver/test-tools/setup-tmr-test.c:15` |
| `C2PMSG_69_OFF` | `0x0214` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe-esc.c:84`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe.c:55` |
| `C2PMSG_70` | `(MP0_BASE + 0x218)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-create-v2.c:52` |
| `C2PMSG_70` | `0x105F8` | `referencias/AMD-BC-250-Windows-Driver/test-tools/setup-tmr-test.c:16` |
| `C2PMSG_70_OFF` | `0x0218` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe-esc.c:85`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe.c:56` |
| `C2PMSG_71` | `(MP0_BASE + 0x21C)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-create-v2.c:53` |
| `C2PMSG_71` | `0x105FC` | `referencias/AMD-BC-250-Windows-Driver/test-tools/setup-tmr-test.c:17` |
| `C2PMSG_71_OFF` | `0x021C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe-esc.c:86`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe.c:57` |
| `C2PMSG_80` | `0x03B10A80` | `referencias/AMD-BC-250-Windows-Driver/test-tools/c2pmsg-probe.c:14`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/governor-full-test.c:14`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/smu-linux-test.c:14` |
| `C2PMSG_81` | `(MP0_BASE + 0x244)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-create-v2.c:54` |
| `C2PMSG_81` | `0x10614` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-mailbox-scan.c:16`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/setup-tmr-test.c:18` |
| `C2PMSG_81` | `0x58244` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-submit-test.c:56` |
| `C2PMSG_81_OFF` | `0x0244` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe-esc.c:87`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-probe.c:58` |
| `C2PMSG_81_OFF` | `0x58244` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_psp_fw_load.c:25` |
| `C2PMSG_82` | `0x03B10A48` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-smn-test.c:32`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/bc250ctl.c:54`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/c2pmsg-probe.c:12`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/governor-full-test.c:12`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/smnu-dpm-test.c:12`<br>+2 ocorrência(s) |
| `C2PMSG_88` | `0x03B10A88` | `referencias/AMD-BC-250-Windows-Driver/test-tools/c2pmsg-probe.c:15`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/governor-full-test.c:15`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/smu-linux-test.c:15` |
| `C2PMSG_90` | `0x03B10A68` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-smn-test.c:33`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/bc250ctl.c:55`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/c2pmsg-probe.c:13`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/governor-full-test.c:13`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/smnu-dpm-test.c:13`<br>+2 ocorrência(s) |
| `C2PMSG_998` | `0x03B10998` | `referencias/AMD-BC-250-Windows-Driver/test-tools/governor-full-test.c:17` |
| `COMPUTE_DIM_X` | `0x80E4` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:55` |
| `COMPUTE_DIM_X` | `S1(0x80E4)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/seg1-dispatch-test.c:21` |
| `COMPUTE_DIM_Y` | `0x80E8` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:56` |
| `COMPUTE_DIM_Y` | `S1(0x80E8)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/seg1-dispatch-test.c:22` |
| `COMPUTE_DIM_Z` | `0x80EC` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:57` |
| `COMPUTE_DIM_Z` | `S1(0x80EC)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/seg1-dispatch-test.c:23` |
| `COMPUTE_DISPATCH_EN_MASK` | `0x00008003u` | `referencias/BC250-V2/wddm_hw.c:1271`<br>`referencias/BC250-V3/wddm_hw.c:1518`<br>`referencias/wddm/wddm_hw.c:1426` |
| `COMPUTE_DISPATCH_INITIATOR` | `0x80E0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:54`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:46` |
| `COMPUTE_DISPATCH_INITIATOR` | `S1(0x80E0)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/seg1-dispatch-test.c:20` |
| `COMPUTE_NUM_THREAD_X` | `0x80FC` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:61` |
| `COMPUTE_NUM_THREAD_X` | `S1(0x80FC)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/seg1-dispatch-test.c:24` |
| `COMPUTE_NUM_THREAD_Y` | `0x8100` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:62` |
| `COMPUTE_NUM_THREAD_Z` | `0x8104` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:63` |
| `COMPUTE_PGM_HI` | `0x8114` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:65`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:48` |
| `COMPUTE_PGM_HI` | `S1(0x8114)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/seg1-dispatch-test.c:26` |
| `COMPUTE_PGM_LO` | `0x8110` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:64`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:47` |
| `COMPUTE_PGM_LO` | `S1(0x8110)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/seg1-dispatch-test.c:25` |
| `COMPUTE_PGM_RSRC1` | `0x8128` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:66` |
| `COMPUTE_PGM_RSRC1` | `S1(0x8128)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/seg1-dispatch-test.c:27` |
| `COMPUTE_PGM_RSRC2` | `0x812C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:67` |
| `COMPUTE_PGM_RSRC2` | `S1(0x812C)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/seg1-dispatch-test.c:28` |
| `COMPUTE_SHADER_EN` | `0x00000001` | `referencias/AMD-BC-250-Windows-Driver/test-tools/dispatch-seg1-test.c:11`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/gfxoff-kill-v2.c:39` |
| `COMPUTE_START_X` | `0x80F0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:58` |
| `COMPUTE_START_Y` | `0x80F4` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:59` |
| `COMPUTE_START_Z` | `0x80F8` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:60` |
| `COMPUTE_STATIC_THREAD_MGMT` | `0x8138` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:68` |
| `COMPUTE_TMPRING_SIZE` | `0x8140` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:69` |
| `COMPUTE_USER_DATA_0` | `0x81E0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:70` |
| `CP_HQD_ACTIVE` | `0x910C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:75`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:36`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-base-writability-test.c:28`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:46`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:51` |
| `CP_HQD_EOP_BASE_ADDR` | `0x9164` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:55` |
| `CP_HQD_EOP_BASE_ADDR_HI` | `0x9168` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:56` |
| `CP_HQD_EOP_CONTROL` | `0x916C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:57` |
| `CP_HQD_PERSISTENT_STATE` | `0x9114` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:43`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:53` |
| `CP_HQD_PQ_BASE` | `0x9124` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:77`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:47`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:53` |
| `CP_HQD_PQ_BASE_HI` | `0x9128` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:78`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:38`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:48`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:54` |
| `CP_HQD_PQ_BASE_LO` | `0x9124` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:37` |
| `CP_HQD_PQ_CONTROL` | `0x9148` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:81`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:39`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:49`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:56` |
| `CP_HQD_PQ_DOORBELL_CONTROL` | `0x9150` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:54` |
| `CP_HQD_PQ_RPTR` | `0x912C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:79`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:55` |
| `CP_HQD_PQ_RPTR` | `0x91C4` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:41` |
| `CP_HQD_PQ_WPTR_HI` | `0x91E0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:51`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:58` |
| `CP_HQD_PQ_WPTR_LO` | `0x91DC` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:80`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:40`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:50`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:57` |
| `CP_HQD_VMID` | `0x9110` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:76`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:42`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:52`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:52` |
| `CP_INT_STATUS` | `0x43A0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/exec-ring-pm4-test.c:19` |
| `CP_ME_CNTL` | `0x4A74` | `referencias/AMD-BC-250-Windows-Driver/test-tools/gfx-ring-ro-test.c:42` |
| `CP_ME_CNTL__CE_HALT_MASK` | `0x20000000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:180` |
| `CP_ME_CNTL__ME_HALT_MASK` | `0x10000000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:178` |
| `CP_ME_CNTL__PFP_HALT_MASK` | `0x40000000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:179` |
| `CP_MQD_BASE_ADDR` | `0x9104` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:73`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:34`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:44`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:49` |
| `CP_MQD_BASE_ADDR_HI` | `0x9108` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:74`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:35`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:45`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:50` |
| `CP_MQD_CONTROL` | `0x9108` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:58` |
| `CP_MQD_CONTROL` | `0x9120` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:44` |
| `CP_RB0_BASE` | `0x89E0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/gfx-ring-ro-test.c:32` |
| `CP_RB0_BASE_HI` | `0x8BA4` | `referencias/AMD-BC-250-Windows-Driver/test-tools/gfx-ring-ro-test.c:33` |
| `CP_RB0_BASE_LEGACY_C100` | `0xC100` | `referencias/AMD-BC-250-Windows-Driver/test-tools/gfx-ring-ro-test.c:39` |
| `CP_RB0_CNTL` | `0x89E4` | `referencias/AMD-BC-250-Windows-Driver/test-tools/gfx-ring-ro-test.c:34` |
| `CP_RB0_CNTL__RB_BLKSZ_MASK` | `0x00003F00` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:184` |
| `CP_RB0_CNTL__RB_BUFSZ_MASK` | `0x0000003F` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:183` |
| `CP_RB0_CNTL__RB_NO_UPDATE_MASK` | `0x08000000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:186` |
| `CP_RB0_CNTL__RB_RPTR_WR_ENA_MASK` | `0x80000000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:187` |
| `CP_RB0_RPTR` | `0x4FE0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/gfx-ring-ro-test.c:35` |
| `CP_RB0_WPTR` | `0x8A30` | `referencias/AMD-BC-250-Windows-Driver/test-tools/gfx-ring-ro-test.c:36` |
| `CP_RING0_BASE_DA60` | `0xDA60` | `referencias/AMD-BC-250-Windows-Driver/test-tools/gfx-ring-ro-test.c:40` |
| `CP_RING0_CNTL__RB_BLKSZ_MASK` | `0x0000FF00` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:727` |
| `CP_RING0_CNTL__RB_BUFSZ_MASK` | `0x000000FF` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:726` |
| `CP_STATUS` | `0x426C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/exec-ring-pm4-test.c:17` |
| `DIRECT_C2PMSG_35_OFFSET` | `0x5818C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:102` |
| `DIRECT_C2PMSG_36_OFFSET` | `0x58190` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:103` |
| `DIRECT_C2PMSG_37_OFFSET` | `0x58194` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:104` |
| `DIRECT_C2PMSG_64_OFFSET` | `0x58200` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:105` |
| `DIRECT_C2PMSG_67_OFFSET` | `0x5820C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:106` |
| `DIRECT_C2PMSG_81_OFFSET` | `0x58244` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:107` |
| `DIRECT_C2PMSG_OK` | `0xF0000010` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:108` |
| `DIRECT_C2PMSG_SOS_ALIVE` | `0x80000000` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:109` |
| `DMCUB_SCRATCH0` | `0x7000` | `referencias/AMD-BC-250-Windows-Driver/test-tools/dcn-init-test.c:23` |
| `DMCUB_SCRATCH1` | `0x7004` | `referencias/AMD-BC-250-Windows-Driver/test-tools/dcn-init-test.c:24` |
| `F_SDMA0_CLK_CTRL__SOFT_OVERRIDE_ALL` | `0xFF000000u` | `dbg/hw.c:801` |
| `F_SDMA0_CNTL__AUTO_CTXSW_ENABLE` | `0x00040000u` | `dbg/hw.c:800` |
| `F_SDMA0_CNTL__MIDCMD_PREEMPT_ENABLE` | `0x00000020u` | `dbg/hw.c:790` |
| `F_SDMA0_CNTL__UTC_L1_ENABLE` | `0x00000002u` | `dbg/hw.c:789` |
| `F_SDMA0_F32_CNTL__CHECKSUM_CLR` | `0x00000100u` | `dbg/hw.c:784` |
| `F_SDMA0_F32_CNTL__HALT` | `0x00000001u` | `dbg/hw.c:783` |
| `F_SDMA0_F32_CNTL__RESET` | `0x00000200u` | `dbg/hw.c:799` |
| `F_SDMA0_GFX_DOORBELL__ENABLE` | `0x10000000u` | `dbg/hw.c:795` |
| `F_SDMA0_GFX_IB_CNTL__IB_ENABLE` | `0x00000001u` | `dbg/hw.c:796` |
| `F_SDMA0_GFX_MINOR_PTR_UPDATE__ENABLE` | `0x00000001u` | `dbg/hw.c:797` |
| `F_SDMA0_GFX_RB_CNTL__RB_ENABLE` | `0x00000001u` | `dbg/hw.c:785` |
| `F_SDMA0_GFX_RB_CNTL__RB_SIZE_MASK` | `0x0000003Eu` | `dbg/hw.c:786` |
| `F_SDMA0_GFX_RB_CNTL__RPTR_WB_ENABLE` | `0x00001000u` | `dbg/hw.c:788` |
| `F_SDMA0_GFX_RB_WPTR_POLL_CNTL__F32_POLL_ENABLE` | `0x00000004u` | `dbg/hw.c:798` |
| `F_SDMA0_UTCL1_CNTL__REDO_DELAY_MASK` | `0x0000003Eu` | `dbg/hw.c:793` |
| `F_SDMA0_UTCL1_CNTL__RESP_MODE_MASK` | `0x00000E00u` | `dbg/hw.c:791` |
| `GCVM_CONTEXT0_CNTL` | `0xB460` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:45`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:61`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/seg1-dispatch-test.c:19` |
| `GCVM_CONTEXT0_CNTL_REG` | `0x0B460` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6195` |
| `GCVM_CONTEXT0_PT_BASE_HI` | `0x6C90` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6198` |
| `GCVM_CONTEXT0_PT_BASE_LO` | `0x6C8C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6197` |
| `GCVM_L2_CNTL_REG` | `0x0B360` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6199` |
| `GL_COMPUTE_SHADER` | `0x91B9` | `referencias/BC250-V2/opengl/bc250_wgl_icd.h:73`<br>`referencias/BC250-V2/tests/test_opengl_phase3_glsl_shaders.c:49` |
| `GL_MAX_COMPUTE_WORK_GROUP_COUNT` | `0x91BE` | `referencias/BC250-V2/tests/test_opengl_phase1_icd_direct.c:71`<br>`referencias/BC250-V2/tests/test_opengl_phase1_wgl_context.c:98` |
| `GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS` | `0x90EB` | `referencias/BC250-V2/tests/test_opengl_phase1_icd_direct.c:68`<br>`referencias/BC250-V2/tests/test_opengl_phase1_wgl_context.c:94`<br>`referencias/BC250-V2/tests/test_opengl_phase2_zink_bridge.c:51` |
| `GL_MAX_COMPUTE_WORK_GROUP_SIZE` | `0x91BF` | `referencias/BC250-V2/tests/test_opengl_phase1_icd_direct.c:74`<br>`referencias/BC250-V2/tests/test_opengl_phase1_wgl_context.c:102` |
| `GPU_BAR5_C2PMSG_81_OFFSET` | `0x58244` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:28` |
| `GPU_BAR5_PHYSICAL` | `0xFE800000ULL` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:5` |
| `GPU_BAR5_SIZE` | `0x80000` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:6` |
| `GRBM_BCAST` | `0xE0000000` | `referencias/AMD-BC-250-Windows-Driver/test-tools/wgp-unlock-test.c:17` |
| `GRBM_BROADCAST` | `0xE0000000` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-base-writability-test.c:23` |
| `GRBM_GFX_CNTL` | `0x4968` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:42` |
| `GRBM_GFX_IDX` | `0x9A60` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-gfx-ring-real-test.c:29` |
| `GRBM_GFX_INDEX` | `0x34D0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-autodetect-test.c:20`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:41`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:33`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-base-writability-test.c:21`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-via-cphqd.c:33`<br>+2 ocorrência(s) |
| `GRBM_GFX_INDEX_BRDCST` | `0xE0000000` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-golden-test.c:25` |
| `GRBM_GFX_INDEX_BROADCAST` | `0xE0000000` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-autodetect-test.c:21`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-cu-unlock-test.c:24` |
| `GRBM_GFX_INDEX_BROADCAST` | `0xE0000000u` | `referencias/BC250-V3/wddm_hw.c:132` |
| `GRBM_GFX_INDEX_LIVE` | `0x34D0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-golden-test.c:24` |
| `GRBM_IDX` | `0x34D0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/wgp-deep-test.c:14`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/wgp-unlock-test.c:16` |
| `GRBM_INDEX` | `0x34D0` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6068` |
| `GRBM_KIQ` | `0x00010000` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-base-writability-test.c:22` |
| `GRBM_STATUS` | `0x3260` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:39`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/gfx-ring-ro-test.c:43`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:59`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/seg1-dispatch-test.c:18`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/smn-gc-alias-scan.c:36` |
| `GRBM_STATUS2` | `0x326C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:40` |
| `HDP_REG_DEBUG0` | `0x12B0` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hdp.c:38` |
| `HDP_REG_FB_BASE` | `(0x0F20 + 0x0004)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hdp.c:48` |
| `HDP_REG_FB_OFFSET` | `(0x0F20 + 0x0000)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hdp.c:47` |
| `HDP_REG_FB_TOP` | `(0x0F20 + 0x0008)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hdp.c:49` |
| `HDP_REG_MEM_COHERENCY_FLUSH_CNTL` | `0x12A0` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hdp.c:37` |
| `HDP_REG_NONSURFACE_BASE` | `0x12C8` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hdp.c:41` |
| `HDP_REG_NONSURFACE_BASE_HI` | `0x12CC` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hdp.c:42` |
| `HDP_REG_NONSURFACE_INFO` | `0x12C0` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hdp.c:39` |
| `HDP_REG_NONSURFACE_SIZE` | `0x12C4` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hdp.c:40` |
| `HQD_PQ_DOORBELL` | `0x9154` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6080` |
| `HQD_PQ_DOORBELL_CONTROL` | `0x9150` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-hqd-probe-test.c:30` |
| `IH_CLIENTID_DCE` | `0x08` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:815` |
| `IH_CLIENTID_GFX` | `0x09` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:811` |
| `IH_CLIENTID_IH` | `0x01` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:813` |
| `IH_CLIENTID_OSS` | `0x0A` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:816` |
| `IH_CLIENTID_SDMA` | `0x0D` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:812` |
| `IH_CLIENTID_VMC` | `0x0B` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:814` |
| `IH_CNTL__ENABLE_INTR_MASK` | `0x00000001` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:190` |
| `IH_CNTL__IH_IDLE_MASK` | `0x00010000` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:192` |
| `IH_CNTL__MC_WRREQ_CREDIT_MASK` | `0x00000006` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:191` |
| `IOCTL_AMDBC250_BAR5_READ_PROXY` | `CTL_CODE_AMDBC250(0x83, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_ioctl.h:669` |
| `IOCTL_AMDBC250_FORCE_ENABLE_MMIO` | `CTL_CODE_AMDBC250(0x7F, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_ioctl.h:571` |
| `IOCTL_AMDBC250_GCVM_PT_SETUP` | `CTL_CODE_AMDBC250(0x62, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_ioctl.h:880` |
| `IOCTL_AMDBC250_LOAD_CP_FW` | `0x80000BD4` | `referencias/AMD-BC-250-Windows-Driver/test-tools/load-cp-fw.c:5`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/tlb-inject.c:8` |
| `IOCTL_AMDBC250_LOAD_CP_FW` | `CTL_CODE_AMDBC250(0x85, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_ioctl.h:710`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/test-load-cp-fw.c:11` |
| `IOCTL_AMDBC250_MMIO_TEST` | `CTL_CODE_AMDBC250(0x80, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_ioctl.h:614` |
| `IOCTL_AMDBC250_PSP_REG_ACCESS` | `((ULONG)0x80000BD0)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-rlc-test.c:9` |
| `IOCTL_AMDBC250_REG_DUMP` | `CTL_CODE_AMDBC250(0x86, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_ioctl.h:721` |
| `IOCTL_AMDBC250_SDMA_COPY` | `CTL_CODE_AMDBC250(0x50, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_ioctl.h:69` |
| `IOCTL_AMDBC250_SDMA_FILL` | `CTL_CODE_AMDBC250(0x51, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_ioctl.h:70` |
| `IOCTL_AMDBC250_SDMA_SELFTEST` | `CTL_CODE_AMDBC250(0x63, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_ioctl.h:891` |
| `IOCTL_AMDBC250_SMN_ACCESS` | `CTL_CODE_AMDBC250(0x81, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_ioctl.h:602` |
| `IOCTL_CP_FW` | `0x80000BD4` | `referencias/AMD-BC-250-Windows-Driver/test-tools/cp-ib-test.c:9`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ib-test-app.c:7` |
| `IOCTL_GCVM_PT` | `0x8000098C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/cp-ib-test.c:8` |
| `IOCTL_GCVM_PT_SETUP` | `0x8000098C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/gcvm-pt-test.c:8` |
| `IOCTL_GCVM_SETUP` | `0x8000098C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-full-init-ring-test.c:20` |
| `IOCTL_GPU_GCVM_PT` | `0x8000098C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/mqd-probe.c:9` |
| `IOCTL_GPU_LOAD_CP_FW` | `0x80000BD4` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar2-kiq-test.c:10` |
| `IOCTL_LOAD_CP_FW` | `0x80000BD4` | `referencias/AMD-BC-250-Windows-Driver/test-tools/mqd-probe.c:8`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/psp-full-init-ring-test.c:21` |
| `IOCTL_PSP_REG_PROG` | `CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-smu-reg-test.c:13`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/spi-pg-psp-prog.c:10` |
| `IOCTL_SDMA_COPY` | `0x80000940` | `referencias/AMD-BC-250-Windows-Driver/test-tools/sdma-selftest.c:11`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/test-gpu-ioctls.c:26` |
| `IOCTL_SDMA_FILL` | `0x80000944` | `referencias/AMD-BC-250-Windows-Driver/test-tools/test-gpu-ioctls.c:27` |
| `IOCTL_SDMA_SELFTEST` | `0x80000988` | `referencias/AMD-BC-250-Windows-Driver/test-tools/sdma-selftest.c:10` |
| `IT_WAIT_REG_MEM` | `0x3C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:781` |
| `KIQ_SCRATCH_OFF` | `0x32D4` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6859` |
| `MM_SCRATCH_REG0` | `0x2040u` | `referencias/BC250-V3/tests/test_cp_execution.c:41` |
| `MP0_BASE` | `0x58000` | `referencias/AMD-BC-250-Windows-Driver/test-tools/psp-ring-create-v2.c:48` |
| `MP0_BASE` | `0x58000u` | `referencias/BC250-V2/wddm_hw.c:31`<br>`referencias/BC250-V3/wddm_hw.c:34`<br>`referencias/wddm/wddm_hw.c:31` |
| `MP0_C2PMSG_101_BYTE` | `0x0294` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_psp.h:16` |
| `MP0_C2PMSG_35_BYTE` | `0x018C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_psp.h:8` |
| `MP0_C2PMSG_36_BYTE` | `0x0190` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_psp.h:9` |
| `MP0_C2PMSG_64_BYTE` | `0x0200` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_psp.h:10` |
| `MP0_C2PMSG_67_BYTE` | `0x020C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_psp.h:11` |
| `MP0_C2PMSG_69_BYTE` | `0x0214` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_psp.h:12` |
| `MP0_C2PMSG_70_BYTE` | `0x0218` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_psp.h:13` |
| `MP0_C2PMSG_71_BYTE` | `0x021C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_psp.h:14` |
| `MP0_C2PMSG_81_BYTE` | `0x0244` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_psp.h:15` |
| `MQD_BAR5_OFFSET` | `0x7F000` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:47` |
| `NBIO_SMN_DATA` | `0x3C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:134` |
| `NBIO_SMN_INDEX` | `0x38` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:133` |
| `OLD_COMPUTE_DISPATCH` | `0xDC60` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:84` |
| `OLD_COMPUTE_PGM_LO` | `0xDC70` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:86` |
| `OLD_COMPUTE_START` | `0xDC64` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:85` |
| `OLD_CP_HQD_ACTIVE` | `0xDAC0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:87` |
| `OLD_CP_MQD_BASE` | `0xDAB8` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:88` |
| `OLD_GRBMIX` | `0x34D0` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-gfx-ring-real-test.c:34` |
| `OLD_GRBM_GFX_CNTL` | `0x2022` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:89` |
| `PM4_IT_WAIT_REG_MEM` | `0x3C` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:227` |
| `PSP_C2PMSG_64` | `(PSP_MP0_BASE + 0x200)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:46` |
| `PSP_C2PMSG_67` | `(PSP_MP0_BASE + 0x20C)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:47` |
| `PSP_C2PMSG_69` | `(PSP_MP0_BASE + 0x214)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:48` |
| `PSP_C2PMSG_70` | `(PSP_MP0_BASE + 0x218)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:49` |
| `PSP_C2PMSG_71` | `(PSP_MP0_BASE + 0x21C)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:50` |
| `PSP_C2PMSG_81` | `(PSP_MP0_BASE + 0x244)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:51` |
| `PSP_IOCTL_REG_PROG` | `CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, METHOD_BUFFERED, FILE_ANY_ACCESS)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:22` |
| `PSP_MP0_BASE` | `0x58000` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:45` |
| `REG_CC_GC_SHADER_ARRAY` | `0x09C1Cu` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:106` |
| `REG_CE_IC_CNTL` | `0x17388` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:83` |
| `REG_CE_IC_HI` | `0x17384` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:85` |
| `REG_CE_IC_LO` | `0x17380` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:84` |
| `REG_CE_UCODE_ADDR` | `0x172C0` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:73` |
| `REG_CE_UCODE_DATA` | `0x172C4` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:74` |
| `REG_COMPUTE_DISPATCH_INITIATOR` | `0x080E0u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:126` |
| `REG_CP_ME_CNTL` | `0x086D8u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:111` |
| `REG_CP_ME_CNTL` | `0x4A74` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:64` |
| `REG_CP_RB0_BASE` | `0x0C100u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:112` |
| `REG_CP_RB0_CNTL` | `0x0C104u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:113` |
| `REG_CP_RB0_RPTR_ADDR` | `0x0C10Cu` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:114` |
| `REG_CP_RB0_RPTR_ADDR_HI` | `0x0C110u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:115` |
| `REG_CP_RB0_RPTR_SUSPECT` | `0x08700u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:138` |
| `REG_CP_RB0_WPTR` | `0x0C150u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:116` |
| `REG_CP_RING0_BASE_LO` | `0x0DA60u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:117` |
| `REG_CP_RING0_RPTR` | `0x0DA6Cu` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:118` |
| `REG_CP_RING0_WPTR` | `0x0DA78u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:119` |
| `REG_CP_SCRATCH` | `0x032D4u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:98` |
| `REG_CP_SCRATCH2` | `0x032D8u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:99` |
| `REG_CP_SCRATCH3` | `0x030E0u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:100` |
| `REG_CP_STAT_INVALID` | `0x08684u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:137` |
| `REG_GRBM_CAM_DATA` | `0x3204` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_golden.c:42` |
| `REG_GRBM_CAM_INDEX` | `0x3200` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_golden.c:41` |
| `REG_GRBM_GFX_INDEX` | `0x034D0u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:103` |
| `REG_GRBM_STATUS` | `0x03260u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:101` |
| `REG_GRBM_STATUS` | `0x3260` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:66` |
| `REG_GRBM_STATUS2` | `0x0326Cu` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:102` |
| `REG_HDP_MEM_COHERENCY_FLUSH_CNTL` | `0x04A80u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:109` |
| `REG_HDP_READ_CACHE_INVALIDATE` | `0x03FC4u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:108` |
| `REG_MEC_IC_CNTL` | `0x17398` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:89` |
| `REG_MEC_IC_HI` | `0x17394` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:91` |
| `REG_MEC_IC_LO` | `0x17390` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:90` |
| `REG_MEC_ME1_CNTL` | `0x7A00` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:88` |
| `REG_ME_IC_CNTL` | `0x17378` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:77` |
| `REG_ME_IC_HI` | `0x17374` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:79` |
| `REG_ME_IC_LO` | `0x17370` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:78` |
| `REG_ME_UCODE_ADDR` | `0x172B8` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:69` |
| `REG_ME_UCODE_DATA` | `0x172BC` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:70` |
| `REG_PFP_IC_CNTL` | `0x17368` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:80` |
| `REG_PFP_IC_HI` | `0x17364` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:82` |
| `REG_PFP_IC_LO` | `0x17360` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:81` |
| `REG_PFP_UCODE_ADDR` | `0x172B0` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:71` |
| `REG_PFP_UCODE_DATA` | `0x172B4` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:72` |
| `REG_PSP_C2PMSG_35` | `0x5818Cu` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:121` |
| `REG_PSP_C2PMSG_36` | `0x58190u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:122` |
| `REG_PSP_C2PMSG_37` | `0x58194u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:123` |
| `REG_PSP_C2PMSG_64` | `0x58200u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:124` |
| `REG_PSP_C2PMSG_81` | `0x58244u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:125` |
| `REG_RLC_CNTL` | `0x3A00` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:94` |
| `REG_RLC_GPM_UCODE_ADDR` | `0x3A4C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:95` |
| `REG_RLC_GPM_UCODE_DATA` | `0x3A50` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:96` |
| `REG_RLC_PG_ALWAYS_ON` | `0x03D64u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:107` |
| `REG_SCRATCH` | `0x32D4` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_fw_load.c:65` |
| `REG_SPI_PG_MASK` | `0x034FCu` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:105` |
| `REG_SPI_PG_STATIC_WGP` | `0x05C3Cu` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:104` |
| `RING_BAR5_OFFSET` | `0x7F400` | `referencias/AMD-BC-250-Windows-Driver/test-tools/kiq-bar5-mqd-test.c:49` |
| `RLC_CP_SCHEDULERS` | `0xECA8` | `referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:64` |
| `RLC_PG` | `0x3D64` | `referencias/AMD-BC-250-Windows-Driver/test-tools/smn-gc-alias-scan.c:33`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/wgp-deep-test.c:13`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/wgp-unlock-test.c:15` |
| `RLC_PG_ALWAYS_ON_WGP_MASK` | `0x3D64` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-cu-unlock-test.c:22` |
| `RLC_REG_CLK_CNTL` | `0x3A14` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:43` |
| `RLC_REG_CNTL` | `0x3A00` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:37` |
| `RLC_REG_CNTL2` | `0x3A04` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:40` |
| `RLC_REG_CNTL3` | `0x3A08` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:41` |
| `RLC_REG_GPM_UCODE_ADDR` | `0x3A4C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:38` |
| `RLC_REG_GPM_UCODE_DATA` | `0x3A50` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:39` |
| `RLC_REG_INT_CLEAR` | `0x3A48` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:53` |
| `RLC_REG_INT_CNTL` | `0x3A40` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:51` |
| `RLC_REG_INT_STATUS` | `0x3A44` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:52` |
| `RLC_REG_MGCG_CTRL` | `0x3A10` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:42` |
| `RLC_REG_SMU_MSG_CNTL` | `0x3A34` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:48` |
| `RLC_REG_SMU_SAFE_MODE` | `0x3A30` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:47` |
| `RLC_REG_SPARE_BOOL` | `0x3A1C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_rlc.c:44` |
| `SCRATCH` | `0x32D4` | `referencias/AMD-BC-250-Windows-Driver/test-tools/gfx-ring-ro-test.c:44`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/smn-gc-alias-scan.c:37` |
| `SCRATCH_OFF` | `0x32D4` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6093` |
| `SDMA_AUTO_CTXSW_ENABLE` | `0x00040000u` | `referencias/BC250-V2/wddm_hw.c:867`<br>`referencias/BC250-V3/wddm_hw.c:1066`<br>`referencias/wddm/wddm_hw.c:1022` |
| `SDMA_CLK_OVERRIDE_ALL` | `0xFF000000u` | `referencias/BC250-V2/wddm_hw.c:866`<br>`referencias/BC250-V3/wddm_hw.c:1065`<br>`referencias/wddm/wddm_hw.c:1021` |
| `SDMA_CNTL_REQUIRED` | `0x00040022u` | `referencias/BC250-V2/wddm_hw.c:868`<br>`referencias/BC250-V3/wddm_hw.c:1067`<br>`referencias/wddm/wddm_hw.c:1023` |
| `SDMA_DOORBELL_ENABLE` | `0x10000000u` | `referencias/BC250-V2/wddm_hw.c:863`<br>`referencias/BC250-V3/wddm_hw.c:1062`<br>`referencias/wddm/wddm_hw.c:1018` |
| `SDMA_F32_HALT` | `0x00000001u` | `referencias/BC250-V2/wddm_hw.c:860`<br>`referencias/BC250-V3/wddm_hw.c:1059`<br>`referencias/wddm/wddm_hw.c:1015` |
| `SDMA_F32_POLL_ENABLE` | `0x00000004u` | `referencias/BC250-V2/wddm_hw.c:865`<br>`referencias/BC250-V3/wddm_hw.c:1064`<br>`referencias/wddm/wddm_hw.c:1020` |
| `SDMA_F32_RESET` | `0x00000200u` | `referencias/BC250-V2/wddm_hw.c:861`<br>`referencias/BC250-V3/wddm_hw.c:1060`<br>`referencias/wddm/wddm_hw.c:1016` |
| `SDMA_IB_ENABLE` | `0x00000001u` | `referencias/BC250-V2/wddm_hw.c:862`<br>`referencias/BC250-V3/wddm_hw.c:1061`<br>`referencias/wddm/wddm_hw.c:1017` |
| `SDMA_MAX_TRANSFER_BYTES` | `0x00400000ull` | `referencias/BC250-V2/wddm_hw.c:869`<br>`referencias/BC250-V3/wddm_hw.c:1068`<br>`referencias/wddm/wddm_hw.c:1024` |
| `SDMA_MINOR_PTR_ENABLE` | `0x00000001u` | `referencias/BC250-V2/wddm_hw.c:864`<br>`referencias/BC250-V3/wddm_hw.c:1063`<br>`referencias/wddm/wddm_hw.c:1019` |
| `SDMA_OP_CONST_WRITE` | `0x07` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:842` |
| `SDMA_OP_COPY` | `0x01` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:300` |
| `SDMA_OP_COPY_LINEAR` | `0x01` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:836` |
| `SDMA_OP_COPY_TILED` | `0x02` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:837` |
| `SDMA_OP_FENCE` | `0x04` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:839` |
| `SDMA_OP_FENCE` | `0x05` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:303` |
| `SDMA_OP_FILL` | `0x03` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:838` |
| `SDMA_OP_INDIRECT` | `0x04` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:302` |
| `SDMA_OP_NOP` | `0x00` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:835`<br>`referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:299` |
| `SDMA_OP_POLL_REGMEM` | `0x06` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:841` |
| `SDMA_OP_POLL_REGMEM` | `0x08` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:305` |
| `SDMA_OP_SRBM_WRITE` | `0x0E` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:307` |
| `SDMA_OP_TIMESTAMP` | `0x0D` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:306` |
| `SDMA_OP_TRAP` | `0x05` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_dream_hw.h:840` |
| `SDMA_OP_TRAP` | `0x06` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:304` |
| `SDMA_OP_WRITE` | `0x02` | `referencias/AMD-BC-250-Windows-Driver/inc/amdbc250_hw_extra.h:301` |
| `SDMA_RB_ENABLE` | `0x00000001u` | `referencias/BC250-V2/wddm_hw.c:856`<br>`referencias/BC250-V3/wddm_hw.c:1055`<br>`referencias/wddm/wddm_hw.c:1011` |
| `SDMA_RB_SIZE_MASK` | `0x0000003Eu` | `referencias/BC250-V2/wddm_hw.c:857`<br>`referencias/BC250-V3/wddm_hw.c:1056`<br>`referencias/wddm/wddm_hw.c:1012` |
| `SDMA_RPTR_WB_ENABLE` | `0x00001000u` | `referencias/BC250-V2/wddm_hw.c:859`<br>`referencias/BC250-V3/wddm_hw.c:1058`<br>`referencias/wddm/wddm_hw.c:1014` |
| `SMN_C2PMSG_66` | `0x03B10A08` | `referencias/AMD-BC-250-Windows-Driver/test-tools/smu-mailbox-test.c:25` |
| `SMN_C2PMSG_82` | `0x03B10A48` | `referencias/AMD-BC-250-Windows-Driver/test-tools/smu-mailbox-test.c:26` |
| `SMN_C2PMSG_90` | `0x03B10A68` | `referencias/AMD-BC-250-Windows-Driver/test-tools/smu-mailbox-test.c:27` |
| `SMN_CORE_MASK_REG` | `0x0115A870` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:340` |
| `SMN_GFXOFF_FEATURE_HIGH` | `0x03B10998` | `referencias/AMD-BC-250-Windows-Driver/src/amdbc250_reg/amdbc250_reg.c:17` |
| `SMN_GFXOFF_FEATURE_LOW` | `0x03B10528` | `referencias/AMD-BC-250-Windows-Driver/src/amdbc250_reg/amdbc250_reg.c:16` |
| `SMN_SMU_Q0_ARG` | `0x03B10A48u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:130` |
| `SMN_SMU_Q0_CMD` | `0x03B10A08u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:129` |
| `SMN_SMU_Q0_RSP` | `0x03B10A68u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:131` |
| `SMN_SMU_Q2_ARG` | `0x03B10998u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:134` |
| `SMN_SMU_Q2_CMD` | `0x03B10528u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:132` |
| `SMN_SMU_Q2_RSP` | `0x03B10564u` | `referencias/BC250-V3/CentralDeTestes/src/test_framework.h:133` |
| `SMU_C2PMSG_66` | `0x16A08` | `referencias/AMD-BC-250-Windows-Driver/test-tools/smu-test.c:26` |
| `SMU_C2PMSG_66_SMN` | `0x03B10A08` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:128` |
| `SMU_C2PMSG_82` | `0x16A48` | `referencias/AMD-BC-250-Windows-Driver/test-tools/smu-test.c:27` |
| `SMU_C2PMSG_82_SMN` | `0x03B10A48` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:129` |
| `SMU_C2PMSG_83` | `0x16A4C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/smu-test.c:28` |
| `SMU_C2PMSG_90` | `0x16A68` | `referencias/AMD-BC-250-Windows-Driver/test-tools/smu-test.c:29` |
| `SMU_C2PMSG_90_SMN` | `0x03B10A68` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:130` |
| `SMU_Q3_ARG_SMN` | `0x03B10A88` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:337` |
| `SMU_Q3_CMD_SMN` | `0x03B10A20` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:335` |
| `SMU_Q3_RSP_SMN` | `0x03B10A80` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:336` |
| `SMU_SMN_C2PMSG_66` | `(0x03B10A08UL)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_power.c:52` |
| `SMU_SMN_C2PMSG_82` | `(0x03B10A48UL)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_power.c:53` |
| `SMU_SMN_C2PMSG_90` | `(0x03B10A68UL)` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_power.c:54` |
| `SPI_PG` | `0x5C3C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/smn-gc-alias-scan.c:32`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/wgp-deep-test.c:11`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/wgp-unlock-test.c:13` |
| `SPI_PG_ENABLE_STATIC_WGP_MASK` | `0x5C3C` | `referencias/AMD-BC-250-Windows-Driver/test-tools/bar5-cu-unlock-test.c:20` |
| `SPI_PG_MASK` | `0x34FC` | `referencias/AMD-BC-250-Windows-Driver/test-tools/correct-compute-test.c:48`<br>`referencias/AMD-BC-250-Windows-Driver/test-tools/ring-dispatch-test.c:62` |
| `T_BAD_COMPUTE_DISP` | `0x080E0u` | `referencias/BC250-V3/tests/test_verdict.h:284` |
| `T_BAD_CP_STAT` | `0x08684u` | `referencias/BC250-V3/tests/test_verdict.h:280` |
| `T_BAD_GRBM_STATUS` | `0x03040u` | `referencias/BC250-V3/tests/test_verdict.h:279` |
| `T_BAD_SCRATCH` | `0x032D4u` | `referencias/BC250-V3/tests/test_verdict.h:283` |
| `T_BAD_SPI_PG` | `0x05C3Cu` | `referencias/BC250-V3/tests/test_verdict.h:281` |
| `T_COMPUTE_DISPATCH` | `0x0B800u` | `referencias/BC250-V3/tests/test_verdict.h:261` |
| `T_CP_MEC_CNTL` | `0x08234u` | `referencias/BC250-V3/tests/test_verdict.h:251` |
| `T_CP_ME_CNTL` | `0x086D8u` | `referencias/BC250-V3/tests/test_verdict.h:250` |
| `T_CP_RB0_BASE` | `0x0C100u` | `referencias/BC250-V3/tests/test_verdict.h:253` |
| `T_CP_RB0_CNTL` | `0x0C104u` | `referencias/BC250-V3/tests/test_verdict.h:254` |
| `T_CP_RB0_RPTR` | `0x08700u` | `referencias/BC250-V3/tests/test_verdict.h:252` |
| `T_CP_RB0_WPTR` | `0x0C150u` | `referencias/BC250-V3/tests/test_verdict.h:255` |
| `T_CP_STAT` | `0x08680u` | `referencias/BC250-V3/tests/test_verdict.h:249` |
| `T_GRBM_GFX_INDEX` | `0x30800u` | `referencias/BC250-V3/tests/test_verdict.h:260` |
| `T_GRBM_STATUS` | `0x08010u` | `referencias/BC250-V3/tests/test_verdict.h:247` |
| `T_GRBM_STATUS2` | `0x08008u` | `referencias/BC250-V3/tests/test_verdict.h:248` |
| `T_PSP_C2PMSG_64` | `0x58200u` | `referencias/BC250-V3/tests/test_verdict.h:262` |
| `T_PSP_C2PMSG_81` | `0x58244u` | `referencias/BC250-V3/tests/test_verdict.h:263` |
| `T_SCRATCH_REG0` | `0x30100u` | `referencias/BC250-V3/tests/test_verdict.h:256` |
| `T_SCRATCH_REG1` | `0x30104u` | `referencias/BC250-V3/tests/test_verdict.h:257` |
| `T_SDMA0_CNTL` | `0x049F0u` | `referencias/BC250-V3/tests/test_verdict.h:266` |
| `T_SDMA0_F32_CNTL` | `0x04A28u` | `referencias/BC250-V3/tests/test_verdict.h:269` |
| `T_SDMA0_FREEZE` | `0x04A2Cu` | `referencias/BC250-V3/tests/test_verdict.h:270` |
| `T_SDMA0_GFX_RB_BASE` | `0x04B84u` | `referencias/BC250-V3/tests/test_verdict.h:274` |
| `T_SDMA0_GFX_RB_CNTL` | `0x04B80u` | `referencias/BC250-V3/tests/test_verdict.h:273` |
| `T_SDMA0_GFX_RB_RPTR` | `0x04B8Cu` | `referencias/BC250-V3/tests/test_verdict.h:275` |
| `T_SDMA0_GFX_RB_WPTR` | `0x04B94u` | `referencias/BC250-V3/tests/test_verdict.h:276` |
| `T_SDMA0_STATUS1_REG` | `0x04A18u` | `referencias/BC250-V3/tests/test_verdict.h:268` |
| `T_SDMA0_STATUS_REG` | `0x04A14u` | `referencias/BC250-V3/tests/test_verdict.h:267` |
| `T_SDMA0_UTCL1_RD_STAT` | `0x04A78u` | `referencias/BC250-V3/tests/test_verdict.h:271` |
| `T_SDMA0_UTCL1_WR_STAT` | `0x04A7Cu` | `referencias/BC250-V3/tests/test_verdict.h:272` |
| `T_SPI_PG_WGP_MASK` | `0x0935Cu` | `referencias/BC250-V3/tests/test_verdict.h:258` |
| `VK_QUEUE_COMPUTE_BIT` | `0x2` | `referencias/BC250-V2/umd/bc250_vulkan.h:168` |
| `VK_SHADER_STAGE_COMPUTE_BIT` | `0x00000020` | `referencias/BC250-V2/umd/bc250_vulkan.h:60` |

## Literais usados diretamente em chamadas de MMIO

| Literal | Proveniência |
|---:|---|
| `0X00000000` | `dbg/hw.c:1982`<br>`dbg/hw.c:2007`<br>`referencias/BC250-V2/wddm_hw.c:628`<br>`referencias/BC250-V2/wddm_hw.c:630`<br>`referencias/BC250-V2/wddm_hw.c:1380`<br>`referencias/BC250-V2/wddm_hw.c:1381`<br>`referencias/BC250-V3/wddm_hw.c:786`<br>`referencias/BC250-V3/wddm_hw.c:788`<br>+8 ocorrência(s) |
| `0X0000` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:4545`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:4584`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:4618` |
| `0X1` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:8815`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_vm.c:901` |
| `0X00000001` | `referencias/BC250-V2/wddm_hw.c:735`<br>`referencias/BC250-V3/wddm_hw.c:934`<br>`referencias/wddm/wddm_hw.c:890` |
| `0X00000003` | `dbg/hw.c:1712`<br>`dbg/hw.c:1998`<br>`dbg/hw.c:2080`<br>`referencias/BC250-V2/wddm_hw.c:672`<br>`referencias/BC250-V2/wddm_hw.c:800`<br>`referencias/BC250-V2/wddm_hw.c:1473`<br>`referencias/BC250-V3/wddm_hw.c:835`<br>`referencias/BC250-V3/wddm_hw.c:999`<br>+4 ocorrência(s) |
| `0X00000007` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:4296` |
| `0X00000009` | `referencias/wddm/wddm_hw.c:792`<br>`referencias/wddm/wddm_hw.c:808` |
| `0X0000000F` | `dbg/hw.c:1745`<br>`referencias/wddm/wddm_hw.c:680` |
| `0XF` | `dbg/hw.c:1797` |
| `0X0000001F` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:4293` |
| `0X1F` | `referencias/BC250-V3/CentralDeTestes/src/t3_write_stick.c:96` |
| `0X38` | `referencias/AMD-BC-250-Windows-Driver/src/amdbc250_reg/amdbc250_reg.c:41`<br>`referencias/AMD-BC-250-Windows-Driver/src/amdbc250_reg/amdbc250_reg.c:42`<br>`referencias/AMD-BC-250-Windows-Driver/src/amdbc250_reg/amdbc250_reg.c:48`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_power.c:65`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_power.c:75` |
| `0X3C` | `referencias/AMD-BC-250-Windows-Driver/src/amdbc250_reg/amdbc250_reg.c:43`<br>`referencias/AMD-BC-250-Windows-Driver/src/amdbc250_reg/amdbc250_reg.c:49`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_power.c:66`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_power.c:76` |
| `0XFF` | `dbg/hw.c:1975`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7728`<br>`referencias/BC250-V2/wddm_hw.c:654`<br>`referencias/BC250-V3/wddm_hw.c:817`<br>`referencias/wddm/wddm_hw.c:750`<br>`referencias/wddm/wddm_hw.c:754`<br>`referencias/wddm/wddm_hw.c:758`<br>`referencias/wddm/wddm_hw.c:762`<br>+1 ocorrência(s) |
| `0X000000FF` | `referencias/BC250-V2/wddm_hw.c:638`<br>`referencias/BC250-V3/wddm_hw.c:796` |
| `0X00000100` | `referencias/wddm/wddm_hw.c:751`<br>`referencias/wddm/wddm_hw.c:755`<br>`referencias/wddm/wddm_hw.c:759`<br>`referencias/wddm/wddm_hw.c:763` |
| `0X0D00` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:5679` |
| `0X00001859` | `dbg/hw.c:1741`<br>`referencias/wddm/wddm_hw.c:677` |
| `0X34D0` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hw_init.c:166`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hw_init.c:171`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6038`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6039`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6043` |
| `0X3D64` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hw_init.c:169` |
| `0X00003F00` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:924` |
| `0X03FC4` | `referencias/wddm/wddm_hw.c:828`<br>`referencias/wddm/wddm_hw.c:843` |
| `0X03FCC` | `dbg/hw.c:1571`<br>`dbg/hw.c:1572`<br>`dbg/hw.c:1573`<br>`dbg/hw.c:1574`<br>`dbg/hw.c:1575` |
| `0X3FFF` | `dbg/hw.c:1252` |
| `0X00004507` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7764` |
| `0X4A74` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6034` |
| `0X4A80` | `dbg/hw.c:1561`<br>`dbg/hw.c:1562` |
| `0X4B14` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6035` |
| `0X04B80` | `dbg/driver.c:796` |
| `0X50D0` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:1238`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:1240` |
| `0X5C3C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hw_init.c:160`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hw_init.c:168`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hw_init.c:172` |
| `0X6C0C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7591` |
| `0X6C10` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7587`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7598` |
| `0X6C8C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6032`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7433`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7563`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7569`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7612`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7614` |
| `0X6C90` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6033`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7434`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7564`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7570`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7613`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7615` |
| `0X08010` | `dbg/hw.c:2126` |
| `0X08708` | `dbg/hw.c:1985`<br>`dbg/hw.c:2012`<br>`dbg/hw.c:2084` |
| `0X9C1C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hw_init.c:167`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_hw_init.c:173` |
| `0X0B460` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6031`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7430` |
| `0XC100` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:1222`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:1228` |
| `0XC180` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:1223`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:1229` |
| `0XE060` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6040`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7431`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7470`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7487` |
| `0XE064` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6041`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7432`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7471`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7488` |
| `0XE06C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7436` |
| `0XE078` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6042`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7435` |
| `0XECA8` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6036` |
| `0X0000FFFF` | `referencias/wddm/wddm_hw.c:696` |
| `0X0001FFFF` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:1231` |
| `0X30934` | `dbg/hw.c:2003` |
| `0X30A04` | `dbg/hw.c:2004` |
| `0X00075FFF` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7876` |
| `0X00080E03` | `referencias/BC250-V2/wddm_hw.c:627`<br>`referencias/BC250-V3/wddm_hw.c:785`<br>`referencias/wddm/wddm_hw.c:685` |
| `0X000FFFE0` | `referencias/BC250-V2/wddm_hw.c:631`<br>`referencias/BC250-V3/wddm_hw.c:789`<br>`referencias/wddm/wddm_hw.c:689` |
| `0X00100000` | `referencias/BC250-V2/wddm_hw.c:629`<br>`referencias/BC250-V3/wddm_hw.c:787`<br>`referencias/wddm/wddm_hw.c:687` |
| `0X00400100` | `referencias/BC250-V2/wddm_hw.c:662`<br>`referencias/BC250-V2/wddm_hw.c:683`<br>`referencias/BC250-V2/wddm_hw.c:803`<br>`referencias/BC250-V2/wddm_hw.c:1476`<br>`referencias/BC250-V3/wddm_hw.c:825`<br>`referencias/BC250-V3/wddm_hw.c:849`<br>`referencias/BC250-V3/wddm_hw.c:1002`<br>`referencias/BC250-V3/wddm_hw.c:1753`<br>+3 ocorrência(s) |
| `0X00403000` | `dbg/hw.c:982`<br>`referencias/BC250-V2/wddm_hw.c:952`<br>`referencias/wddm/wddm_hw.c:1107` |
| `0X007FFE80` | `dbg/hw.c:1763`<br>`referencias/BC250-V2/wddm_hw.c:632`<br>`referencias/BC250-V3/wddm_hw.c:790` |
| `0X007FFE81` | `referencias/wddm/wddm_hw.c:698` |
| `0X12345678` | `referencias/BC250-V3/CentralDeTestes/src/t1_scratch_mmio.c:80` |
| `0X15000150` | `dbg/hw.c:1970`<br>`referencias/BC250-V2/wddm_hw.c:650`<br>`referencias/BC250-V2/wddm_hw.c:734`<br>`referencias/BC250-V3/wddm_hw.c:813`<br>`referencias/BC250-V3/wddm_hw.c:933`<br>`referencias/wddm/wddm_hw.c:745`<br>`referencias/wddm/wddm_hw.c:768`<br>`referencias/wddm/wddm_hw.c:889` |
| `0X80000000` | `dbg/hw.c:1989`<br>`referencias/BC250-V2/wddm_hw.c:665`<br>`referencias/BC250-V3/wddm_hw.c:828` |
| `0X80000009` | `referencias/wddm/wddm_hw.c:786` |
| `0X8000014C` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7766` |
| `0X87654321` | `referencias/BC250-V3/CentralDeTestes/src/t1_scratch_mmio.c:81` |
| `0XBF810000` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7852` |
| `0XC0001000` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7774` |
| `0XCAFEBABE` | `referencias/BC250-V3/CentralDeTestes/src/t1_scratch_mmio.c:82` |
| `0XDEF3DEF3` | `referencias/BC250-V3/CentralDeTestes/src/phase_audit.c:55` |
| `0XE0000000` | `referencias/BC250-V2/wddm_hw.c:1353`<br>`referencias/BC250-V3/CentralDeTestes/src/t3_write_stick.c:100`<br>`referencias/wddm/wddm_hw.c:1508` |
| `0XFFE00000` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:923`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:4292`<br>`referencias/BC250-V2/wddm_hw.c:1352`<br>`referencias/wddm/wddm_hw.c:1507` |
| `0XFFF80000` | `referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:4295` |
| `0XFFFFFFFC` | `dbg/hw.c:985`<br>`dbg/hw.c:1978`<br>`dbg/hw.c:1983`<br>`referencias/BC250-V2/wddm_hw.c:656`<br>`referencias/BC250-V2/wddm_hw.c:660`<br>`referencias/BC250-V2/wddm_hw.c:954`<br>`referencias/BC250-V3/wddm_hw.c:819`<br>`referencias/BC250-V3/wddm_hw.c:823`<br>+4 ocorrência(s) |
| `0XFFFFFFFF` | `dbg/hw.c:980`<br>`dbg/hw.c:1744`<br>`dbg/hw.c:1974`<br>`dbg/psp.c:182`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:6915`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7868`<br>`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c:7974`<br>`referencias/BC250-V2/wddm_hw.c:588`<br>+18 ocorrência(s) |

## Como promover um item

Um item deste inventário só pode entrar no catálogo curado após identificar IP/version, header upstream, `mmREG`, `BASE_IDX`, base Cyan Skillfish, unidade e teste seguro. Leituras plausíveis e write-stick isolado não bastam.

