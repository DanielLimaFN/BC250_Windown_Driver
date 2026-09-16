@echo off
setlocal enabledelayedexpansion

set ROOTDIR=%~dp0..\..
set ODIR=%ROOTDIR%\driver\umd\output
if not exist "%ODIR%" mkdir "%ODIR%"

set VCVARS=
for %%D in (C D E F G) do (
  for %%E in (BuildTools Community Professional Enterprise) do (
    if not defined VCVARS (
      if exist "%%D:\Program Files\Microsoft Visual Studio\18\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\18\%%E\VC\Auxiliary\Build\vcvars64.bat"
      if exist "%%D:\Program Files\Microsoft Visual Studio\2026\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2026\%%E\VC\Auxiliary\Build\vcvars64.bat"
    )
  )
)
if not defined VCVARS (
  echo ERRO: vcvars64.bat do VS2026 nao encontrado.
  exit /b 1
)
call "!VCVARS!" >nul
if !ERRORLEVEL! NEQ 0 exit /b 1
echo [toolchain] VS2026

echo [UMD CC] bc250_runtime.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_runtime.obj" /c "%ROOTDIR%\driver\umd\bc250_runtime.c"
if errorlevel 1 exit /b 1

echo [LLM CC] bc250_llm_kernels.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_llm_kernels.obj" /c "%ROOTDIR%\driver\umd\bc250_llm_kernels.c"
if errorlevel 1 exit /b 1

echo [UMD LINK] bc250_runtime.dll
link /nologo /DLL /OUT:"%ODIR%\bc250_runtime.dll" /IMPLIB:"%ODIR%\bc250_runtime.lib" "%ODIR%\bc250_runtime.obj" "%ODIR%\bc250_llm_kernels.obj" kernel32.lib advapi32.lib
if errorlevel 1 exit /b 1

echo [UMD PRIVATE CC] bc250_umd_private_contract.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_umd_private_contract.obj" /c "%ROOTDIR%\driver\core\bc250_umd_private_contract.c"
if %ERRORLEVEL% NEQ 0 exit /b 1

echo [ALLOCATION ABI CC] bc250_allocation_abi_contract.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_allocation_abi_contract.obj" /c "%ROOTDIR%\driver\core\bc250_allocation_abi_contract.c"
if %ERRORLEVEL% NEQ 0 exit /b 1

echo [CAPS PROBE CC] bc250_umd_caps_probe.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_umd_caps_probe.obj" /c "%ROOTDIR%\driver\umd\bc250_umd_caps_probe.c"
if %ERRORLEVEL% NEQ 0 exit /b 1

echo [DXBC PARSER CC] bc250_dxbc_parser.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_dxbc_parser.obj" /c "%ROOTDIR%\driver\core\bc250_dxbc_parser.c"
if %ERRORLEVEL% NEQ 0 exit /b 1

echo [DXBC-SPIRV BRIDGE CC] bc250_dxbc_spirv_bridge.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\core" /Fo"%ODIR%\bc250_dxbc_spirv_bridge.obj" /c "%ROOTDIR%\driver\core\bc250_dxbc_spirv_bridge.c"
if %ERRORLEVEL% NEQ 0 exit /b 1

echo [VKD3D-SHADER ADAPTER CC] bc250_vkd3d_shader_adapter.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /Fo"%ODIR%\bc250_vkd3d_shader_adapter.obj" /c "%ROOTDIR%\driver\umd\bc250_vkd3d_shader_adapter.c"
if %ERRORLEVEL% NEQ 0 exit /b 1

echo [PM4 PACKET BUILDER CC] bc250_pm4_packet_builder.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_pm4_packet_builder.obj" /c "%ROOTDIR%\driver\core\bc250_pm4_packet_builder.c"
if %ERRORLEVEL% NEQ 0 exit /b 1

echo [SDMA PACKET BUILDER CC] bc250_sdma_packet_builder.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_sdma_packet_builder.obj" /c "%ROOTDIR%\driver\core\bc250_sdma_packet_builder.c"
if %ERRORLEVEL% NEQ 0 exit /b 1

echo [VULKAN BRIDGE CC] bc250_umd_vulkan_bridge.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_umd_vulkan_bridge.obj" /c "%ROOTDIR%\driver\umd\bc250_umd_vulkan_bridge.c"
if %ERRORLEVEL% NEQ 0 exit /b 1

echo [D3D UMD CC] bc250_d3d_umd.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_d3d_umd.obj" /c "%ROOTDIR%\driver\umd\bc250_d3d_umd.c"
if errorlevel 1 exit /b 1

