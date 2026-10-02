# RE DO DWM — o que o dwm.exe/dwmcore.dll exige da nossa UMD (30/09/2026)

> ⚠️ **CORRIGIDO NA §9 (30/09, mesma data).** Duas conclusões deste doc estão **refutadas**: (1) a §4, "D3DReflect/D3DDisassemble inoperantes nesta máquina", era defeito do nosso harness (IID errado, assinatura errada e controle que não compilava); com as chamadas corretas, 206 de 206 contêineres desmontam e todos os programas e bibliotecas refletem. (2) A §3.3, "o nosso compilador precisa suportar library+linking", é refutada pelos nossos logs: o linking acontece dentro do dwm.exe e a UMD recebe SM 4.0 comum. A leitura do crash (§5) também é revista. §1, §6 e §7 se mantêm.

**Natureza:** 100% offline, zero corrida física, zero alteração de driver.
**Vocabulário:** P0 = medido nos bytes desta máquina · P1 = derivado de símbolo/API
pública · H = hipótese · Q = quarentena (nada aqui está em Q).

---

## 1. O que mudou de verdade: temos os símbolos do DWM desta máquina

O alvo da RE **não é `dwm.exe`**. `dwm.exe` tem 94.720 bytes e é um stub; toda a
lógica está em `dwmcore.dll` (3.577.344 bytes, `10.0.19041.1`).

O `dwmcore.pdb` de símbolos públicos **já estava em disco** e é o par exato do
binário instalado:

| Item | Valor medido |
|---|---|
| RSDS de `C:\Windows\System32\dwmcore.dll` | `6842D891-601D-1F5D-0A763346B57345841` idade **1** |
| Caminho do símbolo em disco | `C:\ProgramData\dbg\sym\dwmcore.pdb\6842D891601D1F5D0A763346B57345841\dwmcore.pdb` |
| cdb | carrega como **`pdb symbols`** |

Ou seja: os nomes de função abaixo são do **compilador que gerou o compositor que
está rodando nesta máquina**. Não é aproximação, não é fonte vazada — é o
símbolo público correspondente ao binário (P0).

O servidor de símbolos da Microsoft responde (HTTP 200 em
`https://msdl.microsoft.com/download/symbols/`), então o mesmo caminho serve
para qualquer outro binário de sistema que precisarmos.

## 2. Como isso foi extraído (reproduzível)

Ferramentas novas em `tools/dwm_re/`:

- `pe_census.py` — parser PE próprio (imports, export tables, diretório CodeView,
  inventário DXBC). Rodar: `python tools/dwm_re/pe_census.py imports|dxbc|debug <imagem>`.
- `shader_reflect.py` — enumera e reflete blobs DXBC via `d3dcompiler_47.dll`.
- `control_reflect.py` — **experimento de controle** do refletor (ver §4).
- `pdb_publics.py` — leitor MSF de PDB. **NÃO usado no resultado final**: o
  `cdb.exe` do WDK faz isso melhor e de graça. Mantido porque a arqueologia do
  formato está documentada nas asserções dos comentários e serve de segunda via.

O caminho vencedor foi `cdb.exe` (`Windows Kits\10\Debuggers\x64`):

```
cdb -z <dwm.dmp> -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;C:\ProgramData\dbg\sym" ^
    -c ".reload /f dwmcore.dll; x /n dwmcore!*; q"
```

**17.990 linhas de símbolo público, 15.974 nomes distintos, 692 classes distintas.**
Arquivado em `docs/evidence/dwm-re-2026-09-30/`.

## 3. O mapa do DWM: o que ele pede ao driver (P0/P1)

### 3.1 Device e swapchain

O Barrier documentado em `AGENTS.md` (`CLegacyRenderTarget::EnsureSwapChain` →
`CD3DDevice::CreateD3D11Device`) está **confirmado por símbolo**, e a família é
maior do que se supunha:

```
CD3DDevice            CD3DResource            CD3DTexture
CD3DSurface           CD3DVidMemOnlyTexture    CD3DConstantBuffer
CD3DBatchExecutionContext
CLegacyRenderTarget   CLegacySwapChain         CLegacyStereoSwapChain
CLegacyRemotingSwapChain
CDDisplayRenderTarget CDDisplaySwapChain       CDDASwapChain
CMagnifierRenderTarget CCaptureRenderTarget    CRemoteRenderTarget
CCompSwapChain        CConversionSwapChain     CIndirectSwapchainRenderTarget
```

