/*
 * ==========================================================================
 * bc250_gfx_ib_inspect_contract.h -- classificacao do stream PM4 de um IB
 * submetido ao anel grafico CP_RB0.
 * ==========================================================================
 *
 * POR QUE ESTE CONTRATO EXISTE (medicao, nao hipotese)
 *
 * Na corrida 2026-09-18 10:20 as 87 submissoes do Node 1 GFX sairam TODAS com
 * DmaBufferSize=96 (24 dwords). A RADV submetia apenas o preambulo e deixava o
 * fluxo de clear/draw/destiling atras de um salto PACKET3_INDIRECT_BUFFER
 * ("Chain [0 -> 1] hw_ip=0: PKT3=0xC0023F00 ... VALID=1" no log da RADV).
 *
 * Nenhuma telemetria detectou isso, e a razao e' estrutural: a fence do anel e'
 * gravada por um RELEASE_MEM que o KMD emite NO ANEL, apos o INDIRECT_BUFFER.
 * Ela prova consumo do ANEL. Nao prova nem o tamanho nem o conteudo do IB.
 *
 * O QUE ESTE CONTRATO RESPONDE
 *   "o que exatamente esta no stream PM4 que apontamos ao CP?"
 *   -- em particular, se ele ainda contem saltos de IB (encadeado) ou nenhum
 *      (linear).
 *
 * O QUE ELE NAO RESPONDE, E NAO DEVE SER CITADO COMO SE RESPONDESSE
 *   - se a GPU buscou o IB;
 *   - se o CP executou os pacotes;
 *   - se algum shader despachou (Diretriz Universal 3: PM4/WRITE_DATA/
 *     RELEASE_MEM nao e' dispatch de shader).
 *
 * FAIL-CLOSED: qualquer leitura que falhe, ou qualquer dword que nao decodifique
 * como cabecalho PM4 plausivel, ABORTA a varredura. `ScanComplete` so vai a TRUE
 * quando o stream inteiro declarado foi varrido. Ausencia de salto num stream
 * que nao foi lido por inteiro NAO e' prova de linearidade.
 */
#ifndef BC250_GFX_IB_INSPECT_CONTRACT_H
#define BC250_GFX_IB_INSPECT_CONTRACT_H

/*
 * Mesma convencao de tipos dos outros contratos de driver/core: em kernel-mode o
 * CRT nao esta no caminho de include, entao os tipos de largura fixa vem do
 * ntddk. A guarda compartilhada evita redefinicao quando dois contratos entram
 * na mesma unidade de traducao.
 */
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

#ifndef BC250_GFX_IB_SCAN_MAX_DWORDS
/* Orcamento de varredura. A funcao roda em DISPATCH_LEVEL no KMD: laco livre
 * ali e' como o projeto ganhou o BugCheck 0x133. */
#define BC250_GFX_IB_SCAN_MAX_DWORDS 4096u
#endif

#define BC250_GFX_IB_HEAD_DWORDS 8u

/* Opcode do salto de IB no PM4 type-3. Proveniencia: o mesmo valor que
 * BC250_PM4_IT_INDIRECT_BUFFER usa no construtor de pacotes deste repositorio
 * (driver/core/bc250_pm4_packet_builder.c) e que aparece medido no anel como
 * cabecalho 0xC0023F00. */
#define BC250_GFX_IB_OPCODE_INDIRECT_BUFFER 0x3Fu

