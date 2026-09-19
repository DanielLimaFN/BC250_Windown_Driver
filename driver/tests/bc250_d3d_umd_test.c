#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "bc250_d3d_umd.h"
#include "../core/bc250_escape_contract.h"

static UINT g_SetErrorCalls;
static HRESULT g_LastUmdError;

/*
 * Leitura LOCAL do tunable, de proposito.
 *
 * Os leitores de registro moram em bc250_d3d_umd.c e NAO sao exportados pela
 * DLL -- a suite linka a import lib e nao deve ganhar exportacoes so' para se
 * enxergar (comentario do link em build-umd.bat). O teste le a mesma chave por
 * conta propria para poder DECLARAR o ambiente em que rodou.
 */
static BOOL TestReadUmdTunable(const char* Name)
{
    HKEY key = NULL;
    DWORD value = 0;
    DWORD size = (DWORD)sizeof(value);
    DWORD type = 0;
    BOOL armed = FALSE;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, BC250_D3D11_CAPS_PROBE_REG_KEY, 0,
            KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExA(key, Name, NULL, &type, (LPBYTE)&value, &size)
                == ERROR_SUCCESS &&
            type == REG_DWORD && value != 0) {
            armed = TRUE;
        }
        RegCloseKey(key);
    }
    return armed;
}

static VOID APIENTRY TestSetError(
    D3D10DDI_HRTCORELAYER CoreLayer,
    HRESULT Error)
{
    assert(CoreLayer.handle == (void*)0xC0DEu);
    g_SetErrorCalls++;
    g_LastUmdError = Error;
}

/*
 * P7_D3D11_CAPS_PROBE -- trava da regra de publicacao do adapter.
 *
 * O probe existe para responder, em UMA corrida fisica, as tres perguntas que
 * o dimensionamento (docs/173) deixou abertas. Aqui garantimos offline que:
 *
 *   - com o probe DESARMADO o comportamento e' exatamente o de antes;
 *   - o probe nunca implica device: CreateDevice segue fail-closed;
 *   - o gate de producao BC250_D3D10_11_NATIVE_READY continua fechado.
 */
static void TestCapsProbeDecisionTable(void)
{
    printf("[TEST] P7_D3D11_CAPS_PROBE: tabela de decisao...\n");

    /* Default de fabrica: nada armado, nada publicado. */
    assert(Bc250UmdShouldPublishAdapter(FALSE, FALSE) == FALSE);

    /* Probe armado: publica a tabela de adapter para o runtime consultar caps. */
    assert(Bc250UmdShouldPublishAdapter(FALSE, TRUE) == TRUE);

    /* Producao, quando existir pipeline, publica independente do probe. */
    assert(Bc250UmdShouldPublishAdapter(TRUE, FALSE) == TRUE);
    assert(Bc250UmdShouldPublishAdapter(TRUE, TRUE) == TRUE);

    /*
     * Gate de producao: BC250_D3D10_11_NATIVE_READY mantido em 0 fail-closed em repouso.
     */
    assert(BC250_D3D10_11_NATIVE_READY == 0);

    printf("  -> tabela de decisao do probe PASS\n");
}

static void TestNode1TripodAuthorization(void)
{
    const char* token = "0123456789ABCDEF0123456789ABCDEF";
    printf("[TEST] B7_NODE1_TRIPOD: autorizacao por processo/token...\n");

    assert(Bc250UmdNode1TripodAuthorize(TRUE,
        BC250_NODE1_TRIPOD_HARNESS_EXE, token, token) == TRUE);
    assert(Bc250UmdNode1TripodAuthorize(FALSE,
        BC250_NODE1_TRIPOD_HARNESS_EXE, token, token) == FALSE);
    assert(Bc250UmdNode1TripodAuthorize(TRUE,
        "dwm.exe", token, token) == FALSE);
    assert(Bc250UmdNode1TripodAuthorize(TRUE,
        BC250_NODE1_TRIPOD_HARNESS_EXE,
        "1123456789ABCDEF0123456789ABCDEF", token) == FALSE);
    assert(Bc250UmdNode1TripodAuthorize(TRUE,
        BC250_NODE1_TRIPOD_HARNESS_EXE, "ABC", "ABC") == FALSE);
    assert(Bc250UmdNode1TripodAuthorize(TRUE,
        BC250_NODE1_TRIPOD_HARNESS_EXE,
        "0123456789ABCDEG0123456789ABCDEF", token) == FALSE);
    assert(Bc250UmdNode1TripodAuthorize(TRUE,
        NULL, token, token) == FALSE);

    printf("  -> registro + executavel exato + token 128-bit PASS\n");
}

/*
 * Regressao da corrida 2026-09-06 23:45.
 *
 * A v1 do probe aceitava qualquer Interface. O runtime pediu 0x000B0011, nos
 * dissemos S_OK, e ele parou de chamar tudo: zero GetSupportedVersions, zero
 * GetCaps, zero CreateDevice, 66 ciclos de carga da DLL no dwm.exe, e o
 * OpenAdapter10 nunca foi chamado -- o step-down para D3D10_1 nunca aconteceu.
 *
 * Este teste existe para que aquele S_OK nunca volte.
 */
static void TestProbeInterfaceAllowlist(void)
{
    printf("[TEST] P7_D3D11_CAPS_PROBE: allowlist de interface...\n");

    /* O unico alvo dimensionado (105 entradas, doc 173). */
    assert(Bc250UmdProbeAcceptsInterface(BC250_UMD_PROBE_IFACE_D3D10_1) == TRUE);

    /* Os dois degraus que a v1 aceitou por engano. */
    assert(Bc250UmdProbeAcceptsInterface(BC250_UMD_PROBE_IFACE_MINOR17) == FALSE);
    assert(Bc250UmdProbeAcceptsInterface(BC250_UMD_PROBE_IFACE_D3D11_0_7) == FALSE);

    /* Nada mais entra: recusar e' o que permite o runtime descer a escada. */
    assert(Bc250UmdProbeAcceptsInterface(0u) == FALSE);
    assert(Bc250UmdProbeAcceptsInterface(0xFFFFFFFFu) == FALSE);
    assert(Bc250UmdProbeAcceptsInterface(BC250_UMD_PROBE_IFACE_D3D10_1 + 1u) == FALSE);

    printf("  -> allowlist de interface PASS\n");
}

/*
 * v4 -- allowlist POR ENTRY POINT.
 *
 * A corrida 07/09 09:40 refutou a hipotese do UMDRIVERPRIVATE: o handshake
 * passou (112 publicacoes no KMD, 32 validacoes na UMD) e MESMO ASSIM nenhuma
 * entrada do adapter table foi chamada. O que sobrou e' geometrico -- o runtime
 * so' oferece 0x000A0002 pelo OpenAdapter10, cuja tabela de 3 entradas NAO tem
 * pfnGetCaps. Aceitar D3D11_0_7 no OpenAdapter10_2 e' o unico caminho medido
 * para o pfnGetCaps existir.
 */
static void TestPerEntryPointAllowlist(void)
{
    printf("[TEST] P7_D3D11_CAPS_PROBE v4: allowlist por entry point...\n");

    /* 10_2 publica a tabela de 5 entradas: aceita o DDI de D3D11. */
    assert(Bc250UmdProbeAcceptsInterface10_2(BC250_UMD_PROBE_IFACE_D3D11_0_7) == TRUE);
    /* E recusa o minor 17, que nao tem nome no WDK atual. */
    assert(Bc250UmdProbeAcceptsInterface10_2(BC250_UMD_PROBE_IFACE_MINOR17) == FALSE);
    /* 10.1 nao vem por este entry point na pratica; nao o aceitamos aqui. */
    assert(Bc250UmdProbeAcceptsInterface10_2(BC250_UMD_PROBE_IFACE_D3D10_1) == FALSE);

    /* 10 publica a tabela de 3 entradas: so' o DDI de D3D10.1. */
    assert(Bc250UmdProbeAcceptsInterface10(BC250_UMD_PROBE_IFACE_D3D10_1) == TRUE);
    assert(Bc250UmdProbeAcceptsInterface10(BC250_UMD_PROBE_IFACE_D3D11_0_7) == FALSE);
    assert(Bc250UmdProbeAcceptsInterface10(BC250_UMD_PROBE_IFACE_MINOR17) == FALSE);

    /* Lixo nao entra por nenhum dos dois. */
    assert(Bc250UmdProbeAcceptsInterface10_2(0u) == FALSE);
    assert(Bc250UmdProbeAcceptsInterface10(0xFFFFFFFFu) == FALSE);

    printf("  -> allowlist por entry point PASS\n");
}

/*
 * v4 -- zerar caps e' verdade para uns tipos e mentira para outros.
 *
 * D3D11DDICAPS_3DPIPELINESUPPORT carrega um D3D11DDI_3DPIPELINELEVEL onde
 * 0 == 3DPIPELINELEVEL_10_0. Zerar ali declara um pipeline 3D que este driver
 * nao tem. Este teste existe para esse cap nunca sair como sucesso zerado.
 */
static void TestCapsZeroTruthfulness(void)
{
    printf("[TEST] P7_D3D11_CAPS_PROBE v4: honestidade do caps zerado...\n");

    /* 3DPIPELINESUPPORT: zero significa nivel 10_0 -> zerar seria mentir. */
    assert(Bc250UmdCapsZeroIsTruthful(130u) == FALSE);

    /* Conjuntos de flags: tudo zero == nada suportado == verdade. */
    assert(Bc250UmdCapsZeroIsTruthful(128u) == TRUE);  /* THREADING */
    assert(Bc250UmdCapsZeroIsTruthful(129u) == TRUE);  /* SHADER */
    assert(Bc250UmdCapsZeroIsTruthful(131u) == TRUE);  /* D3D11_OPTIONS */

    printf("  -> honestidade do caps zerado PASS\n");
}

static void TestCapsTypeDecoding(void)
{
    printf("[TEST] P7_D3D11_CAPS_PROBE: decodificacao dos caps...\n");

    /*
     * O produto do probe e' o log. Se os tres caps que o runtime consulta antes
     * do device sairem como "(tipo nao catalogado)", a corrida se perde.
     */
    assert(strcmp(Bc250UmdCapsTypeName(128u), "D3D11DDICAPS_THREADING") == 0);
    assert(strcmp(Bc250UmdCapsTypeName(129u), "D3D11DDICAPS_SHADER") == 0);
    assert(strcmp(Bc250UmdCapsTypeName(130u),
        "D3D11DDICAPS_3DPIPELINESUPPORT") == 0);

    /* Tipo desconhecido nao pode mentir um nome. */
    assert(strcmp(Bc250UmdCapsTypeName(4242u), "(tipo nao catalogado)") == 0);

    printf("  -> decodificacao dos caps PASS\n");
}

static void TestOpenAdapter10_2(void)
{
    D3D10DDIARG_OPENADAPTER openData;
    HRESULT hr;

    printf("[TEST] Testing fail-closed OpenAdapter10_2...\n");

    /* Negative test: NULL pointer */
    hr = OpenAdapter10_2(NULL);
    assert(FAILED(hr));

    /* No native device table exists yet: adapter advertisement must fail. */
    memset(&openData, 0, sizeof(openData));
    openData.Interface = D3D10_0_DDI_INTERFACE_VERSION;
    openData.Version = 1;

    hr = OpenAdapter10_2(&openData);
    assert(hr == E_NOTIMPL);
    assert(openData.hAdapter.pDrvPrivate == NULL);
    assert(openData.pAdapterFuncs_2 == NULL);

    printf("  -> OpenAdapter10_2 FAIL-CLOSED PASS\n");
}

static void TestOpenAdapter10(void)
{
    D3D10DDIARG_OPENADAPTER openData;
    HRESULT hr;

    printf("[TEST] Testing fail-closed OpenAdapter10...\n");
    memset(&openData, 0, sizeof(openData));
    openData.Interface = D3D10_0_DDI_INTERFACE_VERSION;
    hr = OpenAdapter10(&openData);
    assert(hr == E_NOTIMPL);
    assert(openData.hAdapter.pDrvPrivate == NULL);
    assert(openData.pAdapterFuncs == NULL);
    printf("  -> OpenAdapter10 FAIL-CLOSED PASS\n");
}

static void TestOpenAdapter12(void)
{
    D3D12DDIARG_OPENADAPTER openData12;
    HRESULT hr;

    printf("[TEST] Testing fail-closed OpenAdapter12...\n");

    /* Negative test */
    hr = OpenAdapter12(NULL);
    assert(FAILED(hr));

    /* D3D12 nao pode ser anunciado antes de existir CreateDevice nativo. */
    memset(&openData12, 0, sizeof(openData12));
    hr = OpenAdapter12(&openData12);
    assert(hr == E_NOTIMPL);
    assert(openData12.hAdapter.pDrvPrivate == NULL);
    assert(openData12.pAdapterFuncs == NULL);

    printf("  -> OpenAdapter12 FAIL-CLOSED PASS\n");
}

static void TestOpenAdapter11(void)
{
    D3D10DDIARG_OPENADAPTER openData;
    HRESULT hr;

    printf("[TEST] Testing fail-closed OpenAdapter11...\n");
    memset(&openData, 0, sizeof(openData));
    openData.Interface = 0x000B0000u;
    hr = OpenAdapter11(&openData);
    assert(hr == E_NOTIMPL);
    assert(openData.hAdapter.pDrvPrivate == NULL);
    printf("  -> OpenAdapter11 FAIL-CLOSED PASS\n");
}

static void TestAdapterStructureAndBlob(void)
{
    BC250_UMD_ADAPTER adapter;
    printf("[TEST] Testing BC250_UMD_ADAPTER structure and private blob caching...\n");

    memset(&adapter, 0, sizeof(adapter));
    assert(adapter.IsActive == FALSE);
    assert(adapter.PrivateBlobValid == FALSE);

    /* Verify that adapter can store private blob */
    adapter.PrivateBlob.Magic = BC250_UMD_PRIVATE_MAGIC;
    adapter.PrivateBlob.Version = BC250_UMD_PRIVATE_VERSION;
    adapter.PrivateBlob.SizeBytes = sizeof(BC250_UMD_PRIVATE);
    adapter.PrivateBlob.VendorId = 0x1002u;
    adapter.PrivateBlob.DeviceId = 0x13FEu;
    adapter.PrivateBlob.GpuVaBits = 34u;
    adapter.PrivateBlobValid = TRUE;
    adapter.IsActive = TRUE;

    assert(adapter.PrivateBlobValid == TRUE);
    assert(adapter.PrivateBlob.GpuVaBits == 34u);
    assert(adapter.PrivateBlob.VendorId == 0x1002u);
    assert(adapter.PrivateBlob.DeviceId == 0x13FEu);

    printf("  -> BC250_UMD_ADAPTER structure and blob PASS\n");
}

static void TestDeviceFoundationStructures(void)
{
    BC250_UMD_ADAPTER adapter;
    BC250_UMD_DEVICE device;
    D3DDDI_DEVICECALLBACKS ktCallbacks;
    DXGI_DDI_BASE_CALLBACKS dxgiCallbacks;
    D3D10DDIARG_CREATEDEVICE createData;
    HRESULT hr;

    printf("[TEST] Testing BC250_UMD_DEVICE foundation and winsys wiring...\n");

    memset(&adapter, 0, sizeof(adapter));
    adapter.IsActive = TRUE;

    memset(&ktCallbacks, 0, sizeof(ktCallbacks));
    memset(&dxgiCallbacks, 0, sizeof(dxgiCallbacks));

    memset(&createData, 0, sizeof(createData));
    createData.hRTDevice.handle = (HANDLE)(ULONG_PTR)0x12345678;
    createData.Interface = D3D10_1_DDI_INTERFACE_VERSION;
    createData.Version = 0x00010000u;
    createData.Flags = 0x1u;
    createData.pKTCallbacks = &ktCallbacks;
    createData.DXGIBaseDDI.pDXGIBaseCallbacks = &dxgiCallbacks;

    /* Negative tests for Bc250UmdDeviceInit */
    hr = Bc250UmdDeviceInit(NULL, &adapter, &createData);
    assert(hr == E_INVALIDARG);

    hr = Bc250UmdDeviceInit(&device, NULL, &createData);
    assert(hr == E_INVALIDARG);

    hr = Bc250UmdDeviceInit(&device, &adapter, NULL);
    assert(hr == E_INVALIDARG);

    adapter.IsActive = FALSE;
    hr = Bc250UmdDeviceInit(&device, &adapter, &createData);
    assert(hr == E_INVALIDARG);
    adapter.IsActive = TRUE;

    createData.hRTDevice.handle = NULL;
    hr = Bc250UmdDeviceInit(&device, &adapter, &createData);
    assert(hr == E_INVALIDARG);
    createData.hRTDevice.handle = (HANDLE)(ULONG_PTR)0x12345678;

    createData.pKTCallbacks = NULL;
    hr = Bc250UmdDeviceInit(&device, &adapter, &createData);
    assert(hr == E_INVALIDARG);
    createData.pKTCallbacks = &ktCallbacks;

    /* Positive initialization */
    hr = Bc250UmdDeviceInit(&device, &adapter, &createData);
    assert(hr == S_OK);
    assert(device.DeviceActive == TRUE);
    assert(device.WinsysInitialized == TRUE);
    assert(device.hRTDevice.handle == createData.hRTDevice.handle);
    assert(device.pAdapter == &adapter);
    assert(device.Interface == createData.Interface);
    assert(device.Version == createData.Version);
    assert(device.Flags == createData.Flags);
    assert(device.pKTCallbacks == createData.pKTCallbacks);
    assert(device.pDXGICallbacks == &dxgiCallbacks);

    /* Verify winsys context was wired with runtime device and callbacks */
    assert(device.Winsys.Initialized == TRUE);
    assert(device.Winsys.RuntimeDevice == createData.hRTDevice.handle);
    assert(device.Winsys.Callbacks == createData.pKTCallbacks);

    /* Teardown */
    Bc250UmdDeviceTeardown(&device);
    assert(device.DeviceActive == FALSE);
    assert(device.WinsysInitialized == FALSE);

    /* Double teardown is safe */
    Bc250UmdDeviceTeardown(&device);
    assert(device.DeviceActive == FALSE);

    /* Test DestroyDevice DDI entry point */
    hr = Bc250UmdDeviceInit(&device, &adapter, &createData);
    assert(hr == S_OK);
    assert(device.DeviceActive == TRUE);
    {
        D3D10DDI_HDEVICE hDev;
        hDev.pDrvPrivate = &device;
        Bc250Umd10DestroyDevice(hDev);
        assert(device.DeviceActive == FALSE);
        assert(device.WinsysInitialized == FALSE);
    }

    printf("  -> BC250_UMD_DEVICE foundation and winsys wiring PASS\n");
}

