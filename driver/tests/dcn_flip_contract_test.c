/* dcn_flip_contract_test.c -- DCN.1 (doc 710): contrato do flip pelo HUBP0.
 * Os valores do GOP sao os medidos na placa (Bateria A, corridas 250-268):
 * viewport 2560x1080, PITCH=2559, superficie em 0x460000000, OTG ligado. */
#include "../core/bc250_dcn_flip_contract.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);
#endif

/* Falha devolve a linha (o build-contract.bat reprova por exit code != 0). */
#define CHECK(cond, msg) do { if (!(cond)) { (void)(msg); return __LINE__; } } while (0)

static BC250_DCN_GOP_STATE MeasuredGop(void)
{
    BC250_DCN_GOP_STATE s;
    s.SurfaceConfig = 0x00000008u;
    s.ViewportDim = (1080u << 16) | 2560u;
    s.SurfacePitch = 2559u;
    s.PrimaryAddrLo = 0x60000000u;
    s.PrimaryAddrHi = 0x00000004u;
    s.SurfaceControl = 0u;
    s.FlipControl = 0u;
    s.OtgControl = 0x00010001u;
    s.MasterUpdateLock = 0u;
    return s;
}

static int Bc250RunDcnFlipContractTest(void)
{
    BC250_DCN_GOP_STATE s;
    BC250_DCN_FLIP_PLAN plan;
    uint32_t refuse;
    uint64_t addr = 0u;

    /* 1. GOP medido: elegivel, endereco 0x460000000, pitch 2560 px. */
    s = MeasuredGop();
    refuse = Bc250DcnFlipEvaluateGop(&s, 2560u, 1080u, 10240u, &plan);
    CHECK(refuse == 0u, "GOP medido deve ser elegivel");
    CHECK(plan.GopAddress == 0x460000000ull, "endereco do GOP 0x460000000");
    CHECK(plan.PitchPixels == 2560u, "pitch 2559+1");
    CHECK(plan.PixelFormat == 8u, "formato preservado");

    /* 2. Cada recusa isolada. */
    s = MeasuredGop(); s.OtgControl = 0u;
    CHECK(Bc250DcnFlipEvaluateGop(&s, 2560u, 1080u, 10240u, &plan) == BC250_DCN_FLIP_REFUSE_OTG_OFF, "OTG desligado recusa");
    s = MeasuredGop(); s.SurfacePitch = 2047u;
    CHECK(Bc250DcnFlipEvaluateGop(&s, 2560u, 1080u, 10240u, &plan) == BC250_DCN_FLIP_REFUSE_PITCH, "pitch diferente recusa");
    s = MeasuredGop(); s.ViewportDim = (720u << 16) | 1280u;
    CHECK(Bc250DcnFlipEvaluateGop(&s, 2560u, 1080u, 10240u, &plan) == BC250_DCN_FLIP_REFUSE_VIEWPORT, "viewport diferente recusa");
    s = MeasuredGop(); s.SurfaceControl = BC250_DCN_PRIMARY_SURFACE_DCC_EN_MASK;
    CHECK(Bc250DcnFlipEvaluateGop(&s, 2560u, 1080u, 10240u, &plan) == BC250_DCN_FLIP_REFUSE_DCC, "DCC recusa");
    s = MeasuredGop(); s.FlipControl = BC250_DCN_SURFACE_UPDATE_LOCK_MASK;
    CHECK(Bc250DcnFlipEvaluateGop(&s, 2560u, 1080u, 10240u, &plan) == BC250_DCN_FLIP_REFUSE_SURFACE_LOCK, "lock de superficie recusa");
    s = MeasuredGop(); s.MasterUpdateLock = 1u;
    CHECK(Bc250DcnFlipEvaluateGop(&s, 2560u, 1080u, 10240u, &plan) == BC250_DCN_FLIP_REFUSE_MASTER_LOCK, "master lock recusa");
    s = MeasuredGop(); s.PrimaryAddrLo = 0x60000080u;
    CHECK(Bc250DcnFlipEvaluateGop(&s, 2560u, 1080u, 10240u, &plan) == BC250_DCN_FLIP_REFUSE_GOP_ADDRESS, "GOP desalinhado recusa");
    s = MeasuredGop(); s.OtgControl = 0xFFFFFFFFu;
    CHECK(Bc250DcnFlipEvaluateGop(&s, 2560u, 1080u, 10240u, &plan) == BC250_DCN_FLIP_REFUSE_DEAD_BLOCK, "bloco morto recusa");

    /* 3. Endereco: a primaria do DWM medida em VramOff=0xABC000 (Present da 268). */
    CHECK(Bc250DcnFlipAddressForVramOffset(0x460000000ull, 0u, 0xABC000ull, 11059200u, 0x10000000u, &addr) == 1,
        "VramOff 0xABC000 valido");
    CHECK(addr == 0x460ABC000ull, "endereco = GOP + VramOff");
    CHECK(Bc250DcnFlipAddressForVramOffset(0x460000000ull, 0u, 0u, 11059200u, 0x10000000u, &addr) == 1 &&
        addr == 0x460000000ull, "offset 0 = o proprio GOP");
    /* fora da VRAM mapeada, desalinhado, estouro: falha fechada com saida zero */
    CHECK(Bc250DcnFlipAddressForVramOffset(0x460000000ull, 0u, 0x0FF00000ull, 11059200u, 0x10000000u, &addr) == 0 &&
        addr == 0u, "superficie passando do fim da VRAM recusa");
    CHECK(Bc250DcnFlipAddressForVramOffset(0x460000000ull, 0u, 0xABC040ull, 11059200u, 0x10000000u, &addr) == 0,
        "offset nao alinhado a 256 recusa");
    CHECK(Bc250DcnFlipAddressForVramOffset(0u, 0u, 0xABC000ull, 11059200u, 0x10000000u, &addr) == 0,
        "GOP zero recusa");
    CHECK(Bc250DcnFlipAddressForVramOffset(0x460000000ull, 0x10000ull, 0x20000ull, 4096u, 0x10000000u, &addr) == 1 &&
        addr == 0x460010000ull, "scanout fora do zero desloca a base");

    /* 4. FLIP_CONTROL: so' SURFACE_FLIP_TYPE muda (Lei 6). */
    CHECK(Bc250DcnFlipControlValue(0x00300000u, 0) == 0x00300000u, "vsync preserva o resto");
    CHECK(Bc250DcnFlipControlValue(0x00300000u, 1) == 0x00300002u, "imediato liga so' o bit 1");
    CHECK(Bc250DcnFlipControlValue(0x00300003u, 0) == 0x00300001u, "lock preservado, tipo zerado");

    /* 5. Pendencia (hubp2_is_flip_pending). */
    CHECK(Bc250DcnFlipIsPending(BC250_DCN_SURFACE_FLIP_PENDING_MASK, 0x60ABC000u, 4u, 0x460ABC000ull) == 1,
        "bit pendente = pendente");
    CHECK(Bc250DcnFlipIsPending(0u, 0x60000000u, 4u, 0x460ABC000ull) == 1,
        "EARLIEST_INUSE antigo = pendente");
    CHECK(Bc250DcnFlipIsPending(0u, 0x60ABC000u, 4u, 0x460ABC000ull) == 0,
        "latchado = nao pendente");

    /* 6. Contabilidade: cada flip em um balde so'. */
    CHECK(Bc250DcnFlipSupersededBetween(0u, 1u) == 0u, "primeiro flip latchado, nada substituido");
    CHECK(Bc250DcnFlipSupersededBetween(4u, 5u) == 0u, "flip seguinte latchado");
    CHECK(Bc250DcnFlipSupersededBetween(4u, 7u) == 2u, "5 e 6 trocados antes do latch");
    CHECK(Bc250DcnFlipSupersededBetween(7u, 7u) == 0u, "ja resolvido nao conta de novo");
    CHECK(Bc250DcnFlipSupersededBetween(9u, 7u) == 0u, "sequencia velha nao conta");
    {
        /* Simulacao: 10 flips, latches vistos em 1, 2, 5, 6, 9; pendentes 10. */
        const uint32_t seen[5] = { 1u, 2u, 5u, 6u, 9u };
        uint32_t resolved = 0u, latched = 0u, superseded = 0u, i;
        for (i = 0u; i < 5u; ++i) {
            superseded += Bc250DcnFlipSupersededBetween(resolved, seen[i]);
            resolved = seen[i];
            latched++;
        }
        CHECK(latched == 5u && superseded == 4u && (10u - resolved) == 1u, "baldes da simulacao");
        CHECK(latched + superseded + (10u - resolved) == 10u, "a conta fecha");
    }

    /* 7. Medicao da 270: quadros com volta, baldes e refresh. */
    CHECK(Bc250DcnFrameDelta(23789u, 26785u) == 2996u, "quadros sem volta (numeros da 269)");
    CHECK(Bc250DcnFrameDelta(0x00FFFFF0u, 0x00000010u) == 0x20u, "volta do contador de 24 bits");
    CHECK(Bc250DcnFrameDelta(5u, 5u) == 0u, "mesmo quadro");
    CHECK(Bc250DcnLatchFrameBucket(0u) == 0u && Bc250DcnLatchFrameBucket(1u) == 1u &&
          Bc250DcnLatchFrameBucket(2u) == 2u && Bc250DcnLatchFrameBucket(7u) == 3u, "balde em quadros");
    CHECK(Bc250DcnLatchUsBucket(8392u) == 0u && Bc250DcnLatchUsBucket(26787u) == 1u &&
          Bc250DcnLatchUsBucket(49013u) == 2u && Bc250DcnLatchUsBucket(50000u) == 3u, "balde em tempo (269)");
    CHECK(Bc250DcnRefreshMilliHz(605u, 10000000ull) == 60500u, "60,5 Hz medidos na 251b");
    CHECK(Bc250DcnRefreshMilliHz(10u, 0ull) == 0u, "sem tempo = 0");

    /* 8. Completar o flip pela interrupcao (doc 716/718): so' conta via IRQ
     * quando o sinalizador do ISR estava ligado E esta mesma chamada
     * resolveu um latch novo. As quatro combinacoes da tabela verdade. */
    CHECK(Bc250DcnFlipLatchIsViaIrq(0, 0) == 0, "sem flag e sem latch: nao e' via IRQ");
    CHECK(Bc250DcnFlipLatchIsViaIrq(1, 0) == 0, "flag ligada mas sem latch novo: nao conta");
    CHECK(Bc250DcnFlipLatchIsViaIrq(0, 1) == 0, "latch novo so' pelo tique de 4 ms: nao e' via IRQ");
    CHECK(Bc250DcnFlipLatchIsViaIrq(1, 1) == 1, "flag ligada e latch novo na mesma chamada: via IRQ");
    CHECK(Bc250DcnFlipLatchIsViaIrq(-1, 1) == 1, "qualquer valor nao-zero da flag conta como ligada");

    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunDcnFlipContractTest());
}
#else
int main(void) { return Bc250RunDcnFlipContractTest(); }
#endif
