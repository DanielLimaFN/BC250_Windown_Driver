#ifndef BC250_GFX_SHADER_STAGING_CONTRACT_H
#define BC250_GFX_SHADER_STAGING_CONTRACT_H

#include "bc250_gfx_observable_shader.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =========================================================================
 * B.2.3 -- CONTRATO DE STAGING E ALOCACAO DE SHADERS NA VRAM
 *
 * PROVENIENCIA E RIGOR EPISTEMOLOGICO (Leis 1, 2, 4 e 7)
 *
 * 1. Alinhamento de Hardware Estrito (256 bytes — Lei 7):
 *    Os registradores de microcodigo SPI_SHADER_PGM_LO_VS/PS utilizam os bits
 *    [39:8] do endereco de GPUVA (descartam os 8 bits inferiores). Portanto,
 *    qualquer endereco desalinhado a 256 bytes e' TERMINANTEMENTE RECUSADO
 *    por nome (BC250_GFX_SHADER_STAGING_MISALIGNED).
 *
 * 2. Delimitacao de Janela de VRAM e Zonas Reservadas (D2):
 *    Os buffers de shader devem pertencer a janela valida de VRAM (por padrao
 *    [0xA8C000, 0xA8C000 + 0x0F531000)). Enderecos que caiam fora da janela
 *    sao recusados (BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW).
 *    Enderecos que colidam com regioes reservadas conhecidas — scanout GOP
 *    [0, 0xA8C000), anel grafico CP_RB0 ou dummy page global — sao recusados
 *    (BC250_GFX_SHADER_STAGING_RESERVED_COLLISION).
 *
 * 3. Ausencia de Sobreposicao (Zero Overlap):
 *    As faixas de memoria de VS e PS devem ser estritamente disjuntas tanto
 *    no espaco virtual quanto no espaco de GPUVA. Colisoes sao recusadas
 *    (BC250_GFX_SHADER_STAGING_OVERLAP).
 *
 * 4. Prova de Gravacao com Ponto de Injecao e Barreira (Readback Proof — Lei 2 / D1):
 *    Nenhuma escrita em memoria e' assumida como sucesso. A gravacao e releitura
 *    podem utilizar transporte com ponto de injecao (BC250_GFX_SHADER_STAGING_TRANSPORT)
 *    para exercitar e provar falhas de hardware, VRAM defeituosa ou bit flips.
 *    Antes da releitura, e' emitida barreira de memoria (MemoryBarrier).
 *    RESSALVA DE WRITE-COMBINING (WC): O readback por CPU com barreira comprova
 *    que os dados foram entregues a abertura do host. Em mapeamentos BAR WC reais,
 *    a visibilidade da GPU exige flush de HDP (HDP_MEM_COHERENCY_FLUSH_CNTL)
 *    antes da busca de microcodigo pela SPI — pre-requisito mandatario da B.2.4.
 *
 * 5. Limpeza de Memoria e Falha Fechada Estrita (Lei 4 / D3):
 *    Qualquer mismatch de readback ou falha de contrato zera os planos de saida,
 *    limpa com zeros a memoria de destino gravada (MemoryClearedOnFailure = 1)
 *    e devolve o codigo correspondente.
 *
 * 6. Integracao com Contratos B.2.1 e B.2.2:
 *    Apos o staging com sucesso, popula os planos de shader e calcula os
 *    quatro registradores SPI_SHADER_PGM_* para cada estagio, prontos para a
 *    emissao de pacotes PM4 SET_SH_REG na etapa B.2.4.
 *
 * NADA AQUI FOI VALIDADO EM HARDWARE.
 * =========================================================================
 */

#define BC250_GFX_SHADER_STAGING_ALIGNMENT 256ull

/* Janela padrao de VRAM do segmento 1 (pos-scanout de 11.059.200 bytes) */
#define BC250_GFX_VRAM_WINDOW_DEFAULT_BASE    0x0000000000A8C000ull
#define BC250_GFX_VRAM_WINDOW_DEFAULT_BYTES   0x000000000F531000ull

/* Limites de reserva de scanout herdado GOP [0, 0x00A8C000) */
#define BC250_GFX_VRAM_SCANOUT_RESERVED_BYTES 0x0000000000A8C000ull

typedef enum BC250_GFX_SHADER_STAGING_STATUS {
    BC250_GFX_SHADER_STAGING_OK                   = 0,
    BC250_GFX_SHADER_STAGING_BAD_ARGUMENT         = 1,
    BC250_GFX_SHADER_STAGING_MISALIGNED           = 2,
    BC250_GFX_SHADER_STAGING_BUFFER_TOO_SMALL     = 3,
    BC250_GFX_SHADER_STAGING_OVERLAP              = 4,
    BC250_GFX_SHADER_STAGING_READBACK_FAILED      = 5,
    BC250_GFX_SHADER_STAGING_MANIFEST_INVALID     = 6,
    BC250_GFX_SHADER_STAGING_CONTRACT_FAILED      = 7,
    BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW        = 8,
    BC250_GFX_SHADER_STAGING_RESERVED_COLLISION   = 9
} BC250_GFX_SHADER_STAGING_STATUS;

