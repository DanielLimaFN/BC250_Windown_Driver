@echo off
setlocal enabledelayedexpansion

set SRCDIR=%~dp0
set ROOTDIR=%~dp0..\..
set ODIR=%~dp0output
if not exist "%ODIR%" mkdir "%ODIR%"

set HAVE_CL=
where cl.exe >nul 2>&1 && set HAVE_CL=1

set VCVARS=
if not defined HAVE_CL (
  for %%D in (C D E F G) do (
    for %%E in (BuildTools Community Professional Enterprise) do (
      if not defined VCVARS (
        if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
        if exist "%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
      )
    )
  )
  if not defined VCVARS (
    echo ERRO: vcvars64.bat do VS2022 nao encontrado.
    exit /b 1
  )
  call "!VCVARS!" >nul
)

set WDK=C:\Program Files (x86)\Windows Kits\10
if defined WindowsSdkDir set "WDK=%WindowsSdkDir:~0,-1%"
set WDKVER=
for %%V in (10.0.26100.0 10.0.22621.0) do (
  if not defined WDKVER (
    if exist "%WDK%\Include\%%V\km\ntddk.h" set "WDKVER=%%V"
  )
)
if not defined WDKVER (
  echo ERRO: WDK 10.0.22621 ou superior nao encontrado.
  exit /b 1
)

set CFLAGS=/nologo /c /Brepro /W4 /WX /O2 /Oi /kernel /GS- /X ^
 /D_AMD64_ /DAMD64 /D_WIN64 /DBC250_KERNEL_MODE /DNTDDI_VERSION=0x0A00000C ^
 /DDXGKDDI_INTERFACE_VERSION=0xC004 ^
 /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
 /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\driver\core" ^
 /I "%WDK%\Include\%WDKVER%\km" ^
 /I "%WDK%\Include\%WDKVER%\km\crt" ^
 /I "%WDK%\Include\%WDKVER%\shared"

echo [cc] bc250_resource_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_resource_contract.obj" "%ROOTDIR%\driver\core\bc250_resource_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_vram_allocator.c
cl %CFLAGS% /Fo"%ODIR%\bc250_vram_allocator.obj" "%ROOTDIR%\driver\core\bc250_vram_allocator.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_address_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_address_contract.obj" "%ROOTDIR%\driver\core\bc250_address_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_vram_topology_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_vram_topology_contract.obj" "%ROOTDIR%\driver\core\bc250_vram_topology_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_vram_topology_reader.c
cl %CFLAGS% /Fo"%ODIR%\bc250_vram_topology_reader.obj" "%ROOTDIR%\driver\core\bc250_vram_topology_reader.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_discovery_tmr_reader.c
cl %CFLAGS% /Fo"%ODIR%\bc250_discovery_tmr_reader.obj" "%ROOTDIR%\driver\core\bc250_discovery_tmr_reader.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_platform_register_snapshot.c
cl %CFLAGS% /Fo"%ODIR%\bc250_platform_register_snapshot.obj" "%ROOTDIR%\driver\core\bc250_platform_register_snapshot.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_ip_discovery_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_ip_discovery_contract.obj" "%ROOTDIR%\driver\core\bc250_ip_discovery_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_psp_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_psp_contract.obj" "%ROOTDIR%\driver\core\bc250_psp_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_firmware_staging.c
cl %CFLAGS% /Fo"%ODIR%\bc250_firmware_staging.obj" "%ROOTDIR%\driver\core\bc250_firmware_staging.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_compute_bootstrap.c
cl %CFLAGS% /Fo"%ODIR%\bc250_compute_bootstrap.obj" "%ROOTDIR%\driver\core\bc250_compute_bootstrap.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_staging_lease.c
cl %CFLAGS% /Fo"%ODIR%\bc250_staging_lease.obj" "%ROOTDIR%\driver\core\bc250_staging_lease.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_runtime_inventory.c
cl %CFLAGS% /Fo"%ODIR%\bc250_runtime_inventory.obj" "%ROOTDIR%\driver\core\bc250_runtime_inventory.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_gcvm_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_gcvm_contract.obj" "%ROOTDIR%\driver\core\bc250_gcvm_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_sdma_packet_builder.c
cl %CFLAGS% /Fo"%ODIR%\bc250_sdma_packet_builder.obj" "%ROOTDIR%\driver\core\bc250_sdma_packet_builder.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_pm4_packet_builder.c
cl %CFLAGS% /Fo"%ODIR%\bc250_pm4_packet_builder.obj" "%ROOTDIR%\driver\core\bc250_pm4_packet_builder.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_golden_settings_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_golden_settings_contract.obj" "%ROOTDIR%\driver\core\bc250_golden_settings_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_compute_mqd_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_compute_mqd_contract.obj" "%ROOTDIR%\driver\core\bc250_compute_mqd_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_context_dispatcher_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_context_dispatcher_contract.obj" "%ROOTDIR%\driver\core\bc250_context_dispatcher_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_ih_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_ih_contract.obj" "%ROOTDIR%\driver\core\bc250_ih_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_paging_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_paging_contract.obj" "%ROOTDIR%\driver\core\bc250_paging_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_gfxhub_tlb_transaction.c
cl %CFLAGS% /Fo"%ODIR%\bc250_gfxhub_tlb_transaction.obj" "%ROOTDIR%\driver\core\bc250_gfxhub_tlb_transaction.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_gcvm_vmid8_transaction.c
cl %CFLAGS% /Fo"%ODIR%\bc250_gcvm_vmid8_transaction.obj" "%ROOTDIR%\driver\core\bc250_gcvm_vmid8_transaction.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_psp_ring_transaction.c
cl %CFLAGS% /Fo"%ODIR%\bc250_psp_ring_transaction.obj" "%ROOTDIR%\driver\core\bc250_psp_ring_transaction.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_psp_load_fw_transaction.c
cl %CFLAGS% /Fo"%ODIR%\bc250_psp_load_fw_transaction.obj" "%ROOTDIR%\driver\core\bc250_psp_load_fw_transaction.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_sdma_f32_transaction.c
cl %CFLAGS% /Fo"%ODIR%\bc250_sdma_f32_transaction.obj" "%ROOTDIR%\driver\core\bc250_sdma_f32_transaction.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_adapter_info_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_adapter_info_contract.obj" "%ROOTDIR%\driver\core\bc250_adapter_info_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_segment_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_segment_contract.obj" "%ROOTDIR%\driver\core\bc250_segment_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_tdr_recovery_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_tdr_recovery_contract.obj" "%ROOTDIR%\driver\core\bc250_tdr_recovery_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_submit_command_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_submit_command_contract.obj" "%ROOTDIR%\driver\core\bc250_submit_command_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_preemption_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_preemption_contract.obj" "%ROOTDIR%\driver\core\bc250_preemption_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_gpu_clock_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_gpu_clock_contract.obj" "%ROOTDIR%\driver\core\bc250_gpu_clock_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_power_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_power_contract.obj" "%ROOTDIR%\driver\core\bc250_power_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_smu_telemetry_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_smu_telemetry_contract.obj" "%ROOTDIR%\driver\core\bc250_smu_telemetry_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_superio_nct6686_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_superio_nct6686_contract.obj" "%ROOTDIR%\driver\core\bc250_superio_nct6686_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_isa_validator.c
cl %CFLAGS% /Fo"%ODIR%\bc250_isa_validator.obj" "%ROOTDIR%\driver\core\bc250_isa_validator.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_compute_dispatch_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_compute_dispatch_contract.obj" "%ROOTDIR%\driver\core\bc250_compute_dispatch_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_observable_shader.c
cl %CFLAGS% /Fo"%ODIR%\bc250_observable_shader.obj" "%ROOTDIR%\driver\core\bc250_observable_shader.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_owned_adapter_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_owned_adapter_contract.obj" "%ROOTDIR%\driver\core\bc250_owned_adapter_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_mcdm_object_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_mcdm_object_contract.obj" "%ROOTDIR%\driver\core\bc250_mcdm_object_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_allocation_abi_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_allocation_abi_contract.obj" "%ROOTDIR%\driver\core\bc250_allocation_abi_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_gpuva_sdma_broker_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_gpuva_sdma_broker_contract.obj" "%ROOTDIR%\driver\core\bc250_gpuva_sdma_broker_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_gpuva_sdma_executor_contract.c
cl %CFLAGS% /Fo"%ODIR%\bc250_gpuva_sdma_executor_contract.obj" "%ROOTDIR%\driver\core\bc250_gpuva_sdma_executor_contract.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_cm_resource_adapter.c
cl %CFLAGS% /Fo"%ODIR%\bc250_cm_resource_adapter.obj" "%SRCDIR%bc250_cm_resource_adapter.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_start_device_adapter.c
cl %CFLAGS% /Fo"%ODIR%\bc250_start_device_adapter.obj" "%SRCDIR%bc250_start_device_adapter.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_cp_firmware_manifest.c
cl %CFLAGS% /Fo"%ODIR%\bc250_cp_firmware_manifest.obj" "%SRCDIR%bc250_cp_firmware_manifest.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_dxgk_readonly_ddi.c
cl %CFLAGS% /Fo"%ODIR%\bc250_dxgk_readonly_ddi.obj" "%SRCDIR%bc250_dxgk_readonly_ddi.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_pci_bar_reader.c
cl %CFLAGS% /Fo"%ODIR%\bc250_pci_bar_reader.obj" "%SRCDIR%bc250_pci_bar_reader.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_rebar_reader.c
cl %CFLAGS% /Fo"%ODIR%\bc250_rebar_reader.obj" "%SRCDIR%bc250_rebar_reader.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_post_display_reservation.c
cl %CFLAGS% /Fo"%ODIR%\bc250_post_display_reservation.obj" "%SRCDIR%bc250_post_display_reservation.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_vram_probe_transaction.c
cl %CFLAGS% /Fo"%ODIR%\bc250_vram_probe_transaction.obj" "%SRCDIR%bc250_vram_probe_transaction.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_gcvm_backing_transaction.c
cl %CFLAGS% /Fo"%ODIR%\bc250_gcvm_backing_transaction.obj" "%SRCDIR%bc250_gcvm_backing_transaction.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_mmio_topology_transport.c
cl %CFLAGS% /Fo"%ODIR%\bc250_mmio_topology_transport.obj" "%SRCDIR%bc250_mmio_topology_transport.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_readonly_miniport.c
cl %CFLAGS% /Fo"%ODIR%\bc250_readonly_miniport.obj" "%SRCDIR%bc250_readonly_miniport.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_active_miniport.c
cl %CFLAGS% /Fo"%ODIR%\bc250_active_miniport.obj" "%SRCDIR%bc250_active_miniport.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_sdma_ring_work.c
cl %CFLAGS% /Fo"%ODIR%\bc250_sdma_ring_work.obj" "%SRCDIR%bc250_sdma_ring_work.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_pfp_direct_load.c
cl %CFLAGS% /Fo"%ODIR%\bc250_pfp_direct_load.obj" "%SRCDIR%bc250_pfp_direct_load.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_mcdm_callbacks.c
cl %CFLAGS% /Fo"%ODIR%\bc250_mcdm_callbacks.obj" "%SRCDIR%bc250_mcdm_callbacks.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_display_ddi.c (offline)
cl %CFLAGS% /Fo"%ODIR%\bc250_display_ddi.obj" "%SRCDIR%bc250_display_ddi.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_display_ddi.c (Dxgk registration)
cl %CFLAGS% /DBC250_ENABLE_DXGK_REGISTRATION=1 ^
  /Fo"%ODIR%\bc250_display_ddi_driver.obj" "%SRCDIR%bc250_display_ddi.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_probe_telemetry.c ^(offline^)
