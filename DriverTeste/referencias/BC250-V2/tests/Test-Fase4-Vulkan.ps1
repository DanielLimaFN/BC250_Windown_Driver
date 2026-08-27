# =============================================================================
#  FASE 4 (parte 2) - ICD Vulkan: deteccao pelo loader oficial
#
#  Nao chama a DLL do ICD diretamente. Carrega o vulkan-1.dll do sistema e
#  percorre a API como QUALQUER aplicativo faria:
#     vkCreateInstance -> vkEnumeratePhysicalDevices
#     -> vkGetPhysicalDeviceProperties -> vkGetPhysicalDeviceMemoryProperties
#     -> vkCreateDevice -> vkGetDeviceQueue -> vkDestroy*
#  Se o caminho funciona por aqui, funciona para o vulkaninfo, para o
#  llama.cpp e para qualquer outro programa.
#
#  Tambem confere a integridade do pacote (a DLL precisa ser um PE valido -- ja
#  houve um "amdbc250vulkan.dll" de 27 bytes contendo texto) e a validade do
#  manifesto JSON, cujo escape de barra invertida ja derrubou o driver inteiro.
#
#  Uso:  powershell -ExecutionPolicy Bypass -File Test-Fase4-Vulkan.ps1
# =============================================================================
[CmdletBinding()]
param([switch]$PassThru)

. (Join-Path $PSScriptRoot 'lib\Bc250TestLib.ps1')

$ctx = New-Bc250TestContext -Phase 'FASE 4' -Title 'ICD Vulkan (deteccao pelo loader oficial)'
Write-Bc250Header $ctx

# --- 1. Loader presente -----------------------------------------------------
$loader = 'C:\Windows\System32\vulkan-1.dll'
Assert-Bc250 $ctx 'loader Vulkan instalado' (Test-Path -LiteralPath $loader) $loader
if (-not (Test-Path -LiteralPath $loader)) {
    return (Complete-Bc250Test $ctx $PassThru.IsPresent)
}

# --- 2. Registro e manifesto ------------------------------------------------
$regKey = 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers'
$manifests = @()
if (Test-Path $regKey) {
    $manifests = @((Get-Item $regKey).Property | Where-Object { $_ -like '*amdbc250vulkan*' })
}
Assert-Bc250 $ctx 'ICD da BC-250 registrado no loader' ($manifests.Count -gt 0) `
    $(if ($manifests.Count -gt 0) { $manifests[0] } else { "nada em $regKey -- rode umd\install-vulkan-icd.ps1" })

foreach ($m in $manifests) {
    Assert-Bc250 $ctx 'manifesto existe no disco' (Test-Path -LiteralPath $m) $m
    if (-not (Test-Path -LiteralPath $m)) { continue }

    $raw = Get-Content -LiteralPath $m -Raw
    $parsed = $null
    try { $parsed = $raw | ConvertFrom-Json } catch { }
    Assert-Bc250 $ctx 'manifesto e JSON valido' ($null -ne $parsed) `
        $(if ($null -ne $parsed) { 'parse ok' } else { 'escape de barra invertida quebrado?' })

    # BOM faz o parser do loader recusar o arquivo.
    $bytes = [System.IO.File]::ReadAllBytes($m)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    Assert-Bc250 $ctx 'manifesto sem BOM UTF-8' (-not $hasBom) $(if ($hasBom) { 'BOM presente' } else { 'sem BOM' })

    if ($null -ne $parsed) {
        $lib = $parsed.ICD.library_path
        Add-Bc250Check $ctx 'INFO' 'library_path do manifesto' $lib
        # O loader passa esse caminho direto para LoadLibrary. A forma "./x.dll"
        # falha com erro 87 no Windows.
        Assert-Bc250 $ctx 'library_path nao usa a forma "./" (recusada por LoadLibrary)' `
            (-not $lib.StartsWith('./')) $lib

        $dll = $lib
        if (-not [System.IO.Path]::IsPathRooted($dll)) {
            $dll = Join-Path (Split-Path $m -Parent) ($lib -replace '^\.[\\/]', '')
        }
        Assert-Bc250 $ctx 'DLL do ICD existe' (Test-Path -LiteralPath $dll) $dll
        if (Test-Path -LiteralPath $dll) {
            $len = (Get-Item $dll).Length
            $head = [System.IO.File]::ReadAllBytes($dll)[0..1]
            $isPe = ($head[0] -eq 0x4D -and $head[1] -eq 0x5A)
            Assert-Bc250 $ctx 'DLL do ICD e um binario PE (header MZ)' $isPe `
                ("$len bytes, header 0x{0:X2}{1:X2}" -f $head[0], $head[1])
        }
    }
}