`CD3DDevice::Create`, `CheckTextureFormatSupported`, `CalcMaxTextureSize`,
`CheckMPOCache`, `AddCheckMPOCache`, `CopySubresourceRegion`, `CopyTexture2D`,
`CheckOcclusionState`, `CheckForLeakedDWMSwapChain`, `CalcHDRConvertUVTransform`.

**Leitura para a UMD (P1):** o DWM pede suporte a **MPO** (`CheckMPOCache`),
`CopySubresourceRegion`/`CopyTexture2D` (cópias de textura), formatos de textura
específicos, e **várias famílias de swapchain** ao mesmo tempo — não só
`CLegacySwapChain`. Suportar só a legacy não fecha o DWM (H, a testar).

### 3.2 Shaders: o caminho real é *shader linking*, não PSO pré-compilado

Este é o achado mais útil da sessão. `dwmcore.dll` importa de
`D3DCOMPILER_47.dll`: `D3DCreateLinker`, `D3DLoadModule`,
`D3DCreateFunctionLinkingGraph`. E os símbolos mostram por quê:

```
CShaderLinkingGraphBuilder   -> Initialize / AppendNode / CopyNode / Link /
                                CreateNodeFromOutputArgument / SwizzleNode /
                                GetFragmentsModuleNoRef / FulfillInputs
CLinkedShader                -> CreateResource
CRenderingTechnique          -> CreateShaderBodies / GetShaders / GetConstantBuffer /
                                AddShaderToCache / CollectStateFromFragment(s) /
                                ConfigureSampler / CreateResource / UpdateConstantBuffers /
                                HasBackdropInput / HasWindowBackdropInput /
                                IsExternallyImplementedSubgraph
```

E, decisivo, um símbolo **vaza o tipo do container de shaders do DWM**:

```
CRenderingTechnique::CRenderingTechnique (dwmcore!CMap<
    enum CompositingShaderLookupKey,
    wil::com_ptr_t<ID3D11PixelShader, wil::err_returncode_policy>,
    CMapEqualHelper<...> > >::Add?...)
```

Ou seja: **o DWM mantém um mapa `CompositingShaderLookupKey → ID3D11PixelShader`**
— os shaders de composição são **pixel shaders** compilados e indexados por uma enum.
`CRenderingTechnique::CreateShaderBodies` mostra que o corpo do shader é montado
em runtime, não lido de um `.cso`.

### 3.3 Onde os shaders estão de fato (P0, medido)

Não existe HLSL no binário: `cbuffer`=0, `compile `=0, `vs_5_0`=0, `ps_5_0`=0,
`.hlsl`=0. E `SHEX` aparece **2 vezes** contra `SHDR` 278.

Inventário por varredura do `dwmcore.dll` (P0):

| Medido | Valor |
|---|---|
| Contêineres DXBC válidos | **287** |
| Folhas `SHDR`/`SHEX` | **280** (278 `SHDR` + 2 `SHEX`) |
| Objetos total | 1685 |
| Bibliotecas `LIBF` aninhadas | 249 |

O layout do contêiner foi **provado**, não suposto: eu escrevi o parser assumindo
`(fourcc, size)` por chunk e ele **reprovou**. O layout real é:

```
magic 'DXBC'[4] | digest[16] | version u32 | total_size u32 | count u32 |
offsets[count] u32 | objetos...          (objetos começam em 32 + 4*count)
```

Prova: um blob gerado pelo **próprio `D3DCompile`** desta máquina produz
`count=5`, `totalSize=424 == len(blob)`, `offsets[0] = 0x34 = 32+4*5`, chunks
`RDEF/ISGN/OSGN/SHDR/STAT` — exatamente o que o parser exige.

Identificadores legíveis recovered dentro das bibliotecas, que dizem o que o DWM
compõe: `LightingEffectLib`, `InitLightAccumulator`, `lightingResult`,
`unPremultipliedColor`, `RadialGradientCentered_UV`, `SampleTexture0..3`,
`GetCubeTexcoord`, `DecodeTexcoord3`, `FlattenTexcoords`, `MultiSampleTexture0..3`,
`GetSamplerData0..3`, `bitmapColor`, `maskColor`, `surfaceNormalTS`.

**Conclusão (P1):** o DWM 19041 usa o *linker* de HLSL (`D3DCreateLinker`) para
montar os efeitos de composição em runtime a partir de bibliotecas `LIBF`
embutidas. Isso significa que **o nosso compilador de shader precisa suportar o
caminho de library+linking**, ou o `Link()` nunca devolve bytecode utilizável.

