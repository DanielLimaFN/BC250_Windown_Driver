$ErrorActionPreference = 'Stop'
$dest = 'c:\Users\Daniel\Desktop\NamixWDDM\firmware\vcn'
if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Path $dest -Force | Out-Null }

$files = @(
    'raven_vcn.bin', 'picasso_vcn.bin', 'raven2_vcn.bin', 'arcturus_vcn.bin',
    'renoir_vcn.bin', 'green_sardine_vcn.bin', 'navi10_vcn.bin', 'navi14_vcn.bin',
    'navi12_vcn.bin', 'sienna_cichlid_vcn.bin', 'navy_flounder_vcn.bin', 'vangogh_vcn.bin',
    'dimgrey_cavefish_vcn.bin', 'aldebaran_vcn.bin', 'beige_goby_vcn.bin', 'yellow_carp_vcn.bin',
    'vcn_3_1_2.bin', 'vcn_4_0_0.bin', 'vcn_4_0_2.bin', 'vcn_4_0_3.bin',
    'vcn_4_0_4.bin', 'vcn_4_0_5.bin', 'vcn_4_0_6.bin', 'vcn_4_0_6_1.bin',
    'vcn_5_0_0.bin', 'vcn_5_0_1.bin', 'vcn_5_0_2.bin', 'vcn_5_3_0.bin'
)

$baseUrls = @(
    'https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/amdgpu/',
    'https://raw.githubusercontent.com/freedesktop/drm-firmware/master/amdgpu/'
)

$success = 0
$failed = 0

foreach ($f in $files) {
    $target = Join-Path $dest $f
    $downloaded = $false
    Write-Host ("Baixando {0,-26} ... " -f $f) -NoNewline
    
    foreach ($baseUrl in $baseUrls) {
        $url = $baseUrl + $f
        try {
            Invoke-WebRequest -Uri $url -OutFile $target -TimeoutSec 30
            $bytes = [System.IO.File]::ReadAllBytes($target)
            # Check if it starts with AMD magic or is binary (not HTML)
            if ($bytes.Length -gt 100 -and $bytes[0] -ne 0x3C) { # 0x3C is '<'
                Write-Host ("OK ({0} bytes) [de {1}]" -f $bytes.Length, ($baseUrl -split '/')[2]) -ForegroundColor Green
                $downloaded = $true
                $success++
                break
            }
        } catch {
            # try next url
        }
    }
    
    if (-not $downloaded) {
        Write-Host "FALHA em todos os mirrors" -ForegroundColor Red
        $failed++
    }
}

Write-Host ""
Write-Host ("Download concluido: {0} sucessos, {1} falhas." -f $success, $failed)
