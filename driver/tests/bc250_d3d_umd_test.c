#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "bc250_d3d_umd.h"

static void TestOpenAdapter10_2(void)
{
    D3D10DDIARG_OPENADAPTER openData;
    HRESULT hr;
    UINT32 versionCount = 0;
    UINT64 versions[8] = {0};
    D3D10_2DDIARG_GETCAPS capsArgs;
    UINT capData = 0;
    SIZE_T devSize = 0;

    printf("[TEST] Testing OpenAdapter10_2...\n");

    /* Negative test: NULL pointer */
    hr = OpenAdapter10_2(NULL);
    assert(FAILED(hr));

    /* Positive test: Valid OpenData */
    memset(&openData, 0, sizeof(openData));
    openData.Interface = D3D10_0_DDI_INTERFACE_VERSION;
    openData.Version = 1;

    hr = OpenAdapter10_2(&openData);
    assert(SUCCEEDED(hr));
    assert(openData.hAdapter.pDrvPrivate != NULL);
    assert(openData.pAdapterFuncs_2 != NULL);
    assert(openData.pAdapterFuncs_2->pfnGetSupportedVersions != NULL);
    assert(openData.pAdapterFuncs_2->pfnGetCaps != NULL);
    assert(openData.pAdapterFuncs_2->pfnCalcPrivateDeviceSize != NULL);
    assert(openData.pAdapterFuncs_2->pfnCreateDevice != NULL);
    assert(openData.pAdapterFuncs_2->pfnCloseAdapter != NULL);

    /* Test GetSupportedVersions */
    hr = openData.pAdapterFuncs_2->pfnGetSupportedVersions(openData.hAdapter, &versionCount, NULL);
    assert(SUCCEEDED(hr));
    assert(versionCount > 0);

    hr = openData.pAdapterFuncs_2->pfnGetSupportedVersions(openData.hAdapter, &versionCount, versions);
    assert(SUCCEEDED(hr));
    assert(versions[0] == D3D10_0_DDI_SUPPORTED);

    /* Test CalcPrivateDeviceSize */
    devSize = openData.pAdapterFuncs_2->pfnCalcPrivateDeviceSize(openData.hAdapter, NULL);
    assert(devSize >= sizeof(BC250_UMD_DEVICE));

    /* Test GetCaps */
    memset(&capsArgs, 0, sizeof(capsArgs));
    capsArgs.pData = &capData;
    capsArgs.DataSize = sizeof(capData);
    hr = openData.pAdapterFuncs_2->pfnGetCaps(openData.hAdapter, &capsArgs);
    assert(SUCCEEDED(hr));

    /* Test CloseAdapter */
    hr = openData.pAdapterFuncs_2->pfnCloseAdapter(openData.hAdapter);
    assert(SUCCEEDED(hr));

    printf("  -> OpenAdapter10_2 PASS\n");
}

static void TestOpenAdapter12(void)
{
    D3D12DDIARG_OPENADAPTER openData12;
    HRESULT hr;
    UINT32 versionCount = 0;
    UINT64 versions[4] = {0};

    printf("[TEST] Testing OpenAdapter12...\n");

    /* Negative test */
    hr = OpenAdapter12(NULL);
    assert(FAILED(hr));

    /* Positive test */
    memset(&openData12, 0, sizeof(openData12));
    hr = OpenAdapter12(&openData12);
    assert(SUCCEEDED(hr));
    assert(openData12.hAdapter.pDrvPrivate != NULL);
    assert(openData12.pAdapterFuncs != NULL);
    assert(openData12.pAdapterFuncs->pfnGetSupportedVersions != NULL);
    assert(openData12.pAdapterFuncs->pfnCloseAdapter != NULL);

    /* Test GetSupportedVersions */
    hr = openData12.pAdapterFuncs->pfnGetSupportedVersions(openData12.hAdapter, &versionCount, NULL);
    assert(SUCCEEDED(hr));
    assert(versionCount > 0);

    hr = openData12.pAdapterFuncs->pfnGetSupportedVersions(openData12.hAdapter, &versionCount, versions);
    assert(SUCCEEDED(hr));

    /* Test CloseAdapter */
    hr = openData12.pAdapterFuncs->pfnCloseAdapter(openData12.hAdapter);
    assert(SUCCEEDED(hr));

    printf("  -> OpenAdapter12 PASS\n");
}

int main(void)
{
    printf("================ BC250 D3D UMD CONTRACT TEST ================\n");
    TestOpenAdapter10_2();
    TestOpenAdapter12();
    printf("================ ALL UMD CONTRACT TESTS PASSED ===============\n");
    return 0;
}