echo [WDDM WINSYS CC] bc250_wddm_winsys.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_wddm_winsys.obj" /c "%ROOTDIR%\driver\umd\bc250_wddm_winsys.c"
if errorlevel 1 exit /b 1

echo [D3D UMD LINK] bc250umd.dll
rem advapi32.lib: P7_D3D11_CAPS_PROBE le HKLM para saber se foi armado.
link /nologo /DLL /DEF:"%ROOTDIR%\driver\umd\bc250umd.def" /OUT:"%ODIR%\bc250umd.dll" /IMPLIB:"%ODIR%\bc250umd.lib" "%ODIR%\bc250_d3d_umd.obj" "%ODIR%\bc250_umd_vulkan_bridge.obj" "%ODIR%\bc250_dxbc_parser.obj" "%ODIR%\bc250_dxbc_spirv_bridge.obj" "%ODIR%\bc250_vkd3d_shader_adapter.obj" "%ODIR%\bc250_pm4_packet_builder.obj" "%ODIR%\bc250_sdma_packet_builder.obj" "%ODIR%\bc250_umd_caps_probe.obj" "%ODIR%\bc250_umd_private_contract.obj" "%ODIR%\bc250_allocation_abi_contract.obj" "%ODIR%\bc250_wddm_winsys.obj" kernel32.lib advapi32.lib
if errorlevel 1 exit /b 1

echo [WDDM WINSYS TEST CC] bc250_wddm_winsys_test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_wddm_winsys_test.obj" /c "%ROOTDIR%\driver\tests\bc250_wddm_winsys_test.c"
if errorlevel 1 exit /b 1

echo [WDDM WINSYS TEST LINK] bc250_wddm_winsys_test.exe
link /nologo /OUT:"%ODIR%\bc250_wddm_winsys_test.exe" "%ODIR%\bc250_wddm_winsys_test.obj" "%ODIR%\bc250_wddm_winsys.obj" "%ODIR%\bc250_allocation_abi_contract.obj" kernel32.lib
if errorlevel 1 exit /b 1

echo [D3D UMD TEST CC] bc250_d3d_umd_test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_d3d_umd_test.obj" /c "%ROOTDIR%\driver\tests\bc250_d3d_umd_test.c"
if errorlevel 1 exit /b 1

echo [D3D UMD TEST LINK] bc250_d3d_umd_test.exe
rem Linka o OBJETO, nao a import lib: as funcoes de decisao do
rem P7_D3D11_CAPS_PROBE sao internas e NAO devem ser exportadas pela DLL de
rem producao so' para o teste alcanca-las.
link /nologo /OUT:"%ODIR%\bc250_d3d_umd_test.exe" "%ODIR%\bc250_d3d_umd_test.obj" "%ODIR%\bc250_umd_vulkan_bridge.obj" "%ODIR%\bc250_dxbc_parser.obj" "%ODIR%\bc250_pm4_packet_builder.obj" "%ODIR%\bc250_sdma_packet_builder.obj" "%ODIR%\bc250_umd_caps_probe.obj" "%ODIR%\bc250_umd_private_contract.obj" "%ODIR%\bc250_allocation_abi_contract.obj" "%ODIR%\bc250_wddm_winsys.obj" "%ODIR%\bc250umd.lib" kernel32.lib advapi32.lib
if errorlevel 1 exit /b 1

echo [VKD3D SHADER ADAPTER TEST CC] vkd3d_shader_adapter_test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /Fo"%ODIR%\vkd3d_shader_adapter_test.obj" /c "%ROOTDIR%\driver\tests\vkd3d_shader_adapter_test.c"
if errorlevel 1 exit /b 1

echo [VKD3D MOCK PROVIDER CC] vkd3d_shader_mock_provider.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /Fo"%ODIR%\vkd3d_shader_mock_provider.obj" /c "%ROOTDIR%\driver\tests\vkd3d_shader_mock_provider.c"
if errorlevel 1 exit /b 1

echo [VKD3D MOCK PROVIDER LINK] bc250_vkd3d_mock.dll
link /nologo /DLL /OUT:"%ODIR%\bc250_vkd3d_mock.dll" "%ODIR%\vkd3d_shader_mock_provider.obj" kernel32.lib
if errorlevel 1 exit /b 1

