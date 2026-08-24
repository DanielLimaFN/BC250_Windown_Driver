# =============================================================================
#  Bc250TestLib.ps1 - biblioteca comum da suite de testes do driver BC-250
#
#  Nada aqui instala, reinicia ou reconfigura o dispositivo. A suite so LE
#  estado (registro, PnP, WMI, APIs de display) e gera carga de desenho.
#  Qualquer teste que precise de escrita e marcado explicitamente e exige
#  um switch dedicado.
#
#  Requer PowerShell 5.1 (sem operador ternario, sem ?? e sem && / ||).
# =============================================================================

$script:Bc250HwidPrefix   = 'PCI\VEN_1002&DEV_13FE'
$script:Bc250ServiceKey   = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250v2'
$script:Bc250ParamsKey    = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250v2\Parameters'

# -----------------------------------------------------------------------------
#  P/Invoke
#
#  Os structs de display sao lidos/escritos em buffers CRUS com offsets
#  explicitos. Marshaling declarativo de DEVMODE/DISPLAY_DEVICE deu tamanho
#  errado em teste (CharSet default = ANSI), e o sintoma foi enumeracao
#  retornando zero. Buffer cru elimina essa classe de erro.
# -----------------------------------------------------------------------------
if (-not ('Bc250Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class Bc250Native
{
    // ---- user32: enumeracao e mudanca de modo -------------------------------
    [DllImport("user32.dll", EntryPoint="EnumDisplayDevicesW", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern int EnumDisplayDevices_Null(IntPtr dev, uint idx, IntPtr dd, uint flags);

    [DllImport("user32.dll", EntryPoint="EnumDisplayDevicesW", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern int EnumDisplayDevices_Str(string dev, uint idx, IntPtr dd, uint flags);

    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern int EnumDisplaySettingsExW(string dev, int mode, IntPtr dm, uint flags);

    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern int ChangeDisplaySettingsExW(string dev, IntPtr dm, IntPtr wnd, uint flags, IntPtr param);

    // ---- gdi32: interface D3DKMT (escapes do driver) ------------------------
    [DllImport("gdi32.dll")]
    private static extern int D3DKMTOpenAdapterFromGdiDisplayName(IntPtr arg);
    [DllImport("gdi32.dll")]
    private static extern int D3DKMTCloseAdapter(IntPtr arg);
    [DllImport("gdi32.dll")]
    private static extern int D3DKMTEscape(IntPtr arg);

    // DISPLAY_DEVICEW: cb 0 | DeviceName 4 (32 wchar) | DeviceString 68 (128)
    //                  StateFlags 324 | DeviceID 328 (128) | DeviceKey 584 (128)
    private const int DD_SIZE = 840;
    // DEVMODEW: dmSize 68 | dmFields 72 | dmBitsPerPel 168 | dmPelsWidth 172
    //           dmPelsHeight 176 | dmDisplayFlags 180 | dmDisplayFrequency 184
    private const int DM_SIZE = 220;

    public class Adapter {
        public string Name;         // \\.\DISPLAYn
        public string Description;
        public string DeviceId;     // PCI\VEN_...
        public uint   StateFlags;
        public bool   Attached;
        public bool   Primary;
    }

    public class DisplayMode {
        public int Width, Height, Bpp, Hz;
        public override string ToString() { return Width + "x" + Height + " " + Bpp + "bpp " + Hz + "Hz"; }
    }

    private static Adapter ReadAdapter(IntPtr b) {
        uint fl = (uint)Marshal.ReadInt32(b, 324);
        Adapter a = new Adapter();
        a.Name        = Marshal.PtrToStringUni(b + 4,   32).TrimEnd('\0');
        a.Description = Marshal.PtrToStringUni(b + 68, 128).TrimEnd('\0');
        a.DeviceId    = Marshal.PtrToStringUni(b + 328,128).TrimEnd('\0');
        a.StateFlags  = fl;
        a.Attached    = (fl & 0x1) != 0;
        a.Primary     = (fl & 0x4) != 0;
        return a;
    }

    public static Adapter[] Adapters() {
        var list = new System.Collections.Generic.List<Adapter>();
        IntPtr b = Marshal.AllocHGlobal(DD_SIZE);
        try {
            for (uint i = 0; i < 64; i++) {
                for (int z = 0; z < DD_SIZE; z++) Marshal.WriteByte(b, z, 0);
                Marshal.WriteInt32(b, 0, DD_SIZE);
                if (EnumDisplayDevices_Null(IntPtr.Zero, i, b, 0) == 0) break;
                list.Add(ReadAdapter(b));
            }
        } finally { Marshal.FreeHGlobal(b); }
        return list.ToArray();
    }

    public static Adapter[] Monitors(string adapterName) {
        var list = new System.Collections.Generic.List<Adapter>();
        IntPtr b = Marshal.AllocHGlobal(DD_SIZE);
        try {
            for (uint i = 0; i < 8; i++) {
                for (int z = 0; z < DD_SIZE; z++) Marshal.WriteByte(b, z, 0);
                Marshal.WriteInt32(b, 0, DD_SIZE);
                if (EnumDisplayDevices_Str(adapterName, i, b, 0) == 0) break;
                list.Add(ReadAdapter(b));
            }
        } finally { Marshal.FreeHGlobal(b); }
        return list.ToArray();
    }

    private static DisplayMode ReadMode(IntPtr b) {
        DisplayMode m = new DisplayMode();
        m.Bpp    = Marshal.ReadInt32(b, 168);
        m.Width  = Marshal.ReadInt32(b, 172);
        m.Height = Marshal.ReadInt32(b, 176);
        m.Hz     = Marshal.ReadInt32(b, 184);
        return m;
    }

    // flags: 0 = filtrado pelo monitor | 2 = EDS_RAWMODE | 4 = EDS_ROTATEDMODE
    public static DisplayMode[] Modes(string dev, uint flags) {
        var list = new System.Collections.Generic.List<DisplayMode>();
        IntPtr b = Marshal.AllocHGlobal(DM_SIZE);
        try {
            for (int i = 0; i < 1024; i++) {
                for (int z = 0; z < DM_SIZE; z++) Marshal.WriteByte(b, z, 0);
                Marshal.WriteInt16(b, 68, DM_SIZE);
                if (EnumDisplaySettingsExW(dev, i, b, flags) == 0) break;
                list.Add(ReadMode(b));
            }
        } finally { Marshal.FreeHGlobal(b); }
        return list.ToArray();
    }

    public static DisplayMode CurrentMode(string dev) {
        IntPtr b = Marshal.AllocHGlobal(DM_SIZE);
        try {
            for (int z = 0; z < DM_SIZE; z++) Marshal.WriteByte(b, z, 0);
            Marshal.WriteInt16(b, 68, DM_SIZE);
            if (EnumDisplaySettingsExW(dev, -1, b, 0) == 0) return null;
            return ReadMode(b);
        } finally { Marshal.FreeHGlobal(b); }
    }

    // CDS_TEST (0x02): valida o modo sem aplicar. Nunca muda a tela.
    public static int TestMode(string dev, int w, int h, int hz) {
        const uint FIELDS = 0x40000 | 0x80000 | 0x100000 | 0x400000;
        IntPtr b = Marshal.AllocHGlobal(DM_SIZE);
        try {
            for (int z = 0; z < DM_SIZE; z++) Marshal.WriteByte(b, z, 0);
            Marshal.WriteInt16(b, 68, DM_SIZE);
            Marshal.WriteInt32(b, 72, (int)FIELDS);
            Marshal.WriteInt32(b, 168, 32);
            Marshal.WriteInt32(b, 172, w);
            Marshal.WriteInt32(b, 176, h);
            Marshal.WriteInt32(b, 184, hz);
            return ChangeDisplaySettingsExW(dev, b, IntPtr.Zero, 0x02, IntPtr.Zero);
        } finally { Marshal.FreeHGlobal(b); }
    }

    public static string DispChangeName(int rc) {
        switch (rc) {
            case  0: return "SUCESSO";
            case  1: return "RESTART";
            case -1: return "FAILED";
            case -2: return "BADMODE";
            case -3: return "NOTUPDATED";
            case -4: return "BADFLAGS";
            case -5: return "BADPARAM";
            case -6: return "BADDUALVIEW";
            default: return "rc=" + rc;
        }
    }

    // ---- Escape do driver via D3DKMT ---------------------------------------
    // D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME (x64, 80 bytes):
    //   DeviceName[32] @0 | hAdapter @64 | AdapterLuid @68 | VidPnSourceId @76
    // D3DKMT_ESCAPE (x64, 32 bytes):
    //   hAdapter @0 | hDevice @4 | Type @8 | Flags @12
    //   pPrivateDriverData @16 | PrivateDriverDataSize @24 | hContext @28

    public static int OpenAdapter(string gdiName, out uint hAdapter) {
        hAdapter = 0;
        IntPtr b = Marshal.AllocHGlobal(80);
        try {
            for (int z = 0; z < 80; z++) Marshal.WriteByte(b, z, 0);
            byte[] nm = System.Text.Encoding.Unicode.GetBytes(gdiName);
            int n = Math.Min(nm.Length, 62);
            Marshal.Copy(nm, 0, b, n);
            int rc = D3DKMTOpenAdapterFromGdiDisplayName(b);
            if (rc == 0) hAdapter = (uint)Marshal.ReadInt32(b, 64);
            return rc;
        } finally { Marshal.FreeHGlobal(b); }
    }

    public static void CloseAdapter(uint hAdapter) {
        IntPtr b = Marshal.AllocHGlobal(4);
        try { Marshal.WriteInt32(b, 0, (int)hAdapter); D3DKMTCloseAdapter(b); }
        finally { Marshal.FreeHGlobal(b); }
    }

    // Envia payload e devolve o buffer de volta (o driver escreve in-place).
    public static int Escape(uint hAdapter, byte[] payload) {
        IntPtr pd = Marshal.AllocHGlobal(payload.Length);
        IntPtr es = Marshal.AllocHGlobal(32);
        try {
            Marshal.Copy(payload, 0, pd, payload.Length);
            for (int z = 0; z < 32; z++) Marshal.WriteByte(es, z, 0);
            Marshal.WriteInt32(es, 0, (int)hAdapter);
            Marshal.WriteInt32(es, 8, 0);              // D3DKMT_ESCAPE_DRIVERPRIVATE
            Marshal.WriteIntPtr(es, 16, pd);
            Marshal.WriteInt32(es, 24, payload.Length);
            int rc = D3DKMTEscape(es);
            Marshal.Copy(pd, payload, 0, payload.Length);
            return rc;
        } finally { Marshal.FreeHGlobal(pd); Marshal.FreeHGlobal(es); }
    }

    // ---- VSync real, medido pela API -------------------------------------
    // D3DKMTWaitForVerticalBlankEvent bloqueia ate o driver notificar
    // DXGK_INTERRUPT_DISPLAYONLY_VSYNC. Cronometrar N esperas mede o motor
    // de VSync sintetico de ponta a ponta -- muito melhor que ler o contador
    // do registro, que o driver so publica dentro de IsSupportedVidPn.
    // D3DKMT_WAITFORVERTICALBLANKEVENT: hAdapter @0 | hDevice @4 | SourceId @8
    [DllImport("gdi32.dll")]
    private static extern int D3DKMTWaitForVerticalBlankEvent(IntPtr arg);

    public static int WaitVBlank(uint hAdapter, uint sourceId) {
        IntPtr b = Marshal.AllocHGlobal(12);
        try {
            Marshal.WriteInt32(b, 0, (int)hAdapter);
            Marshal.WriteInt32(b, 4, 0);
            Marshal.WriteInt32(b, 8, (int)sourceId);
            return D3DKMTWaitForVerticalBlankEvent(b);
        } finally { Marshal.FreeHGlobal(b); }
    }

    // Devolve os intervalos em ms entre esperas consecutivas de vblank.
    public static double[] SampleVBlank(uint hAdapter, uint sourceId, int samples, out int failRc) {
        failRc = 0;
        var res = new System.Collections.Generic.List<double>();
        int rc = WaitVBlank(hAdapter, sourceId);   // descarta o primeiro (fase parcial)
        if (rc != 0) { failRc = rc; return res.ToArray(); }
        var sw = System.Diagnostics.Stopwatch.StartNew();
        double prev = 0;
        for (int i = 0; i < samples; i++) {
            rc = WaitVBlank(hAdapter, sourceId);
            if (rc != 0) { failRc = rc; break; }
            double now = sw.Elapsed.TotalMilliseconds;
            res.Add(now - prev);
            prev = now;
        }
        return res.ToArray();
    }

    // ---- Referencia de memoria do host (baseline do benchmark de blit) -----
    // Copia com Buffer.BlockCopy para dar uma cota superior de largura de
    // banda do lado da CPU, contra a qual comparar o caminho de apresentacao.
    public static double HostCopyMBs(int bytes, int iterations) {
        byte[] src = new byte[bytes];
        byte[] dst = new byte[bytes];
        for (int i = 0; i < bytes; i += 4096) src[i] = (byte)(i & 0xFF);
        Buffer.BlockCopy(src, 0, dst, 0, bytes);   // aquece
        var sw = System.Diagnostics.Stopwatch.StartNew();
        for (int i = 0; i < iterations; i++) Buffer.BlockCopy(src, 0, dst, 0, bytes);
        sw.Stop();
        double secs = sw.Elapsed.TotalSeconds;
        if (secs <= 0) return 0;
        return ((double)bytes * iterations) / secs / (1024.0 * 1024.0);
    }
}
'@
}

# -----------------------------------------------------------------------------
#  Contexto e registro de resultados
#
#  Estados:
#    PASS  - verificacao passou
#    FAIL  - verificacao falhou; conta como falha da suite
#    XFAIL - falha ESPERADA e documentada (bloqueio conhecido). Nao derruba a
#            suite, mas aparece no sumario. Se um XFAIL passar, vira XPASS e
#            e sinalizado: significa que o bloqueio foi resolvido.
#    SKIP  - pre-requisito ausente; nao conta como falha
#    INFO  - medicao ou observacao, sem julgamento
# -----------------------------------------------------------------------------
function New-Bc250TestContext {
    param([Parameter(Mandatory)][string]$Phase,
          [Parameter(Mandatory)][string]$Title)
    return [pscustomobject]@{
        Phase   = $Phase
        Title   = $Title
        Checks  = New-Object System.Collections.ArrayList
        Bench   = New-Object System.Collections.ArrayList
        Started = Get-Date
    }
}

function Add-Bc250Check {
    param([Parameter(Mandatory)]$Context,
          [Parameter(Mandatory)][ValidateSet('PASS','FAIL','XFAIL','XPASS','SKIP','INFO')][string]$Status,
          [Parameter(Mandatory)][string]$Name,
          [string]$Detail = '')
    [void]$Context.Checks.Add([pscustomobject]@{ Status=$Status; Name=$Name; Detail=$Detail })
    $color = 'Gray'
    if ($Status -eq 'PASS')  { $color = 'Green' }
    if ($Status -eq 'FAIL')  { $color = 'Red' }
    if ($Status -eq 'XFAIL') { $color = 'DarkYellow' }
    if ($Status -eq 'XPASS') { $color = 'Cyan' }
    if ($Status -eq 'SKIP')  { $color = 'Yellow' }
    $line = "  [{0,-5}] {1}" -f $Status, $Name
    if ($Detail -ne '') { $line = $line + "  ->  " + $Detail }
    Write-Host $line -ForegroundColor $color
}

# Assercao booleana. Se -Expected 'FAIL', uma falha e registrada como XFAIL
# (bloqueio conhecido) e um sucesso inesperado como XPASS.
function Assert-Bc250 {
    param([Parameter(Mandatory)]$Context,
          [Parameter(Mandatory)][string]$Name,
          [Parameter(Mandatory)][bool]$Condition,
          [string]$Detail = '',
          [ValidateSet('PASS','FAIL')][string]$Expected = 'PASS')
    if ($Expected -eq 'PASS') {
        if ($Condition) { Add-Bc250Check $Context 'PASS' $Name $Detail }
        else            { Add-Bc250Check $Context 'FAIL' $Name $Detail }
    } else {
        if ($Condition) { Add-Bc250Check $Context 'XPASS' $Name $Detail }
        else            { Add-Bc250Check $Context 'XFAIL' $Name $Detail }
    }
}

function Add-Bc250Bench {
    param([Parameter(Mandatory)]$Context,
          [Parameter(Mandatory)][string]$Metric,
          [Parameter(Mandatory)]$Value,
          [Parameter(Mandatory)][string]$Unit,
          [string]$Note = '')
    [void]$Context.Bench.Add([pscustomobject]@{ Metric=$Metric; Value=$Value; Unit=$Unit; Note=$Note })
    $line = "  [BENCH] {0,-38} {1,12} {2}" -f $Metric, $Value, $Unit
    if ($Note -ne '') { $line = $line + "   (" + $Note + ")" }
    Write-Host $line -ForegroundColor Magenta
}

function Write-Bc250Header {
    param([Parameter(Mandatory)]$Context)
    Write-Host ''
    Write-Host ('=' * 78) -ForegroundColor White
    Write-Host (" {0}: {1}" -f $Context.Phase, $Context.Title) -ForegroundColor White
    Write-Host ('=' * 78) -ForegroundColor White
}

function Write-Bc250Summary {
    param([Parameter(Mandatory)]$Context)
    $g = $Context.Checks | Group-Object Status -AsHashTable -AsString
    function CountOf($h, $k) { if ($h -and $h.ContainsKey($k)) { return $h[$k].Count } else { return 0 } }
    $pass  = CountOf $g 'PASS'
    $fail  = CountOf $g 'FAIL'
    $xfail = CountOf $g 'XFAIL'
    $xpass = CountOf $g 'XPASS'
    $skip  = CountOf $g 'SKIP'
    $secs  = [math]::Round(((Get-Date) - $Context.Started).TotalSeconds, 1)

    Write-Host ''
    Write-Host ("  {0}  ->  PASS {1} | FAIL {2} | XFAIL {3} | XPASS {4} | SKIP {5}   ({6}s)" -f `
        $Context.Phase, $pass, $fail, $xfail, $xpass, $skip, $secs) -ForegroundColor White
    if ($xpass -gt 0) {
        Write-Host "  ATENCAO: um XFAIL passou (XPASS). Um bloqueio conhecido pode ter sido resolvido -- revise o teste." -ForegroundColor Cyan
    }
    return [pscustomobject]@{
        Phase=$Context.Phase; Title=$Context.Title
        Pass=$pass; Fail=$fail; XFail=$xfail; XPass=$xpass; Skip=$skip
        Seconds=$secs; Bench=$Context.Bench; Checks=$Context.Checks
    }
}

# Encerra um teste: imprime o sumario e, se -PassThru, devolve o objeto para o
# orquestrador. Sem -PassThru, sai do processo com codigo 1 se houve FAIL.
# XFAIL nao derruba o codigo de saida (e bloqueio conhecido e documentado).
function Complete-Bc250Test {
    param([Parameter(Mandatory)]$Context,
          [bool]$PassThru = $false)
    $s = Write-Bc250Summary $Context
    if ($PassThru) { return $s }
    if ($s.Fail -gt 0) { exit 1 }
    exit 0
}

# -----------------------------------------------------------------------------
#  Coleta de estado
# -----------------------------------------------------------------------------
function Get-Bc250Telemetry {
    if (-not (Test-Path -LiteralPath $script:Bc250ParamsKey)) { return $null }
    $p = Get-ItemProperty -LiteralPath $script:Bc250ParamsKey -ErrorAction SilentlyContinue
    if ($null -eq $p) { return $null }
    $h = @{}
    foreach ($n in $p.PSObject.Properties.Name) {
        if ($n -like 'PS*') { continue }
        $h[$n] = $p.$n
    }
    return $h
}

function Get-Bc250ServiceConfig {
    if (-not (Test-Path -LiteralPath $script:Bc250ServiceKey)) { return $null }
    $p = Get-ItemProperty -LiteralPath $script:Bc250ServiceKey -ErrorAction SilentlyContinue
    if ($null -eq $p) { return $null }
    $h = @{}
    foreach ($n in $p.PSObject.Properties.Name) {
        if ($n -like 'PS*') { continue }
        $h[$n] = $p.$n
    }
    return $h
}

# Le uma chave de telemetria que carrega um NTSTATUS.
#
# NTSTATUS de erro tem o bit alto ligado (0xC0000483 = 3221226627). Um cast
# para [int] estoura -- foi exatamente o que derrubou o teste da Fase 3 quando
# o driver passou a reportar status de firmware nao-zero. Sempre normalize
# para uint32 antes de comparar.
function Get-TelemetryStatus {
    param($Telemetry, [string]$Name, [uint32]$Default = ([uint32]::MaxValue))
    if ($null -eq $Telemetry) { return $Default }
    if (-not $Telemetry.ContainsKey($Name)) { return $Default }
    return (ConvertTo-Bc250Status $Telemetry[$Name])
}

# Le uma chave de telemetria com default, sem estourar em StrictMode.
function Get-TelemetryValue {
    param($Telemetry, [string]$Name, $Default = $null)
    if ($null -eq $Telemetry) { return $Default }
    if ($Telemetry.ContainsKey($Name)) { return $Telemetry[$Name] }
    return $Default
}

function Get-Bc250PnpDevice {
    $d = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
         Where-Object { $_.InstanceId -like "$($script:Bc250HwidPrefix)*" } |
         Select-Object -First 1
    return $d
}

function Get-Bc250DriverVersion {
    $d = Get-Bc250PnpDevice
    if ($null -eq $d) { return $null }
    try {
        return (Get-PnpDeviceProperty -InstanceId $d.InstanceId -KeyName 'DEVPKEY_Device_DriverVersion' -ErrorAction Stop).Data
    } catch { return $null }
}

# Adaptador GDI ativo que corresponde a BC-250.
function Get-Bc250GdiAdapter {
    $all = [Bc250Native]::Adapters()
    $ours = $all | Where-Object { $_.DeviceId -like '*VEN_1002&DEV_13FE*' -and $_.Attached }
    if ($null -eq $ours) { return $null }
    return ($ours | Select-Object -First 1)
}

function Get-Bc250MonitorModes {
    try {
        $m = Get-CimInstance -Namespace root\wmi -ClassName WmiMonitorListedSupportedSourceModes -ErrorAction Stop
    } catch { return $null }
    $out = New-Object System.Collections.ArrayList
    foreach ($mon in $m) {
        foreach ($sm in $mon.MonitorSourceModes) {
            [void]$out.Add([pscustomobject]@{
                Instance = $mon.InstanceName
                Width    = $sm.HorizontalActivePixels
                Height   = $sm.VerticalActivePixels
            })
        }
    }
    return $out
}

# -----------------------------------------------------------------------------
#  Escapes do driver
#
#  Somente QUERY_INFO e permitido por padrao. Os demais exigem
#  EnableDangerousEscapes=1 no registro do servico (instalador:
#  install-wddm.ps1 -EnableDangerousEscapes).
# -----------------------------------------------------------------------------
$script:Bc250EscQueryInfo    = 0x04
$script:Bc250EscSdmaCopy     = 0xBD04
$script:Bc250EscSdmaBench    = 0xBD05

function Invoke-Bc250Escape {
    param([Parameter(Mandatory)][byte[]]$Payload)
    $a = Get-Bc250GdiAdapter
    if ($null -eq $a) { return [pscustomobject]@{ Ok=$false; Reason='adaptador GDI da BC-250 nao encontrado'; Payload=$null } }
    $h = 0
    $rc = [Bc250Native]::OpenAdapter($a.Name, [ref]$h)
    if ($rc -ne 0 -or $h -eq 0) {
        return [pscustomobject]@{ Ok=$false; Reason=("D3DKMTOpenAdapterFromGdiDisplayName rc=0x{0:X8}" -f (ConvertTo-Bc250Status $rc)); Payload=$null }
    }
    try {
        $rc = [Bc250Native]::Escape($h, $Payload)
        if ($rc -ne 0) {
            $st = ConvertTo-Bc250Status $rc
            $hint = Get-Bc250EscapeHint $rc
            $reason = "D3DKMTEscape rc=0x{0:X8}" -f $st
            if ($hint -ne '') { $reason = $reason + " (" + $hint + ")" }
            return [pscustomobject]@{ Ok=$false; Reason=$reason; Status=$st; Payload=$Payload }
        }
        return [pscustomobject]@{ Ok=$true; Reason=''; Status=0; Payload=$Payload }
    } finally { [Bc250Native]::CloseAdapter($h) }
}

# Os NTSTATUS chegam do P/Invoke como int com sinal: 0xC0000022 vira
# -1073741790, e o cast direto para [uint32] estoura em PowerShell. Normaliza
# passando por int64 e mascarando 32 bits.
function ConvertTo-Bc250Status {
    param($Value)
    return [uint32]([int64]$Value -band 0xFFFFFFFFL)
}

# Traduz os NTSTATUS que os escapes deste driver realmente devolvem, para o
# teste dizer o que fazer em vez de mostrar um hexadecimal cru.
function Get-Bc250EscapeHint {
    param($RawStatus)
    $Status = ConvertTo-Bc250Status $RawStatus
    switch ($Status) {
        0xC0000022 { return 'ACCESS_DENIED -- escape restrito; reinstale com install-wddm.ps1 -EnableDangerousEscapes' }
        0xC0000023 { return 'BUFFER_TOO_SMALL -- payload menor que o struct esperado pelo driver' }
        0xC00000BB { return 'NOT_SUPPORTED -- escape ausente no build instalado' }
        0xC00000A3 { return 'DEVICE_NOT_READY -- o motor exigido nao foi inicializado' }
        0xC000000D { return 'INVALID_PARAMETER' }
        default    { return '' }
    }
}

# BC250_WDDM_ESCAPE_INFO (x64): header(16) + 6 ULONG (24) = 40, ja alinhado a 8
# para VramGpuBase(40) e RingGpuBase(48), + 5 ULONG (20) = 76, com padding de
# struct para 80. O driver exige PrivateDriverDataSize >= sizeof(struct), logo
# 76 devolve STATUS_BUFFER_TOO_SMALL.
function Get-Bc250EscapeInfo {
    $size = 80
    $buf = New-Object byte[] $size
    [BitConverter]::GetBytes([uint32]$script:Bc250EscQueryInfo).CopyTo($buf, 0)
    [BitConverter]::GetBytes([uint32]$size).CopyTo($buf, 8)    # InSize
    $r = Invoke-Bc250Escape -Payload $buf
    if (-not $r.Ok) { return [pscustomobject]@{ Ok=$false; Reason=$r.Reason } }
    $b = $r.Payload
    return [pscustomobject]@{
        Ok                  = $true
        Reason              = ''
        DriverStatus        = [BitConverter]::ToUInt32($b, 4)
        DeviceId            = [BitConverter]::ToUInt32($b, 16)
        HardwareInitialized = [BitConverter]::ToUInt32($b, 20)
        GfxWptr             = [BitConverter]::ToUInt32($b, 24)
        GfxRptr             = [BitConverter]::ToUInt32($b, 28)
        GrbmStatus          = [BitConverter]::ToUInt32($b, 32)
        CpStat              = [BitConverter]::ToUInt32($b, 36)
        VramGpuBase         = [BitConverter]::ToUInt64($b, 40)
        RingGpuBase         = [BitConverter]::ToUInt64($b, 48)
        SdmaInitialized     = [BitConverter]::ToUInt32($b, 56)
        SdmaWptr            = [BitConverter]::ToUInt32($b, 60)
        SdmaRptr            = [BitConverter]::ToUInt32($b, 64)
        SdmaBandwidthMBs    = [BitConverter]::ToUInt32($b, 68)
        SdmaCopyCount       = [BitConverter]::ToUInt32($b, 72)
    }
}

# BC250_WDDM_ESCAPE_SDMA_BENCHMARK: header(16) + Iterations, BlockSizeBytes,
# AvgBandwidthMBs, PeakBandwidthMBs (16)
function Invoke-Bc250SdmaBenchmark {
    param([Parameter(Mandatory)][uint32]$Iterations,
          [Parameter(Mandatory)][uint32]$BlockSizeBytes)
    $size = 32
    $buf = New-Object byte[] $size
    [BitConverter]::GetBytes([uint32]$script:Bc250EscSdmaBench).CopyTo($buf, 0)
    [BitConverter]::GetBytes([uint32]$size).CopyTo($buf, 8)
    [BitConverter]::GetBytes($Iterations).CopyTo($buf, 16)
    [BitConverter]::GetBytes($BlockSizeBytes).CopyTo($buf, 20)
    $r = Invoke-Bc250Escape -Payload $buf
    if (-not $r.Ok) { return [pscustomobject]@{ Ok=$false; Reason=$r.Reason } }
    $b = $r.Payload
    return [pscustomobject]@{
        Ok           = $true
        DriverStatus = [BitConverter]::ToUInt32($b, 4)
        Iterations   = [BitConverter]::ToUInt32($b, 16)
        BlockSize    = [BitConverter]::ToUInt32($b, 20)
        AvgMBs       = [BitConverter]::ToUInt32($b, 24)
        PeakMBs      = [BitConverter]::ToUInt32($b, 28)
    }
}

# BC250_WDDM_ESCAPE_COMPUTE_CAPS: header(16) + 9 ULONG (36) -> offset 52,
# alinhado a 8 para o primeiro ULONGLONG -> 56. Total 80 bytes.
$script:Bc250EscComputeCaps = 0xBD06

function Get-Bc250ComputeCaps {
    $size = 80
    $buf = New-Object byte[] $size
    [BitConverter]::GetBytes([uint32]$script:Bc250EscComputeCaps).CopyTo($buf, 0)
    [BitConverter]::GetBytes([uint32]$size).CopyTo($buf, 8)
    $r = Invoke-Bc250Escape -Payload $buf
    if (-not $r.Ok) { return [pscustomobject]@{ Ok=$false; Reason=$r.Reason } }
    $b = $r.Payload
    return [pscustomobject]@{
        Ok               = $true
        DriverStatus     = [BitConverter]::ToUInt32($b, 4)
        NumCu            = [BitConverter]::ToUInt32($b, 16)
        NumWgp           = [BitConverter]::ToUInt32($b, 20)
        NumSimdPerCu     = [BitConverter]::ToUInt32($b, 24)
        NumTotalShaders  = [BitConverter]::ToUInt32($b, 28)
        GpuClockMhz      = [BitConverter]::ToUInt32($b, 32)
        GfxArch          = [BitConverter]::ToUInt32($b, 36)
        MaxWorkgroupSize = [BitConverter]::ToUInt32($b, 40)
        WaveSize         = [BitConverter]::ToUInt32($b, 44)
        LdsSizeBytes     = [BitConverter]::ToUInt32($b, 48)
        VramTotalBytes   = [BitConverter]::ToUInt64($b, 56)
        ComputeVramBase  = [BitConverter]::ToUInt64($b, 64)
        ComputeVramSize  = [BitConverter]::ToUInt32($b, 72)
        DispatchCount    = [BitConverter]::ToUInt32($b, 76)
    }
}

# Mede a taxa de VSync entregue ao SO. Devolve estatisticas em Hz/ms.
function Measure-Bc250VsyncRate {
    param([int]$Samples = 30)
    $a = Get-Bc250GdiAdapter
    if ($null -eq $a) { return [pscustomobject]@{ Ok=$false; Reason='adaptador GDI da BC-250 nao encontrado' } }
    $h = 0
    $rc = [Bc250Native]::OpenAdapter($a.Name, [ref]$h)
    if ($rc -ne 0 -or $h -eq 0) {
        return [pscustomobject]@{ Ok=$false; Reason=("OpenAdapter rc=0x{0:X8}" -f (ConvertTo-Bc250Status $rc)) }
    }
    try {
        $failRc = 0
        $iv = [Bc250Native]::SampleVBlank($h, 0, $Samples, [ref]$failRc)
        if ($iv.Count -eq 0) {
            return [pscustomobject]@{ Ok=$false; Reason=("WaitForVerticalBlankEvent rc=0x{0:X8}" -f (ConvertTo-Bc250Status $failRc)) }
        }
        $stats = $iv | Measure-Object -Average -Minimum -Maximum
        $avgMs = $stats.Average
        $hz = 0
        if ($avgMs -gt 0) { $hz = 1000.0 / $avgMs }
        return [pscustomobject]@{
            Ok       = $true
            Samples  = $iv.Count
            AvgMs    = [math]::Round($avgMs, 3)
            MinMs    = [math]::Round($stats.Minimum, 3)
            MaxMs    = [math]::Round($stats.Maximum, 3)
            Hz       = [math]::Round($hz, 2)
            PartialRc= $failRc
        }
    } finally { [Bc250Native]::CloseAdapter($h) }
}

# -----------------------------------------------------------------------------
#  Geracao de carga de desenho
#
#  Uma janela sem borda e repintada em loop. Cada repintura passa pelo DWM e
#  desce como PresentDisplayOnly no driver, entao a variacao de Present_Count
#  no registro mede o caminho real de apresentacao.
#
#  -DirtyFraction controla a fracao da janela redesenhada, para comparar
#  frame cheio contra retangulo sujo pequeno.
# -----------------------------------------------------------------------------
function Invoke-Bc250DrawLoad {
    param([int]$Width = 800,
          [int]$Height = 600,
          [double]$Seconds = 3.0,
          [double]$DirtyFraction = 1.0)

    Add-Type -AssemblyName System.Windows.Forms, System.Drawing

    $form            = New-Object System.Windows.Forms.Form
    $form.FormBorderStyle = 'None'
    $form.StartPosition   = 'Manual'
    $form.Left = 0; $form.Top = 0
    $form.Width = $Width; $form.Height = $Height
    $form.TopMost = $true
    $form.BackColor = [System.Drawing.Color]::Black
    $form.ShowInTaskbar = $false

    $frames = 0
    $form.Show()
    [System.Windows.Forms.Application]::DoEvents()

    $g = $form.CreateGraphics()
    $rnd = New-Object System.Random 12345
    $dw = [int]($Width * $DirtyFraction)
    $dh = [int]($Height * $DirtyFraction)
    if ($dw -lt 1) { $dw = 1 }
    if ($dh -lt 1) { $dh = 1 }

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
            $c = [System.Drawing.Color]::FromArgb(255, $rnd.Next(256), $rnd.Next(256), $rnd.Next(256))
            $brush = New-Object System.Drawing.SolidBrush $c
            $g.FillRectangle($brush, 0, 0, $dw, $dh)
            $brush.Dispose()
            $form.Invalidate([System.Drawing.Rectangle]::new(0, 0, $dw, $dh))
            $form.Update()
            [System.Windows.Forms.Application]::DoEvents()
            $frames++
        }
    } finally {
        $sw.Stop()
        $g.Dispose()
        $form.Close()
        $form.Dispose()
        [System.Windows.Forms.Application]::DoEvents()
    }

    return [pscustomobject]@{
        Frames        = $frames
        Seconds       = $sw.Elapsed.TotalSeconds
        DirtyWidth    = $dw
        DirtyHeight   = $dh
        DirtyBytes    = [int64]$dw * $dh * 4
    }
}

# Mede a taxa de apresentacao de forma robusta.
#
# O driver so grava Present_Count no registro quando (count & 0xFF) == 0, ou
# seja em passos de 256. Com o Present limitado pelo VSync a ~60 Hz, um passo
# leva ~4,3 s de carga continua. Medir numa janela de 1,5 s devolve delta 0 e
# produziria uma falha espuria -- foi exatamente o que aconteceu na primeira
# execucao da suite. Por isso a medicao insiste em blocos de carga ate o
# contador andar, com teto de tempo.
function Measure-Bc250PresentRate {
    param([Parameter(Mandatory)][int]$Width,
          [Parameter(Mandatory)][int]$Height,
          [double]$DirtyFraction = 1.0,
          [double]$MaxSeconds = 24.0,
          [double]$ChunkSeconds = 3.0)

    $before = [int64](Get-TelemetryValue (Get-Bc250Telemetry) 'Present_Count' 0)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $frames = 0
    $delta = [int64]0
    $dirtyW = 0
    $dirtyH = 0

    while ($sw.Elapsed.TotalSeconds -lt $MaxSeconds) {
        $l = Invoke-Bc250DrawLoad -Width $Width -Height $Height -Seconds $ChunkSeconds -DirtyFraction $DirtyFraction
        $frames += $l.Frames
        $dirtyW = $l.DirtyWidth
        $dirtyH = $l.DirtyHeight
        $now = [int64](Get-TelemetryValue (Get-Bc250Telemetry) 'Present_Count' 0)
        $delta = $now - $before
        if ($delta -gt 0) { break }
    }
    $sw.Stop()

    $secs = $sw.Elapsed.TotalSeconds
    $pps = 0.0
    if (($secs -gt 0) -and ($delta -gt 0)) { $pps = $delta / $secs }
    $harnessFps = 0.0
    if ($secs -gt 0) { $harnessFps = $frames / $secs }

    return [pscustomobject]@{
        Ok           = ($delta -gt 0)
        Delta        = $delta
        Seconds      = [math]::Round($secs, 2)
        HarnessFrames= $frames
        HarnessFps   = [math]::Round($harnessFps, 1)
        PresentsPerSec = [math]::Round($pps, 1)
        DirtyWidth   = $dirtyW
        DirtyHeight  = $dirtyH
        DirtyBytes   = [int64]$dirtyW * $dirtyH * 4
    }
}

# Mede a variacao de uma chave de telemetria durante a execucao de um bloco.
function Measure-Bc250TelemetryDelta {
    param([Parameter(Mandatory)][string]$Key,
          [Parameter(Mandatory)][scriptblock]$Action)
    $before = Get-TelemetryValue (Get-Bc250Telemetry) $Key 0
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $result = & $Action
    $sw.Stop()
    $after = Get-TelemetryValue (Get-Bc250Telemetry) $Key 0
    return [pscustomobject]@{
        Key     = $Key
        Before  = [int64]$before
        After   = [int64]$after
        Delta   = [int64]$after - [int64]$before
        Seconds = $sw.Elapsed.TotalSeconds
        Result  = $result
    }
}