cl %CFLAGS% /Fo"%ODIR%\bc250_probe_telemetry.obj" "%SRCDIR%bc250_probe_telemetry.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_probe_telemetry.c ^(persistent registry^)
cl %CFLAGS% /DBC250_ENABLE_PERSISTENT_TELEMETRY=1 ^
  /Fo"%ODIR%\bc250_probe_telemetry_driver.obj" "%SRCDIR%bc250_probe_telemetry.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_readonly_miniport.c ^(Dxgk registration^)
cl %CFLAGS% /DBC250_ENABLE_DXGK_REGISTRATION=1 ^
  /Fo"%ODIR%\bc250_readonly_miniport_driver.obj" "%SRCDIR%bc250_readonly_miniport.c"
if errorlevel 1 exit /b 1

rem BC250_ENABLE_PERSISTENT_TELEMETRY habilita Bc250ActiveReadTunable(), a unica
rem leitura de tunable do driver. Sem ela a funcao compila para
rem "return DefaultValue" e o valor gravado em Parameters\SkipHwBringup seria
rem ignorado mesmo que o caminho de bring-up estivesse compilado.
rem
rem ATENCAO - esta flag sozinha NAO liga o bring-up de hardware, e nao e' isso
rem que ela conserta. O chamador de Bc250ActiveReadTunable esta dentro do #else
rem de BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP (bc250_active_miniport.c:2339),
rem que a imagem de producao NAO define - por contrato de seguranca auditado em
rem tools\audit-bc250mcdm-safety.py, que inclusive PROIBE definir essa macro
rem aqui. Na imagem de producao o eco e' fixo:
rem Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, 1u).
rem
rem Ou seja: esta flag corrige um bug latente, que so' se manifesta numa imagem
rem de laboratorio com o bring-up compilado. Ver STATUS_E_ROADMAP_CONSOLIDADO.md
rem secao 2.0.2.
echo [cc] bc250_active_miniport.c ^(Dxgk registration^)
cl %CFLAGS% /DBC250_ENABLE_DXGK_REGISTRATION=1 /DBC250_ENABLE_PERSISTENT_TELEMETRY=1 ^
  /Fo"%ODIR%\bc250_active_miniport_driver.obj" "%SRCDIR%bc250_active_miniport.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_sdma_ring_work.c ^(Dxgk registration^)
cl %CFLAGS% /DBC250_ENABLE_DXGK_REGISTRATION=1 ^
  /Fo"%ODIR%\bc250_sdma_ring_work_driver.obj" "%SRCDIR%bc250_sdma_ring_work.c"
if errorlevel 1 exit /b 1


echo [cc] bc250_pfp_direct_load.c ^(Dxgk registration^)
cl %CFLAGS% /DBC250_ENABLE_DXGK_REGISTRATION=1 ^
  /Fo"%ODIR%\bc250_pfp_direct_load_driver.obj" "%SRCDIR%bc250_pfp_direct_load.c"
if errorlevel 1 exit /b 1

echo [cc] bc250_compute_service.c
cl %CFLAGS% /Fo"%ODIR%\bc250_compute_service.obj" "%SRCDIR%bc250_compute_service.c"
if errorlevel 1 exit /b 1

echo [cc] cm_resource_adapter_test.c
cl %CFLAGS% /Fo"%ODIR%\cm_resource_adapter_test.obj" "%ROOTDIR%\driver\tests\cm_resource_adapter_test.c"
if errorlevel 1 exit /b 1

echo [link] cm_resource_adapter_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\cm_resource_adapter_test.exe" ^
  "%ODIR%\bc250_resource_contract.obj" ^
  "%ODIR%\bc250_cm_resource_adapter.obj" ^
  "%ODIR%\cm_resource_adapter_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\cm_resource_adapter_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(cm_resource_adapter_test^)
  exit /b 1
)

echo [cc] vram_topology_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\vram_topology_contract_test.obj" "%ROOTDIR%\driver\tests\vram_topology_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] vram_topology_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\vram_topology_contract_test.exe" ^
  "%ODIR%\bc250_vram_topology_contract.obj" ^
  "%ODIR%\vram_topology_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\vram_topology_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(vram_topology_contract_test^)
  exit /b 1
)

echo [cc] vram_topology_reader_test.c
cl %CFLAGS% /Fo"%ODIR%\vram_topology_reader_test.obj" "%ROOTDIR%\driver\tests\vram_topology_reader_test.c"
if errorlevel 1 exit /b 1