# --- 3. Percurso da API pelo loader ----------------------------------------
$src = @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class VkProbe
{
    [DllImport("kernel32", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr LoadLibraryW(string p);
    [DllImport("kernel32", SetLastError=true)]
    static extern IntPtr GetProcAddress(IntPtr h, string n);

    delegate int PFN_CreateInstance(IntPtr ci, IntPtr alloc, out IntPtr inst);
    delegate int PFN_EnumPhys(IntPtr inst, ref uint count, IntPtr devs);
    delegate void PFN_GetProps(IntPtr phys, IntPtr props);
    delegate void PFN_GetMemProps(IntPtr phys, IntPtr props);
    delegate void PFN_GetQueueFam(IntPtr phys, ref uint count, IntPtr props);
    delegate int PFN_CreateDevice(IntPtr phys, IntPtr ci, IntPtr alloc, out IntPtr dev);
    delegate void PFN_DestroyDevice(IntPtr dev, IntPtr alloc);
    delegate void PFN_DestroyInstance(IntPtr inst, IntPtr alloc);
    delegate void PFN_GetDeviceQueue(IntPtr dev, uint fam, uint idx, out IntPtr q);

    static T Fn<T>(IntPtr h, string n) where T : class {
        IntPtr p = GetProcAddress(h, n);
        if (p == IntPtr.Zero) return null;
        return Marshal.GetDelegateForFunctionPointer(p, typeof(T)) as T;
    }

    public class Result {
        public bool  LoaderOk;
        public int   CreateInstanceRc = -999;
        public uint  PhysCount;
        public int   EnumRc = -999;
        public uint  ApiVersion, DriverVersion, VendorId, DeviceId, DeviceType;
        public string DeviceName = "";
        public uint  HeapCount, TypeCount;
        public ulong HeapSize;
        public uint  MemPropFlags;
        public uint  QueueFamCount, QueueFlags, QueueCount;
        public int   CreateDeviceRc = -999;
        public bool  QueueOk;
        public string Error = "";
    }

    // VkInstanceCreateInfo (x64, 64 bytes): sType 0 | pNext 8 | flags 16 |
    // pApplicationInfo 24 | layerCount 32 | ppLayers 40 | extCount 48 | ppExts 56
    // VkDeviceCreateInfo  (x64, 72 bytes): sType 0 | pNext 8 | flags 16 |
    // queueCreateInfoCount 20 | pQueueCreateInfos 24 | layerCount 32 |
    // ppLayers 40 | extCount 48 | ppExts 56 | pEnabledFeatures 64
    // VkDeviceQueueCreateInfo (32 bytes): sType 0 | pNext 8 | flags 16 |
    // queueFamilyIndex 20 | queueCount 24 | pQueuePriorities 32? -> 40 total
    public static Result Run() {
        var r = new Result();
        IntPtr h = LoadLibraryW("vulkan-1.dll");
        if (h == IntPtr.Zero) { r.Error = "LoadLibrary vulkan-1.dll falhou"; return r; }
        r.LoaderOk = true;

        var createInstance  = Fn<PFN_CreateInstance>(h, "vkCreateInstance");
        var enumPhys        = Fn<PFN_EnumPhys>(h, "vkEnumeratePhysicalDevices");
        var getProps        = Fn<PFN_GetProps>(h, "vkGetPhysicalDeviceProperties");
        var getMemProps     = Fn<PFN_GetMemProps>(h, "vkGetPhysicalDeviceMemoryProperties");
        var getQueueFam     = Fn<PFN_GetQueueFam>(h, "vkGetPhysicalDeviceQueueFamilyProperties");
        var createDevice    = Fn<PFN_CreateDevice>(h, "vkCreateDevice");
        var destroyDevice   = Fn<PFN_DestroyDevice>(h, "vkDestroyDevice");
        var destroyInstance = Fn<PFN_DestroyInstance>(h, "vkDestroyInstance");
        var getDeviceQueue  = Fn<PFN_GetDeviceQueue>(h, "vkGetDeviceQueue");
        if (createInstance == null || enumPhys == null || getProps == null) {
            r.Error = "entrypoints ausentes no loader"; return r;
        }

        IntPtr ci = Marshal.AllocHGlobal(64);
        try {
            for (int i = 0; i < 64; i++) Marshal.WriteByte(ci, i, 0);
            Marshal.WriteInt32(ci, 0, 1);   // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
            IntPtr inst;
            r.CreateInstanceRc = createInstance(ci, IntPtr.Zero, out inst);
            if (r.CreateInstanceRc != 0) return r;

            uint n = 0;
            r.EnumRc = enumPhys(inst, ref n, IntPtr.Zero);
            r.PhysCount = n;
            if (r.EnumRc != 0 || n == 0) { if (destroyInstance != null) destroyInstance(inst, IntPtr.Zero); return r; }

            IntPtr devs = Marshal.AllocHGlobal((int)n * 8);
            try {
                uint got = n;
                enumPhys(inst, ref got, devs);
                IntPtr phys = Marshal.ReadIntPtr(devs, 0);

                IntPtr props = Marshal.AllocHGlobal(824);
                try {
                    for (int i = 0; i < 824; i++) Marshal.WriteByte(props, i, 0);
                    getProps(phys, props);
                    r.ApiVersion    = (uint)Marshal.ReadInt32(props, 0);
                    r.DriverVersion = (uint)Marshal.ReadInt32(props, 4);
                    r.VendorId      = (uint)Marshal.ReadInt32(props, 8);
                    r.DeviceId      = (uint)Marshal.ReadInt32(props, 12);
                    r.DeviceType    = (uint)Marshal.ReadInt32(props, 16);
                    r.DeviceName    = Marshal.PtrToStringAnsi(props + 20);
                } finally { Marshal.FreeHGlobal(props); }

                if (getMemProps != null) {
                    IntPtr mp = Marshal.AllocHGlobal(520);
                    try {
                        for (int i = 0; i < 520; i++) Marshal.WriteByte(mp, i, 0);
                        getMemProps(phys, mp);
                        // memoryTypeCount 0 | memoryTypes[32] em 4 (8 bytes cada)
                        // memoryHeapCount em 4+256=260 | memoryHeaps[16] em 264
                        r.TypeCount    = (uint)Marshal.ReadInt32(mp, 0);
                        r.MemPropFlags = (uint)Marshal.ReadInt32(mp, 4);
                        r.HeapCount    = (uint)Marshal.ReadInt32(mp, 260);
                        r.HeapSize     = (ulong)Marshal.ReadInt64(mp, 264);
                    } finally { Marshal.FreeHGlobal(mp); }
                }

                if (getQueueFam != null) {
                    uint qn = 0;
                    getQueueFam(phys, ref qn, IntPtr.Zero);
                    r.QueueFamCount = qn;
                    if (qn > 0) {
                        IntPtr qp = Marshal.AllocHGlobal((int)qn * 24);
                        try {
                            uint qgot = qn;
                            getQueueFam(phys, ref qgot, qp);
                            r.QueueFlags = (uint)Marshal.ReadInt32(qp, 0);
                            r.QueueCount = (uint)Marshal.ReadInt32(qp, 4);
                        } finally { Marshal.FreeHGlobal(qp); }
                    }
                }

                if (createDevice != null) {
                    IntPtr prio = Marshal.AllocHGlobal(4);
                    IntPtr qci  = Marshal.AllocHGlobal(40);
                    IntPtr dci  = Marshal.AllocHGlobal(72);
                    try {
                        Marshal.WriteInt32(prio, 0, 0x3F800000); // 1.0f
                        for (int i = 0; i < 40; i++) Marshal.WriteByte(qci, i, 0);
                        Marshal.WriteInt32(qci, 0, 2);   // DEVICE_QUEUE_CREATE_INFO
                        Marshal.WriteInt32(qci, 20, 0);  // queueFamilyIndex
                        Marshal.WriteInt32(qci, 24, 1);  // queueCount
                        Marshal.WriteIntPtr(qci, 32, prio);
                        for (int i = 0; i < 72; i++) Marshal.WriteByte(dci, i, 0);
                        Marshal.WriteInt32(dci, 0, 3);   // DEVICE_CREATE_INFO
                        Marshal.WriteInt32(dci, 20, 1);  // queueCreateInfoCount
                        Marshal.WriteIntPtr(dci, 24, qci);
                        IntPtr dev;
                        r.CreateDeviceRc = createDevice(phys, dci, IntPtr.Zero, out dev);
                        if (r.CreateDeviceRc == 0) {
                            if (getDeviceQueue != null) {
                                IntPtr q;
                                getDeviceQueue(dev, 0, 0, out q);
                                r.QueueOk = (q != IntPtr.Zero);
                            }
                            if (destroyDevice != null) destroyDevice(dev, IntPtr.Zero);
                        }
                    } finally {
                        Marshal.FreeHGlobal(dci); Marshal.FreeHGlobal(qci); Marshal.FreeHGlobal(prio);
                    }
                }

                if (destroyInstance != null) destroyInstance(inst, IntPtr.Zero);
            } finally { Marshal.FreeHGlobal(devs); }
        } catch (Exception e) {
            r.Error = e.Message;
        } finally { Marshal.FreeHGlobal(ci); }
        return r;
    }
}
'@
if (-not ('VkProbe' -as [type])) { Add-Type -TypeDefinition $src }

$r = [VkProbe]::Run()

if ($r.Error -ne '') { Add-Bc250Check $ctx 'INFO' 'excecao no percurso' $r.Error }

Assert-Bc250 $ctx 'vulkan-1.dll carregado' $r.LoaderOk ''
Assert-Bc250 $ctx 'vkCreateInstance pelo loader' ($r.CreateInstanceRc -eq 0) `
    ("rc=" + $r.CreateInstanceRc)

if ($r.CreateInstanceRc -ne 0) {
    Add-Bc250Check $ctx 'SKIP' 'demais verificacoes de API' 'sem instancia'
    return (Complete-Bc250Test $ctx $PassThru.IsPresent)
}

Assert-Bc250 $ctx 'vkEnumeratePhysicalDevices encontrou dispositivo' ($r.PhysCount -ge 1) `
    ("count=" + $r.PhysCount + " rc=" + $r.EnumRc)

if ($r.PhysCount -lt 1) {
    return (Complete-Bc250Test $ctx $PassThru.IsPresent)
}

Assert-Bc250 $ctx 'vendorID = 0x1002 (AMD)' ($r.VendorId -eq 0x1002) ("0x{0:X4}" -f $r.VendorId)
Assert-Bc250 $ctx 'deviceID = 0x13FE (BC-250)' ($r.DeviceId -eq 0x13FE) ("0x{0:X4}" -f $r.DeviceId)
Assert-Bc250 $ctx 'deviceName identifica a BC-250' ($r.DeviceName -match 'BC-250') $r.DeviceName
Add-Bc250Check $ctx 'INFO' 'apiVersion' ("{0}.{1}.{2}" -f (($r.ApiVersion -shr 22) -band 0x7F), (($r.ApiVersion -shr 12) -band 0x3FF), ($r.ApiVersion -band 0xFFF))
Add-Bc250Check $ctx 'INFO' 'deviceType' $(switch($r.DeviceType){0{'OTHER'}1{'INTEGRATED_GPU'}2{'DISCRETE_GPU'}3{'VIRTUAL_GPU'}4{'CPU'}default{"$($r.DeviceType)"}})

Assert-Bc250 $ctx 'memoryHeapCount >= 1' ($r.HeapCount -ge 1) ("heaps=" + $r.HeapCount)
Assert-Bc250 $ctx 'memoryTypeCount >= 1' ($r.TypeCount -ge 1) ("types=" + $r.TypeCount)
Assert-Bc250 $ctx 'heap com tamanho plausivel (nao zero, nao absurdo)' `
    (($r.HeapSize -gt 0) -and ($r.HeapSize -le 17179869184)) `
    ("{0:N0} bytes ({1:N2} MiB)" -f $r.HeapSize, ($r.HeapSize / 1MB))
# DEVICE_LOCAL=1, HOST_VISIBLE=2. Sem HOST_COHERENT (4), por desenho: a subida
# do dado depende de vkFlushMappedMemoryRanges.
Assert-Bc250 $ctx 'memoryType nao anuncia HOST_COHERENT que nao podemos honrar' `
    ((($r.MemPropFlags -band 0x4) -eq 0)) ("propertyFlags=0x{0:X}" -f $r.MemPropFlags)

Assert-Bc250 $ctx 'fila de compute exposta' (($r.QueueFlags -band 0x2) -ne 0) `
    ("queueFlags=0x{0:X} count={1}" -f $r.QueueFlags, $r.QueueCount)
Assert-Bc250 $ctx 'vkCreateDevice pelo loader' ($r.CreateDeviceRc -eq 0) ("rc=" + $r.CreateDeviceRc)
Assert-Bc250 $ctx 'vkGetDeviceQueue devolveu fila valida' $r.QueueOk ''

# --- 4. Execucao de shader: limite conhecido -------------------------------
# Nao existe compilador SPIR-V -> ISA GFX10 no projeto, entao
# vkCreateComputePipelines falha de proposito em vez de substituir um kernel
# que nao computa nada. Codificado como XFAIL: se algum dia passar, alguem
# implementou o compilador e o teste precisa ser revisto.
Assert-Bc250 $ctx 'execucao de shader do usuario disponivel' $false `
    'sem compilador SPIR-V -> GFX10; vkCreateComputePipelines retorna VK_ERROR_INVALID_SHADER_NV de proposito' `
    -Expected FAIL

# --- 5. vulkaninfo como consumidor independente ----------------------------
$vi = Get-Command vulkaninfo.exe -ErrorAction SilentlyContinue
if ($null -eq $vi) {
    Add-Bc250Check $ctx 'SKIP' 'vulkaninfo' 'nao encontrado no PATH'
} else {
    $out = & cmd /c "vulkaninfo.exe --summary 2>&1"
    $found = ($out | Select-String -Pattern 'BC-250').Count -gt 0
    Assert-Bc250 $ctx 'vulkaninfo lista a BC-250' $found `
        $(if ($found) { ($out | Select-String -Pattern 'deviceName' | Select-Object -First 1).ToString().Trim() } else { 'nao apareceu' })
}

return (Complete-Bc250Test $ctx $PassThru.IsPresent)