## 4. **D3DReflect/D3DDisassemble estão INOPERANTES nesta máquina (P0, com controle)**

Este foi um falso negativo quaseFatal, e o experimento de controle é o que o
pegou.

Medido:
1. `D3DReflect` devolve `0x80070057` (`E_INVALIDARG`) para **todos** os 280
   shaders do `dwmcore.dll`.
2. Suspeitei dos meus dados. Compilei um `ps_4_0` de controle com `D3DCompile`
   (que **funciona**, `hr=0`) e o refleți: **também `0x80070057`**.
3. Suspeitei do FFI (ctypes). Escrevi um programa **em C puro** (`cl.exe`,
   `reflect_probe.cpp`): **mesmo `0x80070057`**, e `D3DDisassemble` dá `0x80004005`.
4.-reflection de um contêiner **DXIL** (gerado pelo `dxc` da VulkanSDK, SM 6.0)
   também falha.

Ou seja: **o problema não é o meu código, não é o container e não é DXBC.** As
duas rotinas de reflexão do `d3dcompiler_47.dll` desta máquina recusam qualquer
entrada. Consequência honesta: **não temos disassembly nem reflexão oficial dos
280 shaders.** O que temos é o inventário de bytes, os identificadores legíveis e
o mapa de símbolos — que é onde está a informação de driver.

Caminho restante, se for necessário o binding table com precisão de registro: parser
próprio dos chunks `RDEF`/`SHEX`/`ISG1`. O formato é documentado e o container já
está provado. Não foi feito nesta sessão. **Não VALIDADO EM HARDWARE** — e note que
isso não é uma medida de silício, é análise estática: o vocabulário correto aqui é
"não medido", não "não validado em hardware".

## 5. A pilha do crash de 22/09 com nomes reais (P0)

Dump `docs/evidence/2026-09-22_08-39-11-.../dwm-crash-01-dwm.exe.11040.dmp`
(AV `0xC0000005`), Threads all:

```
dwmcore!CWindowNode::operator new+0xe
dwmcore!CResourceFactory::Create+0x484
dwmcore!CResourceTable::CreateEmptyResource+0xa4
dwmcore!CComposition::Channel_CreateResource+0x27
dwmcore!CComposition::ProcessMessage+0x85
dwmcore!CComposition::ProcessCommandBatch+0xf3
dwmcore!CGlobalComposition::ProcessDataOnChannelSameProcess+0xb9
dwmcore!CComposition::ProcessPartitionCommand+0x42
dwmcore!CKernelTransport::DispatchBatches+0x6c
dwmcore!CComposition::PreRender+0x23e
dwmcore!CPartitionVerticalBlankScheduler::ProcessFrame+0x55f
dwmcore!CPartitionVerticalBlankScheduler::ScheduleAndProcessFrame+0xb4
dwmcore!CConnection::RunCompositionThread+0x21d
```

Leitura (P0 sobre o que a pilha nomeia, H sobre a causa): o DWM morreu criando
um **recurso DirectComposition** recebido por canal — `Channel_CreateResource` →
`CResourceTable::CreateEmptyResource` → `CResourceFactory::Create` →
`operator new` de `CWindowNode`. Ou seja, **o caminho de superfície do DWM é
DirectComposition por canal**, coerente com `dwmcore.dll` importar `dcomp.dll` e
com os símbolos `CComposition`, `CGlobalComposition`, `CKernelTransport`,
`CPartitionVerticalBlankScheduler`.

Isto **confirma por símbolo** o que os docs 187/249/296 já indicavam por
indício: as falhas de "superfície/DWM" não são um `Present` mal formado, são a
**criação de recurso DComp**. Não é a causa do AV (o AV está em `operator new`,
isto é, é bem antes de qualquer chamada de driver), mas é o **roteiro** em que o
DWM trabalha.

## 6. Batching — e aqui o DX.77 está confirmado por terceiro

```
CBatchOptimizer        -> AddRenderingDrawListEntry / AddStateSettingDrawListEntry /
                          AppendEntryToLayer / AppendLayer / ContinueLayer /
                          ConsolidateAdjacentHomogeneousLayers /
                          GetNearestRenderingLayerUnderEmptyLayer /
                          MatchStateToHomogeneousLayer / PurgeStateSettingEntriesFromLayer /
                          DiscardEmptyLayers / FlushBottomLayer / Flush
CD3DBatchExecutionContext -> AppendGeometry / FillVertexAndIndexBuffers / SetBlendMode /
                          SetDrawMethod / SetLights / SetLocalToWorld /
                          SetRenderingEffectState / SetStateGroup /
                          SetNoOpLightingConstantBuffers / GetLightsMask / Flush
```