echo [link] vram_topology_reader_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\vram_topology_reader_test.exe" ^
  "%ODIR%\bc250_vram_topology_contract.obj" ^
  "%ODIR%\bc250_vram_topology_reader.obj" ^
  "%ODIR%\vram_topology_reader_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\vram_topology_reader_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(vram_topology_reader_test^)
  exit /b 1
)

echo [cc] discovery_tmr_reader_test.c
cl %CFLAGS% /Fo"%ODIR%\discovery_tmr_reader_test.obj" "%ROOTDIR%\driver\tests\discovery_tmr_reader_test.c"
if errorlevel 1 exit /b 1

echo [link] discovery_tmr_reader_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\discovery_tmr_reader_test.exe" ^
  "%ODIR%\bc250_discovery_tmr_reader.obj" ^
  "%ODIR%\discovery_tmr_reader_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\discovery_tmr_reader_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(discovery_tmr_reader_test^)
  exit /b 1
)

echo [cc] ip_discovery_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\ip_discovery_contract_test.obj" "%ROOTDIR%\driver\tests\ip_discovery_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] ip_discovery_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\ip_discovery_contract_test.exe" ^
  "%ODIR%\bc250_ip_discovery_contract.obj" ^
  "%ODIR%\ip_discovery_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\ip_discovery_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(ip_discovery_contract_test^)
  exit /b 1
)

echo [cc] mmio_topology_transport_test.c
cl %CFLAGS% /Fo"%ODIR%\mmio_topology_transport_test.obj" "%ROOTDIR%\driver\tests\mmio_topology_transport_test.c"
if errorlevel 1 exit /b 1

echo [link] mmio_topology_transport_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\mmio_topology_transport_test.exe" ^
  "%ODIR%\bc250_vram_topology_contract.obj" ^
  "%ODIR%\bc250_vram_topology_reader.obj" ^
  "%ODIR%\bc250_discovery_tmr_reader.obj" ^
  "%ODIR%\bc250_mmio_topology_transport.obj" ^
  "%ODIR%\bc250_platform_register_snapshot.obj" ^
  "%ODIR%\mmio_topology_transport_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\mmio_topology_transport_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(mmio_topology_transport_test^)
  exit /b 1
)

echo [cc] start_device_adapter_test.c
cl %CFLAGS% /Fo"%ODIR%\start_device_adapter_test.obj" "%ROOTDIR%\driver\tests\start_device_adapter_test.c"
if errorlevel 1 exit /b 1

echo [link] start_device_adapter_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\start_device_adapter_test.exe" ^
  "%ODIR%\bc250_resource_contract.obj" ^
  "%ODIR%\bc250_vram_allocator.obj" ^
  "%ODIR%\bc250_address_contract.obj" ^
  "%ODIR%\bc250_psp_contract.obj" ^
  "%ODIR%\bc250_compute_bootstrap.obj" ^
  "%ODIR%\bc250_owned_adapter_contract.obj" ^
  "%ODIR%\bc250_cm_resource_adapter.obj" ^
  "%ODIR%\bc250_start_device_adapter.obj" ^
  "%ODIR%\bc250_cp_firmware_manifest.obj" ^
  "%ODIR%\start_device_adapter_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\start_device_adapter_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(start_device_adapter_test^)
  exit /b 1
)

echo [cc] firmware_staging_test.c
cl %CFLAGS% /Fo"%ODIR%\firmware_staging_test.obj" "%ROOTDIR%\driver\tests\firmware_staging_test.c"
if errorlevel 1 exit /b 1

echo [link] firmware_staging_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\firmware_staging_test.exe" ^
  "%ODIR%\bc250_vram_allocator.obj" ^
  "%ODIR%\bc250_address_contract.obj" ^
  "%ODIR%\bc250_psp_contract.obj" ^
  "%ODIR%\bc250_compute_bootstrap.obj" ^
  "%ODIR%\bc250_staging_lease.obj" ^
  "%ODIR%\bc250_firmware_staging.obj" ^
  "%ODIR%\firmware_staging_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\firmware_staging_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(firmware_staging_test^)
  exit /b 1
)

echo [cc] dxgk_readonly_ddi_test.c
cl %CFLAGS% /Fo"%ODIR%\dxgk_readonly_ddi_test.obj" "%ROOTDIR%\driver\tests\dxgk_readonly_ddi_test.c"
if errorlevel 1 exit /b 1

echo [link] dxgk_readonly_ddi_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\dxgk_readonly_ddi_test.exe" ^
  "%ODIR%\bc250_resource_contract.obj" ^
  "%ODIR%\bc250_vram_allocator.obj" ^
  "%ODIR%\bc250_address_contract.obj" ^
  "%ODIR%\bc250_psp_contract.obj" ^
  "%ODIR%\bc250_compute_bootstrap.obj" ^
  "%ODIR%\bc250_owned_adapter_contract.obj" ^
  "%ODIR%\bc250_cm_resource_adapter.obj" ^
  "%ODIR%\bc250_start_device_adapter.obj" ^
  "%ODIR%\bc250_cp_firmware_manifest.obj" ^
  "%ODIR%\bc250_pci_bar_reader.obj" ^
  "%ODIR%\bc250_rebar_reader.obj" ^
  "%ODIR%\bc250_vram_topology_contract.obj" ^
  "%ODIR%\bc250_vram_topology_reader.obj" ^
  "%ODIR%\bc250_discovery_tmr_reader.obj" ^
  "%ODIR%\bc250_mmio_topology_transport.obj" ^
  "%ODIR%\bc250_platform_register_snapshot.obj" ^
  "%ODIR%\bc250_post_display_reservation.obj" ^
  "%ODIR%\bc250_vram_probe_transaction.obj" ^
  "%ODIR%\bc250_dxgk_readonly_ddi.obj" ^
  "%ODIR%\bc250_probe_telemetry.obj" ^
  "%ODIR%\dxgk_readonly_ddi_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\dxgk_readonly_ddi_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(dxgk_readonly_ddi_test^)
  exit /b 1
)

echo [cc] pci_bar_reader_test.c
cl %CFLAGS% /Fo"%ODIR%\pci_bar_reader_test.obj" "%ROOTDIR%\driver\tests\pci_bar_reader_test.c"
if errorlevel 1 exit /b 1

echo [link] pci_bar_reader_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\pci_bar_reader_test.exe" ^
  "%ODIR%\bc250_pci_bar_reader.obj" ^
  "%ODIR%\pci_bar_reader_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\pci_bar_reader_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(pci_bar_reader_test^)
  exit /b 1
)

echo [cc] rebar_reader_test.c
cl %CFLAGS% /Fo"%ODIR%\rebar_reader_test.obj" "%ROOTDIR%\driver\tests\rebar_reader_test.c"
if errorlevel 1 exit /b 1

echo [link] rebar_reader_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\rebar_reader_test.exe" ^
  "%ODIR%\bc250_rebar_reader.obj" ^
  "%ODIR%\rebar_reader_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\rebar_reader_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(rebar_reader_test^)
  exit /b 1
)

echo [cc] post_display_reservation_test.c
cl %CFLAGS% /Fo"%ODIR%\post_display_reservation_test.obj" "%ROOTDIR%\driver\tests\post_display_reservation_test.c"
if errorlevel 1 exit /b 1

echo [link] post_display_reservation_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\post_display_reservation_test.exe" ^
  "%ODIR%\bc250_post_display_reservation.obj" ^
  "%ODIR%\post_display_reservation_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\post_display_reservation_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(post_display_reservation_test^)
  exit /b 1
)

echo [cc] runtime_inventory_test.c
cl %CFLAGS% /Fo"%ODIR%\runtime_inventory_test.obj" "%ROOTDIR%\driver\tests\runtime_inventory_test.c"
if errorlevel 1 exit /b 1

echo [link] runtime_inventory_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\runtime_inventory_test.exe" ^
  "%ODIR%\bc250_resource_contract.obj" ^
  "%ODIR%\bc250_vram_allocator.obj" ^
  "%ODIR%\bc250_address_contract.obj" ^
  "%ODIR%\bc250_psp_contract.obj" ^
  "%ODIR%\bc250_compute_bootstrap.obj" ^
  "%ODIR%\bc250_staging_lease.obj" ^
  "%ODIR%\bc250_discovery_tmr_reader.obj" ^
  "%ODIR%\bc250_runtime_inventory.obj" ^
  "%ODIR%\runtime_inventory_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\runtime_inventory_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(runtime_inventory_test^)
  exit /b 1
)

