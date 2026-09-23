#ifndef BC250_VMID_POOL_CONTRACT_H
#define BC250_VMID_POOL_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "bc250_mcdm_object_contract.h"
#include "bc250_gcvm_vmid8_transaction.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =========================================================================
 * DX.7 -- POOL DE VMID POR CONTEXTO
 *
 * POR QUE EXISTE (medido, nao suposto):
 *
 * A Corrida 49 carimbou a raiz instalada em cada submissao GFX e comparou com
 * a raiz no instante do TDR. Resultado: 5 faults, 5 comparacoes, TODAS
 * divergentes -- 2, 8, 9, 14 e 15 carimbos de 16 -- com 6 raizes distintas em
 * 82 submissoes, e o MESMO IB (0x244FE4000) submetido sob TRES raizes
 * diferentes. Varios contextos disputam o VMID 7, e o hardware so' tem UMA
 * raiz por VMID: quando o IB do contexto A executa com a raiz de B instalada,
 * os enderecos de A nao traduzem. E' o WALKER_ERROR=6 com MAPPING_ERROR=1
 * medido desde a Corrida 45, e a onda morrendo em FATAL_HALT com XNACK_ERROR.
 *
 * POR QUE NAO SERIALIZAR EM VEZ DE ALOCAR:
 *
 * Esperar o anel drenar antes de trocar a raiz exigiria bloquear dentro de
 * DxgkDdiSubmitCommandVirtual, que roda em DISPATCH_LEVEL. E' a classe exata
 * do BugCheck 0x133 (DPC_WATCHDOG_VIOLATION) que este projeto ja pagou em
 * 10/09. Com VMID por contexto nao ha troca de raiz, logo nao ha o que
 * serializar -- o problema desaparece por construcao.
 *
 * QUAIS VMIDs ESTAO LIVRES (Corridas 50 e 51, mais leitura de codigo):
 *
 *   VMID 0  -- MICROENGINE MEC. bc250_compute_mqd_contract.c aplica
 *              Cntl=0x007FFE01 para o MEC traduzir a memoria da PROPRIA fila
 *              (ring, MQD, EOP, fences). Transacional, com restauracao.
 *              CONFIRMADO NO SILICIO na Corrida 51: mudou de 0x007FFE80 para
 *              0x007FFE01 com raiz 0xF40FF93000 entre os dois inventarios.
 *   VMID 7  -- contexto de usuario / DWM (o que esta em disputa)
 *   VMID 8  -- paginacao
 *   VMID 9  -- trabalho de compute (BC250_GCVM_COMPUTE_VMID)
 *
 * Livres: 1-6 e 10-15, ou seja DOZE.
 *
 * CORRECAO DE PREMISSA (Corrida 52). Eu havia escrito aqui que "a Corrida 49
 * mediu 6 raizes simultaneas". Isso era FALSO: as 6 raizes distintas foram
 * contadas ao longo de 82 submissoes, no TEMPO, e nao diziam quantos contextos
 * coexistiam. A Corrida 52 mediu a coexistencia de verdade, percorrendo os
 * slots: pico de QUATRO contextos simultaneos em 55 amostras (distribuicao
 * 0:6 1:17 2:19 3:12 4:1), com 27 contextos ao longo da corrida e nenhuma
 * exaustao. A conclusao -- doze slots sobram -- sobrevive; a premissa que eu
 * usei para chega-la, nao.
 *
 * O firmware NAO reserva nenhum: o primeiro inventario da Corrida 50, tirado
 * ANTES de tocarmos em qualquer coisa, devolveu os 16 com Enable=0 e raiz
 * zero. Os quatro ocupados sao todos nossos.
 *
 * ESCOPO DESTA FAIXA -- LEIA ANTES DE MUDAR:
 *
 * Este contrato ALOCA, REGISTRA a raiz observada e publica separadamente o
 * resultado da instalacao lateral no slot; a submissao continua indo em VMID
 * 7. E' deliberado. RootPhysical prova qual raiz pertence ao contexto;
 * InstalledRootPhysical so' ganha autoridade depois de escrita, readback e
 * flush com ACK no proprio VMID alocado. Trocar o VMID da submissao junto com
 * o mapeamento faria o
 * sintoma de um mapeamento errado ser IDENTICO ao do defeito atual -- fault de
 * traducao -- e a corrida nao saberia dizer se foi o conserto ou o bug antigo.
 * Primeiro a medicao prova o mapa; depois a submissao passa a usa-lo.
 * =========================================================================
 */