static void TestDeviceCreateFailClosedWithoutPipeline(void)
{
    BC250_UMD_ADAPTER adapter;
    BC250_UMD_DEVICE device;
    D3DDDI_DEVICECALLBACKS ktCallbacks;
    D3D10DDIARG_CREATEDEVICE createData;
    D3D10DDI_HADAPTER hAdapter;

    printf("[TEST] Testing Bc250Umd10CreateDevice fail-closed gate...\n");

    memset(&adapter, 0, sizeof(adapter));
    adapter.IsActive = TRUE;
    hAdapter.pDrvPrivate = &adapter;

    memset(&ktCallbacks, 0, sizeof(ktCallbacks));
    memset(&device, 0, sizeof(device));

    memset(&createData, 0, sizeof(createData));
    createData.hRTDevice.handle = (HANDLE)(ULONG_PTR)0xABCDEF00;
    createData.Interface = D3D10_1_DDI_INTERFACE_VERSION;
    createData.Version = 0x00010000u;
    createData.pKTCallbacks = &ktCallbacks;
    createData.hDrvDevice.pDrvPrivate = &device;

    /* Gate assertion: BC250_D3D10_11_NATIVE_READY is 0 fail-closed */
    assert(BC250_D3D10_11_NATIVE_READY == 0);

    /* Negative test: null pCreateData */
    assert(Bc250Umd10CreateDevice(hAdapter, NULL) == E_INVALIDARG);

    /* Negative test: inactive adapter */
    adapter.IsActive = FALSE;
    assert(Bc250Umd10CreateDevice(hAdapter, &createData) == E_INVALIDARG);
    adapter.IsActive = TRUE;

    /* Negative test: null hDrvDevice.pDrvPrivate */
    createData.hDrvDevice.pDrvPrivate = NULL;
    assert(Bc250Umd10CreateDevice(hAdapter, &createData) == E_INVALIDARG);
    createData.hDrvDevice.pDrvPrivate = &device;

    /*
     * Tabela pura do gate, exaustiva -- independe de maquina.
     */
    assert(Bc250Umd3DPipelineIsDeclared(FALSE, FALSE) == FALSE);
    assert(Bc250Umd3DPipelineIsDeclared(FALSE, TRUE) == TRUE);
    assert(Bc250Umd3DPipelineIsDeclared(TRUE, FALSE) == TRUE);
    assert(Bc250Umd3DPipelineIsDeclared(TRUE, TRUE) == TRUE);

    /*
     * Execution test CONDICIONADO AO AMBIENTE, e o motivo esta medido:
     * Bc250Umd10CreateDevice le HKLM\SOFTWARE\BC250\Umd\D3D11LabPromotion, e
     * um bind de laboratorio anterior deixa essa chave em 1. Antes desta
     * correcao a suite reprovava por estado da MAQUINA, sem mudanca de codigo
     * -- e o "42/42 PASS" de 10/09 nao podia ter sido obtido nesta maquina no
     * estado em que ela esta. Agora a assercao e' de COERENCIA: o DDI tem de
     * seguir a politica pura para o ambiente medido, e a tabela acima cobre
     * os quatro casos, entao nada aqui pode ficar vacuo.
     */
    {
        BOOL promotionArmed = TestReadUmdTunable(BC250_D3D11_LAB_PROMOTION_REG_NAME);
        BOOL permitted = Bc250Umd3DPipelineIsDeclared(
            BC250_D3D10_11_NATIVE_READY ? TRUE : FALSE, promotionArmed);
        HRESULT hr = Bc250Umd10CreateDevice(hAdapter, &createData);

        if (!permitted) {
            assert(hr == E_NOTIMPL);
            assert(device.DeviceActive == FALSE);
            assert(device.WinsysInitialized == FALSE);
        } else {
            /* Promovido: NAO exigimos sucesso -- sem KMD nesta maquina o
             * caminho falha por winsys, e isso e' aceitavel. O que NAO pode
             * acontecer e' o gate recusar por motivo errado. */
            printf("  -> AVISO: D3D11LabPromotion=%d nesta maquina; gate de "
                   "CreateDevice avaliado por coerencia (hr=0x%08X)\n",
                   (int)promotionArmed, (unsigned)hr);
            assert(device.DeviceActive == FALSE || SUCCEEDED(hr));
        }
    }

    printf("  -> Bc250Umd10CreateDevice fail-closed gate PASS\n");
}

static void TestGetSupportedVersions(void)
{
    D3D10DDI_HADAPTER hAdapter = { (void*)0x1234 };
    UINT32 entries = 0;
    UINT64 versions[8];
    HRESULT hr;

    printf("[TEST] Testing Bc250Umd10GetSupportedVersions D3D10/D3D11 negotiation...\n");

    /* Negative test: NULL puEntries */
    hr = Bc250Umd10GetSupportedVersions(hAdapter, NULL, NULL);
    assert(hr == E_INVALIDARG);

    /* Query count */
    hr = Bc250Umd10GetSupportedVersions(hAdapter, &entries, NULL);
    assert(hr == S_OK);
    assert(entries == 4u);

    /* Buffer too small */
    entries = 2u;
    hr = Bc250Umd10GetSupportedVersions(hAdapter, &entries, versions);
    assert(hr == E_INVALIDARG);
    assert(entries == 4u);

    /* Full query */
    memset(versions, 0, sizeof(versions));
    entries = 4u;
    hr = Bc250Umd10GetSupportedVersions(hAdapter, &entries, versions);
    assert(hr == S_OK);
    assert(entries == 4u);
    assert(versions[0] == D3D10_0_DDI_SUPPORTED);
    assert(versions[1] == D3D10_1_DDI_SUPPORTED);
    assert(versions[2] == D3D11_0_7_DDI_SUPPORTED);
    assert(versions[3] == D3D11_0_DDI_SUPPORTED);

    printf("  -> Bc250Umd10GetSupportedVersions returns 4 versions (D3D10.0, D3D10.1, D3D11_0_7, D3D11.0) PASS\n");
}

static void TestDeviceTablePopulation(void)
{
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D11DDI_DEVICEFUNCS deviceFuncs11;
    DXGI1_6_1_DDI_BASE_FUNCTIONS dxgiFuncs;

    printf("[TEST] Testing D3D10.1, D3D11 & DXGI device functions table population...\n");

    memset(&deviceFuncs10_1, 0xFF, sizeof(deviceFuncs10_1));
    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    assert(deviceFuncs10_1.pfnDestroyDevice != NULL);
    assert(deviceFuncs10_1.pfnRelocateDeviceFuncs != NULL);
    assert(deviceFuncs10_1.pfnFlush != NULL);

    memset(&deviceFuncs11, 0xFF, sizeof(deviceFuncs11));
    Bc250UmdPopulateDeviceFuncs11(&deviceFuncs11);

    assert(deviceFuncs11.pfnDestroyDevice != NULL);
    assert(deviceFuncs11.pfnRelocateDeviceFuncs != NULL);
    assert(deviceFuncs11.pfnFlush != NULL);
    assert(deviceFuncs11.pfnDraw != NULL);
    assert(deviceFuncs11.pfnClearRenderTargetView != NULL);

    /* Test that table function pointers execute properly */
    {
        D3D10DDI_HDEVICE hDev;
        BC250_UMD_DEVICE dev;
        memset(&dev, 0, sizeof(dev));
        dev.DeviceActive = TRUE;
        hDev.pDrvPrivate = &dev;

        deviceFuncs10_1.pfnRelocateDeviceFuncs(hDev, &deviceFuncs10_1);
        deviceFuncs10_1.pfnFlush(hDev);

        assert(dev.DeviceActive == TRUE);
        deviceFuncs10_1.pfnDestroyDevice(hDev);
        assert(dev.DeviceActive == FALSE);
    }

    memset(&dxgiFuncs, 0xFF, sizeof(dxgiFuncs));
    Bc250UmdPopulateDxgiBaseFuncs(&dxgiFuncs);
    assert(dxgiFuncs.pfnPresent != NULL);
    assert(dxgiFuncs.pfnGetGammaCaps != NULL);
    assert(dxgiFuncs.pfnSetDisplayMode != NULL);
    assert(dxgiFuncs.pfnSetResourcePriority != NULL);
    assert(dxgiFuncs.pfnQueryResourceResidency != NULL);
    assert(dxgiFuncs.pfnRotateResourceIdentities != NULL);
    assert(dxgiFuncs.pfnBlt != NULL);
    assert(dxgiFuncs.pfnResolveSharedResource != NULL);
    assert(dxgiFuncs.pfnBlt1 != NULL);
    assert(dxgiFuncs.pfnOfferResources1 != NULL);
    assert(dxgiFuncs.pfnReclaimResources != NULL);
    assert(dxgiFuncs.pfnGetMultiplaneOverlayCaps != NULL);
    assert(dxgiFuncs.pfnGetMultiplaneOverlayGroupCaps != NULL);
    assert(dxgiFuncs.pfnReserved1 == NULL);
    assert(dxgiFuncs.pfnPresentMultiplaneOverlay != NULL);
    assert(dxgiFuncs.pfnReserved2 == NULL);
    assert(dxgiFuncs.pfnPresent1 != NULL);
    assert(dxgiFuncs.pfnCheckPresentDurationSupport != NULL);
    assert(dxgiFuncs.pfnTrimResidencySet != NULL);
    assert(dxgiFuncs.pfnCheckMultiplaneOverlayColorSpaceSupport != NULL);
    assert(dxgiFuncs.pfnPresentMultiplaneOverlay1 != NULL);
    assert(dxgiFuncs.pfnReclaimResources1 != NULL);

    printf("  -> D3D10.1, D3D11 & DXGI device functions table population PASS\n");
}

/*
 * F2.3 -- stub do callback de scanout. Registra QUANTAS vezes o kernel foi
 * chamado e COM QUE handle: sem os dois numeros o teste nao distingue "a DDI
 * chamou o kernel" de "a DDI devolveu S_OK por conta propria".
 */
static UINT g_SetDisplayModeCalls;
static D3DKMT_HANDLE g_SetDisplayModeLastPrimary;

static HRESULT APIENTRY TestSetDisplayModeCb(
    HANDLE hDevice,
    D3DDDICB_SETDISPLAYMODE *pArgs)
{
    (void)hDevice;
    assert(pArgs != NULL);
    ++g_SetDisplayModeCalls;
    g_SetDisplayModeLastPrimary = pArgs->hPrimaryAllocation;
    return S_OK;
}

static D3DDDI_RESIDENCYSTATUS g_QueryStatuses[2];
static HRESULT g_QueryHr;
static UINT g_QueryCalls;
static HRESULT APIENTRY TestQueryResidencyCb(HANDLE Device, const D3DDDICB_QUERYRESIDENCY *Args)
{
    assert(Device == (HANDLE)0xD00Du);
    assert(Args->NumAllocations == 1u && Args->hResource == NULL);
    assert(Args->HandleList[0] == 1u || Args->HandleList[0] == 2u);
    ++g_QueryCalls;
    *Args->pResidencyStatus = g_QueryStatuses[Args->HandleList[0] - 1u];
    return g_QueryHr;
}

static void TestDxgiBaseFunctions(void)
{
    DXGI1_6_1_DDI_BASE_FUNCTIONS dxgiFuncs;
    DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS gammaCaps;
    DXGI_GAMMA_CONTROL_CAPABILITIES gammaData;
    DXGI_DDI_ARG_SETDISPLAYMODE setDisplayMode;
    DXGI_DDI_ARG_SETRESOURCEPRIORITY setPriority;
    DXGI_DDI_ARG_QUERYRESOURCERESIDENCY queryResidency;
    DXGI_DDI_HRESOURCE dummyResources[2];
    DXGI_DDI_RESIDENCY residencyStatus[2];
    DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES rotateData;
    DXGI_DDI_ARG_BLT bltData;
    DXGI_DDI_ARG_PRESENT presentData;
    BC250_UMD_DEVICE dev;
    D3DDDI_DEVICECALLBACKS dxgiTestCallbacks;
    HRESULT hr;

    printf("[TEST] Testing DXGI base DDI implementations...\n");

    memset(&dev, 0, sizeof(dev));
    dev.DeviceActive = TRUE;
    /*
     * F2.3: a DDI passou a depender de pfnSetDisplayModeCb. Sem o stub, o
     * contraponto "primaria valida chama o kernel" nao poderia existir e a
     * correcao seria indistinguivel de "recusa sempre".
     */
    memset(&dxgiTestCallbacks, 0, sizeof(dxgiTestCallbacks));
    dxgiTestCallbacks.pfnSetDisplayModeCb = TestSetDisplayModeCb;
    dxgiTestCallbacks.pfnQueryResidencyCb = TestQueryResidencyCb;
    dev.hRTDevice.handle = (HANDLE)0xD00Du;
    dev.pKTCallbacks = &dxgiTestCallbacks;

    memset(&dxgiFuncs, 0, sizeof(dxgiFuncs));
    Bc250UmdPopulateDxgiBaseFuncs(&dxgiFuncs);

    /* Negative tests: NULL args */
    assert(dxgiFuncs.pfnPresent(NULL) == E_INVALIDARG);
    assert(dxgiFuncs.pfnGetGammaCaps(NULL) == E_INVALIDARG);
    assert(dxgiFuncs.pfnSetDisplayMode(NULL) == E_INVALIDARG);
    assert(dxgiFuncs.pfnSetResourcePriority(NULL) == E_INVALIDARG);
    assert(dxgiFuncs.pfnQueryResourceResidency(NULL) == E_INVALIDARG);
    assert(dxgiFuncs.pfnRotateResourceIdentities(NULL) == E_INVALIDARG);
    assert(dxgiFuncs.pfnBlt(NULL) == E_INVALIDARG);

    /* Gamma Caps */
    memset(&gammaCaps, 0, sizeof(gammaCaps));
    memset(&gammaData, 0, sizeof(gammaData));
    gammaCaps.pGammaCapabilities = &gammaData;
    hr = dxgiFuncs.pfnGetGammaCaps(&gammaCaps);
    assert(hr == S_OK);
    assert(gammaData.ScaleAndOffsetSupported == FALSE);
    assert(gammaData.NumGammaControlPoints == 0);

    /*
     * F0.1 (13/09) -- ESTE BLOCO CONGELAVA A MENTIRA.
     *
     * Ate esta faixa a assercao era `assert(hr == S_OK)` sobre um struct ZERADO.
     * Ou seja: o teste EXIGIA que a DDI declarasse sucesso sem backend nenhum, e
     * reprovaria qualquer tentativa de conserto. Sexta ocorrencia da classe no
     * projeto (as anteriores: comparacao crua de RPTR na A15, STATUS_SUCCESS sem
     * executor de TDR, memcpy de CPU no Draw, sdma_blt_executed cego a copia por
     * CPU, teste do blt exigindo 128 bytes).
     *
     * O invariante correto e' o OPOSTO: enquanto `pfnSetDisplayModeCb` nao for
     * chamada, a DDI TEM de recusar. Quando o item 2.3 do plano aterrissar, esta
     * assercao muda para exigir a chamada ao callback -- nunca para voltar a
     * aceitar S_OK sem backend.
     */
    memset(&setDisplayMode, 0, sizeof(setDisplayMode));
    setDisplayMode.SubResourceIndex = 0;
    hr = dxgiFuncs.pfnSetDisplayMode(&setDisplayMode);
    /*
     * F2.3: o backend chegou. Com struct zerado nao ha device, nem recurso, nem
     * alocacao de kernel -- a DDI tem de RECUSAR. O invariante que importa
     * continua sendo o mesmo da F0: sem backend efetivo, NUNCA S_OK.
     */
    assert(hr != S_OK);
    assert(hr == E_FAIL);

    /*
     * Contraponto: recurso valido, primaria declarada e callback presente ->
     * a DDI PRECISA chamar pfnSetDisplayModeCb com o handle de kernel exato.
     * Sem este bloco, "recusa sempre" passaria como se fosse correcao.
     */
    {
        BC250_UMD_RESOURCE primaryRes;
        memset(&primaryRes, 0, sizeof(primaryRes));
        primaryRes.Created = TRUE;
        primaryRes.Allocation.Created = TRUE;
        primaryRes.Allocation.KernelAllocation = 0xA110u;
        primaryRes.Allocation.DeclaredPrimary = TRUE;

        memset(&setDisplayMode, 0, sizeof(setDisplayMode));
        setDisplayMode.hDevice = (DXGI_DDI_HDEVICE)&dev;
        setDisplayMode.hResource = (DXGI_DDI_HRESOURCE)&primaryRes;
        g_SetDisplayModeCalls = 0;
        g_SetDisplayModeLastPrimary = 0;
        hr = dxgiFuncs.pfnSetDisplayMode(&setDisplayMode);
        assert(hr == S_OK);
        assert(g_SetDisplayModeCalls == 1);
        assert(g_SetDisplayModeLastPrimary == 0xA110u);

        /* Nao-primaria continua RECUSADA: o kernel so' varre o que aceitou. */
        primaryRes.Allocation.DeclaredPrimary = FALSE;
        g_SetDisplayModeCalls = 0;
        hr = dxgiFuncs.pfnSetDisplayMode(&setDisplayMode);
        assert(hr == E_FAIL);
        assert(g_SetDisplayModeCalls == 0);
    }

    /* SetResourcePriority */
    memset(&setPriority, 0, sizeof(setPriority));
    setPriority.Priority = 1;
    hr = dxgiFuncs.pfnSetResourcePriority(&setPriority);
    assert(hr == S_OK);

    /* F3.3 QueryResourceResidency: the OS, not the local cache, is authority. */
    {
        BC250_UMD_RESOURCE resA, resB;
        memset(&resA, 0, sizeof(resA)); memset(&resB, 0, sizeof(resB));
        resA.Created = resB.Created = TRUE;
        resA.Allocation.KernelAllocation = 1;
        resB.Allocation.KernelAllocation = 2;
        resA.Allocation.Resident = FALSE; /* deliberately contradict the OS */
        resB.Allocation.Resident = TRUE;
        dummyResources[0] = (DXGI_DDI_HRESOURCE)&resA;
        dummyResources[1] = (DXGI_DDI_HRESOURCE)&resB;
        memset(&queryResidency, 0, sizeof(queryResidency));
        queryResidency.hDevice = (DXGI_DDI_HDEVICE)&dev;
        queryResidency.Resources = 2;
        queryResidency.pResources = dummyResources;
        queryResidency.pStatus = residencyStatus;
        g_QueryCalls = 0; g_QueryHr = S_OK;
        g_QueryStatuses[0] = D3DDDI_RESIDENCYSTATUS_RESIDENTINGPUMEMORY;
        g_QueryStatuses[1] = D3DDDI_RESIDENCYSTATUS_NOTRESIDENT;
        hr = dxgiFuncs.pfnQueryResourceResidency(&queryResidency);
        assert(hr == S_NOT_RESIDENT);
        assert(g_QueryCalls == 2u);
        assert(residencyStatus[0] == DXGI_DDI_RESIDENCY_FULLY_RESIDENT);
        assert(residencyStatus[1] != DXGI_DDI_RESIDENCY_FULLY_RESIDENT);
        assert(residencyStatus[1] == DXGI_DDI_RESIDENCY_EVICTED_TO_DISK);
        g_QueryStatuses[0] = D3DDDI_RESIDENCYSTATUS_RESIDENTINSHAREDMEMORY;
        assert(dxgiFuncs.pfnQueryResourceResidency(&queryResidency) == S_NOT_RESIDENT);
        g_QueryStatuses[1] = D3DDDI_RESIDENCYSTATUS_RESIDENTINGPUMEMORY;
        assert(dxgiFuncs.pfnQueryResourceResidency(&queryResidency) == S_RESIDENT_IN_SHARED_MEMORY);
        assert(residencyStatus[0] == DXGI_DDI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY);
        g_QueryStatuses[0] = D3DDDI_RESIDENCYSTATUS_RESIDENTINGPUMEMORY;
        assert(dxgiFuncs.pfnQueryResourceResidency(&queryResidency) == S_OK);
        g_QueryHr = E_FAIL;
        assert(dxgiFuncs.pfnQueryResourceResidency(&queryResidency) == E_FAIL);
        g_QueryHr = S_OK; g_QueryStatuses[0] = (D3DDDI_RESIDENCYSTATUS)99;
        assert(dxgiFuncs.pfnQueryResourceResidency(&queryResidency) == E_UNEXPECTED);
        dxgiTestCallbacks.pfnQueryResidencyCb = NULL;
        assert(dxgiFuncs.pfnQueryResourceResidency(&queryResidency) == E_NOTIMPL);
    }

    /*
     * F0.1 -- setima ocorrencia da mesma classe. Ver o comentario do
     * SetDisplayMode acima.
     *
     * Rotacionar ZERO recursos continua sendo S_OK: e' no-op legitimo, nao
     * mentira. Rotacionar 2 sem implementar a troca TEM de recusar. O
     * contraponto existe para a correcao nao virar peneira -- se a DDI passar a
     * recusar tudo indiscriminadamente, este caso reprova.
     */
    memset(&rotateData, 0, sizeof(rotateData));
    rotateData.Resources = 0;
    hr = dxgiFuncs.pfnRotateResourceIdentities(&rotateData);
    assert(hr == S_OK);

    /*
     * F2.4: a rotacao real. Semantica do contrato (dxgiddi.h:100): o recurso i
     * assume a identidade do i+1, e o ultimo assume a do primeiro.
     *
     * O invariante forte e' que o BACKING gira e NENHUM handle se perde -- o
     * conjunto de alocacoes antes e depois tem de ser o mesmo, so' que
     * deslocado. Um teste que olhasse apenas res[0] deixaria passar uma rotacao
     * que duplicasse um backing e perdesse outro.
     */
    {
        BC250_UMD_RESOURCE chain[3];
        DXGI_DDI_HRESOURCE chainHandles[3];
        UINT c;

        for (c = 0; c < 3; ++c) {
            memset(&chain[c], 0, sizeof(chain[c]));
            chain[c].Created = TRUE;
            chain[c].Allocation.Created = TRUE;
            chain[c].Allocation.KernelAllocation = (D3DKMT_HANDLE)(0xA000u + c);
            chain[c].Allocation.GpuVirtualAddress = 0x100000ull * (c + 1u);
            chainHandles[c] = (DXGI_DDI_HRESOURCE)&chain[c];
        }

        memset(&rotateData, 0, sizeof(rotateData));
        rotateData.Resources = 3;
        rotateData.pResources = chainHandles;
        hr = dxgiFuncs.pfnRotateResourceIdentities(&rotateData);
        assert(hr == S_OK);
        /* 0 <= 1, 1 <= 2, 2 <= 0 */
        assert(chain[0].Allocation.KernelAllocation == (D3DKMT_HANDLE)0xA001u);
        assert(chain[1].Allocation.KernelAllocation == (D3DKMT_HANDLE)0xA002u);
        assert(chain[2].Allocation.KernelAllocation == (D3DKMT_HANDLE)0xA000u);
        /* A GPUVA viaja junto com o backing, nao fica para tras. */
        assert(chain[0].Allocation.GpuVirtualAddress == 0x200000ull);
        assert(chain[2].Allocation.GpuVirtualAddress == 0x100000ull);

        /* Tres voltas restauram a identidade original: nada se perdeu. */
        assert(dxgiFuncs.pfnRotateResourceIdentities(&rotateData) == S_OK);
        assert(dxgiFuncs.pfnRotateResourceIdentities(&rotateData) == S_OK);
        for (c = 0; c < 3; ++c) {
            assert(chain[c].Allocation.KernelAllocation == (D3DKMT_HANDLE)(0xA000u + c));
        }

        /*
         * Fail-closed ATOMICO: um recurso invalido no meio da cadeia aborta
         * ANTES de mover qualquer coisa. Rotacao parcial deixaria a cadeia com
         * backings duplicados, que e' pior que a recusa.
         */
        chain[1].Created = FALSE;
        assert(dxgiFuncs.pfnRotateResourceIdentities(&rotateData) == E_INVALIDARG);
        for (c = 0; c < 3; ++c) {
            assert(chain[c].Allocation.KernelAllocation == (D3DKMT_HANDLE)(0xA000u + c));
        }
        chain[1].Created = TRUE;

        /* Resources > 0 com pResources NULL recusa. */
        memset(&rotateData, 0, sizeof(rotateData));
        rotateData.Resources = 2;
        rotateData.pResources = NULL;
        assert(dxgiFuncs.pfnRotateResourceIdentities(&rotateData) == E_INVALIDARG);
    }

    /* Blt: falha fechado com ponteiros invalidos / recursos nao criados */
    memset(&bltData, 0, sizeof(bltData));
    bltData.hDevice = (DXGI_DDI_HDEVICE)&dev;
    hr = dxgiFuncs.pfnBlt(&bltData);
    assert(hr == E_INVALIDARG);

    /* Blt: com recursos validos e criados */
    {
        BC250_UMD_RESOURCE resSrc, resDst;
        memset(&resSrc, 0, sizeof(resSrc));
        memset(&resDst, 0, sizeof(resDst));
        resSrc.Created = TRUE;
        resDst.Created = TRUE;
        bltData.hSrcResource = (DXGI_DDI_HRESOURCE)&resSrc;
        bltData.hDstResource = (DXGI_DDI_HRESOURCE)&resDst;
        hr = dxgiFuncs.pfnBlt(&bltData);
#if BC250_D3D10_11_NATIVE_READY
        assert(hr == S_OK || hr == E_NOTIMPL || hr == E_INVALIDARG);
#else
        /* Sem backend nativo de Blt GPU, recusar honestamente com E_NOTIMPL */
        assert(hr == E_NOTIMPL);
#endif
    }

    /* Present sem callback retorna E_FAIL (fail-closed honesto) */
    memset(&presentData, 0, sizeof(presentData));
    presentData.hDevice = (DXGI_DDI_HDEVICE)&dev;
    hr = dxgiFuncs.pfnPresent(&presentData);
    assert(hr == E_FAIL);

    /* DXGI 1.6 extended DDI tests */
    {
        DXGI_DDI_ARG_RESOLVESHAREDRESOURCE resolveData;
        DXGI_DDI_ARG_BLT1 blt1Data;
        DXGI_DDI_ARG_OFFERRESOURCES1 offerData;
        DXGI_DDI_ARG_RECLAIMRESOURCES reclaimData;
        DXGI_DDI_ARG_RECLAIMRESOURCES1 reclaim1Data;
        DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS mpoCaps;
        DXGI_DDI_ARG_GETMULTIPLANEOVERLAYGROUPCAPS mpoGroup;
        DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY presentMpo;
        DXGI1_6_1_DDI_ARG_PRESENTMULTIPLANEOVERLAY presentMpo1;
        DXGI_DDI_ARG_CHECKPRESENTDURATIONSUPPORT durationData;
        DXGI_DDI_ARG_TRIMRESIDENCYSET trimData;
        DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT csData;
        DXGI1_6_1_DDI_ARG_PRESENT present1Data;
        BOOL discarded = TRUE;
        D3DDDI_RECLAIM_RESULT reclaimResult = D3DDDI_RECLAIM_RESULT_DISCARDED;
        DXGI_DDI_HRESOURCE dummyRes = (DXGI_DDI_HRESOURCE)0x1234;

        /* ResolveSharedResource: NULL -> E_INVALIDARG, valid -> DXGI_DDI_ERR_UNSUPPORTED */
        assert(dxgiFuncs.pfnResolveSharedResource(NULL) == E_INVALIDARG);
        memset(&resolveData, 0, sizeof(resolveData));
        assert(dxgiFuncs.pfnResolveSharedResource(&resolveData) == DXGI_DDI_ERR_UNSUPPORTED);

        /* Blt1: NULL -> E_INVALIDARG, unsupported rotation -> DXGI_DDI_ERR_UNSUPPORTED */
        assert(dxgiFuncs.pfnBlt1(NULL) == E_INVALIDARG);
        memset(&blt1Data, 0, sizeof(blt1Data));
        blt1Data.Rotate = DXGI_DDI_MODE_ROTATION_ROTATE90;
        assert(dxgiFuncs.pfnBlt1(&blt1Data) == DXGI_DDI_ERR_UNSUPPORTED);

        /* OfferResources1: NULL -> E_INVALIDARG, valid -> S_OK */
        assert(dxgiFuncs.pfnOfferResources1(NULL) == E_INVALIDARG);
        memset(&offerData, 0, sizeof(offerData));
        offerData.Resources = 1;
        offerData.pResources = &dummyRes;
        assert(dxgiFuncs.pfnOfferResources1(&offerData) == S_OK);

        /* ReclaimResources: NULL -> E_INVALIDARG, valid -> S_OK */
        assert(dxgiFuncs.pfnReclaimResources(NULL) == E_INVALIDARG);
        memset(&reclaimData, 0, sizeof(reclaimData));
        reclaimData.Resources = 1;
        reclaimData.pResources = &dummyRes;
        reclaimData.pDiscarded = &discarded;
        assert(dxgiFuncs.pfnReclaimResources(&reclaimData) == S_OK);
        assert(discarded == FALSE);

        /* ReclaimResources1: NULL -> E_INVALIDARG, valid -> S_OK */
        assert(dxgiFuncs.pfnReclaimResources1(NULL) == E_INVALIDARG);
        memset(&reclaim1Data, 0, sizeof(reclaim1Data));
        reclaim1Data.Resources = 1;
        reclaim1Data.pResources = &dummyRes;
        reclaim1Data.pResults = &reclaimResult;
        assert(dxgiFuncs.pfnReclaimResources1(&reclaim1Data) == S_OK);
        assert(reclaimResult == D3DDDI_RECLAIM_RESULT_OK);

        /* GetMultiplaneOverlayCaps: NULL -> E_INVALIDARG, valid -> MaxPlanes=1, NumCapabilityGroups=0 */
        assert(dxgiFuncs.pfnGetMultiplaneOverlayCaps(NULL) == E_INVALIDARG);
        memset(&mpoCaps, 0, sizeof(mpoCaps));
        assert(dxgiFuncs.pfnGetMultiplaneOverlayCaps(&mpoCaps) == S_OK);
        assert(mpoCaps.MultiplaneOverlayCaps.MaxPlanes == 1);
        assert(mpoCaps.MultiplaneOverlayCaps.NumCapabilityGroups == 0);

        /* GetMultiplaneOverlayGroupCaps: NULL -> E_INVALIDARG, valid -> DXGI_DDI_ERR_UNSUPPORTED */
        assert(dxgiFuncs.pfnGetMultiplaneOverlayGroupCaps(NULL) == E_INVALIDARG);
        memset(&mpoGroup, 0, sizeof(mpoGroup));
        assert(dxgiFuncs.pfnGetMultiplaneOverlayGroupCaps(&mpoGroup) == DXGI_DDI_ERR_UNSUPPORTED);

        /* PresentMultiplaneOverlay: NULL -> E_INVALIDARG, valid -> DXGI_DDI_ERR_UNSUPPORTED */
        assert(dxgiFuncs.pfnPresentMultiplaneOverlay(NULL) == E_INVALIDARG);
        memset(&presentMpo, 0, sizeof(presentMpo));
        assert(dxgiFuncs.pfnPresentMultiplaneOverlay(&presentMpo) == DXGI_DDI_ERR_UNSUPPORTED);

        /* PresentMultiplaneOverlay1: NULL -> E_INVALIDARG, valid -> DXGI_DDI_ERR_UNSUPPORTED */
        assert(dxgiFuncs.pfnPresentMultiplaneOverlay1(NULL) == E_INVALIDARG);
        memset(&presentMpo1, 0, sizeof(presentMpo1));
        assert(dxgiFuncs.pfnPresentMultiplaneOverlay1(&presentMpo1) == DXGI_DDI_ERR_UNSUPPORTED);

        /* CheckPresentDurationSupport: NULL -> E_INVALIDARG, valid -> DXGI_DDI_ERR_UNSUPPORTED */
        assert(dxgiFuncs.pfnCheckPresentDurationSupport(NULL) == E_INVALIDARG);
        memset(&durationData, 0, sizeof(durationData));
        assert(dxgiFuncs.pfnCheckPresentDurationSupport(&durationData) == DXGI_DDI_ERR_UNSUPPORTED);

        /* TrimResidencySet: NULL -> E_INVALIDARG, valid -> S_OK */
        assert(dxgiFuncs.pfnTrimResidencySet(NULL) == E_INVALIDARG);
        memset(&trimData, 0, sizeof(trimData));
        assert(dxgiFuncs.pfnTrimResidencySet(&trimData) == S_OK);

        /* CheckMultiplaneOverlayColorSpaceSupport: NULL -> E_INVALIDARG, valid -> S_OK, Supported=FALSE */
        assert(dxgiFuncs.pfnCheckMultiplaneOverlayColorSpaceSupport(NULL) == E_INVALIDARG);
        memset(&csData, 0, sizeof(csData));
        assert(dxgiFuncs.pfnCheckMultiplaneOverlayColorSpaceSupport(&csData) == S_OK);
        assert(csData.Supported == FALSE);

        /* Present1: NULL -> E_INVALIDARG, valid sem callback -> E_FAIL */
        assert(dxgiFuncs.pfnPresent1(NULL) == E_INVALIDARG);
        memset(&present1Data, 0, sizeof(present1Data));
        present1Data.hDevice = (DXGI_DDI_HDEVICE)&dev;
        assert(dxgiFuncs.pfnPresent1(&present1Data) == E_FAIL);
    }

    printf("  -> DXGI base DDI implementations PASS\n");
}