echo [cc] gcvm_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\gcvm_contract_test.obj" "%ROOTDIR%\driver\tests\gcvm_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] gcvm_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\gcvm_contract_test.exe" ^
  "%ODIR%\bc250_vram_allocator.obj" ^
  "%ODIR%\bc250_gcvm_contract.obj" ^
  "%ODIR%\gcvm_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\gcvm_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(gcvm_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] sdma_packet_builder_test.c
cl %CFLAGS% /Fo"%ODIR%\sdma_packet_builder_test.obj" "%ROOTDIR%\driver\tests\sdma_packet_builder_test.c"
if errorlevel 1 exit /b 1

echo [link] sdma_packet_builder_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\sdma_packet_builder_test.exe" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\sdma_packet_builder_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\sdma_packet_builder_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(sdma_packet_builder_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] pm4_packet_builder_test.c
cl %CFLAGS% /Fo"%ODIR%\pm4_packet_builder_test.obj" "%ROOTDIR%\driver\tests\pm4_packet_builder_test.c"
if errorlevel 1 exit /b 1

echo [link] pm4_packet_builder_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\pm4_packet_builder_test.exe" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\pm4_packet_builder_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\pm4_packet_builder_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(pm4_packet_builder_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] golden_settings_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\golden_settings_contract_test.obj" "%ROOTDIR%\driver\tests\golden_settings_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] golden_settings_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\golden_settings_contract_test.exe" ^
  "%ODIR%\bc250_golden_settings_contract.obj" ^
  "%ODIR%\golden_settings_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\golden_settings_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(golden_settings_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] compute_mqd_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\compute_mqd_contract_test.obj" "%ROOTDIR%\driver\tests\compute_mqd_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] compute_mqd_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\compute_mqd_contract_test.exe" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_isa_validator.obj" ^
  "%ODIR%\bc250_compute_dispatch_contract.obj" ^
  "%ODIR%\bc250_observable_shader.obj" ^
  "%ODIR%\bc250_compute_mqd_contract.obj" ^
  "%ODIR%\compute_mqd_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\compute_mqd_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(compute_mqd_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] compute_dispatch_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\compute_dispatch_contract_test.obj" "%ROOTDIR%\driver\tests\compute_dispatch_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] compute_dispatch_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\compute_dispatch_contract_test.exe" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_isa_validator.obj" ^
  "%ODIR%\bc250_compute_dispatch_contract.obj" ^
  "%ODIR%\compute_dispatch_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\compute_dispatch_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(compute_dispatch_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] observable_shader_test.c
cl %CFLAGS% /Fo"%ODIR%\observable_shader_test.obj" "%ROOTDIR%\driver\tests\observable_shader_test.c"
if errorlevel 1 exit /b 1

echo [link] observable_shader_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\observable_shader_test.exe" ^
  "%ODIR%\bc250_observable_shader.obj" ^
  "%ODIR%\observable_shader_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\observable_shader_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(observable_shader_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] context_dispatcher_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\context_dispatcher_contract_test.obj" "%ROOTDIR%\driver\tests\context_dispatcher_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] context_dispatcher_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\context_dispatcher_contract_test.exe" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_context_dispatcher_contract.obj" ^
  "%ODIR%\context_dispatcher_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\context_dispatcher_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(context_dispatcher_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] ih_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\ih_contract_test.obj" "%ROOTDIR%\driver\tests\ih_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] ih_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\ih_contract_test.exe" ^
  "%ODIR%\bc250_ih_contract.obj" ^
  "%ODIR%\ih_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\ih_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(ih_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] paging_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\paging_contract_test.obj" "%ROOTDIR%\driver\tests\paging_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] paging_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\paging_contract_test.exe" ^
  "%ODIR%\bc250_gcvm_contract.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_paging_contract.obj" ^
  "%ODIR%\paging_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\paging_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(paging_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] gfxhub_tlb_transaction_test.c
cl %CFLAGS% /Fo"%ODIR%\gfxhub_tlb_transaction_test.obj" "%ROOTDIR%\driver\tests\gfxhub_tlb_transaction_test.c"
if errorlevel 1 exit /b 1

echo [link] gfxhub_tlb_transaction_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\gfxhub_tlb_transaction_test.exe" ^
  "%ODIR%\bc250_gcvm_contract.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_paging_contract.obj" ^
  "%ODIR%\bc250_gfxhub_tlb_transaction.obj" ^
  "%ODIR%\gfxhub_tlb_transaction_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\gfxhub_tlb_transaction_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(gfxhub_tlb_transaction_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] gcvm_vmid8_transaction_test.c
cl %CFLAGS% /Fo"%ODIR%\gcvm_vmid8_transaction_test.obj" "%ROOTDIR%\driver\tests\gcvm_vmid8_transaction_test.c"
if errorlevel 1 exit /b 1

echo [link] gcvm_vmid8_transaction_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\gcvm_vmid8_transaction_test.exe" ^
  "%ODIR%\bc250_gcvm_vmid8_transaction.obj" ^
  "%ODIR%\gcvm_vmid8_transaction_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\gcvm_vmid8_transaction_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(gcvm_vmid8_transaction_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] psp_ring_transaction_test.c
cl %CFLAGS% /Fo"%ODIR%\psp_ring_transaction_test.obj" "%ROOTDIR%\driver\tests\psp_ring_transaction_test.c"
if errorlevel 1 exit /b 1

echo [link] psp_ring_transaction_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\psp_ring_transaction_test.exe" ^
  "%ODIR%\bc250_psp_ring_transaction.obj" ^
  "%ODIR%\psp_ring_transaction_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\psp_ring_transaction_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(psp_ring_transaction_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] psp_load_fw_transaction_test.c
cl %CFLAGS% /Fo"%ODIR%\psp_load_fw_transaction_test.obj" "%ROOTDIR%\driver\tests\psp_load_fw_transaction_test.c"
if errorlevel 1 exit /b 1

echo [link] psp_load_fw_transaction_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\psp_load_fw_transaction_test.exe" ^
  "%ODIR%\bc250_psp_load_fw_transaction.obj" ^
  "%ODIR%\psp_load_fw_transaction_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\psp_load_fw_transaction_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(psp_load_fw_transaction_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] sdma_f32_transaction_test.c
cl %CFLAGS% /Fo"%ODIR%\sdma_f32_transaction_test.obj" "%ROOTDIR%\driver\tests\sdma_f32_transaction_test.c"
if errorlevel 1 exit /b 1

echo [link] sdma_f32_transaction_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\sdma_f32_transaction_test.exe" ^
  "%ODIR%\bc250_sdma_f32_transaction.obj" ^
  "%ODIR%\sdma_f32_transaction_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\sdma_f32_transaction_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(sdma_f32_transaction_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] adapter_info_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\adapter_info_contract_test.obj" "%ROOTDIR%\driver\tests\adapter_info_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] adapter_info_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\adapter_info_contract_test.exe" ^
  "%ODIR%\bc250_adapter_info_contract.obj" ^
  "%ODIR%\adapter_info_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\adapter_info_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(adapter_info_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] segment_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\segment_contract_test.obj" "%ROOTDIR%\driver\tests\segment_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] segment_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\segment_contract_test.exe" ^
  "%ODIR%\bc250_segment_contract.obj" ^
  "%ODIR%\segment_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\segment_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(segment_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] tdr_recovery_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\tdr_recovery_contract_test.obj" "%ROOTDIR%\driver\tests\tdr_recovery_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] tdr_recovery_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\tdr_recovery_contract_test.exe" ^
  "%ODIR%\bc250_tdr_recovery_contract.obj" ^
  "%ODIR%\tdr_recovery_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\tdr_recovery_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(tdr_recovery_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] mcdm_callbacks_test.c