#define BC250_VMID_COUNT 16u

/* Reservados, por medicao e leitura -- nunca entram no pool. */
#define BC250_VMID_RESERVED_MEC     0u
#define BC250_VMID_RESERVED_USER    7u
#define BC250_VMID_RESERVED_PAGING  8u
#define BC250_VMID_RESERVED_COMPUTE 9u

/* bits 0, 7, 8 e 9 */
#define BC250_VMID_RESERVED_MASK \
    ((1u << BC250_VMID_RESERVED_MEC) | (1u << BC250_VMID_RESERVED_USER) | \
     (1u << BC250_VMID_RESERVED_PAGING) | (1u << BC250_VMID_RESERVED_COMPUTE))

#define BC250_VMID_POOL_MASK (0xFFFFu & ~BC250_VMID_RESERVED_MASK)

/*
 * Allowlist de ESCRITA da faixa DX.7. O pool e a fonte unica dos VMIDs que
 * podem receber uma raiz por contexto; os quatro reservados continuam sob
 * suas politicas dedicadas. A funcao reconhece offsets EXATOS, nunca faixas.
 */
static __inline uint32_t Bc250VmidPoolIsWritableGcvmRegister(uint32_t Offset)
{
    uint32_t vmid;

    for (vmid = 0u; vmid < BC250_VMID_COUNT; ++vmid) {
        if (((BC250_VMID_POOL_MASK >> vmid) & 1u) == 0u) continue;
        if (Bc250GcvmIsContextRegisterForVmid(vmid, Offset) != 0u) return 1u;
    }
    return 0u;
}

typedef enum BC250_VMID_POOL_STATUS {
    BC250_VMID_POOL_OK = 0,
    BC250_VMID_POOL_BAD_ARGUMENT,
    BC250_VMID_POOL_EXHAUSTED,
    BC250_VMID_POOL_NOT_FOUND,
    BC250_VMID_POOL_BUSY
} BC250_VMID_POOL_STATUS;

typedef enum BC250_VMID_POOL_INSTALL_STATE {
    BC250_VMID_POOL_INSTALL_NEVER = 0,
    BC250_VMID_POOL_INSTALLING,
    BC250_VMID_POOL_INSTALL_READY,
    BC250_VMID_POOL_INSTALL_FAILED
} BC250_VMID_POOL_INSTALL_STATE;

typedef struct BC250_VMID_POOL_ENTRY {
    BC250_MCDM_HANDLE Context;   /* NULL = slot livre */
    uint32_t Vmid;
    /* Raiz do contexto observada no caminho legado do VMID 7. Diagnostico. */
    uint64_t RootPhysical;
    /* Estado distinto: so muda quando ESTE Vmid recebe a raiz no hardware. */
    uint64_t InstalledRootPhysical;
    BC250_VMID_POOL_INSTALL_STATE InstallState;
    BC250_GCVM_VMID8_STATUS LastInstallStatus;
    uint32_t Generation;         /* sobe a cada alocacao deste VMID */
} BC250_VMID_POOL_ENTRY;

/*
 * DX.10 -- RAIZ RETIDA PARA DIAGNOSTICO POST-MORTEM.
 *
 * Fica deliberadamente FORA de BC250_VMID_POOL_ENTRY: Release precisa limpar
 * toda autoridade da entrada antes de recicla-la, mas a caminhada do fault
 * acontece depois de DestroyContext e ainda precisa saber qual foi a ultima
 * raiz que o silicio aceitou naquele VMID. Esta retencao nunca participa de
 * submissao nem decide se uma instalacao pode ser pulada.
 */
typedef struct BC250_VMID_POOL_RETAINED_ROOT {
    uint64_t RootPhysical;
    BC250_MCDM_HANDLE Context;
    uint32_t Generation;
} BC250_VMID_POOL_RETAINED_ROOT;

typedef enum BC250_VMID_POOL_WALK_ROOT_SOURCE {
    BC250_VMID_POOL_WALK_ROOT_NONE = 0,
    BC250_VMID_POOL_WALK_ROOT_LIVE_READY,
    BC250_VMID_POOL_WALK_ROOT_RETAINED
} BC250_VMID_POOL_WALK_ROOT_SOURCE;