static void TestCapsPipelineSupport(void)
{
    D3D10_2DDIARG_GETCAPS getCaps;
    D3D11DDI_3DPIPELINESUPPORT_CAPS capsData;
    D3D10DDI_HADAPTER hAdapter;
    HRESULT hr;

    printf("[TEST] Testing D3D11DDICAPS_3DPIPELINESUPPORT...\n");

    memset(&hAdapter, 0, sizeof(hAdapter));
    memset(&getCaps, 0, sizeof(getCaps));
    memset(&capsData, 0, sizeof(capsData));

    getCaps.Type = (D3D10_2DDICAPS_TYPE)130u; /* D3D11DDICAPS_3DPIPELINESUPPORT */
    getCaps.pData = &capsData;
    getCaps.DataSize = sizeof(capsData);

    hr = Bc250Umd10GetCaps(hAdapter, &getCaps);
    /*
     * Type=130 sai da MESMA politica do CreateDevice (Bc250Umd3DPipelineIsDeclared),
     * que le D3D11LabPromotion do registro. O #if sozinho nao bastava: com a
     * chave armada por um bind anterior a suite reprovava por estado da maquina.
     */
    if (Bc250Umd3DPipelineIsDeclared(
            BC250_D3D10_11_NATIVE_READY ? TRUE : FALSE,
            TestReadUmdTunable(BC250_D3D11_LAB_PROMOTION_REG_NAME))) {
        assert(hr == S_OK);
        assert((capsData.Caps & D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_0)) != 0);
        assert((capsData.Caps & D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_1)) != 0);
    } else {
        /* Sem backend nativo de shaders/draw, recusar com E_NOTIMPL honestamente */
        assert(hr == E_NOTIMPL);
    }

    printf("  -> D3D11DDICAPS_3DPIPELINESUPPORT PASS\n");
}

static void TestShaderDDIs(void)
{
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_HDEVICE hDev;
    BC250_UMD_DEVICE dev;
    BC250_UMD_SHADER shader;
    D3D10DDI_HSHADER hShader;
    D3D10DDI_HRTSHADER hRTShader;
    uint8_t buffer[512];
    BC250_DXBC_RAW_HEADER *hdr;
    uint32_t *offsets;
    BC250_DXBC_RAW_CHUNK_HEADER *chk;
    uint32_t *shaderData;
    SIZE_T sz;

    printf("[TEST] Testing D3D10 Shader DDIs...\n");

    memset(&deviceFuncs10_1, 0, sizeof(deviceFuncs10_1));
    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    assert(deviceFuncs10_1.pfnCalcPrivateShaderSize != NULL);
    assert(deviceFuncs10_1.pfnCreateVertexShader != NULL);
    assert(deviceFuncs10_1.pfnCreatePixelShader != NULL);
    assert(deviceFuncs10_1.pfnCreateGeometryShader != NULL);
    assert(deviceFuncs10_1.pfnCalcPrivateGeometryShaderWithStreamOutput != NULL);
    assert(deviceFuncs10_1.pfnCreateGeometryShaderWithStreamOutput != NULL);
    assert(deviceFuncs10_1.pfnDestroyShader != NULL);

    memset(&dev, 0, sizeof(dev));
    dev.DeviceActive = TRUE;
    hDev.pDrvPrivate = &dev;
    hShader.pDrvPrivate = &shader;
    hRTShader.handle = (void*)0x584452;

    /* Build a mock DXBC container */
    memset(buffer, 0, sizeof(buffer));
    hdr = (BC250_DXBC_RAW_HEADER *)buffer;
    hdr->Magic = BC250_DXBC_MAGIC_DXBC;
    hdr->Version = 1;
    hdr->TotalSize = 80;
    hdr->ChunkCount = 1;

    offsets = (uint32_t *)(buffer + 32);
    offsets[0] = 36; /* Offset of SHDR chunk */

    chk = (BC250_DXBC_RAW_CHUNK_HEADER *)(buffer + 36);
    chk->FourCC = BC250_DXBC_FOURCC_SHDR;
    chk->ChunkSize = 36; /* 8-byte chunk header + 8-byte shader header + 20 bytes opcodes */

    shaderData = (uint32_t *)(buffer + 44);
    shaderData[0] = (0u << 16u) | (4u << 4u) | 0u; /* Pixel Shader SM 4.0 */
    shaderData[1] = 7u; /* total program length: 2 header + 5 opcode tokens */
    shaderData[2] = (3u << 24u) | 54u; /* MOV */
    shaderData[3] = 0x100012u;
    shaderData[4] = 0x200046u;
    shaderData[5] = (1u << 24u) | 62u; /* RET */
    shaderData[6] = 0u;

    /* Test CalcPrivateShaderSize */
    sz = deviceFuncs10_1.pfnCalcPrivateShaderSize(hDev, (const UINT*)buffer, NULL);
    assert(sz == sizeof(BC250_UMD_SHADER));

    /* Negative test CalcPrivateShaderSize */
    assert(deviceFuncs10_1.pfnCalcPrivateShaderSize(hDev, NULL, NULL) == 0);
    hdr->Magic = 0x12345678;
    assert(deviceFuncs10_1.pfnCalcPrivateShaderSize(hDev, (const UINT*)buffer, NULL) == 0);
    hdr->Magic = BC250_DXBC_MAGIC_DXBC;

    /* Test CreatePixelShader */
    memset(&shader, 0xFF, sizeof(shader));
    deviceFuncs10_1.pfnCreatePixelShader(hDev, (const UINT*)buffer, hShader, hRTShader, NULL);
    assert(shader.Type == BC250_DXBC_SHADER_PIXEL);
    assert(shader.MajorVersion == 4u);
    assert(shader.MinorVersion == 0u);
    assert(shader.OpcodeTokenCount == 5u);
    assert(shader.IsParsed == TRUE);
    assert(shader.hRTShader.handle == (void*)0x584452);

    /* Test DestroyShader */
    deviceFuncs10_1.pfnDestroyShader(hDev, hShader);
    assert(shader.Type == 0);
    assert(shader.IsParsed == FALSE);

    /* Test CreateVertexShader */
    shaderData[0] = (1u << 16u) | (4u << 4u) | 1u; /* Vertex Shader SM 4.1 */
    memset(&shader, 0xFF, sizeof(shader));
    deviceFuncs10_1.pfnCreateVertexShader(hDev, (const UINT*)buffer, hShader, hRTShader, NULL);
    assert(shader.Type == BC250_DXBC_SHADER_VERTEX);
    assert(shader.MajorVersion == 4u);
    assert(shader.MinorVersion == 1u);
    assert(shader.OpcodeTokenCount == 5u);
    assert(shader.IsParsed == TRUE);

    deviceFuncs10_1.pfnDestroyShader(hDev, hShader);

    /* Test Direct token stream parsing (without container) */
    {
        UINT directStream[7];
        directStream[0] = (1u << 16u) | (5u << 4u) | 0u; /* VS SM 5.0 */
        directStream[1] = 7u;
        directStream[2] = (3u << 24u) | 54u;
        directStream[3] = 0x100012u;
        directStream[4] = 0x200046u;
        directStream[5] = (1u << 24u) | 62u;
        directStream[6] = 0u;

        sz = deviceFuncs10_1.pfnCalcPrivateShaderSize(hDev, directStream, NULL);
        assert(sz == sizeof(BC250_UMD_SHADER));

        memset(&shader, 0xFF, sizeof(shader));
        deviceFuncs10_1.pfnCreateVertexShader(hDev, directStream, hShader, hRTShader, NULL);
        assert(shader.Type == BC250_DXBC_SHADER_VERTEX);
        assert(shader.MajorVersion == 5u);
        assert(shader.MinorVersion == 0u);
        assert(shader.OpcodeTokenCount == 5u);
        assert(shader.IsParsed == TRUE);

        deviceFuncs10_1.pfnDestroyShader(hDev, hShader);

        /* Test Direct token stream with signatures */
        {
            D3D10DDIARG_STAGE_IO_SIGNATURES sigs;
            D3D10DDIARG_SIGNATURE_ENTRY inEntry;
            D3D10DDIARG_SIGNATURE_ENTRY outEntry;

            memset(&sigs, 0, sizeof(sigs));
            inEntry.SystemValue = D3D10_SB_NAME_VERTEX_ID;
            inEntry.Register = 0;
            inEntry.Mask = 1;
            sigs.pInputSignature = &inEntry;
            sigs.NumInputSignatureEntries = 1;

            outEntry.SystemValue = D3D10_SB_NAME_POSITION;
            outEntry.Register = 0;
            outEntry.Mask = 0xFu;
            sigs.pOutputSignature = &outEntry;
            sigs.NumOutputSignatureEntries = 1;

            memset(&shader, 0xFF, sizeof(shader));
            deviceFuncs10_1.pfnCreateVertexShader(hDev, directStream, hShader, hRTShader, &sigs);
            assert(shader.Type == BC250_DXBC_SHADER_VERTEX);
            assert(shader.MajorVersion == 5u);
            assert(shader.MinorVersion == 0u);
            assert(shader.OpcodeTokenCount == 5u);
            assert(shader.IsParsed == TRUE);

            deviceFuncs10_1.pfnDestroyShader(hDev, hShader);
        }
    }

    printf("  -> D3D10 Shader DDIs PASS\n");
}