cl %CFLAGS% /Fo"%ODIR%\mcdm_callbacks_test.obj" "%ROOTDIR%\driver\tests\mcdm_callbacks_test.c"
if errorlevel 1 exit /b 1

echo [link] mcdm_callbacks_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\mcdm_callbacks_test.exe" ^
  "%ODIR%\bc250_adapter_info_contract.obj" ^
  "%ODIR%\bc250_segment_contract.obj" ^
  "%ODIR%\bc250_tdr_recovery_contract.obj" ^
  "%ODIR%\bc250_context_dispatcher_contract.obj" ^
  "%ODIR%\bc250_paging_contract.obj" ^
  "%ODIR%\bc250_gfxhub_tlb_transaction.obj" ^
  "%ODIR%\bc250_ih_contract.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_isa_validator.obj" ^
  "%ODIR%\bc250_compute_mqd_contract.obj" ^
  "%ODIR%\bc250_compute_dispatch_contract.obj" ^
  "%ODIR%\bc250_observable_shader.obj" ^
  "%ODIR%\bc250_gcvm_contract.obj" ^
  "%ODIR%\bc250_submit_command_contract.obj" ^
  "%ODIR%\bc250_preemption_contract.obj" ^
  "%ODIR%\bc250_gpu_clock_contract.obj" ^
  "%ODIR%\bc250_power_contract.obj" ^
  "%ODIR%\bc250_mcdm_callbacks.obj" ^
  "%ODIR%\mcdm_callbacks_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\mcdm_callbacks_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(mcdm_callbacks_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] mcdm_object_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\mcdm_object_contract_test.obj" "%ROOTDIR%\driver\tests\mcdm_object_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] mcdm_object_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\mcdm_object_contract_test.exe" ^
  "%ODIR%\bc250_mcdm_object_contract.obj" ^
  "%ODIR%\mcdm_object_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\mcdm_object_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(mcdm_object_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] gpuva_sdma_broker_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\gpuva_sdma_broker_contract_test.obj" "%ROOTDIR%\driver\tests\gpuva_sdma_broker_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] gpuva_sdma_broker_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\gpuva_sdma_broker_contract_test.exe" ^
  "%ODIR%\bc250_mcdm_object_contract.obj" ^
  "%ODIR%\bc250_gpuva_sdma_broker_contract.obj" ^
  "%ODIR%\gpuva_sdma_broker_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\gpuva_sdma_broker_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(gpuva_sdma_broker_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] gpuva_sdma_executor_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\gpuva_sdma_executor_contract_test.obj" "%ROOTDIR%\driver\tests\gpuva_sdma_executor_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] gpuva_sdma_executor_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\gpuva_sdma_executor_contract_test.exe" ^
  "%ODIR%\bc250_mcdm_object_contract.obj" ^
  "%ODIR%\bc250_gpuva_sdma_broker_contract.obj" ^
  "%ODIR%\bc250_gpuva_sdma_executor_contract.obj" ^
  "%ODIR%\gpuva_sdma_executor_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\gpuva_sdma_executor_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(gpuva_sdma_executor_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] allocation_abi_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\allocation_abi_contract_test.obj" "%ROOTDIR%\driver\tests\allocation_abi_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] allocation_abi_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\allocation_abi_contract_test.exe" ^
  "%ODIR%\bc250_allocation_abi_contract.obj" ^
  "%ODIR%\allocation_abi_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\allocation_abi_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(allocation_abi_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] submit_command_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\submit_command_contract_test.obj" "%ROOTDIR%\driver\tests\submit_command_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] submit_command_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\submit_command_contract_test.exe" ^
  "%ODIR%\bc250_adapter_info_contract.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_submit_command_contract.obj" ^
  "%ODIR%\submit_command_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\submit_command_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(submit_command_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] preemption_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\preemption_contract_test.obj" "%ROOTDIR%\driver\tests\preemption_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] preemption_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\preemption_contract_test.exe" ^
  "%ODIR%\bc250_adapter_info_contract.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_preemption_contract.obj" ^
  "%ODIR%\preemption_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\preemption_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(preemption_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] gpu_clock_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\gpu_clock_contract_test.obj" "%ROOTDIR%\driver\tests\gpu_clock_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] gpu_clock_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\gpu_clock_contract_test.exe" ^
  "%ODIR%\bc250_adapter_info_contract.obj" ^
  "%ODIR%\bc250_gpu_clock_contract.obj" ^
  "%ODIR%\gpu_clock_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\gpu_clock_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(gpu_clock_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] power_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\power_contract_test.obj" "%ROOTDIR%\driver\tests\power_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] power_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\power_contract_test.exe" ^
  "%ODIR%\bc250_power_contract.obj" ^
  "%ODIR%\power_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\power_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(power_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] smu_telemetry_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\smu_telemetry_contract_test.obj" "%ROOTDIR%\driver\tests\smu_telemetry_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] smu_telemetry_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\smu_telemetry_contract_test.exe" ^
  "%ODIR%\bc250_smu_telemetry_contract.obj" ^
  "%ODIR%\smu_telemetry_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\smu_telemetry_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(smu_telemetry_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] superio_nct6686_contract_test.c
cl %CFLAGS% /Fo"%ODIR%\superio_nct6686_contract_test.obj" "%ROOTDIR%\driver\tests\superio_nct6686_contract_test.c"
if errorlevel 1 exit /b 1

echo [link] superio_nct6686_contract_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\superio_nct6686_contract_test.exe" ^
  "%ODIR%\bc250_superio_nct6686_contract.obj" ^
  "%ODIR%\superio_nct6686_contract_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\superio_nct6686_contract_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(superio_nct6686_contract_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] global_readiness_audit_test.c
cl %CFLAGS% /Fo"%ODIR%\global_readiness_audit_test.obj" "%ROOTDIR%\driver\tests\global_readiness_audit_test.c"
if errorlevel 1 exit /b 1

echo [link] global_readiness_audit_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\global_readiness_audit_test.exe" ^
  "%ODIR%\bc250_resource_contract.obj" ^
  "%ODIR%\bc250_vram_allocator.obj" ^
  "%ODIR%\bc250_address_contract.obj" ^
  "%ODIR%\bc250_psp_contract.obj" ^
  "%ODIR%\bc250_compute_bootstrap.obj" ^
  "%ODIR%\bc250_staging_lease.obj" ^
  "%ODIR%\bc250_firmware_staging.obj" ^
  "%ODIR%\bc250_runtime_inventory.obj" ^
  "%ODIR%\bc250_gcvm_contract.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_golden_settings_contract.obj" ^
  "%ODIR%\bc250_isa_validator.obj" ^
  "%ODIR%\bc250_compute_mqd_contract.obj" ^
  "%ODIR%\bc250_compute_dispatch_contract.obj" ^
  "%ODIR%\bc250_observable_shader.obj" ^
  "%ODIR%\bc250_context_dispatcher_contract.obj" ^
  "%ODIR%\bc250_ih_contract.obj" ^
  "%ODIR%\bc250_paging_contract.obj" ^
  "%ODIR%\bc250_gfxhub_tlb_transaction.obj" ^
  "%ODIR%\bc250_adapter_info_contract.obj" ^
  "%ODIR%\bc250_segment_contract.obj" ^
  "%ODIR%\bc250_tdr_recovery_contract.obj" ^
  "%ODIR%\bc250_submit_command_contract.obj" ^
  "%ODIR%\bc250_preemption_contract.obj" ^
  "%ODIR%\bc250_gpu_clock_contract.obj" ^
  "%ODIR%\bc250_power_contract.obj" ^
  "%ODIR%\bc250_vram_topology_contract.obj" ^
  "%ODIR%\bc250_vram_topology_reader.obj" ^
  "%ODIR%\bc250_discovery_tmr_reader.obj" ^
  "%ODIR%\bc250_mmio_topology_transport.obj" ^
  "%ODIR%\bc250_platform_register_snapshot.obj" ^
  "%ODIR%\global_readiness_audit_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\global_readiness_audit_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(global_readiness_audit_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] compute_umd_bridge_test.c