echo [VKD3D SHADER ADAPTER TEST LINK] vkd3d_shader_adapter_test.exe
link /nologo /OUT:"%ODIR%\vkd3d_shader_adapter_test.exe" "%ODIR%\vkd3d_shader_adapter_test.obj" "%ODIR%\bc250_vkd3d_shader_adapter.obj" "%ODIR%\bc250_dxbc_spirv_bridge.obj" "%ODIR%\bc250_dxbc_parser.obj" kernel32.lib
if errorlevel 1 exit /b 1

echo [VKD3D REAL SMOKE CC] vkd3d_shader_real_smoke.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /Fo"%ODIR%\vkd3d_shader_real_smoke.obj" /c "%ROOTDIR%\driver\tests\vkd3d_shader_real_smoke.c"
if errorlevel 1 exit /b 1

echo [VKD3D REAL SMOKE LINK] vkd3d_shader_real_smoke.exe
link /nologo /OUT:"%ODIR%\vkd3d_shader_real_smoke.exe" "%ODIR%\vkd3d_shader_real_smoke.obj" "%ODIR%\bc250_vkd3d_shader_adapter.obj" "%ODIR%\bc250_dxbc_spirv_bridge.obj" "%ODIR%\bc250_dxbc_parser.obj" kernel32.lib
if errorlevel 1 exit /b 1

echo [NODE1 SUBMIT TEST CC] umd_node1_submit_contract_test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\umd_node1_submit_contract_test.obj" /c "%ROOTDIR%\driver\tests\umd_node1_submit_contract_test.c"
if errorlevel 1 exit /b 1

echo [NODE1 SUBMIT TEST LINK] umd_node1_submit_contract_test.exe
link /nologo /OUT:"%ODIR%\umd_node1_submit_contract_test.exe" "%ODIR%\umd_node1_submit_contract_test.obj" "%ODIR%\bc250_wddm_winsys.obj" "%ODIR%\bc250_allocation_abi_contract.obj" "%ODIR%\bc250umd.lib" kernel32.lib
if errorlevel 1 exit /b 1

echo [NODE1 TRIPOD TEST CC] umd_node1_tripod_contract_test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\core" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\umd_node1_tripod_contract_test.obj" /c "%ROOTDIR%\driver\tests\umd_node1_tripod_contract_test.c"
if errorlevel 1 exit /b 1

echo [NODE1 TRIPOD TEST LINK] umd_node1_tripod_contract_test.exe
link /nologo /OUT:"%ODIR%\umd_node1_tripod_contract_test.exe" "%ODIR%\umd_node1_tripod_contract_test.obj" "%ODIR%\bc250_wddm_winsys.obj" "%ODIR%\bc250_allocation_abi_contract.obj" "%ODIR%\bc250umd.lib" kernel32.lib
if errorlevel 1 exit /b 1

echo [NODE1 PHYSICAL HARNESS CC] bc250-umd-node1-tripod.c
cl /nologo /W4 /WX /O2 /MD /Fo"%ODIR%\bc250-umd-node1-tripod.obj" /c "%ROOTDIR%\tools\bc250-umd-node1-tripod.c"
if errorlevel 1 exit /b 1

echo [NODE1 PHYSICAL HARNESS LINK] bc250-umd-node1-tripod.exe
link /nologo /OUT:"%ODIR%\bc250-umd-node1-tripod.exe" "%ODIR%\bc250-umd-node1-tripod.obj" d3d10_1.lib dxgi.lib dxguid.lib kernel32.lib
if errorlevel 1 exit /b 1

echo [VK ICD CC] bc250_vulkan_icd.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250_vulkan_icd.obj" /c "%ROOTDIR%\driver\umd\bc250_vulkan_icd.c"
if errorlevel 1 exit /b 1

echo [VK ICD LINK] bc250_vulkan.dll
link /nologo /DLL /OUT:"%ODIR%\bc250_vulkan.dll" /IMPLIB:"%ODIR%\bc250_vulkan.lib" "%ODIR%\bc250_vulkan_icd.obj" "%ODIR%\bc250_runtime.lib" kernel32.lib
if errorlevel 1 exit /b 1

echo [UMD CONTRACT TEST CC] umd_runtime_contract_test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\umd_runtime_contract_test.obj" /c "%ROOTDIR%\driver\tests\umd_runtime_contract_test.c"
if errorlevel 1 exit /b 1

echo [UMD CONTRACT TEST LINK] umd_runtime_contract_test.exe
link /nologo /OUT:"%ODIR%\umd_runtime_contract_test.exe" "%ODIR%\umd_runtime_contract_test.obj" "%ODIR%\bc250_runtime.lib" kernel32.lib
if errorlevel 1 exit /b 1

