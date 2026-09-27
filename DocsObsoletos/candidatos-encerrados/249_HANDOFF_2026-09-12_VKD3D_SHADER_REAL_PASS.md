# Handoff 249 — DXBC → SPIR-V com vkd3d-shader real

**Data:** 2026-09-12  
**Escopo:** somente host/offline; zero bind, zero instalação e zero acesso ao hardware.  
**Veredito físico:** **NÃO VALIDADO EM HARDWARE**.

## Resultado

A fronteira criada no doc 248 carregou `libvkd3d-shader-1.dll` x64 real, identificou
`vkd3d-shader 1.17` e traduziu dois shaders produzidos pelo `fxc` do SDK 28000:

- VS 4.0: DXBC 624 bytes → SPIR-V 1480 bytes, estágio vertex preservado;
- PS 4.0: DXBC 452 bytes → SPIR-V 396 bytes, estágio pixel preservado.

Veredito do harness: `VKD3D SHADER REAL SMOKE: PASS`.

## Defeito descoberto e corrigido

O teste real inicialmente parou em `BC250_DXBC_SPIRV_INVALID_DXBC`. A causa era o
nosso parser: o segundo DWORD de `SHDR/SHEX` é o comprimento total do programa,
incluindo os DWORDs de versão e comprimento. O código e os testes sintéticos o
tratavam como número de opcodes depois do cabeçalho.

`bc250_dxbc_parser.c` agora exige comprimento mínimo 2, valida-o contra o tamanho
do chunk e publica `OpcodeTokenCount = ProgramTokenCount - 2`. O caminho de token
stream direto da UMD foi corrigido com a mesma semântica e o cálculo de bytes deixou
de somar dois DWORDs pela segunda vez. Os fixtures foram atualizados para impedir
regressão.

## Artefato externo ensaiado

Origem: pacote Fedora 44 `mingw64-vkd3d-1.17-2.fc44.noarch.rpm`.

- RPM SHA-256: `2327CB48BB8A44F5ECF162AC8574C66E0FEA5D3F3CC9352B2B39C83B9BF514E2`;
- `libvkd3d-shader-1.dll`: 1.468.340 bytes, SHA-256
  `098CCD8A08045BD1267424CB6F1E3467CDDAA15691A35F6D6A37468A871CA93C`;
- `libgcc_s_seh-1.dll`: SHA-256
  `864CC874F4EF5774F8EA3132CC1F6145CE6DBAA238A7ACB706CB4988ABBF6D31`;
- `libwinpthread-1.dll`: SHA-256
  `80EB2559EA2A3E0C165EA8A4813CD9BA80937B4BBE13A2318A7B3CBAF2DFAC43`.

Tudo permaneceu em `%LOCALAPPDATA%\Temp`; nenhuma DLL externa foi vendorizada ou
incluída no pacote do driver.

## Implementação e validação

- `driver/tests/shaders/vkd3d_smoke_vs.hlsl` e `vkd3d_smoke_ps.hlsl`;
- `driver/tests/vkd3d_shader_real_smoke.c`;
- `tools/test-vkd3d-shader-real.ps1`;
- `build-umd.bat`: PASS, incluindo teste mock e suíte UMD completa;
- `build-contract.bat`: PASS;
- auditoria de segurança: PASS;
- `git diff --check`: PASS.

## Próximo bloco

Integrar tradução ao objeto de shader da UMD ainda de forma offline e default-off:
armazenar o módulo SPIR-V com ownership explícito por shader e liberá-lo em
`DestroyShader`. Isso ainda não autoriza anunciar pipeline D3D: faltam descriptors,
pipeline gráfico RADV/ACO, render target e Draw real.
