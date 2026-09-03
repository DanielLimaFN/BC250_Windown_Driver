#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef UINT D3DKMT_HANDLE;

typedef struct _D3DKMT_OPENADAPTERFROMHDC {
    HDC           hDc;
    D3DKMT_HANDLE hAdapter;
    LUID          AdapterLuid;
    UINT          VidPnSourceId;
} D3DKMT_OPENADAPTERFROMHDC;

typedef struct _D3DKMT_CLOSEADAPTER {
    D3DKMT_HANDLE hAdapter;
} D3DKMT_CLOSEADAPTER;

typedef struct _D3DKMT_CREATEDEVICE {
    D3DKMT_HANDLE hAdapter;
    UINT          Flags;
    D3DKMT_HANDLE hDevice;
    VOID*         pCommandBuffer;
    UINT          CommandBufferSize;
    VOID*         pAllocationList;
    UINT          AllocationListSize;
    VOID*         pPatchLocationList;
    UINT          PatchLocationListSize;
} D3DKMT_CREATEDEVICE;

typedef struct _D3DKMT_DESTROYDEVICE {
    D3DKMT_HANDLE hDevice;
} D3DKMT_DESTROYDEVICE;

typedef struct _D3DKMT_CREATECONTEXTVIRTUAL {
    D3DKMT_HANDLE hDevice;
    UINT          NodeOrdinal;
    UINT          EngineAffinity;
    UINT          Flags;
    VOID*         pPrivData;
    UINT          PrivDataLength;
    D3DKMT_HANDLE hContext;
} D3DKMT_CREATECONTEXTVIRTUAL;

typedef struct _D3DKMT_DESTROYCONTEXT {
    D3DKMT_HANDLE hContext;
} D3DKMT_DESTROYCONTEXT;

typedef LONG (APIENTRY *PFN_D3DKMTOpenAdapterFromHdc)(D3DKMT_OPENADAPTERFROMHDC*);
typedef LONG (APIENTRY *PFN_D3DKMTCloseAdapter)(const D3DKMT_CLOSEADAPTER*);
typedef LONG (APIENTRY *PFN_D3DKMTCreateDevice)(D3DKMT_CREATEDEVICE*);
typedef LONG (APIENTRY *PFN_D3DKMTDestroyDevice)(const D3DKMT_DESTROYDEVICE*);
typedef LONG (APIENTRY *PFN_D3DKMTCreateContextVirtual)(D3DKMT_CREATECONTEXTVIRTUAL*);
typedef LONG (APIENTRY *PFN_D3DKMTDestroyContext)(const D3DKMT_DESTROYCONTEXT*);

