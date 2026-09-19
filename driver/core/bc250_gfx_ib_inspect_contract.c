/*
 * bc250_gfx_ib_inspect_contract.c -- ver o cabecalho para o porque deste
 * contrato existir e, principalmente, para o que ele NAO afirma.
 *
 * Logica pura: nao toca hardware, nao le registrador, nao mapeia memoria. Toda
 * leitura entra pelo callback, o que torna a suite offline capaz de exercitar
 * os casos que importam -- inclusive os negativos, que sao a razao do contrato
 * existir separado.
 */
#include "bc250_gfx_ib_inspect_contract.h"

/*
 * Sem <string.h>: em kernel-mode o CRT nao esta no caminho de include (foi assim
 * que a primeira versao deste arquivo quebrou o build). A zeragem e' explicita e
 * local, o que tambem deixa obvio que a saida e' SEMPRE inicializada antes de
 * qualquer caminho de retorno -- o chamador nunca le lixo.
 */
static void Bc250GfxIbInspectionClear(BC250_GFX_IB_INSPECTION *Inspection)
{
    uint32_t i;

    Inspection->RequestedDwords = 0u;
    Inspection->ScannedDwords = 0u;
    for (i = 0u; i < BC250_GFX_IB_HEAD_DWORDS; ++i) {
        Inspection->HeadWords[i] = 0u;
    }
    Inspection->HeadValidDwords = 0u;
    Inspection->IndirectBufferPackets = 0u;
    Inspection->ChainJumps = 0u;
    Inspection->Ib2Calls = 0u;
    Inspection->FirstChainDwordOffset = 0u;
    Inspection->FirstIndirectDwordOffset = 0u;
    Inspection->PacketsWalked = 0u;
    Inspection->ScanComplete = 0u;
    Inspection->HeadIsType3 = 0u;
    Inspection->WalkAborted = 0u;
}

void Bc250GfxIbInspectStream(
    uint32_t RequestedDwords,
    BC250_GFX_IB_READ_DWORD Read,
    void *ReaderContext,
    BC250_GFX_IB_INSPECTION *Inspection)
{
    uint32_t budget;
    uint32_t idx = 0u;
    uint32_t i;

    if (Inspection == NULL) {
        return;
    }
    Bc250GfxIbInspectionClear(Inspection);
    Inspection->RequestedDwords = RequestedDwords;

    if (Read == NULL || RequestedDwords == 0u) {
        return;
    }

    budget = (RequestedDwords > BC250_GFX_IB_SCAN_MAX_DWORDS)
        ? BC250_GFX_IB_SCAN_MAX_DWORDS
        : RequestedDwords;

    /* Cabeca do stream, guardada para o decode do primeiro cabecalho no log. */
    for (i = 0u; i < BC250_GFX_IB_HEAD_DWORDS && i < budget; ++i) {
        uint32_t word = 0u;
        if (!Read(ReaderContext, i, &word)) {
            break;
        }
        Inspection->HeadWords[i] = word;
        ++Inspection->HeadValidDwords;
    }
    if (Inspection->HeadValidDwords > 0u) {
        Inspection->HeadIsType3 = ((Inspection->HeadWords[0] >> 30) == 3u) ? 1u : 0u;
    }

    /*
     * Caminhada de pacotes. O tamanho de cada pacote vem do proprio cabecalho,
     * entao um cabecalho corrompido desalinharia tudo a partir dali -- por isso
     * qualquer implausibilidade aborta em vez de seguir adivinhando.
     */
    while (idx < budget) {
        uint32_t header = 0u;
        uint32_t type;
        uint32_t packetDwords;

        if (!Read(ReaderContext, idx, &header)) {
            Inspection->WalkAborted = 1u;
            break;
        }
        type = header >> 30;

        if (type == 3u) {
            const uint32_t opcode = (header >> 8) & 0xFFu;
            const uint32_t count = (header >> 16) & 0x3FFFu;
            packetDwords = count + 2u;
            if (opcode == BC250_GFX_IB_OPCODE_INDIRECT_BUFFER) {
                /*
                 * O dword de controle e' o ULTIMO do pacote: header, addr_lo,
                 * addr_hi, ctrl. Com count=2 sao 4 dwords, logo ctrl = idx+3.
                 * Se nao conseguirmos ler o controle, NAO adivinhamos a classe:
                 * o pacote e' contado como salto (o desfecho pessimista) e a
                 * varredura aborta, porque classificar errado aqui foi o que
                 * produziu o falso IB_AINDA_ENCADEADO da corrida 13:12.
                 */
                uint32_t ctrl = 0u;
                if (Inspection->IndirectBufferPackets == 0u) {
                    Inspection->FirstIndirectDwordOffset = idx;
                }
                ++Inspection->IndirectBufferPackets;

                if (packetDwords >= 4u &&
                    Read(ReaderContext, idx + packetDwords - 1u, &ctrl)) {
                    if ((ctrl & BC250_GFX_IB_CTRL_CHAIN_MASK) != 0u) {
                        if (Inspection->ChainJumps == 0u) {
                            Inspection->FirstChainDwordOffset = idx;
                        }
                        ++Inspection->ChainJumps;
                    } else {
                        ++Inspection->Ib2Calls;
                    }
                } else {
                    if (Inspection->ChainJumps == 0u) {
                        Inspection->FirstChainDwordOffset = idx;
                    }
                    ++Inspection->ChainJumps;
                    Inspection->WalkAborted = 1u;
                    ++Inspection->PacketsWalked;
                    break;
                }
            }
        } else if (type == 2u) {
            packetDwords = 1u; /* NOP type-2 ocupa um dword */
        } else if (type == 0u) {
            packetDwords = ((header >> 16) & 0x3FFFu) + 2u;
        } else {
            /* type 1 nao existe em PM4. */
            Inspection->WalkAborted = 1u;
            break;
        }

        if (packetDwords == 0u || packetDwords > BC250_GFX_IB_SCAN_MAX_DWORDS) {
            Inspection->WalkAborted = 1u;
            break;
        }

        ++Inspection->PacketsWalked;
        idx += packetDwords;
        Inspection->ScannedDwords = (idx < budget) ? idx : budget;
    }

    /*
     * ScanComplete e' deliberadamente estreito. Exige as quatro condicoes:
     * nao abortou, andou pelo menos um pacote, cobriu o orcamento inteiro, e o
     * orcamento nao truncou o stream declarado. Sem a ultima, um IB maior que
     * BC250_GFX_IB_SCAN_MAX_DWORDS sairia como "completo" tendo sido varrido
     * parcialmente -- e o veredito de linearidade seria falso.
     */
    Inspection->ScanComplete =
        (!Inspection->WalkAborted &&
         Inspection->PacketsWalked > 0u &&
         Inspection->ScannedDwords >= budget &&
         budget == Inspection->RequestedDwords) ? 1u : 0u;
}

const char *Bc250GfxIbInspectVerdictName(
    const BC250_GFX_IB_INSPECTION *Inspection)
{
    if (Inspection == NULL) {
        return "INDETERMINADO";
    }
    if (!Inspection->ScanComplete) {
        return "INDETERMINADO";
    }
    /*
     * So' SALTO (CHAIN=1) desqualifica. Chamada IB2 (CHAIN=0) e' aninhamento
     * legitimo: o CP executa o alvo e volta. Ver a nota de proveniencia do bit
     * em bc250_gfx_ib_inspect_contract.h.
     */
    return (Inspection->ChainJumps == 0u)
        ? "IB_LINEAR_CONFIRMADO"
        : "IB_AINDA_ENCADEADO";
}
