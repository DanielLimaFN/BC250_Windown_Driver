# Handoff 248 — Fundação DXBC → SPIR-V para o backend D3D

**Data:** 2026-09-12  
**Escopo:** desenvolvimento offline; zero bind e zero acesso ao hardware.  
**Veredito físico:** **NÃO VALIDADO EM HARDWARE**.

## Decisão confirmada

O Mesa/RADV local não contém um frontend DXBC. A inspeção do código oficial do
Wine vkd3d, revisão `84a77b438f0b7a45441db20d390a240a8580f3bc`, confirmou que
`vkd3d_shader_compile()` suporta a transformação
`VKD3D_SHADER_SOURCE_DXBC_TPF` → `VKD3D_SHADER_TARGET_SPIRV_BINARY`.

Não foi vendorizada nem carregada uma DLL nesta faixa. O projeto usa MSVC; o build
Windows do vkd3d é orientado a MinGW e a fronteira de ABI/licença permanece isolada.

## Implementado

- `driver/core/bc250_dxbc_spirv_bridge.h/.c`: valida o DXBC antes do tradutor;
  exige SPIR-V alinhado entre 20 bytes e 16 MiB; valida magic, versão 1.0–1.6,
  `bound != 0` e `schema == 0`; preserva o estágio e o ownership da saída.
- `driver/tests/dxbc_spirv_bridge_test.c`: cobre sucesso, liberação única, SPIR-V
  inválido e DXBC inválido.
- `driver/umd/bc250_vkd3d_shader_adapter.h/.c`: adaptador dinâmico MSVC que aceita
  somente caminho absoluto, restringe a busca da DLL ao diretório informado e ao
  System32, exige as três exports da API e copia a saída para memória própria da UMD.
- `driver/umd/bc250_vkd3d_shader_abi.h`: fonte única da ABI mínima usada pelo
  adaptador, com constantes nomeadas e asserts de tamanho/offset no build x64.
- `driver/tests/vkd3d_shader_mock_provider.c` e `vkd3d_shader_adapter_test.c`:
  provam carregamento, resolução das três exports, layout da chamada, cópia da saída,
  validação SPIR-V, ownership/liberação e estado zerado nas falhas. A DLL mock é
  artefato exclusivo de teste e o empacotador copia somente `bc250umd.dll`.
- A ponte é compilada e ligada à `bc250umd.dll`, mas ainda não é chamada pelas DDIs
  de shader. Portanto `BC250_D3D10_11_NATIVE_READY` permanece `0`.

## Validação

- `dxbc_spirv_bridge_test.exe`: PASS;
- `build-contract.bat`: PASS;
- `build-umd.bat`: PASS, incluindo toda a suíte D3D UMD/winsys/Node 1;
- `vkd3d_shader_adapter_test.exe`: PASS (`load, ABI, translate, ownership, fail-closed`);
- auditoria de segurança: PASS.

## Próximo bloco

Obter uma DLL `vkd3d-shader` x64 real e reproduzível, registrar identidade/hash e
validar a ABI mínima contra ela. Depois, um gate offline deverá compilar VS/PS DXBC conhecidos
para SPIR-V. Nenhum cap D3D será aberto antes de pipeline, descriptors, render target
e Draw estarem completos.
