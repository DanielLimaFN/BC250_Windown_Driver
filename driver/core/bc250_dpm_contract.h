#ifndef BC250_DPM_CONTRACT_H
#define BC250_DPM_CONTRACT_H

/*
 * C08 / DPM.1 (doc 765): governador de clock do GFX pela carga, com teto
 * termico. Politica pura (sem kernel, sem registrador), testada offline por
 * driver/tests/dpm_contract_test.c. Quem chama mede a carga e o Tctl e aplica
 * o clock pela SMU (RequestGfxclk + UnforceGfxVid, as mensagens da CLK.1).
 *
 * Procedencia das regras (P1 de desenho, nao de silicio nosso): o governador
 * do Nagi (referencias/amdgpu-wddm docs/design/dpm.md, governor e "thermal"):
 *   - sobe quando a janela fica >= 900 permille ocupada, direto ao nivel em que
 *     o mesmo trabalho encheria 80% (clock * ocupacao / 800), no minimo um nivel;
 *   - desce um nivel quando a media (3/4 do valor velho) fica < 650 permille por
 *     200 ms;
 *   - Tctl >= 87 C: teto um nivel abaixo do clock e mais um a cada 500 ms quente,
 *     sem subir; >= 90 C: teto no piso; < 82 C: teto sobe um nivel por segundo;
 *   - sem leitura de Tctl: teto no maximo em 1500 MHz (o ponto do firmware).
 * Diferencas declaradas contra o Nagi: piso 1000 MHz (o minimo do amdgpu; nada
 * abaixo, nunca medido por nos), tensao escolhida pelo firmware (UnforceGfxVid,
 * medido em 1800 MHz na Corrida 302) em vez da tabela V/F com ForceGfxVid, e a
 * carga vem da nossa contabilidade de submissoes, nao de leitura de GRBM a 1 kHz.
 */

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

#define BC250_DPM_MIN_MHZ              1000u   /* CYAN_SKILLFISH_SCLK_MIN do amdgpu */
#define BC250_DPM_HARD_MAX_MHZ         2000u   /* CYAN_SKILLFISH_SCLK_MAX do amdgpu */
#define BC250_DPM_FIRMWARE_MHZ         1500u   /* ponto do firmware (Corridas 300/302) */
#define BC250_DPM_STEP_MHZ             100u
#define BC250_DPM_UP_PERMILLE          900u
#define BC250_DPM_TARGET_PERMILLE      800u
#define BC250_DPM_DOWN_PERMILLE        650u
#define BC250_DPM_DOWN_HOLD_MS         200u
#define BC250_DPM_HOT_MILLIC           87000u
#define BC250_DPM_CRITICAL_MILLIC      90000u
#define BC250_DPM_COOL_MILLIC          82000u
#define BC250_DPM_HOT_STEP_MS          500u
#define BC250_DPM_COOL_STEP_MS         1000u

typedef struct BC250_DPM_STATE {
    uint32_t MaxMhz;        /* teto configurado (DpmMaxMhz), 1000..2000 */
    uint32_t ClockMhz;      /* clock em vigor */
    uint32_t CapMhz;        /* teto termico em vigor */
    uint32_t AvgPermille;   /* media com 3/4 de peso no valor velho */
    uint32_t LowMs;         /* tempo com a media abaixo de DOWN */
    uint32_t HotMs;         /* tempo quente desde o ultimo degrau */
    uint32_t CoolMs;        /* tempo frio desde o ultimo degrau */
    uint32_t HotEpisode;    /* 1 = ja deu o primeiro degrau deste episodio quente */
} BC250_DPM_STATE;