O `CBatchOptimizer::ConsolidateAdjacentHomogeneousLayers` é **o mesmo
algoritmo** que o `DX77_COALESCENCIA_MULTIDRAW_DWM` (docs/445) reproduziu por
observação. O nome do método é agora P1. Isso confirma que o alvo real não é
"cada draw", mas **camadas homogêneas de draw coalescidos** — que é o que o
backend precisa sustentar.

Também presentes: `CSharedFence`, `CSharedCircularQueue`, `CComputeScribbleRenderer`,
`WarpTextureState`, `CSpectreCallbackRenderer`, `CD2DCommandList`, `CBrushRenderingGraph`
(o DWM também usa **Direct2D** para brushes).

## 7. Importações estáticas: uma única entrada de D3D11 (P0)

`dwmcore.dll` importa de `d3d11.dll` **apenas `D3D11CreateDevice`**. Todo o resto
da API D3D11 (centenas de pontos de entrada) é resolvido por outro caminho — logo
`imports` estático **não** descreve o contrato, e um censo baseado só em IAT seria
falso por construção. importing: `dxgi.dll` → `CreateDXGIFactory1/2`;
`d2d1.dll` → 5 ordinals; `dcomp.dll` → 2 ordinals; `d3dkmt` →
`D3DKMTQueryAdapterInfo`.

## 8. O que fazer com isso — ordenado por retorno, sem gastar corrida

1. **Parser `RDEF`/`SHEX` dos 280 shaders** (offline). Fecha a lacuna da §4 e dá a
   tabela de bindings por registro — o que a UMD precisa para montar o PSO e o
   constant buffer. Barato: o container já está provado.
2. **Demontar `CompositingShaderLookupKey`** a partir de `dwmcore.dll`. É uma enum
   pequena e nomeada; seus valores dizem **quantos e quais shaders de composição**
   o DWM instancia. Alto valor por baixo custo.
3. **Censo dinâmico pela nossa própria UMD** — já temos 100% das DDIs passando por
   nós. Registrar, por chamada, *qual* PSO/shader/blend/topology o DWM pede. É
   medição, não RE, e é o que fecha contrato.
4. **Rastrear `CComposition::Channel_CreateResource`** com o `cdb` nos dois dumps
   grandes (205 MB e 320 MB) para achar o segundo crash com nome. Já pagou uma vez.

Nada acima exige subir versão de driver, empacotar candidato ou rodar a máquina.

---

### Correções de registro

- A primeira tentativa deparser DXBC assumiu chunk `(fourcc,size)` e **reprovou**.
  O layout real usa tabela de offsets. A hipótese foi substituída por medição, e o
  parser agora **exige** a identidade `offsets[0] == 32 + 4*count`, que o
  `D3DCompile` da própria máquina satisfaz.
- Dizer "0 de 280 shaders refletidos" sem o controle teria sido um **falso
  negativo presented como fato**. O controle (`ps_4_0` recém-compilado + C puro)
  provou que a falha é do ambiente, não nossa. Registrado em
  `tools/dwm_re/control_reflect.py` justamente para isso se repetir.
- `pdb_publics.py` falhou em três tentativas de layout MSF antes de o `cdb` resolver.
  Mantido no repositório com as asserções documentadas, **não** como caminho
  vigente.

---

## 9. Correções (30/09/2026, verificação independente)

### 9.1 A reflexão funciona: o defeito era do harness (refuta a §4)

Três defeitos no nosso código, todos em `tools/dwm_re/`:

1. **IID errado:** `D3DReflect` era chamada com `IID_ID3DBlob`. A função exige a interface de reflexão, `IID_ID3D11ShaderReflection` = `{8D536CA1-0CCA-4956-A837-786963755584}`. Daí o `0x80070057` em toda chamada.
2. **Assinatura errada do `D3DDisassemble`:** a documentada é `(pSrcData, SrcDataSize, Flags, szComments, ppDisassembly)`. O harness passava um `D3D_SHADER_BYTECODE` no lugar do ponteiro e as flags no lugar do tamanho. Daí o `0x80004005`.
3. **O controle não compilava:** HLSL com `g_cb.uvScale` (erro `X3004`) e entrypoint `main` para a função `PSMain`. A frase da §4, "D3DCompile funciona, `hr=0`", não se reproduz com o `control_reflect.py` que estava no repositório. O `reflect_probe.cpp` citado na §4 **não está** no repositório.

