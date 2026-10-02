# 517 — SHADER.1: a UMD passa a registrar a mensagem do vkd3d e a gravar o shader que falha; critério da Corrida 161

> 28/09/2026, offline. Base: a Corrida 160 (doc 516). Menu Iniciar: um PS SM 4.0 de 89 tokens falha na tradução (`status=3`), sem mensagem.

## 1. O que foi apurado offline (sem corrida)

**P1, código:** a mensagem do vkd3d era descartada.
- `Bc250Vkd3dCompile` chamava `vkd3d_shader_compile(&info, &output, NULL)` com `log_level = 0`.
- Proveniência de `log_level` (`NONE, ERROR, WARNING, INFO`), `vkd3d_shader_compile(info, out, char **messages)` e `vkd3d_shader_free_messages`: `include/vkd3d_shader.h` do espelho ValveSoftware/vkd3d. A DLL local (`vkd3d-shader 1.17`) exporta `vkd3d_shader_free_messages`.

**Hipótese descartada (P1, `d3d10umddi.h`):** "assinaturas lidas com o tamanho errado".
- A interface negociada é `0x000B000B` = `D3D11_0_7_DDI_INTERFACE_VERSION`.
- O `pfnCreatePixelShader` dela é o `PFND3D10DDI_CREATEPIXELSHADER`, com `D3D10DDIARG_SIGNATURE_ENTRY` de 12 bytes, igual à nossa estrutura. A de 20 bytes (`D3D11_1DDIARG_SIGNATURE_ENTRY`) é só da interface 11.1.

**Candidato encontrado e testado (P0 no teste, P2 na identificação):**
- a varredura dos contêineres DXBC em DLLs do sistema achou **um** PS SM 4.0 com 89 tokens, no `d2d1.dll` (offset 4.352.224, 656 bytes, com `ISG1`/`OSG1`/`SHEX`/`SFI0` e precisão mínima);
- o teste offline `repro_ps89` (adaptador, ponte e parser reais, e vkd3d 1.17 real) **traduz o candidato pelos dois caminhos**: o DXBC original (SPIR-V de 2.048 bytes) e o caminho TokenStream da UMD com as assinaturas do próprio shader (2.220 bytes);
- logo, ou o shader que falha é **outro** com o mesmo tamanho, ou o runtime entrega à UMD **assinaturas diferentes** (o WDK descreve a assinatura como a união do que os estágios ligados usam). Só o shader real decide.

## 2. Mudança (só UMD e binder; KMD igual ao da 3.308, DX.97)
1. **Adaptador vkd3d:**
   - carrega `vkd3d_shader_free_messages` (opcional);
   - pede `log_level = WARNING`;
   - guarda resultado e mensagens da última compilação **por thread** (`Bc250Vkd3dShaderAdapterLastMessages`);
   - quebras de linha viram ` | `, com truncamento marcado.
2. **UMD, na falha de tradução (DXBC ou TokenStream):**
   - registra `SHADER_TRANSLATOR: vkd3d resultado=<n> mensagens=[...]`;
   - grava **uma vez por shader distinto** o arquivo `%TEMP%\BC250_SHADER_FAIL_<pid>_<fnv32>.bin`: `'BCSF'`, versão, tipo, bytes, nº de ISGN/OSGN, código e as entradas `D3D10DDIARG_SIGNATURE_ENTRY` cruas;
   - o tamanho é limitado a 1 MiB, e há no máximo 16 shaders por processo.
3. **Binder:** no fim da janela, copia `BC250_UMD_LOG_*.txt` e `BC250_SHADER_FAIL_*.bin` de `C:\Users\*\AppData\Local\Packages\*\AC\Temp` (apps isolados) e do `%TEMP%` do usuário para `umd-logs-appcontainer\`, com o nome do pacote.

**Provas offline:**
- `build-umd` EXIT 0; as suítes exercitaram o caminho novo com o vkd3d real (`E0003: Invalid DXBC checksum.` num teste negativo e arquivo gravado);
- auditoria de segurança PASS; build de laboratório EXIT 0; auditoria do pacote PASS;
- o script do binder faz parse sem erro;
- conferido por string na UMD assinada: `SHADER_TRANSLATOR: vkd3d resultado=`, `BC250_SHADER_FAIL_` e `vkd3d_shader_free_messages`.

| Artefato (3.309.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `9EC1954C1A58FEF4CC5652AC1B4A2FD7B8A35318AE823CB4564F7573B45D71D0` |
| `bc250mcdm.sys` fonte lab | `D01FF49701888C9C90DB0F8D716E997C46E41603ED3C3235E793D3005251F5F3` |
| `bc250umd.dll` | `60FF14760BE31ACA0A958E3A002CCAFC8B374C9FF4FF7B9645E3914630EF6A49` |
| `bc250mcdm.inf` | `D4DBE94A521746C2F40048B07D15D7C54FC34B68AB5241A17F15445B0929E0D6` |
| `bc250mcdm.cat` | `ECCBFF3E656EC46CAD3DDF504EE8A97CC0294C135D9BB271D9CE9DAA0726BE3A` |

## 3. Critério da Corrida 161 (escrito antes)
- **Runner:** `tools\EXECUTAR_CORRIDA161_SHADER1_600S.cmd`.
- **O que o Daniel faz:** clicar no menu Iniciar algumas vezes durante a janela.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | `umd-logs-appcontainer\` com `BC250_SHADER_FAIL_*.bin` do StartMenuExperienceHost e a linha `vkd3d resultado=... mensagens=[...]` | Reproduzir offline com o arquivo e corrigir (assinatura, contêiner ou suporte no vkd3d); o menu é testado na corrida seguinte. |
| **B** | Mensagens presentes, mas sem arquivo | Gravação bloqueada no AppContainer; a mensagem já orienta, e o próximo passo é gravar noutro lugar. |
| **C** | O menu Iniciar não chamou a tradução nesta corrida | Repetir abrindo o menu várias vezes. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Observação: o KMD é o mesmo da 159/160. Esta corrida também soma uma amostra de estabilidade da DX.97 (travas: 159 sim, 160 não).