typedef struct BC250_VMID_POOL_WALK_ROOT {
    uint64_t RootPhysical;
    BC250_MCDM_HANDLE Context;
    uint32_t Generation;
    BC250_VMID_POOL_WALK_ROOT_SOURCE Source;
} BC250_VMID_POOL_WALK_ROOT;

typedef struct BC250_VMID_POOL {
    BC250_VMID_POOL_ENTRY Entries[BC250_VMID_COUNT];
    BC250_VMID_POOL_RETAINED_ROOT RetainedRoots[BC250_VMID_COUNT];
    uint32_t InUseMask;
    uint32_t AllocCount;
    uint32_t FreeCount;
    uint32_t ExhaustedCount;     /* quantas vezes faltou VMID -- nunca mascarar */
    uint32_t NextGeneration;
} BC250_VMID_POOL;

void Bc250VmidPoolInitialize(BC250_VMID_POOL *Pool);

/*
 * Devolve o VMID ja atribuido ao contexto, ou aloca um novo do pool.
 * Reentrante para o mesmo contexto: chamar duas vezes devolve o MESMO VMID.
 * Exaustao NAO cai de volta no VMID compartilhado -- devolve EXHAUSTED, e o
 * chamador tem de falhar fechado. Silenciar isso reintroduziria o defeito que
 * a faixa existe para consertar, so' que disfarcado de "modo degradado".
 */
BC250_VMID_POOL_STATUS Bc250VmidPoolAcquire(
    BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint32_t *Vmid,
    uint32_t *WasAlreadyAssigned);

BC250_VMID_POOL_STATUS Bc250VmidPoolRelease(
    BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint32_t *ReleasedVmid);

BC250_VMID_POOL_STATUS Bc250VmidPoolLookup(
    const BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint32_t *Vmid);

BC250_VMID_POOL_STATUS Bc250VmidPoolNoteRoot(
    BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint64_t RootPhysical);

/* Chamado sob VmidPoolLock; o hardware e tocado somente depois de soltar. */
BC250_VMID_POOL_STATUS Bc250VmidPoolBeginInstall(
    BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint32_t Vmid,
    uint64_t RootPhysical,
    uint32_t *NeedsHardwareInstall);

BC250_VMID_POOL_STATUS Bc250VmidPoolRecordInstall(
    BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint32_t Vmid,
    uint64_t RootPhysical,
    BC250_GCVM_VMID8_STATUS InstallStatus);

/*
 * Raiz REALMENTE instalada no silicio para um VMID, ou 0 se aquele slot nunca
 * chegou a READY. Existe para a caminhada do fault: a partir do momento em que
 * o pacote carrega o VMID do pool, caminhar a tabela do VMID 7 no endereco que
 * falhou leria a tabela ERRADA -- e "a PTE nao existe" viraria conclusao falsa.
 * Leitor puro; nao toca hardware e nao muda estado.
 */
uint64_t Bc250VmidPoolInstalledRootForVmid(
    const BC250_VMID_POOL *Pool,
    uint32_t Vmid);

/*
 * Fonte da caminhada de fault. Prefere a entrada viva READY; somente quando
 * ela nao tem autoridade usa a ultima instalacao comprovada retida. O chamador
 * deve serializar com Acquire/Release/RecordInstall.
 */
/*
 * DX.12 -- mascara dos VMIDs do pool que precisam de invalidacao quando o
 * VidMm publica uma tabela nova. Somente slots ocupados, READY e com raiz
 * realmente instalada. Os reservados 0/7/8/9 nunca aparecem aqui.
 *
 * Leitor PURO: nao toca hardware e nao toma lock. O chamador copia a mascara
 * sob o lock do pool e solta o lock ANTES do MMIO -- o flush por VMID pode
 * gastar centenas de polls em DISPATCH_LEVEL.
 */
uint32_t Bc250VmidPoolReadyFlushMask(const BC250_VMID_POOL *Pool);

BC250_VMID_POOL_STATUS Bc250VmidPoolRootForFaultWalk(
    const BC250_VMID_POOL *Pool,
    uint32_t Vmid,
    BC250_VMID_POOL_WALK_ROOT *WalkRoot);

uint32_t Bc250VmidIsReserved(uint32_t Vmid);

#ifdef __cplusplus
}
#endif

#endif