cl %CFLAGS% /Fo"%ODIR%\compute_umd_bridge_test.obj" "%ROOTDIR%\driver\tests\compute_umd_bridge_test.c"
if errorlevel 1 exit /b 1

echo [link] compute_umd_bridge_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\compute_umd_bridge_test.exe" ^
  "%ODIR%\bc250_adapter_info_contract.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_submit_command_contract.obj" ^
  "%ODIR%\bc250_isa_validator.obj" ^
  "%ODIR%\compute_umd_bridge_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\compute_umd_bridge_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(compute_umd_bridge_test^) - Linha/Codigo: %errorlevel%
  exit /b 1
)

echo [cc] readonly_miniport_test.c
cl %CFLAGS% /Fo"%ODIR%\readonly_miniport_test.obj" "%ROOTDIR%\driver\tests\readonly_miniport_test.c"
if errorlevel 1 exit /b 1

echo [link] readonly_miniport_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\readonly_miniport_test.exe" ^
  "%ODIR%\bc250_resource_contract.obj" ^
  "%ODIR%\bc250_vram_allocator.obj" ^
  "%ODIR%\bc250_address_contract.obj" ^
  "%ODIR%\bc250_psp_contract.obj" ^
  "%ODIR%\bc250_compute_bootstrap.obj" ^
  "%ODIR%\bc250_owned_adapter_contract.obj" ^
  "%ODIR%\bc250_cm_resource_adapter.obj" ^
  "%ODIR%\bc250_start_device_adapter.obj" ^
  "%ODIR%\bc250_cp_firmware_manifest.obj" ^
  "%ODIR%\bc250_pci_bar_reader.obj" ^
  "%ODIR%\bc250_rebar_reader.obj" ^
  "%ODIR%\bc250_vram_topology_contract.obj" ^
  "%ODIR%\bc250_vram_topology_reader.obj" ^
  "%ODIR%\bc250_discovery_tmr_reader.obj" ^
  "%ODIR%\bc250_mmio_topology_transport.obj" ^
  "%ODIR%\bc250_platform_register_snapshot.obj" ^
  "%ODIR%\bc250_post_display_reservation.obj" ^
  "%ODIR%\bc250_vram_probe_transaction.obj" ^
  "%ODIR%\bc250_dxgk_readonly_ddi.obj" ^
  "%ODIR%\bc250_adapter_info_contract.obj" ^
  "%ODIR%\bc250_segment_contract.obj" ^
  "%ODIR%\bc250_tdr_recovery_contract.obj" ^
  "%ODIR%\bc250_context_dispatcher_contract.obj" ^
  "%ODIR%\bc250_paging_contract.obj" ^
  "%ODIR%\bc250_gfxhub_tlb_transaction.obj" ^
  "%ODIR%\bc250_ih_contract.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_gcvm_contract.obj" ^
  "%ODIR%\bc250_compute_mqd_contract.obj" ^
  "%ODIR%\bc250_compute_dispatch_contract.obj" ^
  "%ODIR%\bc250_observable_shader.obj" ^
  "%ODIR%\bc250_submit_command_contract.obj" ^
  "%ODIR%\bc250_preemption_contract.obj" ^
  "%ODIR%\bc250_gpu_clock_contract.obj" ^
  "%ODIR%\bc250_power_contract.obj" ^
  "%ODIR%\bc250_isa_validator.obj" ^
  "%ODIR%\bc250_mcdm_callbacks.obj" ^
  "%ODIR%\bc250_probe_telemetry.obj" ^
  "%ODIR%\bc250_readonly_miniport.obj" ^
  "%ODIR%\readonly_miniport_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\readonly_miniport_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(readonly_miniport_test^)
  exit /b 1
)

echo [link] bc250readonly.sys
link /NOLOGO /Brepro /NODEFAULTLIB /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ^
  /OUT:"%ODIR%\bc250readonly.sys" ^
  "%ODIR%\bc250_resource_contract.obj" ^
  "%ODIR%\bc250_vram_allocator.obj" ^
  "%ODIR%\bc250_address_contract.obj" ^
  "%ODIR%\bc250_psp_contract.obj" ^
  "%ODIR%\bc250_compute_bootstrap.obj" ^
  "%ODIR%\bc250_staging_lease.obj" ^
  "%ODIR%\bc250_runtime_inventory.obj" ^
  "%ODIR%\bc250_gcvm_contract.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_golden_settings_contract.obj" ^
  "%ODIR%\bc250_compute_mqd_contract.obj" ^
  "%ODIR%\bc250_compute_dispatch_contract.obj" ^
  "%ODIR%\bc250_observable_shader.obj" ^
  "%ODIR%\bc250_context_dispatcher_contract.obj" ^
  "%ODIR%\bc250_ih_contract.obj" ^
  "%ODIR%\bc250_paging_contract.obj" ^
  "%ODIR%\bc250_gfxhub_tlb_transaction.obj" ^
  "%ODIR%\bc250_adapter_info_contract.obj" ^
  "%ODIR%\bc250_segment_contract.obj" ^
  "%ODIR%\bc250_tdr_recovery_contract.obj" ^
  "%ODIR%\bc250_submit_command_contract.obj" ^
  "%ODIR%\bc250_preemption_contract.obj" ^
  "%ODIR%\bc250_gpu_clock_contract.obj" ^
  "%ODIR%\bc250_power_contract.obj" ^
  "%ODIR%\bc250_isa_validator.obj" ^
  "%ODIR%\bc250_owned_adapter_contract.obj" ^
  "%ODIR%\bc250_cm_resource_adapter.obj" ^
  "%ODIR%\bc250_start_device_adapter.obj" ^
  "%ODIR%\bc250_cp_firmware_manifest.obj" ^
  "%ODIR%\bc250_pci_bar_reader.obj" ^
  "%ODIR%\bc250_rebar_reader.obj" ^
  "%ODIR%\bc250_vram_topology_contract.obj" ^
  "%ODIR%\bc250_vram_topology_reader.obj" ^
  "%ODIR%\bc250_discovery_tmr_reader.obj" ^
  "%ODIR%\bc250_mmio_topology_transport.obj" ^
  "%ODIR%\bc250_platform_register_snapshot.obj" ^
  "%ODIR%\bc250_dxgk_readonly_ddi.obj" ^
  "%ODIR%\bc250_mcdm_callbacks.obj" ^
  "%ODIR%\bc250_probe_telemetry_driver.obj" ^
  "%ODIR%\bc250_readonly_miniport_driver.obj" ^
  "%WDK%\Lib\%WDKVER%\km\x64\ntoskrnl.lib" ^
  "%WDK%\Lib\%WDKVER%\km\x64\hal.lib" ^
  "%WDK%\Lib\%WDKVER%\km\x64\BufferOverflowK.lib" ^
  "%WDK%\Lib\%WDKVER%\km\x64\displib.lib"
if errorlevel 1 exit /b 1

echo [cc] active_miniport_test.c
cl %CFLAGS% /Fo"%ODIR%\active_miniport_test.obj" "%ROOTDIR%\driver\tests\active_miniport_test.c"
if errorlevel 1 exit /b 1

