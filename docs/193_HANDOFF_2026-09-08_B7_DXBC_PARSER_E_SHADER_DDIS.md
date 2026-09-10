# 193 — Handoff 2026-09-08: Parser DXBC e Shader DDIs (Passo 9 de B7)

> **Tipo de evidência:** Testes de contrato offline (P1) em conformidade com as Leis 1 a 4.  
> **Status:** Parser DXBC determinístico, iterador de opcodes e DDIs de shader D3D10/11 implementados e validados nas suítes `build-contract.bat` e `build-umd.bat`.  
> **Gate de produção:** `BC250_D3D10_11_NATIVE_READY = 0` mantido estritamente (Lei 4: sem declaração falsa de prontidão no silício).

---

## 1. O que foi construído

### A. Parser e Validador DXBC (`driver/core/bc250_dxbc_parser.h` e `.c`)
- **Contêiner DXBC**: Validação de integridade do cabeçalho de 32 bytes (FourCC `DXBC` / `0x43425844`, hash MD5/SHA1 de 128 bits, versão de container, tamanho total e número de chunks).
- **Tabela de Chunks com bounds checking**:
  - Verificação de limites estritos da tabela de offsets e do tamanho de cada chunk relativo a `TotalSize` e `BufferSize`.
  - Suporte e ponteiros rápidos para chunks padrão: `RDEF` (Resource Definition), `ISGN` (Input Signature), `OSGN` (Output Signature), `SHDR` / `SHEX` (Shader Bytecode SM4/SM5) e `STAT` (Statistics).
- **Decodificação de Shader Bytecode**:
  - Extração de tipo de shader (`BC250_DXBC_SHADER_PIXEL`, `VERTEX`, `GEOMETRY`, `HULL`, `DOMAIN`, `COMPUTE`).
  - Extração de versão maior (SM4 / SM5) e menor.
  - Extração e validação do fluxo de opcodes (`OpcodeTokens`, `OpcodeTokenCount`).
- **Iterador de Opcodes (`Bc250DxbcNextInstruction`)**:
  - Decodificação de comprimento em dwords (bits 24..30 do primeiro token).
  - Suporte específico para `D3D10_SB_OPCODE_CUSTOMDATA` (opcode 116), cujo tamanho total reside no segundo DWORD.
  - Fail-closed determinístico contra streams truncados ou comprimento zero.
- **Portabilidade**: Compatível tanto com kernel mode (`ntddk.h` / WDK) quanto com user mode (`windows.h` / SDK).

### B. Suíte de Testes de Contrato DXBC (`driver/tests/dxbc_parser_test.c`)
- Contêiner sintético DXBC válido de 220 bytes contendo 5 chunks (`RDEF`, `ISGN`, `OSGN`, `SHDR`, `STAT`) e stream de 14 DWORDs (`MOV`, `ADD`, `CUSTOMDATA`, `RET`).
- Testes exaustivos de caminho positivo e negativo:
  - Argumentos nulos.
  - Buffer menor que o cabeçalho.
  - Magic corrompido.
  - Divergência de `TotalSize` vs `BufferSize`.
  - Contagem de chunks excedendo o teto (32).
  - Offset de chunk fora dos limites.
  - Carga do chunk extrapolando os limites do buffer.
  - Contagem de tokens declarada extrapolando o tamanho do chunk.
  - Malformação de opcodes no iterador (comprimento zero, comprimento além do buffer, custom data truncado).
- Executado e aprovado dentro de `build-contract.bat`.

### C. Integração no Build WDK (`driver/wdk/build-contract.bat`)
- Compilação de `bc250_dxbc_parser.c` com `%CFLAGS%`.
- Compilação e link de `dxbc_parser_test.exe`.
- Execução síncrona com fail-closed (`if %ERRORLEVEL% NEQ 0 exit /b 1`).
- Resultado: `DXBC PARSER CONTRACT: OK (fail-closed SM4/SM5 parser + opcode stream iterator, offline)`.

### D. Integração e Fiação das DDIs de Shader na UMD (`driver/umd/`)
- `driver/umd/bc250_d3d_umd.h`:
  - Definição da estrutura interna `BC250_UMD_SHADER`.
  - Declarações de protótipos para:
    - `Bc250Umd10CalcPrivateShaderSize`
    - `Bc250Umd10CreateVertexShader`
    - `Bc250Umd10CreatePixelShader`
    - `Bc250Umd10CreateGeometryShader`
    - `Bc250Umd10CalcPrivateGeometryShaderWithStreamOutput`
    - `Bc250Umd10CreateGeometryShaderWithStreamOutput`
    - `Bc250Umd10DestroyShader`
- `driver/umd/bc250_d3d_umd.c`:
  - Implementação completa com decodificação dual (tanto contêiner `DXBC` nativo quanto payload de token stream direto de `SHDR`/`SHEX`).
  - Telemetria de rastreamento detalhada por processo (`Bc250UmdTrace`).
  - População na tabela `D3D10_1DDI_DEVICEFUNCS` via `Bc250UmdPopulateDeviceFuncs10_1`.
  - `BC250_D3D10_11_NATIVE_READY` mantido em 0 (portão de produção sem bypass).
- `driver/umd/build-umd.bat`:
  - Compilação de `bc250_dxbc_parser.c` em modo MD.
  - Link de `bc250_dxbc_parser.obj` em `bc250umd.dll` e `bc250_d3d_umd_test.exe`.
- `driver/tests/bc250_d3d_umd_test.c`:
  - Bloco `TestShaderDDIs` testando a tabela de device populada, cálculo de tamanho, criação de Vertex e Pixel shader, fluxo direto de tokens e desmontagem.

---

## 2. Validação das Auditorias e Builds

1. **`build-contract.bat`**: EXIT = 0 (39 testes de contrato aprovados).
2. **`build-umd.bat`**: EXIT = 0 (testes UMD runtime, D3D UMD e WDDM winsys aprovados).
3. **`audit-bc250mcdm-safety.py`**: PASS.
4. **`audit-mcdm-ddi-matrix.py`**: PASS (18/18 MCDM prohibited, 98 anunciados WDDM).
5. **`audit-ddi-noop-callbacks.py`**: PASS.

---

## 3. Próximos Passos
1. **Passo 10 de B7**: Implementação das DDIs de criação e gerenciamento de recursos (`CreateResource`, `DestroyResource`, `CalcPrivateResourceSize`) conectadas às alocações do winsys WDDM unificado.
2. **Passo 11 de B7**: Gerenciamento de Render Target Views (`CreateRenderTargetView`, `CalcPrivateRenderTargetViewSize`) e Depth Stencil Views.
3. **Passo 12 de B7**: Mapeamento e emissão de pipeline de renderização no Node 0 / Node 1.
