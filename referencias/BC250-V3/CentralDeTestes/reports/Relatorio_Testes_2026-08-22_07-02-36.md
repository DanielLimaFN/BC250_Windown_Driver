# Relatorio da Central de Testes - AMD BC-250 (BC250-V3)

**Data da Execucao:** 2026-08-22 07:02:36
**Adaptador Alvo:** AMD BC-250 (Cyan Skillfish / Oberon, PCI\VEN_1002&DEV_13FE)

---

## Matriz de Resultados Consolidados

| ID | Teste | Veredito | Criterio | Medicao |
|---|---|---|---|---|
| T1 | Scratch Round-Trip Host MMIO | PROVADO | Gravar padrao 0x4D585042 em CP_SCRATCH (0x32D4) via MMIO do Host e ler exatamente o mesmo valor de volta. | 0x32D4: Original=0x4D585042, Escrito=0x4D585042, Lido=0x4D585042 (Match: SIM). 0x32D8: Lido=0x000000BE. |
| T2 | Scratch via PM4 Ring Execution | NAO VALIDADO | Host grava 0x11111111 em 0x32D4; PM4 WRITE_DATA submete 0x22222222 ao mesmo endereco; CP deve consumir o ring (RPTR==WPTR) e 0x32D4 deve ler 0x22222222. | 0x32D4 Inicial=0x11111111 (escrito A=0x11111111), Apos PM4=0x11111111 (esperado B=0x22222222). WPTR=0x00100010, RPTR=0x01200000, GRBM_STATUS=0x00000000. |
| T3 | Write-Stick Sweep em Registradores | PROVADO | Varredura de escrita/leitura/restauracao em registradores do CP, GC e SPI para mapear read-only, gravaveis e SOS-locked. | [CP_RB0_CNTL (0xC104): Orig=0x50000709, Test=0x50000709, Lido=0x50000709 -> GRUDOU] [CP_RB0_WPTR (0xC150): Orig=0x00000018, Test=0x000000AA, Lido=0x00000018 -> IGNORADO] [CP_RING0_WPTR (0xDA78): Orig=0x00100010, Test=0x000000AA, Lido=0x000000AA -> GRUDOU] [SPI_PG_STATIC_WGP (0x5C3C): Orig=0x00000000, Test=0x0000001F, Lido=0x00000000 -> IGNORADO] [CC_GC_SHADER_ARRAY (0x9C1C): Orig=0x1F000000, Test=0x00000000, Lido=0x00000000 -> GRUDOU] [GRBM_GFX_INDEX (0x34D0): Orig=0xE0000000, Test=0xE0000000, Lido=0xE0000000 -> GRUDOU]  |
| T4 | Detector de Offsets Invalidos / Fill Patterns | PROVADO | Varredura em offsets conhecidos do MMIO; sinalizar leituras 0xDEFxDEFx ou 0xBADBADxx como offsets invalidos. | [A_CP_STAT (antigo driver) (0x8684)=0xDEF6DEF6: FILL_PATTERN(INVALIDO)] [0x8688 (espaco adjacente) (0x8688)=0xDEF2DEF2: FILL_PATTERN(INVALIDO)] [A_CP_RB0_RPTR (suspeito) (0x8700)=0x00000000: DADO_VALIDO] [GRBM_STATUS (oficial) (0x3260)=0x00000000: DADO_VALIDO] [CP_SCRATCH (oficial) (0x32D4)=0x00000000: DADO_VALIDO] [PSP_C2PMSG_64 (oficial) (0x58200)=0x80020000: DADO_VALIDO] [PSP_C2PMSG_81 (oficial) (0x58244)=0x00672333: DADO_VALIDO]  |
| T5 | SDMA0 DMA Copy & Hardware Fence | PROVADO | Pre-envenenar fence com ~val, submeter copia SDMA de 4MB na VRAM GDDR6, verificar fence escrita pelo hardware e medir throughput > 5000 MB/s. | Copia 4MB: Status=0, Duracao=328 us, FenceRetornada=#28, LarguraBanda=11.91 GB/s. Benchmark 10x: Media=12258 MB/s (11.97 GB/s), Pico=12383 MB/s. |
| T6 | Telemetria SMU & Sensores Termicos | PROVADO | Amostrar telemetria do SMU a 10 Hz por 10 amostras; validar GfxTemperature > 0, GfxClock > 0, PowerWatts > 0 e ausencia de valores hardcoded (ex: 48C/42W constantes). | Amostras validas: 10/10. Media: GFX Temp=67 C, GFX Clock=1500 MHz, Potencia=30 W, Tensao=924 mV. Primeira=[69C, 1500MHz, 31W], Ultima=[68C, 1500MHz, 31W]. |
| T7 | PSP Protocol & GPCOM Structure Offline Validation | PROVADO | Assertar sizeof(PSP_GFX_RB_FRAME)==64, offsets de C2PMSG (64=0x58200, 81=0x58244, 35=0x5818C), offset de resposta=864 e comandos GPCOM. | PSP_FRAME_SIZE=64 bytes (Match 64B: OK). Offsets: C2P64=0x58200 (OK), C2P81=0x58244 (OK), C2P35=0x5818C (OK), C2P36=0x58190, C2P37=0x58194. |
| T10 | Baseline de Plausibilidade & Capacidade de Silicio | PROVADO | Comparar taxas com tetos teoricos: BC-250 (24 CUs @ 1500MHz) = 4.608 TFLOPS FP32; SDMA Copy > 5 GB/s; GDDR6 256-bit. | Clock Atual: 1500 MHz. Teto Teorico 24 CUs: 4.608 TFLOPS FP32. Teto 40 CUs: 7.680 TFLOPS. SDMA Medido: 12232 MB/s (11.95 GB/s). |

---
*Relatorio gerado automaticamente pela Central de Testes BC250-V3.*
