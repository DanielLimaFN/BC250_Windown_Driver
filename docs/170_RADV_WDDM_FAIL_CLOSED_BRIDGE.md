# Ponte RADV/WDDM fail-closed

Data: 2026-09-06

## Resultado desta etapa

A arvore externa `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm`
foi compilada depois de remover caminhos que fabricavam sucesso. O artefato
`build\src\amd\vulkan\vulkan_radeon.dll` foi gerado, mas nao foi instalado nem
executado contra a BC-250.

Correcoes aplicadas no fork:

- falha de `CreateContextVirtual` nao reutiliza mais o handle de device como
  se fosse contexto;
- falha de `CreateAllocation2` nao fabrica handle de alocacao;
- falha de reserva/mapeamento nao fabrica GPUVA;
- falha de `Lock2` nao cria memoria host fingindo ser BO de GPU;
- fences monitoradas nao sao mais avancadas pela CPU;
- timeout ou falha de wait nao retorna sucesso;
- WSI do Windows nao anuncia dispositivo de software;
- o bypass de PM4 por escape foi removido;
- `D3DKMTSubmitCommand` propaga o NTSTATUS como `VK_ERROR_DEVICE_LOST`;
- a BC-250 usa contexto virtual no no 1 e nao pede hardware queue, pois o KMD
  atual nao publica `CreateHwQueue`/`SubmitCommandToHwQueue`;
- BOs da BC-250 usam `BC250_ALLOCATION_PRIVATE_V1`, a ABI aceita pelo KMD.

No KMD, a deteccao de primary deixou de usar o tamanho ou a primeira resource.
Somente o token de classe `BC250_ALLOCATION_TOKEN_CLASS_PRIMARY` pode assumir
ownership da primary de display. Assim, um BO Vulkan nao sequestra o caminho de
`Present`.

Auditorias:

- `python tools/audit-radv-wddm-safety.py --mesa-root <mesa>`: PASS;
- `python tools/audit-bc250mcdm-safety.py`: PASS;
- `driver/wdk/build-contract.bat`: PASS;
- `tools/audit-bc250mcdm-package.ps1`: PASS de integridade.

## Harness físico preparado

`tools/build-radv-compute-gate.bat` compila um shader SPIR-V e o programa
`driver/tests/output_radv_gate/bc250-radv-compute-gate.exe`. O programa:

1. exige exatamente o adaptador `1002:13FE`;
2. cria BO real host-visible/coherent;
3. grava poison `0x43DAFFFE` pela CPU;
4. submete um shader que grava `0xBC250C01`;
5. espera fence Vulkan real;
6. falha se o resultado continuar poison ou divergir.

O manifesto descreve o candidato, mas nao e usado como mecanismo de isolamento
na corrida elevada: o Vulkan Loader ignora `VK_DRIVER_FILES` nesse contexto.
O harness nao importa mais `vulkan-1.dll`; ele recebe o caminho absoluto da
DLL, usa `LoadLibraryEx`, confere o caminho retornado por `GetModuleFileName` e
entra pela ABI `vk_icdGetInstanceProcAddr`. Assim a identidade do ICD e provada
antes de `vkCreateInstance`, sem instalar a RADV no sistema.

O executor transacional e `tools/test-radv-wddm-compute-tripod.ps1`. Em
dry-run ele confere os artefatos e as duas auditorias sem tocar a GPU. A
corrida fisica exige o diretorio de evidencia de um binding cujo
`watchdog-state.json` ainda esteja em `ARMED`, o SYS instalado com o hash do
manifesto ativo, BC-250 exata em `bc250mcdm`/Codigo 0 e `P1EngineState=1`.
O ICD e carregado diretamente pelo processo filho e seu caminho efetivo entra
no veredito. Timeout mata esse processo e reprova a corrida; nao ha repeticao
automatica.

O fluxo integrado, ainda opt-in, e:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\bind-bc250mcdm-failclosed.ps1 `
  -Execute -IAcceptBlackScreenRisk -RunRadvComputeTripod `
  -WatchdogTimeoutSeconds 120
```

O watchdog continua armado mesmo que o compute passe. Ele so e desarmado pela
confirmacao visual explicita ja exigida pelo binding; sem imagem, ocorre o
rollback automatico.

## Gate e economia de corridas

Teste nomeado: `RADV_WDDM_COMPUTE_TRIPOD`.

Teto: **uma corrida física por build**. Abandono imediato se preflight, criação
de contexto, alocação, GPUVA ou submissão falhar. Não repetir o mesmo build.

PASS exige simultaneamente:

1. `P1LastRptrMmio == P1LastWptrMmioReadback` depois da submissão;
2. fence de ring gravada pela GPU, com poison substituído;
3. `V2TrackerPushedTotal` e `V2NotifiedTotal` avançando sem overflow/out-of-order;
4. `BC250 RADV COMPUTE BYTE PROOF: PASS` com `0xBC250C01`;
5. evidencia completa no subdiretorio
   `radv-compute-tripod-<timestamp>/` da evidencia do binding.

O teste ainda nao deve ser confundido com graphics, WSI ou DWM. Mesmo que passe,
ele valida somente compute Vulkan pela ponte WDDM.

RADV compute pela ponte WDDM: **NÃO VALIDADO EM HARDWARE**.

RADV graphics/WSI: **NÃO VALIDADO EM HARDWARE**.

Correcao da tela preta do DWM: **NÃO VALIDADO EM HARDWARE**.