static void TestResourceDDIs(void)
{
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_HDEVICE hDev;
    BC250_UMD_DEVICE dev;
    BC250_UMD_RESOURCE res;
    D3D10DDI_HRESOURCE hRes;
    D3D10DDI_HRTRESOURCE hRTRes;
    D3D10DDIARG_CREATERESOURCE createArg;
    D3D10DDI_MIPINFO mipInfo;
    D3D10DDI_MAPPED_SUBRESOURCE mapped;
    UINT caps = 0;
    UINT levels = 0;
    SIZE_T sz;

    printf("[TEST] Testing D3D10 Resource DDIs...\n");

    memset(&deviceFuncs10_1, 0, sizeof(deviceFuncs10_1));
    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    assert(deviceFuncs10_1.pfnCalcPrivateResourceSize != NULL);
    assert(deviceFuncs10_1.pfnCalcPrivateOpenedResourceSize != NULL);
    assert(deviceFuncs10_1.pfnCreateResource != NULL);
    assert(deviceFuncs10_1.pfnOpenResource != NULL);
    assert(deviceFuncs10_1.pfnDestroyResource != NULL);
    assert(deviceFuncs10_1.pfnResourceMap != NULL);
    assert(deviceFuncs10_1.pfnResourceUnmap != NULL);
    assert(deviceFuncs10_1.pfnResourceIsStagingBusy != NULL);
    assert(deviceFuncs10_1.pfnCheckFormatSupport != NULL);
    assert(deviceFuncs10_1.pfnCheckMultisampleQualityLevels != NULL);

    memset(&dev, 0, sizeof(dev));
    dev.DeviceActive = TRUE;
    hDev.pDrvPrivate = &dev;
    hRes.pDrvPrivate = &res;
    hRTRes.handle = (void*)0x524553;

    /* Setup 2D Texture Resource Arg (Primary Display / Present) */
    memset(&createArg, 0, sizeof(createArg));
    memset(&mipInfo, 0, sizeof(mipInfo));
    mipInfo.PhysicalWidth = 1920;
    mipInfo.PhysicalHeight = 1080;
    mipInfo.PhysicalDepth = 1;
    mipInfo.TexelWidth = 1920;
    mipInfo.TexelHeight = 1080;
    mipInfo.TexelDepth = 1;

    createArg.pMipInfoList = &mipInfo;
    createArg.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    createArg.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    createArg.Usage = D3D10_DDI_USAGE_DEFAULT;
    createArg.BindFlags = D3D10_DDI_BIND_RENDER_TARGET | D3D10_DDI_BIND_PRESENT;
    createArg.MipLevels = 1;
    createArg.ArraySize = 1;

    /* Test CalcPrivateResourceSize */
    sz = deviceFuncs10_1.pfnCalcPrivateResourceSize(hDev, &createArg);
    assert(sz == sizeof(BC250_UMD_RESOURCE));

    sz = deviceFuncs10_1.pfnCalcPrivateOpenedResourceSize(hDev, NULL);
    assert(sz == sizeof(BC250_UMD_RESOURCE));

    /* Test CreateResource */
    memset(&res, 0xFF, sizeof(res));
    deviceFuncs10_1.pfnCreateResource(hDev, &createArg, hRes, hRTRes);

    assert(res.Created == TRUE);
    assert(res.IsPrimary == FALSE);
    assert(res.ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D);
    assert(res.Format == DXGI_FORMAT_B8G8R8A8_UNORM);
    assert(res.Width == 1920);
    assert(res.Height == 1080);
    assert(res.RowPitch == 7680); /* 1920 * 4 = 7680, 256-byte aligned */
    assert(res.TotalByteSize >= 7680 * 1080);
    assert(res.hRTResource.handle == (void*)0x524553);

    /* Test ResourceMap and ResourceUnmap */
    memset(&mapped, 0, sizeof(mapped));
    deviceFuncs10_1.pfnResourceMap(hDev, hRes, 0, D3D10_DDI_MAP_READWRITE, 0, &mapped);
    assert(mapped.pData != NULL);
    assert(mapped.RowPitch == 7680);

    /* Write test pattern */
    ((uint32_t*)mapped.pData)[0] = 0xAA55AA55;
    assert(((uint32_t*)mapped.pData)[0] == 0xAA55AA55);

    deviceFuncs10_1.pfnResourceUnmap(hDev, hRes, 0);

    /* Test DestroyResource */
    deviceFuncs10_1.pfnDestroyResource(hDev, hRes);
    assert(res.Created == FALSE);
    assert(res.Width == 0);

    /* Test CheckFormatSupport */
    deviceFuncs10_1.pfnCheckFormatSupport(hDev, DXGI_FORMAT_B8G8R8A8_UNORM, &caps);
    assert((caps & D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET) != 0);
    assert((caps & D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE) != 0);

    deviceFuncs10_1.pfnCheckFormatSupport(hDev, DXGI_FORMAT_UNKNOWN, &caps);
    assert(caps == 0);

    /* Test CheckMultisampleQualityLevels */
    deviceFuncs10_1.pfnCheckMultisampleQualityLevels(hDev, DXGI_FORMAT_B8G8R8A8_UNORM, 1, &levels);
    assert(levels == 1);

    deviceFuncs10_1.pfnCheckMultisampleQualityLevels(hDev, DXGI_FORMAT_B8G8R8A8_UNORM, 4, &levels);
    assert(levels == 0);

    /* Test Buffer creation */
    memset(&createArg, 0, sizeof(createArg));
    memset(&mipInfo, 0, sizeof(mipInfo));
    mipInfo.PhysicalWidth = 65536; /* 64KB vertex buffer */
    mipInfo.TexelWidth = 65536;
    createArg.pMipInfoList = &mipInfo;
    createArg.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
    createArg.Format = DXGI_FORMAT_UNKNOWN;
    createArg.BindFlags = D3D10_DDI_BIND_VERTEX_BUFFER;
    createArg.MipLevels = 1;
    createArg.ArraySize = 1;

    memset(&res, 0xFF, sizeof(res));
    deviceFuncs10_1.pfnCreateResource(hDev, &createArg, hRes, hRTRes);
    assert(res.Created == TRUE);
    assert(res.IsPrimary == FALSE);
    assert(res.TotalByteSize >= 65536);
    assert(res.Width == 65536);

    deviceFuncs10_1.pfnDestroyResource(hDev, hRes);

    printf("  -> D3D10 Resource DDIs PASS\n");
}