echo [link] active_miniport_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\active_miniport_test.exe" ^
  "%ODIR%\bc250_mcdm_object_contract.obj" ^
  "%ODIR%\bc250_gpuva_sdma_broker_contract.obj" ^
  "%ODIR%\bc250_allocation_abi_contract.obj" ^
  "%ODIR%\bc250_resource_contract.obj" ^
  "%ODIR%\bc250_vram_allocator.obj" ^
  "%ODIR%\bc250_address_contract.obj" ^
  "%ODIR%\bc250_psp_contract.obj" ^
  "%ODIR%\bc250_compute_bootstrap.obj" ^
  "%ODIR%\bc250_runtime_inventory.obj" ^
  "%ODIR%\bc250_owned_adapter_contract.obj" ^
  "%ODIR%\bc250_cm_resource_adapter.obj" ^
  "%ODIR%\bc250_start_device_adapter.obj" ^
  "%ODIR%\bc250_cp_firmware_manifest.obj" ^
  "%ODIR%\bc250_pci_bar_reader.obj" ^
  "%ODIR%\bc250_rebar_reader.obj" ^
  "%ODIR%\bc250_vram_topology_contract.obj" ^
  "%ODIR%\bc250_vram_topology_reader.obj" ^
  "%ODIR%\bc250_discovery_tmr_reader.obj" ^
  "%ODIR%\bc250_mmio_topology_transport.obj" ^
  "%ODIR%\bc250_platform_register_snapshot.obj" ^
  "%ODIR%\bc250_post_display_reservation.obj" ^
  "%ODIR%\bc250_vram_probe_transaction.obj" ^
  "%ODIR%\bc250_gcvm_backing_transaction.obj" ^
  "%ODIR%\bc250_dxgk_readonly_ddi.obj" ^
  "%ODIR%\bc250_adapter_info_contract.obj" ^
  "%ODIR%\bc250_segment_contract.obj" ^
  "%ODIR%\bc250_tdr_recovery_contract.obj" ^
  "%ODIR%\bc250_context_dispatcher_contract.obj" ^
  "%ODIR%\bc250_paging_contract.obj" ^
  "%ODIR%\bc250_ih_contract.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_compute_mqd_contract.obj" ^
  "%ODIR%\bc250_compute_dispatch_contract.obj" ^
  "%ODIR%\bc250_observable_shader.obj" ^
  "%ODIR%\bc250_gcvm_contract.obj" ^
  "%ODIR%\bc250_submit_command_contract.obj" ^
  "%ODIR%\bc250_preemption_contract.obj" ^
  "%ODIR%\bc250_gpu_clock_contract.obj" ^
  "%ODIR%\bc250_power_contract.obj" ^
  "%ODIR%\bc250_isa_validator.obj" ^
  "%ODIR%\bc250_mcdm_callbacks.obj" ^
  "%ODIR%\bc250_display_ddi.obj" ^
  "%ODIR%\bc250_probe_telemetry.obj" ^
  "%ODIR%\bc250_gfxhub_tlb_transaction.obj" ^
  "%ODIR%\bc250_gcvm_vmid8_transaction.obj" ^
  "%ODIR%\bc250_psp_ring_transaction.obj" ^
  "%ODIR%\bc250_psp_load_fw_transaction.obj" ^
  "%ODIR%\bc250_sdma_f32_transaction.obj" ^
  "%ODIR%\bc250_sdma_ring_work.obj" ^
  "%ODIR%\bc250_active_miniport.obj" ^
  "%ODIR%\active_miniport_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\active_miniport_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(active_miniport_test^)
  exit /b 1
)

echo [cc] display_vidpn_test.c
cl %CFLAGS% /Fo"%ODIR%\display_vidpn_test.obj" "%ROOTDIR%\driver\tests\display_vidpn_test.c"
if errorlevel 1 exit /b 1

echo [link] display_vidpn_test.exe
link /NOLOGO /Brepro /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup ^
  /OUT:"%ODIR%\display_vidpn_test.exe" ^
  "%ODIR%\bc250_display_ddi.obj" ^
  "%ODIR%\display_vidpn_test.obj" kernel32.lib
if errorlevel 1 exit /b 1

"%ODIR%\display_vidpn_test.exe"
if errorlevel 1 (
  echo TESTE FALHOU ^(display_vidpn_test^)
  exit /b 1
)

echo [link] bc250mcdm.sys
link /NOLOGO /Brepro /NODEFAULTLIB /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ^
  /OUT:"%ODIR%\bc250mcdm.sys" ^
  "%ODIR%\bc250_mcdm_object_contract.obj" ^
  "%ODIR%\bc250_gpuva_sdma_broker_contract.obj" ^
  "%ODIR%\bc250_allocation_abi_contract.obj" ^
  "%ODIR%\bc250_resource_contract.obj" ^
  "%ODIR%\bc250_vram_allocator.obj" ^
  "%ODIR%\bc250_address_contract.obj" ^
  "%ODIR%\bc250_psp_contract.obj" ^
  "%ODIR%\bc250_compute_bootstrap.obj" ^
  "%ODIR%\bc250_staging_lease.obj" ^
  "%ODIR%\bc250_firmware_staging.obj" ^
  "%ODIR%\bc250_runtime_inventory.obj" ^
  "%ODIR%\bc250_gcvm_contract.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_golden_settings_contract.obj" ^
  "%ODIR%\bc250_compute_mqd_contract.obj" ^
  "%ODIR%\bc250_compute_dispatch_contract.obj" ^
  "%ODIR%\bc250_observable_shader.obj" ^
  "%ODIR%\bc250_context_dispatcher_contract.obj" ^
  "%ODIR%\bc250_ih_contract.obj" ^
  "%ODIR%\bc250_paging_contract.obj" ^
  "%ODIR%\bc250_adapter_info_contract.obj" ^
  "%ODIR%\bc250_segment_contract.obj" ^
  "%ODIR%\bc250_tdr_recovery_contract.obj" ^
  "%ODIR%\bc250_submit_command_contract.obj" ^
  "%ODIR%\bc250_preemption_contract.obj" ^
  "%ODIR%\bc250_gpu_clock_contract.obj" ^
  "%ODIR%\bc250_power_contract.obj" ^
  "%ODIR%\bc250_isa_validator.obj" ^
  "%ODIR%\bc250_owned_adapter_contract.obj" ^
  "%ODIR%\bc250_cm_resource_adapter.obj" ^
  "%ODIR%\bc250_start_device_adapter.obj" ^
  "%ODIR%\bc250_cp_firmware_manifest.obj" ^
  "%ODIR%\bc250_pci_bar_reader.obj" ^
  "%ODIR%\bc250_rebar_reader.obj" ^
  "%ODIR%\bc250_vram_topology_contract.obj" ^
  "%ODIR%\bc250_vram_topology_reader.obj" ^
  "%ODIR%\bc250_discovery_tmr_reader.obj" ^
  "%ODIR%\bc250_mmio_topology_transport.obj" ^
  "%ODIR%\bc250_platform_register_snapshot.obj" ^
  "%ODIR%\bc250_post_display_reservation.obj" ^
  "%ODIR%\bc250_vram_probe_transaction.obj" ^
  "%ODIR%\bc250_dxgk_readonly_ddi.obj" ^
  "%ODIR%\bc250_mcdm_callbacks.obj" ^
  "%ODIR%\bc250_display_ddi_driver.obj" ^
  "%ODIR%\bc250_probe_telemetry_driver.obj" ^
  "%ODIR%\bc250_gcvm_backing_transaction.obj" ^
  "%ODIR%\bc250_gfxhub_tlb_transaction.obj" ^
  "%ODIR%\bc250_gcvm_vmid8_transaction.obj" ^
  "%ODIR%\bc250_psp_ring_transaction.obj" ^
  "%ODIR%\bc250_psp_load_fw_transaction.obj" ^
  "%ODIR%\bc250_sdma_f32_transaction.obj" ^
  "%ODIR%\bc250_sdma_ring_work_driver.obj" ^
  "%ODIR%\bc250_pfp_direct_load_driver.obj" ^
  "%ODIR%\bc250_active_miniport_driver.obj" ^
  "%WDK%\Lib\%WDKVER%\km\x64\ntoskrnl.lib" ^
  "%WDK%\Lib\%WDKVER%\km\x64\hal.lib" ^
  "%WDK%\Lib\%WDKVER%\km\x64\BufferOverflowK.lib" ^
  "%WDK%\Lib\%WDKVER%\km\x64\displib.lib"
if errorlevel 1 exit /b 1

if exist "%ODIR%\bc250compute.sys" (
  del "%ODIR%\bc250compute.sys" >nul 2>&1
)

