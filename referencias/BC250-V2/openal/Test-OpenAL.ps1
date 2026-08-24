<#
.SYNOPSIS
    Suite de Testes e Diagnóstico do Subsistema OpenAL para AMD BC-250
.DESCRIPTION
    Valida o carregamento dinâmico do OpenAL Soft, inicialização de dispositivos (alcOpenDevice),
    criação de contextos de áudio 3D (alcCreateContext) e alocação de fontes de som para Minecraft e jogos.
#>

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "   AMD BC-250: DIAGNOSTICO E TESTE DO OPENAL SOFT       " -ForegroundColor Cyan
Write-Host "   (Cyan Skillfish / Navi12 Lite - Oberon PS5 APU)      " -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

$Sys32    = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::System)
$SysWOW64 = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::SystemX86)
$ProgramData = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::CommonApplicationData)

# 1. Auditoria de Arquivos e Chaves
Write-Host "`n[1/3] Verificando integridade dos arquivos e configuracoes..." -ForegroundColor Yellow

$checks = @(
    @{ Name = "OpenAL32.dll (64-bit System32)"; Path = "$Sys32\OpenAL32.dll" },
    @{ Name = "soft_oal.dll (64-bit System32)"; Path = "$Sys32\soft_oal.dll" },
    @{ Name = "OpenAL32.dll (32-bit SysWOW64)"; Path = "$SysWOW64\OpenAL32.dll" },
    @{ Name = "alsoft.ini (ProgramData)";       Path = "$ProgramData\alsoft.ini" },
    @{ Name = "alsoft.ini (AppData)";           Path = "$env:APPDATA\alsoft.ini" }
)

foreach ($c in $checks) {
    if (Test-Path $c.Path) {
        $size = (Get-Item $c.Path).Length
        Write-Host "  [+] $($c.Name): OK ($size bytes)" -ForegroundColor Green
    } else {
        Write-Host "  [-] $($c.Name): Nao encontrado (Pode usar fallback local)" -ForegroundColor DarkYellow
    }
}

$alsoftConf = [Environment]::GetEnvironmentVariable("ALSOFT_CONF", "Machine")
if (-not $alsoftConf) { $alsoftConf = [Environment]::GetEnvironmentVariable("ALSOFT_CONF", "User") }
$alsoftDrivers = [Environment]::GetEnvironmentVariable("ALSOFT_DRIVERS", "Machine")
if (-not $alsoftDrivers) { $alsoftDrivers = [Environment]::GetEnvironmentVariable("ALSOFT_DRIVERS", "User") }

Write-Host "  • ALSOFT_CONF:    $alsoftConf" -ForegroundColor Cyan
Write-Host "  • ALSOFT_DRIVERS: $alsoftDrivers" -ForegroundColor Cyan

# 2. Teste Funcional de API OpenAL (P/Invoke)
Write-Host "`n[2/3] Executando teste funcional de runtime OpenAL..." -ForegroundColor Yellow

$csharpCode = @"
using System;
using System.Runtime.InteropServices;

public class OpenALDiagnosticsV3 {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr LoadLibrary(string path);

    [DllImport("kernel32.dll")]
    public static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

    [DllImport("kernel32.dll")]
    public static extern bool FreeLibrary(IntPtr hModule);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate IntPtr alcOpenDeviceFunc(string devicename);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate IntPtr alcGetStringFunc(IntPtr device, int param);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate IntPtr alcCreateContextFunc(IntPtr device, IntPtr attrlist);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate byte alcMakeContextCurrentFunc(IntPtr context);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate byte alcCloseDeviceFunc(IntPtr device);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void alcDestroyContextFunc(IntPtr context);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate IntPtr alGetStringFunc(int param);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void alGenSourcesFunc(int n, [Out] uint[] sources);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void alDeleteSourcesFunc(int n, uint[] sources);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int alGetErrorFunc();