echo [UMD TEST CC] bc250-umd-test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250-umd-test.obj" /c "%ROOTDIR%\tools\bc250-umd-test.c"
if errorlevel 1 exit /b 1

echo [UMD TEST LINK] bc250-umd-test.exe
link /nologo /OUT:"%ODIR%\bc250-umd-test.exe" "%ODIR%\bc250-umd-test.obj" "%ODIR%\bc250_runtime.lib" kernel32.lib
if errorlevel 1 exit /b 1

echo [VK TEST CC] bc250-vulkan-test.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250-vulkan-test.obj" /c "%ROOTDIR%\tools\bc250-vulkan-test.c"
if errorlevel 1 exit /b 1

echo [VK TEST LINK] bc250-vulkan-test.exe
link /nologo /OUT:"%ODIR%\bc250-vulkan-test.exe" "%ODIR%\bc250-vulkan-test.obj" "%ODIR%\bc250_vulkan.lib" kernel32.lib
if errorlevel 1 exit /b 1

echo [LLM BENCHMARK CC] bc250-llm-benchmark.c
cl /nologo /W4 /WX /O2 /MD /I "%ROOTDIR%\driver\umd" /I "%ROOTDIR%\driver\wdk" /I "%ROOTDIR%\inc" /Fo"%ODIR%\bc250-llm-benchmark.obj" /c "%ROOTDIR%\tools\bc250-llm-benchmark.c"
if errorlevel 1 exit /b 1

echo [LLM BENCHMARK LINK] bc250-llm-benchmark.exe
link /nologo /OUT:"%ODIR%\bc250-llm-benchmark.exe" "%ODIR%\bc250-llm-benchmark.obj" "%ODIR%\bc250_runtime.lib" kernel32.lib
if errorlevel 1 exit /b 1

copy /y "%ROOTDIR%\driver\umd\bc250_icd.json" "%ODIR%\bc250_icd.json" >nul

echo.
echo === EXECUTANDO TESTE DE CONTRATO UMD RUNTIME ===
"%ODIR%\umd_runtime_contract_test.exe"
if not "!ERRORLEVEL!"=="0" exit /b 1

echo.
echo === EXECUTANDO TESTE DE CONTRATO D3D UMD ===
"%ODIR%\bc250_d3d_umd_test.exe"
if not "!ERRORLEVEL!"=="0" exit /b 1

echo.
echo === EXECUTANDO TESTE DO ADAPTADOR VKD3D-SHADER ===
"%ODIR%\vkd3d_shader_adapter_test.exe"
if not "!ERRORLEVEL!"=="0" exit /b 1

echo.
echo === EXECUTANDO TESTE DO WINSYS WDDM ===
"%ODIR%\bc250_wddm_winsys_test.exe"
if not "!ERRORLEVEL!"=="0" exit /b 1

echo.
echo === EXECUTANDO TESTE DE SUBMISSAO NODE 1 DA UMD ===
"%ODIR%\umd_node1_submit_contract_test.exe"
if not "!ERRORLEVEL!"=="0" exit /b 1

echo.
echo === EXECUTANDO TESTE DO TRIPE NODE 1 DA UMD ===
"%ODIR%\umd_node1_tripod_contract_test.exe"
if not "!ERRORLEVEL!"=="0" exit /b 1

echo.
echo === EXECUTANDO AUDITORIA SEMANTICA NODE 1 ===
python "%ROOTDIR%\tools\audit-umd-node1-submission.py"
if not "!ERRORLEVEL!"=="0" exit /b 1

echo.
echo =======================================================================
echo D3D UMD:       %ODIR%\bc250umd.dll
echo UMD RUNTIME:   %ODIR%\bc250_runtime.dll
echo VULKAN ICD:    %ODIR%\bc250_vulkan.dll
echo ICD MANIFEST:  %ODIR%\bc250_icd.json
echo HARNESS D3D:   %ODIR%\bc250_d3d_umd_test.exe
echo HARNESS WINSYS:%ODIR%\bc250_wddm_winsys_test.exe
echo HARNESS NODE1: %ODIR%\umd_node1_submit_contract_test.exe
echo TRIPOD NODE1:  %ODIR%\umd_node1_tripod_contract_test.exe
echo PHYSICAL NODE1:%ODIR%\bc250-umd-node1-tripod.exe ^(NAO EXECUTADO PELO BUILD^)
echo HARNESS UMD:   %ODIR%\bc250-umd-test.exe
echo HARNESS VK:    %ODIR%\bc250-vulkan-test.exe
echo HARNESS LLM:   %ODIR%\bc250-llm-benchmark.exe
echo =======================================================================