/*
 * Bit CHAIN do dword de controle do INDIRECT_BUFFER -- o dword 3 do pacote.
 *
 * POR QUE ESTA DISTINCAO EXISTE (medicao da corrida 2026-09-18 13:12): a
 * primeira versao deste contrato contava QUALQUER opcode 0x3F como "ainda
 * encadeado", e o veredito saiu IB_AINDA_ENCADEADO nas tres submissoes. Mas o
 * dword de controle medido foi 0x000000B0: so' o campo de tamanho, com o bit
 * CHAIN em ZERO. Sao duas coisas diferentes no silicio:
 *
 *   CHAIN=1 -> SALTO. O CP abandona o IB corrente e nao volta. E' o
 *              encadeamento que a concatenacao linear do pacote 26 removeu; o
 *              pacote medido na corrida 25 tinha controle 0x07D00280.
 *   CHAIN=0 -> CHAMADA IB2. O CP executa o IB alvo e RETORNA para o corrente.
 *              E' aninhamento legitimo, nao o defeito.
 *
 * Tratar os dois como iguais fez o veredito acusar regressao onde o log da RADV
 * provava o contrario ("BC-250 UNIFICADO: 2 CS concatenados ... DW=664" e ZERO
 * linhas de Chain). Confundir os dois custaria a proxima corrida.
 *
 * Proveniencia do bit: S_3F3_CHAIN(x) = (x & 0x1) << 20, definido em
 * src/amd/common/amd_cp_packets_gfx11.h e em amdgfxregs.h da arvore Mesa
 * vendorizada. Nao inferido.
 */
#define BC250_GFX_IB_CTRL_CHAIN_BIT 20u
#define BC250_GFX_IB_CTRL_CHAIN_MASK (1u << BC250_GFX_IB_CTRL_CHAIN_BIT)

typedef struct BC250_GFX_IB_INSPECTION {
    uint32_t RequestedDwords;          /* DmaBufferSize / 4 -- o que declaramos ao CP */
    uint32_t ScannedDwords;            /* quantos dwords a varredura cobriu de fato */
    uint32_t HeadWords[BC250_GFX_IB_HEAD_DWORDS];
    uint32_t HeadValidDwords;
    uint32_t IndirectBufferPackets;    /* TODOS os opcodes 0x3F, salto + IB2 */
    uint32_t ChainJumps;               /* somente CHAIN=1 -- o defeito */
    uint32_t Ib2Calls;                 /* somente CHAIN=0 -- aninhamento legitimo */
    uint32_t FirstChainDwordOffset;    /* offset do primeiro SALTO, se houver */
    uint32_t FirstIndirectDwordOffset; /* offset do primeiro 0x3F de qualquer tipo */
    uint32_t PacketsWalked;
    uint32_t ScanComplete;             /* 1 somente se o stream inteiro foi varrido */
    uint32_t HeadIsType3;
    uint32_t WalkAborted;              /* dword implausivel ou leitura falhou */
} BC250_GFX_IB_INSPECTION;

/*
 * Leitor de um dword do stream, por indice. Devolve 0 quando o dword nao pode
 * ser lido -- o contrato trata isso como aborto, nunca como fim normal.
 * O KMD injeta um leitor que caminha a tabela de paginas do contexto e le pela
 * janela de VRAM ja mapeada; a suite offline injeta um array.
 */
typedef int (*BC250_GFX_IB_READ_DWORD)(
    void *ReaderContext,
    uint32_t DwordIndex,
    uint32_t *Value);

/*
 * Classifica o stream. `Inspection` e' sempre preenchido (zerado primeiro), de
 * modo que o chamador nunca le lixo mesmo quando nada pode ser lido.
 */
void Bc250GfxIbInspectStream(
    uint32_t RequestedDwords,
    BC250_GFX_IB_READ_DWORD Read,
    void *ReaderContext,
    BC250_GFX_IB_INSPECTION *Inspection);

/*
 * Veredito em texto, para o log. Tres valores possiveis e nada mais:
 *   "IB_LINEAR_CONFIRMADO"  -- stream inteiro varrido, ZERO saltos (CHAIN=1).
 *                              Chamadas IB2 (CHAIN=0) NAO desqualificam: o CP
 *                              volta delas, e aninhamento e' legitimo.
 *   "IB_AINDA_ENCADEADO"    -- stream inteiro varrido e ha pelo menos um SALTO
 *                              com CHAIN=1, que e' o defeito de verdade.
 *   "INDETERMINADO"         -- varredura incompleta; nada se conclui.
 */
const char *Bc250GfxIbInspectVerdictName(
    const BC250_GFX_IB_INSPECTION *Inspection);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_IB_INSPECT_CONTRACT_H */