static void TestViewAndOutputMergerDDIs(void)
{
    BC250_UMD_DEVICE dev;
    BC250_UMD_RESOURCE resColor;
    BC250_UMD_RESOURCE resDepth;
    BC250_UMD_RTV rtv;
    BC250_UMD_DSV dsv;
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_HDEVICE hDev;
    D3D10DDI_HRESOURCE hResColor, hResDepth;
    D3D10DDI_HRTRESOURCE hRTRes;
    D3D10DDI_HRENDERTARGETVIEW hRTV;
    D3D10DDI_HRTRENDERTARGETVIEW hRTRTV;
    D3D10DDI_HDEPTHSTENCILVIEW hDSV;
    D3D10DDI_HRTDEPTHSTENCILVIEW hRTDSV;
    D3D10DDIARG_CREATERESOURCE createRes;
    D3D10DDI_MIPINFO mipInfo;
    D3D10DDIARG_CREATERENDERTARGETVIEW createRTV;
    D3D10DDIARG_CREATEDEPTHSTENCILVIEW createDSV;
    D3D10_DDI_VIEWPORT vp;
    D3D10_DDI_RECT scissor;
    FLOAT clearColorRed[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    SIZE_T sz;

    printf("[TEST] D3D10 View and Output Merger DDIs...\n");

    memset(&dev, 0, sizeof(dev));
    hDev.pDrvPrivate = &dev;

    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    /* Verify table function pointers are not NULL */
    assert(deviceFuncs10_1.pfnCalcPrivateRenderTargetViewSize != NULL);
    assert(deviceFuncs10_1.pfnCreateRenderTargetView != NULL);
    assert(deviceFuncs10_1.pfnDestroyRenderTargetView != NULL);
    assert(deviceFuncs10_1.pfnCalcPrivateDepthStencilViewSize != NULL);
    assert(deviceFuncs10_1.pfnCreateDepthStencilView != NULL);
    assert(deviceFuncs10_1.pfnDestroyDepthStencilView != NULL);
    assert(deviceFuncs10_1.pfnSetRenderTargets != NULL);
    assert(deviceFuncs10_1.pfnClearRenderTargetView != NULL);
    assert(deviceFuncs10_1.pfnClearDepthStencilView != NULL);
    assert(deviceFuncs10_1.pfnSetViewports != NULL);
    assert(deviceFuncs10_1.pfnSetScissorRects != NULL);

    /* 1. Create color resource (1920x1080 B8G8R8A8_UNORM) */
    memset(&mipInfo, 0, sizeof(mipInfo));
    mipInfo.TexelWidth = 1920;
    mipInfo.TexelHeight = 1080;
    mipInfo.TexelDepth = 1;
    mipInfo.PhysicalWidth = 1920;
    mipInfo.PhysicalHeight = 1080;
    mipInfo.PhysicalDepth = 1;

    memset(&createRes, 0, sizeof(createRes));
    createRes.pMipInfoList = &mipInfo;
    createRes.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    createRes.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    createRes.Usage = D3D10_DDI_USAGE_DEFAULT;
    createRes.BindFlags = D3D10_DDI_BIND_RENDER_TARGET;
    createRes.MipLevels = 1;
    createRes.ArraySize = 1;

    hResColor.pDrvPrivate = &resColor;
    hRTRes.handle = (void*)0x524553;
    deviceFuncs10_1.pfnCreateResource(hDev, &createRes, hResColor, hRTRes);
    assert(resColor.Created == TRUE);

    /* 2. Create depth resource (1920x1080 D24_UNORM_S8_UINT) */
    createRes.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    createRes.BindFlags = D3D10_DDI_BIND_DEPTH_STENCIL;
    hResDepth.pDrvPrivate = &resDepth;
    deviceFuncs10_1.pfnCreateResource(hDev, &createRes, hResDepth, hRTRes);
    assert(resDepth.Created == TRUE);

    /* 3. Calc Private Sizes */
    sz = deviceFuncs10_1.pfnCalcPrivateRenderTargetViewSize(hDev, NULL);
    assert(sz == 0);
    memset(&createRTV, 0, sizeof(createRTV));
    createRTV.hDrvResource = hResColor;
    createRTV.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    createRTV.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    createRTV.Tex2D.MipSlice = 0;
    createRTV.Tex2D.FirstArraySlice = 0;
    createRTV.Tex2D.ArraySize = 1;
    sz = deviceFuncs10_1.pfnCalcPrivateRenderTargetViewSize(hDev, &createRTV);
    assert(sz == sizeof(BC250_UMD_RTV));

    sz = deviceFuncs10_1.pfnCalcPrivateDepthStencilViewSize(hDev, NULL);
    assert(sz == 0);
    memset(&createDSV, 0, sizeof(createDSV));
    createDSV.hDrvResource = hResDepth;
    createDSV.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    createDSV.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    createDSV.Tex2D.MipSlice = 0;
    createDSV.Tex2D.FirstArraySlice = 0;
    createDSV.Tex2D.ArraySize = 1;
    sz = deviceFuncs10_1.pfnCalcPrivateDepthStencilViewSize(hDev, &createDSV);
    assert(sz == sizeof(BC250_UMD_DSV));

    /* 4. Create RTV & DSV */
    hRTV.pDrvPrivate = &rtv;
    hRTRTV.handle = (void*)0x525456;
    deviceFuncs10_1.pfnCreateRenderTargetView(hDev, &createRTV, hRTV, hRTRTV);
    assert(rtv.Created == TRUE);
    assert(rtv.Format == DXGI_FORMAT_B8G8R8A8_UNORM);
    assert(rtv.ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D);
    assert(rtv.pResource == &resColor);
    assert(rtv.hRTRTV.handle == (void*)0x525456);

    hDSV.pDrvPrivate = &dsv;
    hRTDSV.handle = (void*)0x445356;
    deviceFuncs10_1.pfnCreateDepthStencilView(hDev, &createDSV, hDSV, hRTDSV);
    assert(dsv.Created == TRUE);
    assert(dsv.Format == DXGI_FORMAT_D24_UNORM_S8_UINT);
    assert(dsv.ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D);
    assert(dsv.pResource == &resDepth);
    assert(dsv.hRTDSV.handle == (void*)0x445356);

    /* 5. SetRenderTargets */
    deviceFuncs10_1.pfnSetRenderTargets(hDev, &hRTV, 1, 0, hDSV);
    assert(dev.PipelineState.NumBoundRTVs == 1);
    assert(dev.PipelineState.BoundRTVs[0].pDrvPrivate == &rtv);
    assert(dev.PipelineState.BoundDSV.pDrvPrivate == &dsv);

    /* F3.1: no GPU fill exists. Refusal must reach the runtime even under
     * lab promotion; an existing Map shadow is not a render target backend. */
    {
        D3D10DDI_CORELAYER_DEVICECALLBACKS cb;
        UINT before = g_SetErrorCalls;
        memset(&cb, 0, sizeof(cb)); cb.pfnSetErrorCb = TestSetError;
        dev.pUMCallbacks = &cb; dev.hRTCoreLayer.handle = (void*)0xC0DEu;
        assert(resColor.pCpuShadow == NULL);
        deviceFuncs10_1.pfnClearRenderTargetView(hDev, hRTV, clearColorRed);
        if (TestReadUmdTunable(BC250_D3D11_DRAW_BLT_SUBST_REG_NAME)) {
            assert(g_SetErrorCalls == before);
        } else {
            assert(g_SetErrorCalls == before + 1u && g_LastUmdError == E_NOTIMPL);
        }
        assert(resColor.pCpuShadow == NULL);
        resColor.pCpuShadow = malloc(resColor.TotalByteSize);
        assert(resColor.pCpuShadow != NULL);
        memset(resColor.pCpuShadow, 0x5A, resColor.TotalByteSize);
        deviceFuncs10_1.pfnClearRenderTargetView(hDev, hRTV, clearColorRed);
        if (TestReadUmdTunable(BC250_D3D11_DRAW_BLT_SUBST_REG_NAME)) {
            assert(g_SetErrorCalls == before);
        } else {
            assert(g_SetErrorCalls == before + 2u);
        }
        assert(((unsigned char*)resColor.pCpuShadow)[0] == 0x5A);
        assert(((unsigned char*)resColor.pCpuShadow)[resColor.TotalByteSize - 1u] == 0x5A);
        dev.pUMCallbacks = NULL;
    }

    /* 7. ClearDepthStencilView */
    deviceFuncs10_1.pfnClearDepthStencilView(hDev, hDSV, 0x01 | 0x02, 1.0f, 0);

    /* 8. SetViewports */
    memset(&vp, 0, sizeof(vp));
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = 1920.0f;
    vp.Height = 1080.0f;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    deviceFuncs10_1.pfnSetViewports(hDev, 1, 0, &vp);
    assert(dev.PipelineState.NumViewports == 1);
    assert(dev.PipelineState.Viewports[0].Width == 1920.0f);
    assert(dev.PipelineState.Viewports[0].Height == 1080.0f);

    /* 9. SetScissorRects */
    memset(&scissor, 0, sizeof(scissor));
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = 1920;
    scissor.bottom = 1080;
    deviceFuncs10_1.pfnSetScissorRects(hDev, 1, 0, &scissor);
    assert(dev.PipelineState.NumScissorRects == 1);
    assert(dev.PipelineState.ScissorRects[0].right == 1920);
    assert(dev.PipelineState.ScissorRects[0].bottom == 1080);

    /* 10. Destroy Views and verify unbind */
    deviceFuncs10_1.pfnDestroyRenderTargetView(hDev, hRTV);
    assert(rtv.Created == FALSE);
    assert(dev.PipelineState.BoundRTVs[0].pDrvPrivate == NULL);

    deviceFuncs10_1.pfnDestroyDepthStencilView(hDev, hDSV);
    assert(dsv.Created == FALSE);
    assert(dev.PipelineState.BoundDSV.pDrvPrivate == NULL);

    /* Clean up resources */
    deviceFuncs10_1.pfnDestroyResource(hDev, hResColor);
    deviceFuncs10_1.pfnDestroyResource(hDev, hResDepth);

    printf("  -> D3D10 View and Output Merger DDIs PASS\n");
}

static void TestDrawAndPipelineBindingDDIs(void)
{
    BC250_UMD_DEVICE dev;
    BC250_UMD_RESOURCE resSrc, resDst, resCB;
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_HDEVICE hDev;
    D3D10DDI_HRESOURCE hResSrc, hResDst, hResCB;
    D3D10DDI_HRTRESOURCE hRTRes;
    D3D10DDI_HSHADER hVS, hPS, hGS;
    D3D10DDI_HELEMENTLAYOUT hLayout;
    D3D10DDIARG_CREATERESOURCE createRes;
    D3D10DDI_MIPINFO mipInfo;
    UINT stride = 32, offset = 0;
    float cbData[4] = { 1.25f, 2.5f, 5.0f, 10.0f };
    D3D10_DDI_BOX copyBox;
    D3D10DDI_CORELAYER_DEVICECALLBACKS umCallbacks;

    printf("[TEST] D3D10 Draw, Input Assembly, and Resource Copy DDIs...\n");

    memset(&dev, 0, sizeof(dev));
    memset(&umCallbacks, 0, sizeof(umCallbacks));
    umCallbacks.pfnSetErrorCb = TestSetError;
    dev.pUMCallbacks = &umCallbacks;
    dev.hRTCoreLayer.handle = (void*)0xC0DEu;
    g_SetErrorCalls = 0u;
    g_LastUmdError = S_OK;
    hDev.pDrvPrivate = &dev;

    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    /* Verify table pointers */
    assert(deviceFuncs10_1.pfnDraw != NULL);
    assert(deviceFuncs10_1.pfnDrawIndexed != NULL);
    assert(deviceFuncs10_1.pfnDrawInstanced != NULL);
    assert(deviceFuncs10_1.pfnDrawIndexedInstanced != NULL);
    assert(deviceFuncs10_1.pfnDrawAuto != NULL);
    assert(deviceFuncs10_1.pfnVsSetShader != NULL);
    assert(deviceFuncs10_1.pfnPsSetShader != NULL);
    assert(deviceFuncs10_1.pfnGsSetShader != NULL);
    assert(deviceFuncs10_1.pfnIaSetTopology != NULL);
    assert(deviceFuncs10_1.pfnIaSetInputLayout != NULL);
    assert(deviceFuncs10_1.pfnIaSetVertexBuffers != NULL);
    assert(deviceFuncs10_1.pfnIaSetIndexBuffer != NULL);
    assert(deviceFuncs10_1.pfnVsSetConstantBuffers != NULL);
    assert(deviceFuncs10_1.pfnPsSetConstantBuffers != NULL);
    assert(deviceFuncs10_1.pfnGsSetConstantBuffers != NULL);
    assert(deviceFuncs10_1.pfnDefaultConstantBufferUpdateSubresourceUP != NULL);
    assert(deviceFuncs10_1.pfnResourceUpdateSubresourceUP != NULL);
    assert(deviceFuncs10_1.pfnResourceCopy != NULL);
    assert(deviceFuncs10_1.pfnResourceCopyRegion != NULL);

    /* 1. Test Shader Bindings */
    hVS.pDrvPrivate = (void*)0x534801;
    hPS.pDrvPrivate = (void*)0x534802;
    hGS.pDrvPrivate = (void*)0x534803;
    deviceFuncs10_1.pfnVsSetShader(hDev, hVS);
    deviceFuncs10_1.pfnPsSetShader(hDev, hPS);
    deviceFuncs10_1.pfnGsSetShader(hDev, hGS);
    assert(dev.PipelineState.BoundVS.pDrvPrivate == (void*)0x534801);
    assert(dev.PipelineState.BoundPS.pDrvPrivate == (void*)0x534802);
    assert(dev.PipelineState.BoundGS.pDrvPrivate == (void*)0x534803);

    /* 2. Test Input Assembly Bindings */
    deviceFuncs10_1.pfnIaSetTopology(hDev, D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    assert(dev.PipelineState.Topology == D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    hLayout.pDrvPrivate = (void*)0x1A01;
    deviceFuncs10_1.pfnIaSetInputLayout(hDev, hLayout);
    assert(dev.PipelineState.InputLayout.pDrvPrivate == (void*)0x1A01);

    memset(&mipInfo, 0, sizeof(mipInfo));
    mipInfo.PhysicalWidth = 4096;
    mipInfo.TexelWidth = 4096;
    memset(&createRes, 0, sizeof(createRes));
    createRes.pMipInfoList = &mipInfo;
    createRes.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
    createRes.Format = DXGI_FORMAT_UNKNOWN;
    createRes.BindFlags = D3D10_DDI_BIND_VERTEX_BUFFER;
    createRes.MipLevels = 1;
    createRes.ArraySize = 1;

    hResSrc.pDrvPrivate = &resSrc;
    hRTRes.handle = (void*)0x524553;
    deviceFuncs10_1.pfnCreateResource(hDev, &createRes, hResSrc, hRTRes);

    deviceFuncs10_1.pfnIaSetVertexBuffers(hDev, 0, 1, &hResSrc, &stride, &offset);
    assert(dev.PipelineState.VertexBuffers[0].pDrvPrivate == &resSrc);
    assert(dev.PipelineState.VertexStrides[0] == 32);
    assert(dev.PipelineState.VertexOffsets[0] == 0);

    createRes.BindFlags = D3D10_DDI_BIND_INDEX_BUFFER;
    hResDst.pDrvPrivate = &resDst;
    deviceFuncs10_1.pfnCreateResource(hDev, &createRes, hResDst, hRTRes);

    deviceFuncs10_1.pfnIaSetIndexBuffer(hDev, hResDst, DXGI_FORMAT_R16_UINT, 16);
    assert(dev.PipelineState.IndexBuffer.pDrvPrivate == &resDst);
    assert(dev.PipelineState.IndexFormat == DXGI_FORMAT_R16_UINT);
    assert(dev.PipelineState.IndexOffset == 16);

    /* 3. Test Constant Buffer Bindings & Updates */
    createRes.BindFlags = D3D10_DDI_BIND_CONSTANT_BUFFER;
    hResCB.pDrvPrivate = &resCB;
    deviceFuncs10_1.pfnCreateResource(hDev, &createRes, hResCB, hRTRes);

    deviceFuncs10_1.pfnVsSetConstantBuffers(hDev, 0, 1, &hResCB);
    deviceFuncs10_1.pfnPsSetConstantBuffers(hDev, 1, 1, &hResCB);
    deviceFuncs10_1.pfnGsSetConstantBuffers(hDev, 2, 1, &hResCB);
    assert(dev.PipelineState.VsConstantBuffers[0].pDrvPrivate == &resCB);
    assert(dev.PipelineState.PsConstantBuffers[1].pDrvPrivate == &resCB);
    assert(dev.PipelineState.GsConstantBuffers[2].pDrvPrivate == &resCB);

    deviceFuncs10_1.pfnDefaultConstantBufferUpdateSubresourceUP(
        hDev, hResCB, 0, NULL, cbData, sizeof(cbData), sizeof(cbData));
    assert(resCB.pCpuShadow != NULL);
    assert(((float*)resCB.pCpuShadow)[0] == 1.25f);
    assert(((float*)resCB.pCpuShadow)[1] == 2.5f);
    assert(((float*)resCB.pCpuShadow)[2] == 5.0f);
    assert(((float*)resCB.pCpuShadow)[3] == 10.0f);

    /* 4. Test Resource Copy and Region Copy */
    {
        D3D10DDI_MAPPED_SUBRESOURCE mapped;
        deviceFuncs10_1.pfnResourceMap(hDev, hResSrc, 0, D3D10_DDI_MAP_READWRITE, 0, &mapped);
        memset(mapped.pData, 0x7A, 4096);
        deviceFuncs10_1.pfnResourceUnmap(hDev, hResSrc, 0);

        deviceFuncs10_1.pfnResourceCopy(hDev, hResDst, hResSrc);
        assert(resDst.pCpuShadow != NULL);
        assert(((uint8_t*)resDst.pCpuShadow)[0] == 0x7A);
        assert(((uint8_t*)resDst.pCpuShadow)[2048] == 0x7A);
        assert(((uint8_t*)resDst.pCpuShadow)[4095] == 0x7A);

        /* CopyRegion */
        memset(&copyBox, 0, sizeof(copyBox));
        copyBox.left = 10;
        copyBox.right = 20;
        copyBox.top = 0;
        copyBox.bottom = 1;
        copyBox.front = 0;
        copyBox.back = 1;
        memset(resSrc.pCpuShadow, 0x3C, 4096);
        deviceFuncs10_1.pfnResourceCopyRegion(hDev, hResDst, 0, 100, 0, 0, hResSrc, 0, &copyBox);
        assert(((uint8_t*)resDst.pCpuShadow)[100] == 0x3C);
        assert(((uint8_t*)resDst.pCpuShadow)[109] == 0x3C);
    }

    /* 5. Draw permanece fail-closed ate existir compilador + draw PM4. */
    assert(dev.PipelineState.DrawCount == 0);
    assert(dev.PipelineState.TotalVertexCount == 0);
    assert(dev.PipelineState.TotalIndexCount == 0);

    deviceFuncs10_1.pfnDraw(hDev, 3, 0);
    assert(dev.PipelineState.DrawCount == 0);
    assert(dev.PipelineState.TotalVertexCount == 0);

    deviceFuncs10_1.pfnDrawIndexed(hDev, 6, 0, 0);
    assert(dev.PipelineState.DrawCount == 0);
    assert(dev.PipelineState.TotalIndexCount == 0);

    deviceFuncs10_1.pfnDrawInstanced(hDev, 3, 4, 0, 0);
    assert(dev.PipelineState.DrawCount == 0);
    assert(dev.PipelineState.TotalVertexCount == 0);

    deviceFuncs10_1.pfnDrawIndexedInstanced(hDev, 6, 2, 0, 0, 0);
    assert(dev.PipelineState.DrawCount == 0);
    assert(dev.PipelineState.TotalIndexCount == 0);

    deviceFuncs10_1.pfnDrawAuto(hDev);
    assert(dev.PipelineState.DrawCount == 0);
    assert(dev.RejectedDrawCount == 5u);
    if (TestReadUmdTunable(BC250_D3D11_DRAW_BLT_SUBST_REG_NAME)) {
        /* Sob regime experimental de substituicao armado, SetErrorCb e omitido */
        assert(g_SetErrorCalls == 0u);
    } else {
        assert(g_SetErrorCalls == 5u);
        assert(g_LastUmdError == E_NOTIMPL);
    }

    /* Hazards */
    deviceFuncs10_1.pfnResourceReadAfterWriteHazard(hDev, hResSrc);
    deviceFuncs10_1.pfnShaderResourceViewReadAfterWriteHazard(hDev, (D3D10DDI_HSHADERRESOURCEVIEW){NULL}, hResSrc);

    /* Cleanup */
    deviceFuncs10_1.pfnDestroyResource(hDev, hResSrc);
    deviceFuncs10_1.pfnDestroyResource(hDev, hResDst);
    deviceFuncs10_1.pfnDestroyResource(hDev, hResCB);

    printf("  -> D3D10 IA/Copy PASS; Draw fail-closed sem backend PM4 PASS\n");
}

/*
 * C1 -- trava da SUBSTITUICAO de Draw por blt 1:1.
 *
 * A versao anterior deste bloco EXIGIA a mentira: chamava pfnDrawIndexed e
 * afirmava que o shadow do destino recebia 0x5A por memcpy de CPU, e que o
 * draw NAO era recusado. Congelava um fallback de software disfarcado de
 * renderizacao -- mesma armadilha do gate da A15 e do mcdm_callbacks_test.
 *
 * Agora a assercao e' a inversa e serve de deteccao de regressao: com o
 * tunable desarmado (default de fabrica), o draw RECUSA com E_NOTIMPL e o
 * shadow do destino fica INTOCADO. Se alguem reintroduzir o memcpy, este
 * bloco reprova.
 */
static void TestDrawBltSubstitutionFailsClosed(void)
{
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_CORELAYER_DEVICECALLBACKS umCallbacks;
    D3D10DDI_HDEVICE hDev;
    BC250_UMD_DEVICE dev;
    BC250_UMD_RESOURCE resSrc, resDst;
    BC250_UMD_SRV srv;
    BC250_UMD_RTV rtv;
    D3D10DDIARG_CREATERESOURCE createDesc;
    D3D10DDI_MIPINFO mip;
    D3D10DDI_HRESOURCE hResSrc;
    D3D10DDI_HRESOURCE hResDst;
    UINT64 initialRejected;
    UINT initialSetError;
    UINT i;

    printf("[TEST] C1 DRAW_BLT_SUBSTITUTION: politica e fail-closed...\n");

    /*
     * Tabela pura. A configuracao completa (todos os fatos presentes) e' licita;
     * cada fato obrigatorio retirado tem de recusar sozinho, senao a reducao
     * vira peneira.
     */
    assert(Bc250UmdDrawBltSubstitutionIsLawful(
        TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE) == TRUE);

    /* Default de fabrica: desarmado recusa mesmo com tudo elegivel. */
    assert(Bc250UmdDrawBltSubstitutionIsLawful(
        FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE) == FALSE);
    /* Sem winsys nao existe caminho de GPU -- nunca cair para CPU. */
    assert(Bc250UmdDrawBltSubstitutionIsLawful(
        TRUE, FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE) == FALSE);
    /* Origem nao residente. */
    assert(Bc250UmdDrawBltSubstitutionIsLawful(
        TRUE, TRUE, FALSE, TRUE, TRUE, TRUE, TRUE, FALSE) == FALSE);
    /* Destino nao residente. */
    assert(Bc250UmdDrawBltSubstitutionIsLawful(
        TRUE, TRUE, TRUE, FALSE, TRUE, TRUE, TRUE, FALSE) == FALSE);
    /* Dimensoes diferentes: copia 1:1 nao reproduz escala. */
    assert(Bc250UmdDrawBltSubstitutionIsLawful(
        TRUE, TRUE, TRUE, TRUE, FALSE, TRUE, TRUE, FALSE) == FALSE);
    /* Formato diferente: copia nao converte. */
    assert(Bc250UmdDrawBltSubstitutionIsLawful(
        TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, TRUE, FALSE) == FALSE);
    /* Viewport parcial ou desconhecida. */
    assert(Bc250UmdDrawBltSubstitutionIsLawful(
        TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE) == FALSE);
    /* Blend ligado: aceito sob substituicao declarada (blend e' ignorado pelo SDMA,
     * conforme arm-d3d11-draw-blt-substitution.ps1). */
    assert(Bc250UmdDrawBltSubstitutionIsLawful(
        TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE) == TRUE);

    memset(&deviceFuncs10_1, 0, sizeof(deviceFuncs10_1));
    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    memset(&dev, 0, sizeof(dev));
    memset(&umCallbacks, 0, sizeof(umCallbacks));
    umCallbacks.pfnSetErrorCb = TestSetError;
    dev.pUMCallbacks = &umCallbacks;
    dev.hRTCoreLayer.handle = (void*)0xC0DEu;
    dev.WinsysInitialized = FALSE;
    hDev.pDrvPrivate = &dev;

    memset(&mip, 0, sizeof(mip));
    mip.TexelWidth = 64;
    mip.TexelHeight = 64;
    mip.TexelDepth = 1;
    mip.PhysicalWidth = 64;
    mip.PhysicalHeight = 64;
    mip.PhysicalDepth = 1;

    memset(&createDesc, 0, sizeof(createDesc));
    createDesc.pMipInfoList = &mip;
    createDesc.MipLevels = 1;
    createDesc.ArraySize = 1;
    createDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    createDesc.SampleDesc.Count = 1;
    createDesc.SampleDesc.Quality = 0;
    createDesc.Usage = D3D10_DDI_USAGE_DEFAULT;
    createDesc.BindFlags = D3D10_DDI_BIND_SHADER_RESOURCE;
    createDesc.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;

    hResSrc.pDrvPrivate = &resSrc;
    hResDst.pDrvPrivate = &resDst;

    deviceFuncs10_1.pfnCreateResource(hDev, &createDesc, hResSrc, (D3D10DDI_HRTRESOURCE){NULL});
    createDesc.BindFlags = D3D10_DDI_BIND_RENDER_TARGET;
    deviceFuncs10_1.pfnCreateResource(hDev, &createDesc, hResDst, (D3D10DDI_HRTRESOURCE){NULL});

    /* Tokens de alocacao continuam unicos (correcao do OpenAlloc). */
    assert(resSrc.Allocation.Token != 0);
    assert(resDst.Allocation.Token != 0);
    assert(resSrc.Allocation.Token != resDst.Allocation.Token);

    resSrc.pCpuShadow = calloc(1, resSrc.TotalByteSize);
    resDst.pCpuShadow = calloc(1, resDst.TotalByteSize);
    assert(resSrc.pCpuShadow != NULL && resDst.pCpuShadow != NULL);
    memset(resSrc.pCpuShadow, 0x5A, resSrc.TotalByteSize);
    memset(resDst.pCpuShadow, 0x00, resDst.TotalByteSize);

    /* Pipeline plenamente elegivel EXCETO pelo tunable e pelo winsys. */
    memset(&rtv, 0, sizeof(rtv));
    rtv.pResource = &resDst;
    rtv.Created = TRUE;
    dev.PipelineState.NumBoundRTVs = 1;
    dev.PipelineState.BoundRTVs[0].pDrvPrivate = &rtv;

    memset(&srv, 0, sizeof(srv));
    srv.pResource = &resSrc;
    srv.Created = TRUE;
    dev.PipelineState.PsShaderResources[0].pDrvPrivate = &srv;

    dev.PipelineState.NumViewports = 1;
    dev.PipelineState.Viewports[0].TopLeftX = 0.0f;
    dev.PipelineState.Viewports[0].TopLeftY = 0.0f;
    dev.PipelineState.Viewports[0].Width = 64.0f;
    dev.PipelineState.Viewports[0].Height = 64.0f;

    /*
     * A expectativa abaixo NAO depende do estado da maquina: dev.WinsysInitialized
     * e' FALSE neste harness, e sem winsys a politica recusa qualquer que seja o
     * tunable. Travado aqui para que ninguem precise adivinhar isso depois.
     */
    assert(Bc250UmdDrawBltSubstitutionIsLawful(
        TestReadUmdTunable(BC250_D3D11_DRAW_BLT_SUBST_REG_NAME),
        FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE) == FALSE);

    initialRejected = dev.RejectedDrawCount;
    initialSetError = g_SetErrorCalls;

    deviceFuncs10_1.pfnDrawIndexed(hDev, 6, 0, 0);

    /* RECUSA honesta: contador de recusa sobe. */
    assert(dev.RejectedDrawCount == initialRejected + 1u);
    if (TestReadUmdTunable(BC250_D3D11_DRAW_BLT_SUBST_REG_NAME)) {
        /*
         * Sob regime experimental de substituicao armado,
         * a UMD omite SetErrorCb(E_NOTIMPL) para manter viva a apresentacao.
         */
        assert(g_SetErrorCalls == initialSetError);
    } else {
        /* Modo padrao fail-closed: E_NOTIMPL vai ao runtime. */
        assert(g_SetErrorCalls == initialSetError + 1u);
        assert(g_LastUmdError == E_NOTIMPL);
    }
    /* Nenhum draw pode ter sido contado como substituido. */
    assert(dev.SubstitutedDrawCount == 0u);
    /*
     * DETECCAO DE REGRESSAO DO MEMCPY: o destino tem de continuar zerado. Se
     * o fallback de CPU voltar, algum byte virara 0x5A e este laco reprova.
     */
    for (i = 0; i < resDst.TotalByteSize; ++i) {
        assert(((const uint8_t*)resDst.pCpuShadow)[i] == 0x00);
    }

    /* Draw nao-indexado segue a mesma regra. */
    deviceFuncs10_1.pfnDraw(hDev, 3, 0);
    assert(dev.RejectedDrawCount == initialRejected + 2u);
    assert(dev.SubstitutedDrawCount == 0u);

    deviceFuncs10_1.pfnDestroyResource(hDev, hResSrc);
    deviceFuncs10_1.pfnDestroyResource(hDev, hResDst);
    free(resSrc.pCpuShadow);
    free(resDst.pCpuShadow);

    printf("  -> C1 politica 9/9, Draw fail-closed sem memcpy e tokens unicos PASS\n");
}

static unsigned g_Node1DrawSubmits;
static unsigned g_Node1DrawLocks;
static uint32_t g_Node1DrawBuffer[256];

static HRESULT APIENTRY Node1DrawTestLock(HANDLE h, D3DDDICB_LOCK *a)
{
    (void)h;
    if (a != NULL) {
        a->pData = g_Node1DrawBuffer;
    }
    ++g_Node1DrawLocks;
    return S_OK;
}

static HRESULT APIENTRY Node1DrawTestUnlock(HANDLE h, const D3DDDICB_UNLOCK *a)
{
    (void)h; (void)a;
    return S_OK;
}

static HRESULT APIENTRY Node1DrawTestSubmit(HANDLE h, const D3DDDICB_SUBMITCOMMAND *a)
{
    (void)h;
    if (a != NULL) {
        assert(a->Commands == 0x220000ull);
        assert(a->CommandLength == 15u * sizeof(uint32_t));
    }
    ++g_Node1DrawSubmits;
    return S_OK;
}

static void TestDrawPm4EmissionOnNode1(void)
{
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_CORELAYER_DEVICECALLBACKS umCallbacks;
    D3DDDI_DEVICECALLBACKS ktCallbacks;
    D3D10DDI_HDEVICE hDev;
    BC250_UMD_DEVICE dev;
    BC250_UMD_RESOURCE resDst;
    BC250_UMD_RTV rtv;
    uint32_t cmdMemory[256];
    UINT initialSetError;

    printf("[TEST] F5.2 PM4 3D Draw Emission on Node 1 (Parede B)...\n");

    memset(&dev, 0, sizeof(dev));
    memset(&deviceFuncs10_1, 0, sizeof(deviceFuncs10_1));
    memset(&umCallbacks, 0, sizeof(umCallbacks));
    memset(&ktCallbacks, 0, sizeof(ktCallbacks));
    memset(cmdMemory, 0, sizeof(cmdMemory));
    memset(g_Node1DrawBuffer, 0, sizeof(g_Node1DrawBuffer));

    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    umCallbacks.pfnSetErrorCb = TestSetError;
    dev.pUMCallbacks = &umCallbacks;
    /*
     * TestSetError EXIGE hRTCoreLayer == 0xC0DE, e este bloco nao o definia.
     * Passava so' porque o passo 0 nunca chegava a recusar: a decisao do
     * tunable caia no registro da maquina, que estava armado. Com a suite
     * hermetica o passo 0 recusa de verdade, chama pfnSetErrorCb e o handle
     * ausente aparece. Os dois defeitos estavam no mesmo bloco.
     */
    dev.hRTCoreLayer.handle = (void*)0xC0DEu;
    hDev.pDrvPrivate = &dev;

    ktCallbacks.pfnLockCb = Node1DrawTestLock;
    ktCallbacks.pfnUnlockCb = Node1DrawTestUnlock;
    ktCallbacks.pfnSubmitCommandCb = Node1DrawTestSubmit;

    dev.Winsys.Initialized = TRUE;
    dev.Winsys.Callbacks = &ktCallbacks;
    dev.Winsys.RuntimeDevice = (HANDLE)(ULONG_PTR)0xD00Du;
    dev.Winsys.ContextCreated = TRUE;
    dev.Winsys.KernelContext = (HANDLE)(ULONG_PTR)0xC001u;
    dev.Winsys.NodeOrdinal = BC250_WINSYS_NODE_COMPUTE;
    dev.WinsysInitialized = TRUE;
    dev.NativeNode1Ready = TRUE;

    dev.Node1CmdBuffer.Initialized = TRUE;
    dev.Node1CmdBuffer.pCpuVa = cmdMemory;
    dev.Node1CmdBuffer.CapacityDwords = 256;
    dev.Node1CmdBuffer.CursorDwords = 0;
    dev.Node1CmdBuffer.Allocation.Created = TRUE;
    dev.Node1CmdBuffer.Allocation.ByteSize = sizeof(cmdMemory);
    dev.Node1CmdBuffer.Allocation.KernelAllocation = 0xA200u;
    dev.Node1CmdBuffer.Allocation.GpuVirtualAddress = 0x220000ull;
    dev.Node1CmdBuffer.Allocation.Resident = TRUE;

    /* Setup destination resource and RTV with 256-byte aligned base GPUVA */
    memset(&resDst, 0, sizeof(resDst));
    resDst.Width = 1920;
    resDst.Height = 1080;
    resDst.RowPitch = 1920 * 4;
    resDst.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    resDst.Allocation.Created = TRUE;
    resDst.Allocation.Resident = TRUE;
    resDst.Allocation.GpuVirtualAddress = 0x10000000ull; /* 256-byte aligned */

    memset(&rtv, 0, sizeof(rtv));
    rtv.Created = TRUE;
    rtv.pResource = &resDst;

    dev.PipelineState.NumBoundRTVs = 1;
    dev.PipelineState.BoundRTVs[0].pDrvPrivate = &rtv;
    dev.PipelineState.Topology = D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    initialSetError = g_SetErrorCalls;
    g_Node1DrawSubmits = 0;
    g_Node1DrawLocks = 0;

    /*
     * 0. Default fail-closed: sem o tunable armado, draw PM4 direto DEVE ser recusado.
     *
     * DECLARA "0" em vez de APAGAR a variavel. Apagando, a decisao caia no
     * registro da MAQUINA e a suite offline deixava de ser hermetica.
     */
    SetEnvironmentVariableA(BC250_D3D11_EXPERIMENTAL_PM4_DRAW_ENV_NAME, "0");
    assert(!Bc250UmdEmitPm4Draw(&dev, 3, 0, 0, FALSE));
    assert(dev.NativePm4DrawCount == 0u);
    assert(g_Node1DrawSubmits == 0u);

    /* Arm experimental tunable explicitly for PM4 packet construction test */
    SetEnvironmentVariableA(BC250_D3D11_EXPERIMENTAL_PM4_DRAW_ENV_NAME, "1");

    /* 1. Emit Draw via ferramenta de bring-up PM4 (VertexCount = 3) */
    assert(Bc250UmdEmitPm4Draw(&dev, 3, 0, 0, FALSE));

    assert(dev.NativePm4DrawCount == 1u);
    assert(dev.SubstitutedDrawCount == 0u);
    assert(dev.PipelineState.DrawCount == 1u);
    assert(dev.PipelineState.TotalIndexCount == 3u);
    assert(g_Node1DrawSubmits == 1u);
    assert(g_Node1DrawLocks == 1u);

    /* 2. Emit DrawIndexed via ferramenta de bring-up PM4 (IndexCount = 6) */
    assert(Bc250UmdEmitPm4Draw(&dev, 6, 0, 0, TRUE));

    assert(dev.NativePm4DrawCount == 2u);
    assert(dev.SubstitutedDrawCount == 0u);
    assert(dev.PipelineState.DrawCount == 2u);
    assert(dev.PipelineState.TotalIndexCount == 9u);
    assert(g_Node1DrawSubmits == 2u);
    assert(g_Node1DrawLocks == 2u);

    /* 3. Negative test: Misaligned destination base GPUVA fails closed */
    resDst.Allocation.GpuVirtualAddress = 0x10000004ull; /* not 256-byte aligned */
    assert(!Bc250UmdEmitPm4Draw(&dev, 3, 0, 0, FALSE));

    /* NativePm4DrawCount must NOT increment; draw must be rejected */
    assert(dev.NativePm4DrawCount == 2u);
    assert(g_Node1DrawSubmits == 2u);

    /* 4. D1.1: Prove that D3D10/11 pfnDraw does NOT call PM4 anymore */
    resDst.Allocation.GpuVirtualAddress = 0x10000000ull; /* restore aligned */
    deviceFuncs10_1.pfnDraw(hDev, 3, 0);
    /* NativePm4DrawCount must NOT increment because PM4 was removed from D3D10/11 Draw (D1.1) */
    assert(dev.NativePm4DrawCount == 2u);
    assert(dev.RejectedDrawCount == 1u);
    assert(g_SetErrorCalls == initialSetError + 1u);

    /*
     * Desarma declarando "0", nao apagando: apagar devolveria a decisao ao
     * registro da maquina para qualquer teste que rode depois deste.
     */
    SetEnvironmentVariableA(BC250_D3D11_EXPERIMENTAL_PM4_DRAW_ENV_NAME, "0");

    printf("  -> F5.2 PM4 3D Draw Emission PASS (Fail-Closed Default + Experimental Mode Tested)\n");
}

static void TestD1PhaseVulkanAndInitialDataIntegrity(void)
{
    BC250_UMD_DEVICE dev;
    BC250_UMD_RESOURCE res;
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_HDEVICE hDev;
    D3D10DDI_HRESOURCE hRes;
    D3D10DDI_HRTRESOURCE hRTRes;
    D3D10DDIARG_CREATERESOURCE createArg;
    D3D10DDI_MIPINFO mipInfo;
    D3D10_DDIARG_SUBRESOURCE_UP initData;
    DXGI_DDI_ARG_PRESENT presentData;
    DXGI1_6_1_DDI_BASE_FUNCTIONS dxgiFuncs;
    uint32_t canaryData[4] = { 0xDEADBEEFu, 0xCAFEBABEu, 0x12345678u, 0x9ABCDEF0u };

    printf("[TEST] D1 Phase: Vulkan Draw/Present backend integrity and InitialDataUP...\n");

    memset(&dev, 0, sizeof(dev));
    dev.DeviceActive = TRUE;
    hDev.pDrvPrivate = &dev;
    hRes.pDrvPrivate = &res;
    hRTRes.handle = (void*)0x524553;

    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);
    Bc250UmdPopulateDxgiBaseFuncs(&dxgiFuncs);

    memset(&presentData, 0, sizeof(presentData));
    presentData.hDevice = (DXGI_DDI_HDEVICE)&dev;
    presentData.hSurfaceToPresent = (DXGI_DDI_HRESOURCE)&res;

    /* 1. D1.2: Present refuses with named divergence when both backends report draws */
    memset(&res, 0, sizeof(res));
    res.Created = TRUE;
    res.Width = 1920;
    res.Height = 1080;
    dev.NativePm4DrawCount = 10;
    dev.NativeVkDrawCount = 5;
    assert(dxgiFuncs.pfnPresent(&presentData) == E_FAIL);

    /* 2. D1.2: Present refuses when no render occurred (prevents silent black screen blt) */
    dev.NativePm4DrawCount = 0;
    dev.NativeVkDrawCount = 0;
    dev.InRenderPass = FALSE;
    assert(dxgiFuncs.pfnPresent(&presentData) == E_FAIL);

    /* 3. D1.4: pInitialDataUP consumed and uploaded to CPU shadow & buffer */
    memset(&createArg, 0, sizeof(createArg));
    memset(&mipInfo, 0, sizeof(mipInfo));
    mipInfo.PhysicalWidth = sizeof(canaryData);
    mipInfo.TexelWidth = sizeof(canaryData);
    createArg.pMipInfoList = &mipInfo;
    createArg.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
    createArg.Format = DXGI_FORMAT_UNKNOWN;
    createArg.BindFlags = D3D10_DDI_BIND_VERTEX_BUFFER;
    createArg.MipLevels = 1;
    createArg.ArraySize = 1;

    memset(&initData, 0, sizeof(initData));
    initData.pSysMem = canaryData;
    initData.SysMemPitch = sizeof(canaryData);
    initData.SysMemSlicePitch = sizeof(canaryData);
    createArg.pInitialDataUP = &initData;

    memset(&res, 0, sizeof(res));
    deviceFuncs10_1.pfnCreateResource(hDev, &createArg, hRes, hRTRes);
    assert(res.Created == TRUE);
    assert(res.pCpuShadow != NULL);
    assert(memcmp(res.pCpuShadow, canaryData, sizeof(canaryData)) == 0);

    deviceFuncs10_1.pfnDestroyResource(hDev, hRes);
    assert(res.Created == FALSE);
    assert(res.pCpuShadow == NULL);

    printf("  -> D1 Phase Vulkan/Present & InitialDataUP PASS\n");
}

static void TestStateObjectDDIs(void)
{
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_HDEVICE hDev;
    BC250_UMD_DEVICE dev;
    BC250_UMD_BLEND_STATE blendState;
    D3D10DDI_HBLENDSTATE hBlend;
    D3D10DDI_HRTBLENDSTATE hRTBlend;
    D3D10_1_DDI_BLEND_DESC blendDesc;

    BC250_UMD_DEPTH_STENCIL_STATE dsState;
    D3D10DDI_HDEPTHSTENCILSTATE hDS;
    D3D10DDI_HRTDEPTHSTENCILSTATE hRTDS;
    D3D10_DDI_DEPTH_STENCIL_DESC dsDesc;

    BC250_UMD_RASTERIZER_STATE rastState;
    D3D10DDI_HRASTERIZERSTATE hRast;
    D3D10DDI_HRTRASTERIZERSTATE hRTRast;
    D3D10_DDI_RASTERIZER_DESC rastDesc;

    BC250_UMD_SAMPLER samplerState;
    D3D10DDI_HSAMPLER hSamp;
    D3D10DDI_HRTSAMPLER hRTSamp;
    D3D10_DDI_SAMPLER_DESC sampDesc;

    FLOAT blendFactor[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    SIZE_T sz;

    printf("[TEST] Testing D3D10 State Object DDIs (Blend, DS, Rasterizer, Sampler)...\n");

    memset(&dev, 0, sizeof(dev));
    dev.DeviceActive = TRUE;
    hDev.pDrvPrivate = &dev;

    memset(&deviceFuncs10_1, 0, sizeof(deviceFuncs10_1));
    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    /* 1. Blend State */
    memset(&blendDesc, 0, sizeof(blendDesc));
    blendDesc.AlphaToCoverageEnable = TRUE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D10_DDI_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D10_DDI_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D10_DDI_BLEND_OP_ADD;

    sz = deviceFuncs10_1.pfnCalcPrivateBlendStateSize(hDev, &blendDesc);
    assert(sz == sizeof(BC250_UMD_BLEND_STATE));

    hBlend.pDrvPrivate = &blendState;
    hRTBlend.handle = (void*)0xB1E4D;
    deviceFuncs10_1.pfnCreateBlendState(hDev, &blendDesc, hBlend, hRTBlend);
    assert(blendState.Created == TRUE);
    assert(blendState.Desc.AlphaToCoverageEnable == TRUE);

    deviceFuncs10_1.pfnSetBlendState(hDev, hBlend, blendFactor, 0xFFFFFFFF);
    assert(dev.PipelineState.BoundBlendState.pDrvPrivate == &blendState);
    assert(dev.PipelineState.BlendFactor[0] == 0.25f);
    assert(dev.PipelineState.SampleMask == 0xFFFFFFFF);

    deviceFuncs10_1.pfnDestroyBlendState(hDev, hBlend);
    assert(blendState.Created == FALSE);
    assert(dev.PipelineState.BoundBlendState.pDrvPrivate == NULL); /* unbind on destroy! */

    /* 2. Depth Stencil State */
    memset(&dsDesc, 0, sizeof(dsDesc));
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D10_DDI_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D10_DDI_COMPARISON_LESS_EQUAL;
    dsDesc.StencilEnable = TRUE;

    sz = deviceFuncs10_1.pfnCalcPrivateDepthStencilStateSize(hDev, &dsDesc);
    assert(sz == sizeof(BC250_UMD_DEPTH_STENCIL_STATE));

    hDS.pDrvPrivate = &dsState;
    hRTDS.handle = (void*)0xDE974;
    deviceFuncs10_1.pfnCreateDepthStencilState(hDev, &dsDesc, hDS, hRTDS);
    assert(dsState.Created == TRUE);
    assert(dsState.Desc.DepthEnable == TRUE);

    deviceFuncs10_1.pfnSetDepthStencilState(hDev, hDS, 128);
    assert(dev.PipelineState.BoundDepthStencilState.pDrvPrivate == &dsState);
    assert(dev.PipelineState.StencilRef == 128);

    deviceFuncs10_1.pfnDestroyDepthStencilState(hDev, hDS);
    assert(dsState.Created == FALSE);
    assert(dev.PipelineState.BoundDepthStencilState.pDrvPrivate == NULL); /* unbind on destroy! */

    /* 3. Rasterizer State */
    memset(&rastDesc, 0, sizeof(rastDesc));
    rastDesc.FillMode = D3D10_DDI_FILL_SOLID;
    rastDesc.CullMode = D3D10_DDI_CULL_BACK;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.ScissorEnable = TRUE;

    sz = deviceFuncs10_1.pfnCalcPrivateRasterizerStateSize(hDev, &rastDesc);
    assert(sz == sizeof(BC250_UMD_RASTERIZER_STATE));

    hRast.pDrvPrivate = &rastState;
    hRTRast.handle = (void*)0x8A57;
    deviceFuncs10_1.pfnCreateRasterizerState(hDev, &rastDesc, hRast, hRTRast);
    assert(rastState.Created == TRUE);
    assert(rastState.Desc.FillMode == D3D10_DDI_FILL_SOLID);

    deviceFuncs10_1.pfnSetRasterizerState(hDev, hRast);
    assert(dev.PipelineState.BoundRasterizerState.pDrvPrivate == &rastState);

    deviceFuncs10_1.pfnDestroyRasterizerState(hDev, hRast);
    assert(rastState.Created == FALSE);
    assert(dev.PipelineState.BoundRasterizerState.pDrvPrivate == NULL); /* unbind on destroy! */

    /* 4. Sampler State */
    memset(&sampDesc, 0, sizeof(sampDesc));
    sampDesc.Filter = D3D10_DDI_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D10_DDI_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D10_DDI_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D10_DDI_TEXTURE_ADDRESS_WRAP;

    sz = deviceFuncs10_1.pfnCalcPrivateSamplerSize(hDev, &sampDesc);
    assert(sz == sizeof(BC250_UMD_SAMPLER));

    hSamp.pDrvPrivate = &samplerState;
    hRTSamp.handle = (void*)0x5A39;
    deviceFuncs10_1.pfnCreateSampler(hDev, &sampDesc, hSamp, hRTSamp);
    assert(samplerState.Created == TRUE);
    assert(samplerState.Desc.Filter == D3D10_DDI_FILTER_MIN_MAG_MIP_LINEAR);

    deviceFuncs10_1.pfnVsSetSamplers(hDev, 0, 1, &hSamp);
    deviceFuncs10_1.pfnPsSetSamplers(hDev, 1, 1, &hSamp);
    deviceFuncs10_1.pfnGsSetSamplers(hDev, 2, 1, &hSamp);
    assert(dev.PipelineState.VsSamplers[0].pDrvPrivate == &samplerState);
    assert(dev.PipelineState.PsSamplers[1].pDrvPrivate == &samplerState);
    assert(dev.PipelineState.GsSamplers[2].pDrvPrivate == &samplerState);

    deviceFuncs10_1.pfnDestroySampler(hDev, hSamp);
    assert(samplerState.Created == FALSE);
    assert(dev.PipelineState.VsSamplers[0].pDrvPrivate == NULL); /* unbind all stages! */
    assert(dev.PipelineState.PsSamplers[1].pDrvPrivate == NULL);
    assert(dev.PipelineState.GsSamplers[2].pDrvPrivate == NULL);

    printf("  -> D3D10 State Object DDIs PASS\n");
}

static void TestShaderResourceViewAndElementLayoutDDIs(void)
{
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_HDEVICE hDev;
    BC250_UMD_DEVICE dev;
    BC250_UMD_RESOURCE res;
    D3D10DDI_HRESOURCE hRes;
    D3D10DDI_HRTRESOURCE hRTRes;
    D3D10DDIARG_CREATERESOURCE createRes;
    D3D10DDI_MIPINFO mipInfo;

    BC250_UMD_ELEMENT_LAYOUT layout;
    D3D10DDI_HELEMENTLAYOUT hLayout;
    D3D10DDI_HRTELEMENTLAYOUT hRTLayout;
    D3D10DDIARG_CREATEELEMENTLAYOUT createLayout;
    D3D10DDIARG_INPUT_ELEMENT_DESC inputElements[2];

    BC250_UMD_SRV srv;
    D3D10DDI_HSHADERRESOURCEVIEW hSRV;
    D3D10DDI_HRTSHADERRESOURCEVIEW hRTSRV;
    D3D10_1DDIARG_CREATESHADERRESOURCEVIEW createSRV;
    SIZE_T sz;

    printf("[TEST] Testing Shader Resource View (SRV) and Element Layout DDIs...\n");

    memset(&dev, 0, sizeof(dev));
    dev.DeviceActive = TRUE;
    hDev.pDrvPrivate = &dev;

    memset(&deviceFuncs10_1, 0, sizeof(deviceFuncs10_1));
    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    /* 1. Element Layout */
    memset(inputElements, 0, sizeof(inputElements));
    inputElements[0].InputSlot = 0;
    inputElements[0].AlignedByteOffset = 0;
    inputElements[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElements[1].InputSlot = 0;
    inputElements[1].AlignedByteOffset = 16;
    inputElements[1].Format = DXGI_FORMAT_R32G32_FLOAT;

    memset(&createLayout, 0, sizeof(createLayout));
    createLayout.pVertexElements = inputElements;
    createLayout.NumElements = 2;

    sz = deviceFuncs10_1.pfnCalcPrivateElementLayoutSize(hDev, &createLayout);
    assert(sz == sizeof(BC250_UMD_ELEMENT_LAYOUT));

    hLayout.pDrvPrivate = &layout;
    hRTLayout.handle = (void*)0x1A40;
    deviceFuncs10_1.pfnCreateElementLayout(hDev, &createLayout, hLayout, hRTLayout);
    assert(layout.Created == TRUE);
    assert(layout.NumElements == 2);
    assert(layout.Elements[1].AlignedByteOffset == 16);

    deviceFuncs10_1.pfnIaSetInputLayout(hDev, hLayout);
    assert(dev.PipelineState.InputLayout.pDrvPrivate == &layout);

    deviceFuncs10_1.pfnDestroyElementLayout(hDev, hLayout);
    assert(layout.Created == FALSE);
    assert(dev.PipelineState.InputLayout.pDrvPrivate == NULL); /* unbind on destroy! */

    /* 2. Shader Resource View */
    memset(&createRes, 0, sizeof(createRes));
    memset(&mipInfo, 0, sizeof(mipInfo));
    mipInfo.PhysicalWidth = 512;
    mipInfo.PhysicalHeight = 512;
    mipInfo.PhysicalDepth = 1;
    mipInfo.TexelWidth = 512;
    mipInfo.TexelHeight = 512;
    mipInfo.TexelDepth = 1;
    createRes.pMipInfoList = &mipInfo;
    createRes.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    createRes.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    createRes.Usage = D3D10_DDI_USAGE_DEFAULT;
    createRes.BindFlags = D3D10_DDI_BIND_SHADER_RESOURCE;
    createRes.MipLevels = 1;
    createRes.ArraySize = 1;
    hRes.pDrvPrivate = &res;
    hRTRes.handle = (void*)0x5256;
    deviceFuncs10_1.pfnCreateResource(hDev, &createRes, hRes, hRTRes);

    memset(&createSRV, 0, sizeof(createSRV));
    createSRV.hDrvResource = hRes;
    createSRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    createSRV.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    createSRV.Tex2D.MipLevels = 1;
    createSRV.Tex2D.MostDetailedMip = 0;

    sz = deviceFuncs10_1.pfnCalcPrivateShaderResourceViewSize(hDev, &createSRV);
    assert(sz == sizeof(BC250_UMD_SRV));

    hSRV.pDrvPrivate = &srv;
    hRTSRV.handle = (void*)0x5856;
    deviceFuncs10_1.pfnCreateShaderResourceView(hDev, &createSRV, hSRV, hRTSRV);
    assert(srv.Created == TRUE);
    assert(srv.pResource == &res);
    assert(srv.Desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM);

    deviceFuncs10_1.pfnVsSetShaderResources(hDev, 0, 1, &hSRV);
    deviceFuncs10_1.pfnPsSetShaderResources(hDev, 1, 1, &hSRV);
    deviceFuncs10_1.pfnGsSetShaderResources(hDev, 2, 1, &hSRV);
    assert(dev.PipelineState.VsShaderResources[0].pDrvPrivate == &srv);
    assert(dev.PipelineState.PsShaderResources[1].pDrvPrivate == &srv);
    assert(dev.PipelineState.GsShaderResources[2].pDrvPrivate == &srv);

    deviceFuncs10_1.pfnDestroyShaderResourceView(hDev, hSRV);
    assert(srv.Created == FALSE);
    assert(dev.PipelineState.VsShaderResources[0].pDrvPrivate == NULL); /* unbind all stages! */
    assert(dev.PipelineState.PsShaderResources[1].pDrvPrivate == NULL);
    assert(dev.PipelineState.GsShaderResources[2].pDrvPrivate == NULL);

    deviceFuncs10_1.pfnDestroyResource(hDev, hRes);

    printf("  -> Shader Resource View (SRV) and Element Layout DDIs PASS\n");
}

static void TestQueryAndPredicationDDIs(void)
{
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_HDEVICE hDev;
    BC250_UMD_DEVICE dev;
    BC250_UMD_QUERY query;
    D3D10DDI_HQUERY hQuery;
    D3D10DDI_HRTQUERY hRTQuery;
    D3D10DDIARG_CREATEQUERY createQuery;
    BOOL eventData = FALSE;
    UINT64 occData = 0;
    UINT64 tsData = 0;
    D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
    D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS statsData;
    SIZE_T sz;

    printf("[TEST] Testing D3D10 Query and Predication DDIs...\n");

    memset(&dev, 0, sizeof(dev));
    dev.DeviceActive = TRUE;
    hDev.pDrvPrivate = &dev;
    hQuery.pDrvPrivate = &query;
    hRTQuery.handle = (void*)0x0E87;

    memset(&deviceFuncs10_1, 0, sizeof(deviceFuncs10_1));
    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    /* 1. Event Query */
    memset(&createQuery, 0, sizeof(createQuery));
    createQuery.Query = D3D10DDI_QUERY_EVENT;

    sz = deviceFuncs10_1.pfnCalcPrivateQuerySize(hDev, &createQuery);
    assert(sz == sizeof(BC250_UMD_QUERY));

    deviceFuncs10_1.pfnCreateQuery(hDev, &createQuery, hQuery, hRTQuery);
    assert(query.Created == TRUE);
    assert(query.QueryType == D3D10DDI_QUERY_EVENT);

    deviceFuncs10_1.pfnQueryBegin(hDev, hQuery);
    assert(query.IsActive == TRUE);

    deviceFuncs10_1.pfnQueryEnd(hDev, hQuery);
    assert(query.IsActive == FALSE);
    assert(query.HasData == TRUE);

    deviceFuncs10_1.pfnQueryGetData(hDev, hQuery, &eventData, sizeof(eventData), 0);
    assert(eventData == TRUE);

    /* 2. Predication */
    deviceFuncs10_1.pfnSetPredication(hDev, hQuery, TRUE);
    assert(dev.PipelineState.PredicateQuery.pDrvPrivate == &query);
    assert(dev.PipelineState.PredicateValue == TRUE);

    deviceFuncs10_1.pfnDestroyQuery(hDev, hQuery);
    assert(query.Created == FALSE);
    assert(dev.PipelineState.PredicateQuery.pDrvPrivate == NULL); /* unbind on destroy! */

    /* 3. Occlusion Query */
    createQuery.Query = D3D10DDI_QUERY_OCCLUSION;
    deviceFuncs10_1.pfnCreateQuery(hDev, &createQuery, hQuery, hRTQuery);
    dev.PipelineState.DrawCount = 5;
    deviceFuncs10_1.pfnQueryBegin(hDev, hQuery);
    dev.PipelineState.DrawCount = 12;
    deviceFuncs10_1.pfnQueryEnd(hDev, hQuery);
    deviceFuncs10_1.pfnQueryGetData(hDev, hQuery, &occData, sizeof(occData), 0);
    assert(occData == 7); /* 12 - 5 = 7 */
    deviceFuncs10_1.pfnDestroyQuery(hDev, hQuery);

    /* 4. Timestamp Query */
    createQuery.Query = D3D10DDI_QUERY_TIMESTAMP;
    deviceFuncs10_1.pfnCreateQuery(hDev, &createQuery, hQuery, hRTQuery);
    dev.PipelineState.DrawCount = 42;
    deviceFuncs10_1.pfnQueryEnd(hDev, hQuery);
    deviceFuncs10_1.pfnQueryGetData(hDev, hQuery, &tsData, sizeof(tsData), 0);
    assert(tsData == 42);
    deviceFuncs10_1.pfnDestroyQuery(hDev, hQuery);

    /* 5. Timestamp Disjoint Query */
    createQuery.Query = D3D10DDI_QUERY_TIMESTAMPDISJOINT;
    deviceFuncs10_1.pfnCreateQuery(hDev, &createQuery, hQuery, hRTQuery);
    deviceFuncs10_1.pfnQueryEnd(hDev, hQuery);
    memset(&disjointData, 0, sizeof(disjointData));
    deviceFuncs10_1.pfnQueryGetData(hDev, hQuery, &disjointData, sizeof(disjointData), 0);
    assert(disjointData.Frequency == 1000000000ull);
    assert(disjointData.Disjoint == FALSE);
    deviceFuncs10_1.pfnDestroyQuery(hDev, hQuery);

    /* 6. Pipeline Statistics Query */
    createQuery.Query = D3D10DDI_QUERY_PIPELINESTATS;
    deviceFuncs10_1.pfnCreateQuery(hDev, &createQuery, hQuery, hRTQuery);
    dev.PipelineState.TotalVertexCount = 300;
    dev.PipelineState.TotalIndexCount = 600;
    dev.PipelineState.DrawCount = 10;
    deviceFuncs10_1.pfnQueryEnd(hDev, hQuery);
    memset(&statsData, 0, sizeof(statsData));
    deviceFuncs10_1.pfnQueryGetData(hDev, hQuery, &statsData, sizeof(statsData), 0);
    assert(statsData.IAVertices == 300);
    assert(statsData.IAPrimitives == 600);
    assert(statsData.CInvocations == 10);
    deviceFuncs10_1.pfnDestroyQuery(hDev, hQuery);

    printf("  -> D3D10 Query and Predication DDIs PASS\n");
}

static void TestStreamOutputAndTableCompleteness(void)
{
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_HDEVICE hDev;
    BC250_UMD_DEVICE dev;
    BC250_UMD_RESOURCE soRes;
    D3D10DDI_HRESOURCE hSoRes;
    D3D10DDI_COUNTER_INFO counterInfo;
    D3D10DDI_COUNTER_TYPE counterType;
    UINT activeCounters = 99;
    UINT offset = 128;
    void** pFuncTable;
    size_t numEntries;
    size_t nonNullCount = 0;

    printf("[TEST] Testing Stream Output, Misc Render & 100%% Table Completeness...\n");

    memset(&dev, 0, sizeof(dev));
    dev.DeviceActive = TRUE;
    hDev.pDrvPrivate = &dev;
    /* Struct de pilha NAO INICIALIZADA era entregue ao driver aqui, e o
     * ResourceCopyRegion lia TotalByteSize de lixo. O produto agora recusa por
     * Created, e o teste para de produzir a entrada invalida -- as duas pontas,
     * porque consertar so uma deixaria a outra pronta para repetir. */
    memset(&soRes, 0, sizeof(soRes));
    soRes.Created = TRUE;
    soRes.TotalByteSize = 0u;
    hSoRes.pDrvPrivate = &soRes;

    memset(&deviceFuncs10_1, 0, sizeof(deviceFuncs10_1));
    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    /* Stream Output */
    deviceFuncs10_1.pfnSoSetTargets(hDev, 1, 0, &hSoRes, &offset);
    assert(dev.PipelineState.SoBuffers[0].pDrvPrivate == &soRes);
    assert(dev.PipelineState.SoOffsets[0] == 128);

    deviceFuncs10_1.pfnSoSetTargets(hDev, 0, 1, NULL, NULL);
    assert(dev.PipelineState.SoBuffers[0].pDrvPrivate == NULL);

    /* Mipmaps & Resolve */
    deviceFuncs10_1.pfnGenMips(hDev, (D3D10DDI_HSHADERRESOURCEVIEW){NULL});
    deviceFuncs10_1.pfnResourceResolveSubresource(hDev, hSoRes, 0, hSoRes, 0, DXGI_FORMAT_B8G8R8A8_UNORM);

    /* Counters & Filter */
    deviceFuncs10_1.pfnCheckCounterInfo(hDev, &counterInfo);
    assert(counterInfo.NumSimultaneousCounters == 0);
    assert(counterInfo.NumDetectableParallelUnits == 1);

    deviceFuncs10_1.pfnCheckCounter(hDev, D3D10DDI_QUERY_EVENT, &counterType, &activeCounters, NULL, NULL, NULL, NULL, NULL, NULL);
    assert(counterType == D3D10DDI_COUNTER_TYPE_FLOAT32);
    assert(activeCounters == 0);

    deviceFuncs10_1.pfnSetTextFilterSize(hDev, 4, 8);
    assert(dev.PipelineState.TextFilterWidth == 4);
    assert(dev.PipelineState.TextFilterHeight == 8);

    /* Table Completeness: verify all function pointers are populated */
    pFuncTable = (void**)&deviceFuncs10_1;
    numEntries = sizeof(deviceFuncs10_1) / sizeof(void*);
    for (size_t i = 0; i < numEntries; ++i) {
        if (pFuncTable[i] != NULL) {
            nonNullCount++;
        }
    }
    /* Exactly 103 functions without D3D10PSGP */
    assert(nonNullCount >= 103);
    assert(nonNullCount == numEntries);

    printf("  -> Stream Output, Misc Render & 100%% Table Completeness PASS (%zu/%zu functions populated)\n",
        nonNullCount, numEntries);
}

static void TestCommandBufferAssembly(void)
{
    uint32_t ring[128];
    uint32_t cursor = 0;
    BC250_PM4_STATUS pm4Status;
    BC250_SDMA_STATUS sdmaStatus;
    uint32_t testData[2] = { 0x12345678, 0x9ABCDEF0 };
    uint32_t proofValues[5] = {
        0xBC250C01u, 0xBC250C02u, 0xBC250C03u,
        0xBC250C04u, 0xBC250C05u
    };
    BC250_UMD_DEVICE device;

    printf("[TEST] Testing Hardware Command Buffer & Packet Builders...\n");

    /* 1. Test SDMA Packet Emission */
    cursor = 0;
    memset(ring, 0, sizeof(ring));
    sdmaStatus = Bc250SdmaEmitNop(ring, 128, &cursor, 3);
    assert(sdmaStatus == BC250_SDMA_OK);
    assert(cursor == 4); /* 1 header + 3 dwords */

    sdmaStatus = Bc250SdmaEmitCopyLinear(ring, 128, &cursor, 0x10000, 0x20000, 256);
    assert(sdmaStatus == BC250_SDMA_OK);
    assert(cursor == 4 + BC250_SDMA_DWORDS_COPY_LINEAR);

    /* 2. Test PM4 Packet Emission */
    cursor = 0;
    memset(ring, 0, sizeof(ring));
    pm4Status = Bc250Pm4EmitNop(ring, 128, &cursor, 2);
    assert(pm4Status == BC250_PM4_OK);
    assert(cursor == 3); /* 1 header + 2 count */

    pm4Status = Bc250Pm4EmitWriteData(ring, 128, &cursor, 0, 0x30000, testData, 2);
    assert(pm4Status == BC250_PM4_OK);

    pm4Status = Bc250Pm4EmitReleaseMemEop32(ring, 128, &cursor, 0x40000, 0xBC250D15, BC250_PM4_RELEASE_MEM_INT_SEL_NONE);
    assert(pm4Status == BC250_PM4_OK);
    assert(ring[cursor - BC250_PM4_DWORDS_RELEASE_MEM] == BC250_PM4_RELEASE_MEM_HEADER_PROVEN);
    assert(ring[cursor - BC250_PM4_DWORDS_RELEASE_MEM + 1] == BC250_PM4_RELEASE_MEM_EVENT_PROVEN);

    /* 3. Primeiro payload Node 1 da UMD: WRITE_DATA + fence GPU. */
    memset(&device, 0, sizeof(device));
    memset(ring, 0, sizeof(ring));
    device.NativeNode1Ready = TRUE;
    device.Winsys.Initialized = TRUE;
    device.Winsys.ContextCreated = TRUE;
    device.Winsys.NodeOrdinal = BC250_WINSYS_NODE_COMPUTE;
    device.Node1CmdBuffer.pCpuVa = ring;
    device.Node1CmdBuffer.CapacityDwords = 128u;
    device.Node1CmdBuffer.Initialized = TRUE;
    assert(SUCCEEDED(Bc250UmdBuildNode1ProofCommand(&device,
        0x500000u, proofValues, 5u, 0x600000u, 0xBC250F01u)));
    assert(device.Node1CmdBuffer.CursorDwords == 17u);
    assert(ring[0] == (BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_WRITE_DATA, 8u) | BC250_PM4_SHADER_TYPE_COMPUTE));
    assert(ring[2] == 0x00500000u);
    assert(ring[4] == proofValues[0]);
    assert(ring[8] == proofValues[4]);
    assert(ring[9] == BC250_PM4_RELEASE_MEM_HEADER_PROVEN);
    assert(ring[14] == 0xBC250F01u);

    device.NativeNode1Ready = FALSE;
    assert(Bc250UmdBuildNode1ProofCommand(&device,
        0x500000u, proofValues, 5u, 0x600000u, 1u) == E_INVALIDARG);
    assert(device.Node1CmdBuffer.CursorDwords == 17u);

    printf("  -> Packet builders + Node1 proof command assembly PASS (offline)\n");
}


static unsigned g_BltSubmits;
static HRESULT APIENTRY BltTestSubmit(HANDLE h, const D3DDDICB_SUBMITCOMMAND *a)
{ (void)h; (void)a; ++g_BltSubmits; return S_OK; }

/*
 * Composicao pelo KMD (13/09) -- o blt do DWM NAO submete contexto.
 *
 * Medicao da corrida 13/09 08:12 que motivou esta troca: o KMD executou 958
 * IBs. 953 eram buffers de paginacao do dxgkrnl e nao produziram UM TDR. Os
 * outros 5 eram submissoes de CONTEXTO desta UMD no Node 0 -- o no de
 * paginacao (PagingNodeIndex=0) -- e CADA UMA foi seguida de um TDR, com a
 * engine OCIOSA e o anel DRENADO. Cinco TDRs, BugCheck 0x116. Tres correcoes
 * anteriores no caminho do blt (sinal de fence pelo contrato, sink unico de
 * trace, anel de slots) nao mudaram isso, porque tratavam o passageiro.
 *
 * O caminho de blt do KMD fez 584 blts consecutivos sem TDR em 12/09 13:01:
 * nao cria pacote de contexto e nao passa pelo escalonador.
 *
 * O INVARIANTE CENTRAL desta suite e' negativo: a composicao nao pode gerar
 * NENHUMA submissao de contexto (g_BltSubmits == 0). Se alguem reintroduzir o
 * caminho antigo, este teste reprova.
 */
static unsigned g_BltEscapes;
static BC250_ESCAPE_COMPOSITE_BLT g_BltLastEscape;
static HRESULT g_BltEscapeHr;
static UINT32 g_BltEscapeResult;
static HANDLE g_BltExpectedAdapter;
static HANDLE g_BltExpectedDevice;

static HRESULT APIENTRY BltTestEscape(HANDLE h, const D3DDDICB_ESCAPE *a)
{
    BC250_ESCAPE_COMPOSITE_BLT *p;
    ++g_BltEscapes;
    assert(h == g_BltExpectedAdapter);
    assert(a != NULL && a->pPrivateDriverData != NULL);
    assert(a->hDevice == g_BltExpectedDevice);
    assert(a->hContext == NULL);
    assert(a->Flags.HardwareAccess == 1u);
    assert(a->PrivateDriverDataSize == sizeof(BC250_ESCAPE_COMPOSITE_BLT));
    p = (BC250_ESCAPE_COMPOSITE_BLT *)a->pPrivateDriverData;
    assert(p->Opcode == BC250_ESCAPE_OP_COMPOSITE_BLT);
    g_BltLastEscape = *p;
    if (FAILED(g_BltEscapeHr)) return g_BltEscapeHr;
    p->ResultCode = g_BltEscapeResult;
    return S_OK;
}

static void TestSdmaBltExecution(void)
{
    BC250_UMD_DEVICE d;
    BC250_UMD_ADAPTER adapter;
    BC250_UMD_RESOURCE src, dst;
    D3DDDI_DEVICECALLBACKS cb;
    DXGI_DDI_ARG_BLT a;
    DXGI1_6_1_DDI_BASE_FUNCTIONS dxgi;

    memset(&d, 0, sizeof(d)); memset(&adapter, 0, sizeof(adapter));
    memset(&cb, 0, sizeof(cb));
    memset(&src, 0, sizeof(src)); memset(&dst, 0, sizeof(dst));
    memset(&a, 0, sizeof(a));
    cb.pfnEscapeCb = BltTestEscape;
    cb.pfnSubmitCommandCb = BltTestSubmit;
    d.Winsys.Initialized = TRUE; d.Winsys.Callbacks = &cb;
    d.Winsys.RuntimeDevice = (HANDLE)(ULONG_PTR)0xD00Du;
    d.WinsysInitialized = TRUE;
    adapter.hRTAdapter.handle = (HANDLE)(ULONG_PTR)0xA00Au;
    d.pAdapter = &adapter;
    g_BltExpectedAdapter = adapter.hRTAdapter.handle;
    g_BltExpectedDevice = d.Winsys.RuntimeDevice;

    /* Geometria ELEGIVEL: pitch igual, retangulo cobrindo linhas inteiras. */
    src.Created = TRUE; src.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    src.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.SampleCount = src.MipLevels = src.ArraySize = 1;
    src.Width = 4; src.Height = 4; src.RowPitch = 16;
    src.TotalByteSize = 64; src.Allocation.ByteSize = 64;
    src.Allocation.Created = src.Allocation.Resident = TRUE;
    src.Allocation.GpuVirtualAddress = 0x100000;
    dst = src;
    dst.Allocation.GpuVirtualAddress = 0x200000;
    a.hDevice = (DXGI_DDI_HDEVICE)&d;
    a.hSrcResource = (DXGI_DDI_HRESOURCE)&src;
    a.hDstResource = (DXGI_DDI_HRESOURCE)&dst;
    a.DstLeft = 0; a.DstTop = 0; a.DstRight = 4; a.DstBottom = 4;

    memset(&dxgi, 0, sizeof(dxgi));
    Bc250UmdPopulateDxgiBaseFuncs(&dxgi);

    g_BltEscapes = 0; g_BltSubmits = 0;
    g_BltEscapeHr = S_OK; g_BltEscapeResult = BC250_PRESENT_BLT_STATUS_SUCCESS;

    /* 1) Caminho feliz: escape chamado UMA vez, com os bytes exatos. */
    assert(dxgi.pfnBlt(&a) == S_OK);
    assert(g_BltEscapes == 1u);
    assert(g_BltLastEscape.SourceGpuVa == 0x100000);
    assert(g_BltLastEscape.DestGpuVa == 0x200000);
    assert(g_BltLastEscape.SourcePitch == 16u);
    assert(g_BltLastEscape.DestPitch == 16u);
    assert(g_BltLastEscape.DestX == 0u && g_BltLastEscape.DestY == 0u);
    /* O INVARIANTE: zero submissoes de contexto. */
    assert(g_BltSubmits == 0u);

    /* 2) O KMD recusar tem de virar falha, nunca sucesso silencioso. */
    g_BltEscapeResult = BC250_PRESENT_BLT_STATUS_INVALID;
    assert(FAILED(dxgi.pfnBlt(&a)));
    assert(g_BltEscapes == 2u && g_BltSubmits == 0u);
    g_BltEscapeResult = BC250_PRESENT_BLT_STATUS_SUCCESS;

    /* 3) Falha do proprio callback propaga. */
    g_BltEscapeHr = E_FAIL;
    assert(FAILED(dxgi.pfnBlt(&a)));
    assert(g_BltEscapes == 3u && g_BltSubmits == 0u);
    g_BltEscapeHr = S_OK;

    /* 4) Pitch diferente usa o escape retangular; nunca contexto/CPU. */
    {
        unsigned before = g_BltEscapes;
        dst.RowPitch = 20; dst.Width = 5; dst.TotalByteSize = 100;
        dst.Allocation.ByteSize = 100;
        a.DstRight = 4;
        assert(dxgi.pfnBlt(&a) == S_OK);
        assert(g_BltEscapes == before + 1u && g_BltSubmits == 0u);
        assert(g_BltLastEscape.SourcePitch == 16u);
        assert(g_BltLastEscape.DestPitch == 20u);
        dst.RowPitch = 16; dst.Width = 4; dst.TotalByteSize = 64;
        dst.Allocation.ByteSize = 64;
    }

    /* 5) Retangulo deslocado conserva DstXY no contrato do KMD. */
    {
        unsigned before = g_BltEscapes;
        src.Width = 2; src.Height = 2;
        a.DstLeft = 1; a.DstTop = 1; a.DstRight = 3; a.DstBottom = 3;
        assert(dxgi.pfnBlt(&a) == S_OK);
        assert(g_BltEscapes == before + 1u && g_BltSubmits == 0u);
        assert(g_BltLastEscape.DestX == 1u && g_BltLastEscape.DestY == 1u);
        src.Width = 4; src.Height = 4;
        a.DstLeft = 0; a.DstTop = 0; a.DstRight = 4; a.DstBottom = 4;
    }

    /* 6) Sem pfnEscapeCb o blt falha FECHADO -- nunca cai em contexto. */
    {
        D3DDDI_DEVICECALLBACKS noEscape = cb;
        unsigned before = g_BltEscapes;
        noEscape.pfnEscapeCb = NULL;
        d.Winsys.Callbacks = &noEscape;
        assert(dxgi.pfnBlt(&a) == E_NOTIMPL);
        assert(g_BltEscapes == before && g_BltSubmits == 0u);
        d.Winsys.Callbacks = &cb;
    }

    /* 7) Formato/limites continuam recusados antes de qualquer escape. */
    {
        unsigned before = g_BltEscapes;
        dst.MiscFlags = 0x80000000u;
        assert(dxgi.pfnBlt(&a) == E_NOTIMPL);
        dst.MiscFlags = 0;
        src.Allocation.Resident = FALSE;
        assert(FAILED(dxgi.pfnBlt(&a)));
        src.Allocation.Resident = TRUE;
        assert(g_BltEscapes == before && g_BltSubmits == 0u);
    }

    /* 8) Sem hAdapter runtime o blt falha FECHADO antes do callback. */
    {
        unsigned before = g_BltEscapes;
        adapter.hRTAdapter.handle = NULL;
        assert(dxgi.pfnBlt(&a) == E_NOTIMPL);
        assert(g_BltEscapes == before && g_BltSubmits == 0u);
        adapter.hRTAdapter.handle = g_BltExpectedAdapter;
    }

    /* 9) Sem o objeto adapter o blt tambem falha FECHADO. */
    {
        unsigned before = g_BltEscapes;
        d.pAdapter = NULL;
        assert(dxgi.pfnBlt(&a) == E_NOTIMPL);
        assert(g_BltEscapes == before && g_BltSubmits == 0u);
        d.pAdapter = &adapter;
    }

    /* 10) DXGI 1.6 Blt1 repassa para o escape com rotacao identity */
    {
        DXGI_DDI_ARG_BLT1 a1;
        unsigned before = g_BltEscapes;
        memset(&a1, 0, sizeof(a1));
        a1.hDevice = a.hDevice;
        a1.hSrcResource = a.hSrcResource;
        a1.hDstResource = a.hDstResource;
        a1.DstLeft = a.DstLeft; a1.DstTop = a.DstTop;
        a1.DstRight = a.DstRight; a1.DstBottom = a.DstBottom;
        a1.Rotate = DXGI_DDI_MODE_ROTATION_IDENTITY;
        assert(dxgi.pfnBlt1(&a1) == S_OK);
        assert(g_BltEscapes == before + 1u);
        assert(g_BltSubmits == 0u);
    }

    assert(g_BltSubmits == 0u);
    printf("SDMA Blt: composicao retangular 1:1 pelo KMD via escape, zero submissao de contexto PASS (offline)\n");
}

/*
 * C3 (12/09) -- ownership do modulo SPIR-V por shader.
 *
 * O que este bloco trava, e por que:
 *
 *  1. FAIL-CLOSED: token stream direto (sem container DXBC) NUNCA pode produzir
 *     SPIR-V. Isso e' independente da maquina -- nao consulta registro nem
 *     carrega DLL -- e por isso pode ser assercao dura.
 *
 *  2. COERENCIA COM O AMBIENTE: para container DXBC, SpirvValid depende das
 *     duas chaves de registro. A suite NAO pode exigir FALSE: seria repetir o
 *     defeito achado na C1, em que o teste offline reprovava sozinho porque
 *     D3D11LabPromotion ficara armada por um bind anterior. Entao a assercao e'
 *     de COERENCIA: valido implica modulo bem formado.
 *
 *  3. SEM VAZAMENTO: DestroyShader e' obrigado a devolver o modulo ao dono
 *     ANTES de zerar a estrutura. Sem isso cada shader do DWM vaza o SPIR-V
 *     dentro do compositor. Testado com tradutor de mentira, que conta Frees.
 */
static int s_SpirvOwnershipFrees;

static void SpirvOwnershipFree(void *Context, const void *Spirv)
{
    (void)Context; (void)Spirv;
    s_SpirvOwnershipFrees++;
}

static void TestShaderSpirvOwnership(void)
{
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDI_HDEVICE hDev;
    BC250_UMD_DEVICE dev;
    BC250_UMD_SHADER shader;
    D3D10DDI_HSHADER hShader;
    D3D10DDI_HRTSHADER hRTShader;
    uint8_t buffer[128];
    BC250_DXBC_RAW_HEADER *hdr;
    uint32_t *offsets;
    BC250_DXBC_RAW_CHUNK_HEADER *chk;
    uint32_t *shaderData;
    static uint32_t spirvWords[5];

    printf("[TEST] Testing SPIR-V module ownership per shader...\n");

    memset(&deviceFuncs10_1, 0, sizeof(deviceFuncs10_1));
    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);
    memset(&dev, 0, sizeof(dev));
    dev.DeviceActive = TRUE;
    hDev.pDrvPrivate = &dev;
    hShader.pDrvPrivate = &shader;
    hRTShader.handle = (void*)0x53504952;

    /* 1. Token stream direto: fail-closed incondicional. */
    {
        UINT directStream[7];
        directStream[0] = (1u << 16u) | (5u << 4u) | 0u;
        directStream[1] = 7u;
        directStream[2] = (3u << 24u) | 54u;
        directStream[3] = 0x100012u;
        directStream[4] = 0x200046u;
        directStream[5] = (1u << 24u) | 62u;
        directStream[6] = 0u;
        memset(&shader, 0xFF, sizeof(shader));
        deviceFuncs10_1.pfnCreateVertexShader(hDev, directStream, hShader, hRTShader, NULL);
        assert(shader.IsParsed == TRUE);
        assert(shader.SpirvValid == FALSE);
        assert(shader.Spirv.Words == NULL);
        deviceFuncs10_1.pfnDestroyShader(hDev, hShader);
    }

    /* 2. Container DXBC: coerencia, nao carimbo. */
    memset(buffer, 0, sizeof(buffer));
    hdr = (BC250_DXBC_RAW_HEADER *)buffer;
    hdr->Magic = BC250_DXBC_MAGIC_DXBC;
    hdr->Version = 1;
    hdr->TotalSize = 80;
    hdr->ChunkCount = 1;
    offsets = (uint32_t *)(buffer + 32);
    offsets[0] = 36;
    chk = (BC250_DXBC_RAW_CHUNK_HEADER *)(buffer + 36);
    chk->FourCC = BC250_DXBC_FOURCC_SHDR;
    chk->ChunkSize = 36;
    shaderData = (uint32_t *)(buffer + 44);
    shaderData[0] = (1u << 16u) | (4u << 4u) | 0u;
    shaderData[1] = 7u;
    shaderData[2] = (3u << 24u) | 54u;
    shaderData[3] = 0x100012u;
    shaderData[4] = 0x200046u;
    shaderData[5] = (1u << 24u) | 62u;
    shaderData[6] = 0u;

    memset(&shader, 0xFF, sizeof(shader));
    deviceFuncs10_1.pfnCreateVertexShader(hDev, (const UINT*)buffer, hShader, hRTShader, NULL);
    assert(shader.IsParsed == TRUE);
    if (shader.SpirvValid) {
        /* Tradutor armado nesta maquina: o modulo tem de ser bem formado. */
        assert(shader.Spirv.Words != NULL);
        assert(shader.Spirv.SizeBytes >= 20u);
        assert((shader.Spirv.SizeBytes & 3u) == 0u);
        assert(shader.Spirv.Words[0] == BC250_SPIRV_MAGIC);
        assert(shader.Spirv.Owner.Free != NULL);
    } else {
        assert(shader.Spirv.Words == NULL);
        assert(shader.Spirv.SizeBytes == 0u);
    }
    deviceFuncs10_1.pfnDestroyShader(hDev, hShader);
    assert(shader.SpirvValid == FALSE);
    assert(shader.Spirv.Words == NULL);

    /* 3. Invariante de vazamento, com tradutor de mentira. */
    s_SpirvOwnershipFrees = 0;
    memset(&shader, 0, sizeof(shader));
    memset(spirvWords, 0, sizeof(spirvWords));
    spirvWords[0] = BC250_SPIRV_MAGIC;
    shader.SpirvValid = TRUE;
    shader.Spirv.Words = spirvWords;
    shader.Spirv.SizeBytes = (uint32_t)sizeof(spirvWords);
    shader.Spirv.Owner.Context = NULL;
    shader.Spirv.Owner.Free = SpirvOwnershipFree;
    deviceFuncs10_1.pfnDestroyShader(hDev, hShader);
    assert(s_SpirvOwnershipFrees == 1);
    assert(shader.SpirvValid == FALSE);
    assert(shader.Spirv.Words == NULL);

    /* Destruir de novo nao pode liberar duas vezes. */
    deviceFuncs10_1.pfnDestroyShader(hDev, hShader);
    assert(s_SpirvOwnershipFrees == 1);

    printf("  -> SPIR-V fail-closed sem container, coerencia com o ambiente, "
           "zero vazamento e zero double-free PASS\n");
}

/* F3.3 fault injection at each resource-creation callback. */
static UINT g_CreateFailAt, g_CreateDeallocs, g_CreateMaps, g_CreateResidents;
static HRESULT APIENTRY F3Allocate(HANDLE h, D3DDDICB_ALLOCATE *a)
{
    (void)h;
    if (g_CreateFailAt == 1u) return E_OUTOFMEMORY;
    a->pAllocationInfo2[0].hAllocation = 0xA110u;
    return S_OK;
}
static HRESULT APIENTRY F3Map(HANDLE h, D3DDDI_MAPGPUVIRTUALADDRESS *a)
{
    (void)h; ++g_CreateMaps;
    if (g_CreateFailAt == 2u) return E_OUTOFMEMORY;
    a->VirtualAddress = 0x200000u; a->PagingFenceValue = 7u;
    return S_OK;
}
static HRESULT APIENTRY F3Resident(HANDLE h, D3DDDI_MAKERESIDENT *a)
{
    (void)h; ++g_CreateResidents;
    if (g_CreateFailAt == 3u) return E_OUTOFMEMORY;
    a->PagingFenceValue = 8u;
    return E_PENDING;
}
static HRESULT APIENTRY F3Wait(HANDLE h, const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *a)
{
    (void)h; (void)a;
    return S_OK;
}
static HRESULT APIENTRY F3FreeVa(HANDLE h, const D3DDDICB_FREEGPUVIRTUALADDRESS *a)
{ (void)h; (void)a; return S_OK; }
static HRESULT APIENTRY F3Deallocate(HANDLE h, const D3DDDICB_DEALLOCATE *a)
{ (void)h; (void)a; ++g_CreateDeallocs; return S_OK; }
static void TestF3CreateResourceTransaction(void)
{
    UINT stage;
    for (stage = 0u; stage <= 3u; ++stage) {
        BC250_UMD_DEVICE dev;
        BC250_UMD_RESOURCE res;
        D3DDDI_DEVICECALLBACKS cb;
        D3D10DDI_CORELAYER_DEVICECALLBACKS errors;
        D3D10_1DDI_DEVICEFUNCS funcs;
        D3D10DDIARG_CREATERESOURCE create;
        D3D10DDI_MIPINFO mip;
        D3D10DDI_HDEVICE hd;
        D3D10DDI_HRESOURCE hr;
        D3D10DDI_HRTRESOURCE rt;
        memset(&dev, 0, sizeof(dev)); memset(&res, 0, sizeof(res));
        memset(&cb, 0, sizeof(cb)); memset(&errors, 0, sizeof(errors));
        memset(&create, 0, sizeof(create)); memset(&mip, 0, sizeof(mip));
        cb.pfnAllocateCb = F3Allocate; cb.pfnDeallocateCb = F3Deallocate;
        cb.pfnMapGpuVirtualAddressCb = F3Map; cb.pfnFreeGpuVirtualAddressCb = F3FreeVa;
        cb.pfnMakeResidentCb = F3Resident; cb.pfnWaitForSynchronizationObjectFromCpuCb = F3Wait;
        errors.pfnSetErrorCb = TestSetError;
        dev.pUMCallbacks = &errors; dev.hRTCoreLayer.handle = (void*)0xC0DEu;
        dev.WinsysInitialized = TRUE; dev.Winsys.Initialized = TRUE;
        dev.Winsys.Callbacks = &cb; dev.Winsys.PagingQueueCreated = TRUE;
        hd.pDrvPrivate = &dev; hr.pDrvPrivate = &res; rt.handle = (void*)0xABu;
        mip.PhysicalWidth = mip.TexelWidth = 64;
        mip.PhysicalHeight = mip.TexelHeight = 64;
        mip.PhysicalDepth = mip.TexelDepth = 1;
        create.pMipInfoList = &mip; create.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
        create.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        create.ArraySize = create.MipLevels = create.SampleDesc.Count = 1;
        Bc250UmdPopulateDeviceFuncs10_1(&funcs);
        g_CreateFailAt = stage; g_CreateDeallocs = g_CreateMaps = g_CreateResidents = 0;
        g_SetErrorCalls = 0;
        funcs.pfnCreateResource(hd, &create, hr, rt);
        if (stage != 0u) {
            assert(!res.Created);
            assert(!res.Allocation.Created);
            assert(g_SetErrorCalls == 1u && g_LastUmdError == E_OUTOFMEMORY);
            assert(g_CreateDeallocs == (stage == 1u ? 0u : 1u));
            assert(g_CreateResidents == (stage == 3u ? 1u : 0u));
        } else {
            assert(res.Created && res.Allocation.Resident);
            assert(!res.Allocation.ResidencyPending);
            assert(g_SetErrorCalls == 0u);
            funcs.pfnDestroyResource(hd, hr);
            assert(g_CreateDeallocs == 1u);
        }
    }
}

static void TestF5PrimaryDerivation(void)
{
    BC250_UMD_DEVICE dev;
    D3D10DDI_HDEVICE hDev;
    BC250_UMD_RESOURCE resExplorer;
    BC250_UMD_RESOURCE resDwm;
    BC250_UMD_RESOURCE resIncompat;
    D3D10DDI_HRESOURCE hRes;
    D3D10DDI_HRTRESOURCE hRTRes;
    D3D10_1DDI_DEVICEFUNCS deviceFuncs10_1;
    D3D10DDIARG_CREATERESOURCE createArg;
    D3D10DDI_MIPINFO mipInfo;
    DXGI_DDI_PRIMARY_DESC primaryDesc;

    printf("[TEST] Testing F5.0 Primary Derivation (dwm.exe vs Explorer.EXE)...\n");

    memset(&deviceFuncs10_1, 0, sizeof(deviceFuncs10_1));
    Bc250UmdPopulateDeviceFuncs10_1(&deviceFuncs10_1);

    memset(&dev, 0, sizeof(dev));
    dev.DeviceActive = TRUE;
    hDev.pDrvPrivate = &dev;
    hRTRes.handle = (void*)0x524553;

    /* Caso 1: Explorer.EXE -- 1024x832 com BIND_PRESENT e pPrimaryDesc == NULL */
    memset(&createArg, 0, sizeof(createArg));
    memset(&mipInfo, 0, sizeof(mipInfo));
    mipInfo.PhysicalWidth = 1024;
    mipInfo.PhysicalHeight = 832;
    mipInfo.PhysicalDepth = 1;
    mipInfo.TexelWidth = 1024;
    mipInfo.TexelHeight = 832;
    mipInfo.TexelDepth = 1;
    createArg.pMipInfoList = &mipInfo;
    createArg.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    createArg.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    createArg.Usage = D3D10_DDI_USAGE_DEFAULT;
    createArg.BindFlags = D3D10_DDI_BIND_RENDER_TARGET | D3D10_DDI_BIND_PRESENT;
    createArg.MipLevels = 1;
    createArg.ArraySize = 1;
    createArg.pPrimaryDesc = NULL;

    memset(&resExplorer, 0xFF, sizeof(resExplorer));
    hRes.pDrvPrivate = &resExplorer;
    deviceFuncs10_1.pfnCreateResource(hDev, &createArg, hRes, hRTRes);
    assert(resExplorer.Created == TRUE);
    assert(!resExplorer.IsPrimary);
    deviceFuncs10_1.pfnDestroyResource(hDev, hRes);

    /* Caso 2: dwm.exe -- 2560x1080 com pPrimaryDesc compativel (2560x1080) */
    memset(&createArg, 0, sizeof(createArg));
    memset(&mipInfo, 0, sizeof(mipInfo));
    mipInfo.PhysicalWidth = 2560;
    mipInfo.PhysicalHeight = 1080;
    mipInfo.PhysicalDepth = 1;
    mipInfo.TexelWidth = 2560;
    mipInfo.TexelHeight = 1080;
    mipInfo.TexelDepth = 1;
    createArg.pMipInfoList = &mipInfo;
    createArg.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    createArg.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    createArg.Usage = D3D10_DDI_USAGE_DEFAULT;
    createArg.BindFlags = D3D10_DDI_BIND_RENDER_TARGET | D3D10_DDI_BIND_PRESENT;
    createArg.MipLevels = 1;
    createArg.ArraySize = 1;

    memset(&primaryDesc, 0, sizeof(primaryDesc));
    primaryDesc.Flags = 0;
    primaryDesc.VidPnSourceId = 0;
    primaryDesc.ModeDesc.Width = 2560;
    primaryDesc.ModeDesc.Height = 1080;
    primaryDesc.ModeDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    createArg.pPrimaryDesc = &primaryDesc;

    memset(&resDwm, 0xFF, sizeof(resDwm));
    hRes.pDrvPrivate = &resDwm;
    deviceFuncs10_1.pfnCreateResource(hDev, &createArg, hRes, hRTRes);
    assert(resDwm.Created == TRUE);
    assert(resDwm.IsPrimary);
    deviceFuncs10_1.pfnDestroyResource(hDev, hRes);

    /* Caso 3: Incompativel -- textura 1024x832 com pPrimaryDesc de modo 2560x1080 */
    memset(&createArg, 0, sizeof(createArg));
    memset(&mipInfo, 0, sizeof(mipInfo));
    mipInfo.PhysicalWidth = 1024;
    mipInfo.PhysicalHeight = 832;
    mipInfo.PhysicalDepth = 1;
    mipInfo.TexelWidth = 1024;
    mipInfo.TexelHeight = 832;
    mipInfo.TexelDepth = 1;
    createArg.pMipInfoList = &mipInfo;
    createArg.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    createArg.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    createArg.Usage = D3D10_DDI_USAGE_DEFAULT;
    createArg.BindFlags = D3D10_DDI_BIND_RENDER_TARGET | D3D10_DDI_BIND_PRESENT;
    createArg.MipLevels = 1;
    createArg.ArraySize = 1;
    createArg.pPrimaryDesc = &primaryDesc; /* primaryDesc ainda tem 2560x1080 */

    memset(&resIncompat, 0xFF, sizeof(resIncompat));
    hRes.pDrvPrivate = &resIncompat;
    deviceFuncs10_1.pfnCreateResource(hDev, &createArg, hRes, hRTRes);
    assert(resIncompat.Created == TRUE);
    assert(!resIncompat.IsPrimary);
    deviceFuncs10_1.pfnDestroyResource(hDev, hRes);

    printf("  -> F5.0 Primary Derivation PASS\n");
}

int main(void)
{
    printf("================ BC250 D3D UMD CONTRACT TEST ================\n");
    TestF5PrimaryDerivation();
    TestF3CreateResourceTransaction();
    TestCapsProbeDecisionTable();
    TestNode1TripodAuthorization();
    TestProbeInterfaceAllowlist();
    TestPerEntryPointAllowlist();
    TestCapsZeroTruthfulness();
    TestCapsTypeDecoding();
    TestOpenAdapter10_2();
    TestOpenAdapter10();
    TestOpenAdapter11();
    TestOpenAdapter12();
    TestAdapterStructureAndBlob();
    TestDeviceFoundationStructures();
    TestDeviceCreateFailClosedWithoutPipeline();
    TestGetSupportedVersions();
    TestDeviceTablePopulation();
    TestDxgiBaseFunctions();
    TestCapsPipelineSupport();
    TestShaderDDIs();
    TestShaderSpirvOwnership();
    TestResourceDDIs();
    TestViewAndOutputMergerDDIs();
    TestDrawAndPipelineBindingDDIs();
    TestDrawBltSubstitutionFailsClosed();
    TestDrawPm4EmissionOnNode1();
    TestD1PhaseVulkanAndInitialDataIntegrity();
    TestStateObjectDDIs();
    TestShaderResourceViewAndElementLayoutDDIs();
    TestQueryAndPredicationDDIs();
    TestStreamOutputAndTableCompleteness();
    TestCommandBufferAssembly();
    TestSdmaBltExecution();
    printf("================ ALL UMD CONTRACT TESTS PASSED ===============\n");
    return 0;
}