Além disso, `reflect_blob` tratava a saída do `D3DReflect` como texto; ela é uma interface COM.

**Corrigido:** `shader_reflect.py` (IIDs certos, `D3DReflectLibrary` para bibliotecas, assinatura certa, liberação das interfaces) e `control_reflect.py` (HLSL válido, `PSMain`, e **sai com erro** se qualquer passo falhar). Medido depois da correção (P0):

| Medida | Resultado |
|---|---|
| Controle `ps_4_0`, `ps_5_0`, `ps_4_0_level_9_1` | compila, reflete e desmonta (`hr=0` nos três); `CONTROLE PASS`, saída 0 |
| `shader_reflect.py` sobre `dwmcore.dll` | **280 de 280 desmontam** |
| Contêineres DXBC de nível superior | 206: **31 programas** (refletem por `D3DReflect`), **5 bibliotecas** `lib_4_0_ps_2_x`/`ps_2_0` (refletem por `D3DReflectLibrary`) e **170 contêineres de uma função só** (chunk `LFS0`) |

Os 170 contêineres `LFS0` são funções de biblioteca isoladas. Nenhuma API de reflexão aceita uma função solta, só a biblioteca inteira; todas desmontam. É o contrato da API, não defeito da máquina.

### 9.2 O driver não participa do linking (refuta a conclusão da §3.3)

- `D3DCreateLinker`/`D3DLoadModule` rodam **dentro do processo do dwm.exe**, no `d3dcompiler_47`. A UMD recebe o shader **já ligado**, pela DDI `CreatePixelShader`/`CreateVertexShader`.
- Medido no log do dwm.exe da Corrida 185 (`docs/evidence/2026-09-30_09-45-53-.../bc250_umd_log_1280-FINAL.txt`): `CreatePixelShader: ... SM=4.0 tokens=14`, **56 traduções SPIR-V e zero falhas** (P0).
- **Nenhum trabalho de linking é necessário no driver.** A recomendação da §3.3 está retirada.

### 9.3 O que o desmonte correto acrescenta (P0)

- Os perfis dos shaders embutidos são de **nível 9**: `vs_2_0`, `vs_2_x`, `ps_2_0`, `ps_2_x` (com chunk `Aon9`), bibliotecas `lib_4_0_ps_2_x`/`lib_4_0_ps_2_0`, e só **4 `ps_4_0`**.
- Exemplo de biblioteca: 19 funções de iluminação (`AmbientLightingLib0`, `DistantLightingLib0/1`, `PointLightingLib0/1`, `SpotLightingLib0/1`, `InitLightAccumulator`, `PostLightingLib`, …) sobre o `cbuffer LightConstants`.
- **Leitura:** o DWM liga essas bibliotecas em runtime para montar os efeitos de composição. Os shaders são pequenos e variados, o que casa com o que a nossa UMD vê.

### 9.4 Revisão da leitura do crash de 22/09 (§5)

- A §5 diz que o AV em `CWindowNode::operator new` é "bem antes de qualquer chamada de driver".
- Um AV **dentro do alocador** é a assinatura típica de **heap corrompido antes**, por qualquer código do mesmo processo. A nossa UMD roda dentro do dwm.exe, e as faixas T4.1 e T4.3 corrigiram dois use-after-free nela.
- A pilha nomeia **onde a corrupção foi detectada**, não quem a causou. Ela é compatível com corrupção vinda da UMD (H) e **não absolve o driver**.

### 9.5 Ressalva de método

- Símbolos públicos e o desmonte de blobs de shader com a ferramenta oficial são observação legítima.
- A §8.2 (desmontar o código do `dwmcore` para extrair a enum `CompositingShaderLookupKey`) é engenharia reversa de código, que a licença do Windows restringe, e é **desnecessária**: a UMD já vê cada shader, estado e draw que o DWM pede.
- **Caminho vigente:** a §8.3 (censo pela nossa UMD), que é a Frente R1 do `PLANO_DESKTOP_ACELERADO.md`. Os símbolos da §1 ficam como ferramenta para nomear pilhas de dump.

### 9.6 Lição registrada

Um resultado negativo quase virou fato por defeito do próprio instrumento, a mesma classe que o `CLAUDE.md` registra como "o gate congelava a mentira". O controle existia, mas **ele mesmo estava quebrado** e só imprimia. Controle tem de **reprovar com código de saída**, e o resultado negativo só vale depois de um controle positivo passar no mesmo harness.