static __inline uint32_t Bc250DpmClamp(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static __inline void Bc250DpmInit(BC250_DPM_STATE *S, uint32_t MaxMhz, uint32_t StartMhz)
{
    if (S == 0) return;
    S->MaxMhz = Bc250DpmClamp(MaxMhz, BC250_DPM_MIN_MHZ, BC250_DPM_HARD_MAX_MHZ);
    S->MaxMhz -= S->MaxMhz % BC250_DPM_STEP_MHZ;
    S->ClockMhz = Bc250DpmClamp(StartMhz - StartMhz % BC250_DPM_STEP_MHZ, BC250_DPM_MIN_MHZ, S->MaxMhz);
    S->CapMhz = S->MaxMhz;
    S->AvgPermille = 0u;
    S->LowMs = 0u;
    S->HotMs = 0u;
    S->CoolMs = 0u;
    S->HotEpisode = 0u;
}

/*
 * Um passo da politica. BusyPermille: ocupacao da janela que acabou (0..1000).
 * TctlMilliC: 0 = sem leitura valida. DtMs: duracao da janela. Devolve o clock
 * pedido (multiplo de 100 MHz, em [MIN, min(MaxMhz, CapMhz)]); nao muda
 * ClockMhz -- quem chama confirma pela SMU e chama Bc250DpmCommit.
 */
static __inline uint32_t Bc250DpmStep(BC250_DPM_STATE *S, uint32_t BusyPermille,
    uint32_t TctlMilliC, uint32_t DtMs)
{
    uint32_t want, top;
    if (S == 0) return BC250_DPM_FIRMWARE_MHZ;
    if (BusyPermille > 1000u) BusyPermille = 1000u;
    S->AvgPermille = (S->AvgPermille * 3u + BusyPermille) / 4u;

    /* Teto termico. */
    if (TctlMilliC == 0u) {
        if (S->CapMhz > BC250_DPM_FIRMWARE_MHZ) S->CapMhz = BC250_DPM_FIRMWARE_MHZ;
        S->HotMs = 0u; S->CoolMs = 0u;
    } else if (TctlMilliC >= BC250_DPM_CRITICAL_MILLIC) {
        S->CapMhz = BC250_DPM_MIN_MHZ;
        S->HotEpisode = 1u; S->HotMs = 0u; S->CoolMs = 0u;
    } else if (TctlMilliC >= BC250_DPM_HOT_MILLIC) {
        S->CoolMs = 0u;
        S->HotMs += DtMs;
        if (S->HotEpisode == 0u || S->HotMs >= BC250_DPM_HOT_STEP_MS) {
            uint32_t base = S->CapMhz < S->ClockMhz ? S->CapMhz : S->ClockMhz;
            S->CapMhz = base > BC250_DPM_MIN_MHZ + BC250_DPM_STEP_MHZ ?
                base - BC250_DPM_STEP_MHZ : BC250_DPM_MIN_MHZ;
            S->HotEpisode = 1u; S->HotMs = 0u;
        }
    } else {
        S->HotMs = 0u;
        if (TctlMilliC < BC250_DPM_COOL_MILLIC) {
            S->HotEpisode = 0u;
            S->CoolMs += DtMs;
            if (S->CapMhz < S->MaxMhz && S->CoolMs >= BC250_DPM_COOL_STEP_MS) {
                S->CapMhz += BC250_DPM_STEP_MHZ;
                S->CoolMs = 0u;
            }
        } else {
            S->CoolMs = 0u;   /* 82..87 C: teto parado */
        }
    }
    if (S->CapMhz > S->MaxMhz) S->CapMhz = S->MaxMhz;

    /* Carga. */
    want = S->ClockMhz;
    if (BusyPermille >= BC250_DPM_UP_PERMILLE) {
        uint32_t need = (S->ClockMhz * BusyPermille + BC250_DPM_TARGET_PERMILLE - 1u) / BC250_DPM_TARGET_PERMILLE;
        need = ((need + BC250_DPM_STEP_MHZ - 1u) / BC250_DPM_STEP_MHZ) * BC250_DPM_STEP_MHZ;
        want = need > S->ClockMhz + BC250_DPM_STEP_MHZ ? need : S->ClockMhz + BC250_DPM_STEP_MHZ;
        S->LowMs = 0u;
    } else if (S->AvgPermille < BC250_DPM_DOWN_PERMILLE) {
        S->LowMs += DtMs;
        if (S->LowMs >= BC250_DPM_DOWN_HOLD_MS) {
            want = S->ClockMhz > BC250_DPM_MIN_MHZ ? S->ClockMhz - BC250_DPM_STEP_MHZ : BC250_DPM_MIN_MHZ;
            S->LowMs = 0u;
        }
    } else {
        S->LowMs = 0u;
    }
    /* Quente: nada sobe (descer continua permitido). */
    if (TctlMilliC >= BC250_DPM_HOT_MILLIC && want > S->ClockMhz) want = S->ClockMhz;
    top = S->CapMhz < S->MaxMhz ? S->CapMhz : S->MaxMhz;
    return Bc250DpmClamp(want, BC250_DPM_MIN_MHZ, top);
}

/* O clock pedido foi confirmado pela SMU. */
static __inline void Bc250DpmCommit(BC250_DPM_STATE *S, uint32_t Mhz)
{
    if (S != 0) S->ClockMhz = Mhz;
}

#endif