    public static bool Run(string dllPath) {
        Console.WriteLine("Carregando DLL: " + dllPath);
        IntPtr hMod = LoadLibrary(dllPath);
        if (hMod == IntPtr.Zero) {
            Console.WriteLine("ERRO: Nao foi possivel carregar a DLL!");
            return false;
        }

        var openDev    = Marshal.GetDelegateForFunctionPointer<alcOpenDeviceFunc>(GetProcAddress(hMod, "alcOpenDevice"));
        var getAlcStr  = Marshal.GetDelegateForFunctionPointer<alcGetStringFunc>(GetProcAddress(hMod, "alcGetString"));
        var closeDev   = Marshal.GetDelegateForFunctionPointer<alcCloseDeviceFunc>(GetProcAddress(hMod, "alcCloseDevice"));
        var createCtx  = Marshal.GetDelegateForFunctionPointer<alcCreateContextFunc>(GetProcAddress(hMod, "alcCreateContext"));
        var makeCur    = Marshal.GetDelegateForFunctionPointer<alcMakeContextCurrentFunc>(GetProcAddress(hMod, "alcMakeContextCurrent"));
        var destroyCtx = Marshal.GetDelegateForFunctionPointer<alcDestroyContextFunc>(GetProcAddress(hMod, "alcDestroyContext"));
        var getAlStr   = Marshal.GetDelegateForFunctionPointer<alGetStringFunc>(GetProcAddress(hMod, "alGetString"));
        var genSrc     = Marshal.GetDelegateForFunctionPointer<alGenSourcesFunc>(GetProcAddress(hMod, "alGenSources"));
        var delSrc     = Marshal.GetDelegateForFunctionPointer<alDeleteSourcesFunc>(GetProcAddress(hMod, "alDeleteSources"));
        var getAlErr   = Marshal.GetDelegateForFunctionPointer<alGetErrorFunc>(GetProcAddress(hMod, "alGetError"));

        IntPtr dev = openDev(null);
        if (dev == IntPtr.Zero) {
            Console.WriteLine("ERRO: alcOpenDevice(null) retornou ponteiro nulo!");
            FreeLibrary(hMod);
            return false;
        }

        IntPtr spec = getAlcStr(dev, 0x1005); // ALC_DEVICE_SPECIFIER
        string specName = spec != IntPtr.Zero ? Marshal.PtrToStringAnsi(spec) : "Desconhecido";
        Console.WriteLine("  • Dispositivo OpenAL: " + specName);

        IntPtr ctx = createCtx(dev, IntPtr.Zero);
        if (ctx == IntPtr.Zero) {
            Console.WriteLine("ERRO: alcCreateContext retornou ponteiro nulo!");
            closeDev(dev);
            FreeLibrary(hMod);
            return false;
        }

        makeCur(ctx);

        IntPtr pVendor   = getAlStr(0xB001); // AL_VENDOR
        IntPtr pVersion  = getAlStr(0xB002); // AL_VERSION
        IntPtr pRenderer = getAlStr(0xB003); // AL_RENDERER

        string vendor   = pVendor   != IntPtr.Zero ? Marshal.PtrToStringAnsi(pVendor)   : "N/A";
        string version  = pVersion  != IntPtr.Zero ? Marshal.PtrToStringAnsi(pVersion)  : "N/A";
        string renderer = pRenderer != IntPtr.Zero ? Marshal.PtrToStringAnsi(pRenderer) : "N/A";

        Console.WriteLine("  • AL_VENDOR:   " + vendor);
        Console.WriteLine("  • AL_RENDERER: " + renderer);
        Console.WriteLine("  • AL_VERSION:  " + version);

        // Teste de alocação de fonte de áudio 3D
        uint[] sources = new uint[1];
        genSrc(1, sources);
        int err = getAlErr();
        bool sourceOk = (err == 0 && sources[0] != 0);
        Console.WriteLine("  • Teste de Fonte de Audio 3D (alGenSources): " + (sourceOk ? "SUCESSO (Source ID=" + sources[0] + ")" : "FALHA (err=" + err + ")"));

        if (sourceOk) delSrc(1, sources);

        makeCur(IntPtr.Zero);
        destroyCtx(ctx);
        closeDev(dev);
        FreeLibrary(hMod);

        return sourceOk;
    }
}
"@

Add-Type -TypeDefinition $csharpCode

$testDll = "$Sys32\OpenAL32.dll"
if (-not (Test-Path $testDll)) {
    $testDll = "$PSScriptRoot\x64\OpenAL32.dll"
}

$success = [OpenALDiagnosticsV3]::Run($testDll)

Write-Host "`n[3/3] Resumo da Avaliacao:" -ForegroundColor Yellow
if ($success) {
    Write-Host "  [APROVADO] O OpenAL Soft esta 100% operacional no sistema AMD BC-250!" -ForegroundColor Green
    Write-Host "  Minecraft Java Edition e outros jogos iniciarao seus motores de som perfeitamente." -ForegroundColor Green
} else {
    Write-Host "  [FALHA] Nao foi possivel inicializar o contexto OpenAL. Verifique o alsoft.ini." -ForegroundColor Red
}
Write-Host "========================================================`n" -ForegroundColor Cyan