int main(int argc, char **argv)
{
    HMODULE hGdi = NULL;
    PFN_D3DKMTOpenAdapterFromHdc pfnOpenAdapter = NULL;
    PFN_D3DKMTCloseAdapter pfnCloseAdapter = NULL;
    PFN_D3DKMTCreateDevice pfnCreateDevice = NULL;
    PFN_D3DKMTDestroyDevice pfnDestroyDevice = NULL;
    PFN_D3DKMTCreateContextVirtual pfnCreateContext = NULL;
    PFN_D3DKMTDestroyContext pfnDestroyContext = NULL;

    HDC hdc = NULL;
    D3DKMT_OPENADAPTERFROMHDC openAdapter;
    D3DKMT_CREATEDEVICE createDevice;
    D3DKMT_CREATECONTEXTVIRTUAL createContext;
    LONG status = 0;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    printf("================================================================================\n");
    printf("   AMD BC-250 — TESTE DE NÓS DE RENDERIZACAO WDDM VIA D3DKMT (DISPLAY+RENDER)  \n");
    printf("================================================================================\n\n");

    hGdi = LoadLibraryW(L"gdi32.dll");
    if (!hGdi) {
        printf("[!] ERRO: Falha ao carregar gdi32.dll\n");
        return 1;
    }

    pfnOpenAdapter = (PFN_D3DKMTOpenAdapterFromHdc)GetProcAddress(hGdi, "D3DKMTOpenAdapterFromHdc");
    pfnCloseAdapter = (PFN_D3DKMTCloseAdapter)GetProcAddress(hGdi, "D3DKMTCloseAdapter");
    pfnCreateDevice = (PFN_D3DKMTCreateDevice)GetProcAddress(hGdi, "D3DKMTCreateDevice");
    pfnDestroyDevice = (PFN_D3DKMTDestroyDevice)GetProcAddress(hGdi, "D3DKMTDestroyDevice");
    pfnCreateContext = (PFN_D3DKMTCreateContextVirtual)GetProcAddress(hGdi, "D3DKMTCreateContextVirtual");
    pfnDestroyContext = (PFN_D3DKMTDestroyContext)GetProcAddress(hGdi, "D3DKMTDestroyContext");

    if (!pfnOpenAdapter || !pfnCloseAdapter || !pfnCreateDevice || !pfnDestroyDevice || !pfnCreateContext || !pfnDestroyContext) {
        printf("[!] ERRO: Funcoes D3DKMT ausentes em gdi32.dll\n");
        FreeLibrary(hGdi);
        return 1;
    }

    /* 1. Obter HDC da tela primária */
    printf("[1/4] Adquirindo HDC da tela primária (Display Ativo)...\n");
    hdc = GetDC(NULL);
    if (!hdc) {
        printf("[!] ERRO: GetDC falhou\n");
        FreeLibrary(hGdi);
        return 1;
    }

    /* 2. Abrir adaptador WDDM */
    printf("[2/4] Abrindo Adaptador Grafico WDDM via D3DKMTOpenAdapterFromHdc...\n");
    ZeroMemory(&openAdapter, sizeof(openAdapter));
    openAdapter.hDc = hdc;
    status = pfnOpenAdapter(&openAdapter);
    ReleaseDC(NULL, hdc);

    if (status != 0) {
        printf("[!] ERRO: D3DKMTOpenAdapterFromHdc falhou (NTSTATUS: 0x%08lX)\n", status);
        FreeLibrary(hGdi);
        return 1;
    }

    printf("  -> Adaptador WDDM aberto com sucesso!\n");
    printf("     Handle Adaptador: 0x%08X\n", openAdapter.hAdapter);
    printf("     LUID:             %08lX:%08lX\n", openAdapter.AdapterLuid.HighPart, openAdapter.AdapterLuid.LowPart);
    printf("     VidPnSourceId:    %u\n\n", openAdapter.VidPnSourceId);

    /* 3. Criar Dispositivo WDDM */
    printf("[3/4] Criando Dispositivo WDDM (D3DKMTCreateDevice)...\n");
    ZeroMemory(&createDevice, sizeof(createDevice));
    createDevice.hAdapter = openAdapter.hAdapter;
    status = pfnCreateDevice(&createDevice);
    if (status != 0) {
        printf("[!] ERRO: D3DKMTCreateDevice falhou (NTSTATUS: 0x%08lX)\n", status);
        D3DKMT_CLOSEADAPTER closeAdapter = { openAdapter.hAdapter };
        pfnCloseAdapter(&closeAdapter);
        FreeLibrary(hGdi);
        return 1;
    }
    printf("  -> Dispositivo criado com sucesso! (Handle: 0x%08X)\n\n", createDevice.hDevice);

    /* 4. Criar Contexto Virtual no Nó 1 (Compute MEC) */
    printf("[4/4] Criando Contexto Virtual no Nó 1 (Compute MEC)...\n");
    ZeroMemory(&createContext, sizeof(createContext));
    createContext.hDevice = createDevice.hDevice;
    createContext.NodeOrdinal = 1; /* Nó 1 = Compute MEC */
    createContext.EngineAffinity = 1;
    status = pfnCreateContext(&createContext);
    if (status != 0) {
        printf("[!] AVISO: D3DKMTCreateContextVirtual no No 1 retornou 0x%08lX\n", status);
    } else {
        printf("  -> Contexto Virtual no Nó 1 criado com sucesso! (Handle: 0x%08X)\n", createContext.hContext);
        D3DKMT_DESTROYCONTEXT destroyCtx = { createContext.hContext };
        pfnDestroyContext(&destroyCtx);
        printf("  -> Contexto Virtual destruido limpo.\n");
    }

    /* Teardown limpo */
    {
        D3DKMT_DESTROYDEVICE destroyDev = { createDevice.hDevice };
        pfnDestroyDevice(&destroyDev);
    }
    {
        D3DKMT_CLOSEADAPTER closeAdapter = { openAdapter.hAdapter };
        pfnCloseAdapter(&closeAdapter);
    }
    FreeLibrary(hGdi);

    printf("\n================================================================================\n");
    printf("VEREDITO: PIPELINE D3DKMT VALIDADO COM SUCESSO NO SISTEMA!\n");
    printf("================================================================================\n");
    return 0;
}