echo [link] bc250compute.sys
link /NOLOGO /Brepro /NODEFAULTLIB /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ^
  /OUT:"%ODIR%\bc250compute.sys" ^
  "%ODIR%\bc250_compute_service.obj" ^
  "%ODIR%\bc250_smu_telemetry_contract.obj" ^
  "%ODIR%\bc250_superio_nct6686_contract.obj" ^
  "%ODIR%\bc250_vram_allocator.obj" ^
  "%ODIR%\bc250_isa_validator.obj" ^
  "%ODIR%\bc250_sdma_packet_builder.obj" ^
  "%ODIR%\bc250_pm4_packet_builder.obj" ^
  "%ODIR%\bc250_gpu_clock_contract.obj" ^
  "%ODIR%\bc250_power_contract.obj" ^
  "%ODIR%\bc250_psp_load_fw_transaction.obj" ^
  "%ODIR%\bc250_psp_ring_transaction.obj" ^
  "%ODIR%\bc250_sdma_f32_transaction.obj" ^
  "%ODIR%\bc250_compute_mqd_contract.obj" ^
  "%ODIR%\bc250_compute_dispatch_contract.obj" ^
  "%ODIR%\bc250_observable_shader.obj" ^
  "%WDK%\Lib\%WDKVER%\km\x64\ntoskrnl.lib" ^
  "%WDK%\Lib\%WDKVER%\km\x64\hal.lib" ^
  "%WDK%\Lib\%WDKVER%\km\x64\BufferOverflowK.lib"
if errorlevel 1 exit /b 1

echo [cc] bc250-compute-cli.c
cl /nologo /W4 /O2 /Fo"%ODIR%\bc250-compute-cli.obj" /Fe"%ODIR%\bc250-compute-cli.exe" "%ROOTDIR%\tools\bc250-compute-cli.c" /link kernel32.lib advapi32.lib
if errorlevel 1 exit /b 1

echo [cc] bc250-gpuva-sdma-gate.c
cl /nologo /W4 /O2 /Fo"%ODIR%\bc250-gpuva-sdma-gate.obj" /Fe"%ODIR%\bc250-gpuva-sdma-gate.exe" "%ROOTDIR%\tools\bc250-gpuva-sdma-gate.c" /link kernel32.lib
if errorlevel 1 exit /b 1

rem bc250-benchmark.c e bc250_silicon_compute_benchmark.c foram REMOVIDOS em 2026-08-29.
rem Nao mediam nada: GFLOPS de um kernel que nunca foi despachado, topologia e banda como
rem strings fixas, e "Integridade: 100%% VERIFICADA" incondicional comparando contra uma
rem constante em vez do buffer lido. Ver STATUS_E_ROADMAP_CONSOLIDADO.md secao 8.1.
rem O UMD segue compilado: e' consumido pela suite de testes e e' a base da Fase 1.
echo [cc] bc250_runtime.c
cl /nologo /W4 /O2 /Fo"%ODIR%\bc250_runtime.obj" /c "%ROOTDIR%\driver\umd\bc250_runtime.c"
if errorlevel 1 exit /b 1

echo [cc] bc250-hardware-diag.c
cl /nologo /W4 /O2 /Fo"%ODIR%\bc250-hardware-diag.obj" /Fe"%ODIR%\bc250-hardware-diag.exe" "%ROOTDIR%\tools\bc250-hardware-diag.c" /link kernel32.lib advapi32.lib
if errorlevel 1 exit /b 1

python "%ROOTDIR%\tools\audit-mcdm-ddi-matrix.py"
if errorlevel 1 exit /b 1
python "%ROOTDIR%\tools\audit-bc250mcdm-safety.py"
if errorlevel 1 exit /b 1

echo WDK RESOURCE ADAPTER: OK ^(CM real types + variable full lists + fail-closed^)
echo VRAM TOPOLOGY CONTRACT: OK ^(RCC + GCMC + CPU window, offline^)
echo VRAM TOPOLOGY READER: OK ^(two stable readonly passes, offline^)
echo DISCOVERY TMR READER: OK ^(DRIVER_SCRATCH_0/1/2, two stable readonly passes^)
echo IP DISCOVERY CONTRACT: OK ^(GC 10.1.3 + checksums + bounds, offline^)
echo MMIO TOPOLOGY TRANSPORT: OK ^(PnP BAR bounds + map/unmap, fake callbacks^)
echo WDK STARTDEVICE ADAPTER: OK ^(ownership + readonly preflight + teardown^)
echo FIRMWARE STAGING CONTRACT: OK ^(sealed lease + SHA-256 + readback + restore, fake transport^)
echo RUNTIME VRAM INVENTORY: OK ^(scanout + discovery TMR + carveouts + driver allocations, offline^)
echo GCVM PAGE TABLE CONTRACT: OK ^(PTE encoding + 8 TB translation + GFXHUB invalidation, offline^)
echo SDMA PACKET BUILDER: OK ^(COPY + FENCE + CONST_FILL + wraparound, offline^)
echo PM4 PACKET BUILDER: OK ^(NOP + WRITE_DATA + SH/CTX/UCONFIG + RELEASE_MEM + wraparound, offline^)
echo GOLDEN SETTINGS CONTRACT: OK ^(34 entries + RMW + 2-pass preflight + LIFO rollback, offline^)
echo COMPUTE MQD CONTRACT: OK ^(GFX10 512B MQD + EOP buffer + HQD queue bounds, offline^)
echo CONTEXT DISPATCHER CONTRACT: OK ^(SDMA + Compute engine context dispatch + IB + EOP fences, offline^)
echo INTERRUPT HANDLER CONTRACT: OK ^(GFX10 16B IV vectors + SDMA/CP routing + modular RPTR, offline^)
echo PAGING/TLB CONTRACT: OK ^(GFXHUB ACK + owned VMID8 inert transaction validated in hardware^)
echo ADAPTER INFO CONTRACT: OK ^(2 nodes: SDMA0 + Compute MEC + 48-bit GPUVA, offline^)
echo SEGMENT CONTRACT: OK ^(Segment 1 Local VRAM + Segment 2 GART Aperture, offline^)
echo TDR RECOVERY CONTRACT: OK ^(Two-pass hang detection + Node 0/1 atomic reset + HQD dequeue, offline^)
echo SUBMIT COMMAND CONTRACT: OK ^(DxgkDdiSubmitCommand + Indirect Buffers + Release Mem EOP, offline^)
echo PREEMPTION CONTRACT: OK ^(DxgkDdiPreemptCommand + HQD dequeue + SDMA preemption fence, offline^)
echo GPU CLOCK CONTRACT: OK ^(DxgkDdiCalibrateGpuClock + 100MHz timestamp counter + QPC sync, offline^)
echo POWER MANAGEMENT CONTRACT: OK ^(DxgkDdiSetPowerState D0/D3 + SetStablePowerState + GFXOFF gating, offline^)
echo ISA VALIDATOR ^& COMPUTE UMD: OK ^(Wave32/64 + SGPR/VGPR/LDS bounds + IT_DISPATCH_DIRECT, offline^)
echo OBSERVABLE COMPUTE DISPATCH: OK ^(shader state + complete dispatch + post-dispatch fence + strict verdict, offline^)
echo OBSERVABLE GFX1010 SHADER: OK ^(allowlisted blob + SHA-256 provenance + negative mutation test, offline^)
echo CONTRACT INTEGRATION AUDIT: OK ^(offline structures only; not hardware readiness^)
echo MCDM DDI CALLBACK TABLE: OK ^(36 minimum + 5 GPUVA pointers; semantics fail-closed^)
echo MCDM OBJECT LIFECYCLE: OK ^(typed handles + allocation open/close + debug snapshot + Stop race, offline^)
echo DXGK READONLY DDI: OK ^(real signature + callback + fail-closed^)
echo PCI BAR READER: OK ^(DxgkCbReadDeviceSpace readonly, no sizing writes^)
echo REBAR READER: OK ^(supported + selected + assigned, readonly^)
echo POST-DISPLAY RESERVATION: OK ^(active StartDevice integration + fail-closed telemetry^)
echo READONLY MCDM IMAGE: OK ^(DriverEntry links DxgkInitialize via displib; StartDevice fail-closed^)
echo MCDM DEVELOPMENT IMAGE: OK ^(DriverEntry links DxgkInitialize; StartDevice fail-closed^)
echo KERNEL COMPUTE SERVICE: OK ^(DriverEntry links IoCreateDevice; Direct MMIO + SDMA + MEC0 IOCTLs^)
echo Zero driver, zero install, zero hardware.
echo Veredito: NAO VALIDADO EM HARDWARE.
endlocal