/*
 * Janela de VRAM e regioes reservadas para validacao de geometria (D2).
 */
typedef struct BC250_GFX_SHADER_STAGING_WINDOW {
    uint64_t VramWindowBase;         /* Base da janela de alocacao (ex: 0xA8C000) */
    uint64_t VramWindowBytes;        /* Tamanho da janela (ex: 0x0F531000) */
    uint64_t ReservedScanoutBytes;   /* Scanout ocupando [0, ReservedScanoutBytes) */
    uint64_t ReservedRingGpuVa;      /* GPUVA do anel grafico (se != 0, checa overlap) */
    uint32_t ReservedRingBytes;      /* Tamanho do anel grafico */
    uint64_t ReservedDummyPageGpuVa; /* GPUVA da dummy page global (se != 0) */
    uint32_t ReservedDummyPageBytes; /* Tamanho da dummy page (ex: 4096) */
} BC250_GFX_SHADER_STAGING_WINDOW;

/*
 * Transporte para injecao e teste da prova de readback (D1).
 * Permite simular falhas reais de barramento, WC desatualizado ou VRAM defeituosa.
 */
typedef struct BC250_GFX_SHADER_STAGING_TRANSPORT {
    void    *Context;
    uint32_t (*Write32)(void *Context, void *VirtualAddress, uint32_t DwordIndex, uint32_t Value);
    uint32_t (*Read32)(void *Context, void *VirtualAddress, uint32_t DwordIndex, uint32_t *ValueOut);
    void     (*MemoryBarrier)(void *Context);
} BC250_GFX_SHADER_STAGING_TRANSPORT;

typedef struct BC250_GFX_SHADER_STAGING_BUFFER {
    void     *VirtualAddress;
    uint64_t  GpuAddress;
    uint32_t  BufferSizeBytes;
} BC250_GFX_SHADER_STAGING_BUFFER;

typedef struct BC250_GFX_SHADER_STAGING_EVIDENCE {
    uint32_t              VsDwordsWritten;
    uint32_t              PsDwordsWritten;
    uint32_t              VsReadbackMatches;
    uint32_t              PsReadbackMatches;
    uint32_t              VsReadbackMismatches;
    uint32_t              PsReadbackMismatches;
    BC250_GFX_SHADER_REGS VsRegs;
    BC250_GFX_SHADER_REGS PsRegs;
    uint32_t              MemoryClearedOnFailure;
    uint32_t              StagingCompleted;
} BC250_GFX_SHADER_STAGING_EVIDENCE;

const char *Bc250GfxShaderStagingStatusName(BC250_GFX_SHADER_STAGING_STATUS Status);

/*
 * Executa o staging completo de Vertex Shader e Pixel Shader.
 *
 * Window: delimitacao de VRAM e reservas. Se NULL, usa a janela default
 *         [BC250_GFX_VRAM_WINDOW_DEFAULT_BASE, +BC250_GFX_VRAM_WINDOW_DEFAULT_BYTES)
 *         e protege o scanout [0, BC250_GFX_VRAM_SCANOUT_RESERVED_BYTES).
 *
 * Transport: interface de leitura/escrita e barreira. Se NULL, executa acesso
 *            direto a memoria virtual com barreira de compilacao/CPU.
 *
 * Em caso de erro:
 * - Devolve o codigo de erro nomeado.
 * - Zera completamente os planos VsPlanOut e PsPlanOut.
 * - Se a falha ocorreu apos gravacao (ex: readback mismatch), limpa a memoria
 *   de destino com zeros e registra MemoryClearedOnFailure = 1 (D3).
 */
BC250_GFX_SHADER_STAGING_STATUS Bc250GfxShaderStagePipeline(
    const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *VsManifest,
    const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *PsManifest,
    const BC250_GFX_SHADER_STAGING_BUFFER *VsBuffer,
    const BC250_GFX_SHADER_STAGING_BUFFER *PsBuffer,
    const BC250_GFX_SHADER_STAGING_WINDOW *Window,
    const BC250_GFX_SHADER_STAGING_TRANSPORT *Transport,
    BC250_GFX_SHADER_PLAN *VsPlanOut,
    BC250_GFX_SHADER_PLAN *PsPlanOut,
    BC250_GFX_SHADER_STAGING_EVIDENCE *EvidenceOut);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_SHADER_STAGING_CONTRACT_H */
