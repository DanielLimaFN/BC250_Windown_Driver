# 539 — T4.2: OpenResource mapeia a superfície compartilhada no próprio processo, correção do diagnóstico da Corrida 181 e critério da Corrida 182

> 29/09/2026, offline. Candidato **3.327.1.0** (T4.2 sobre T4.1 e todas as anteriores). **Nada desta faixa foi validado em hardware.**
> Corrige o diagnóstico do [doc 538](538_HANDOFF_2026-09-29_CORRIDA181_T41_DIAGNOSTICO_E_PROXIMO_PASSO.md).

---

## 1. O diagnóstico do doc 538 não se sustenta

O doc 538 atribuiu a queda da sessão da Corrida 181 ao BYTE_PROOF de 6,6 s no Present #5. A evidência refuta:

1. **O Present #1 também levou 6,59 s e o DWM seguiu apresentando** os #2 a #5 (P0, `display_log_slice_corrida181.txt`).
2. **O log final do dwm.exe** (`final-elevado/BC250_UMD_LOG_1280.txt`, copiado elevado de `C:\Windows\Temp`) mostra `PRESENT_CB LEAVE hr=0` às 21:55:13.837 e o DWM trabalhando normalmente depois disso.
3. **O BYTE_PROOF de ~6,5 s nos quadros 1, 5 e 10 aparece em praticamente todas as corridas desde 25/09**, inclusive nas estáveis por 600 s (mineração de `docs/evidence/2026-09-2[5-9]*`). É uma constante das corridas boas, não a causa da queda.
4. O "bloqueio no PRESENT_CB" do doc 538 era artefato de coleta: o `bc250_umd_log_1280.txt` da pasta é um snapshot tirado com o #5 em andamento.

O defeito real de desempenho do BYTE_PROOF (laço `volatile` byte a byte pela BAR, [bc250_display_ddi.c:609](../driver/wdk/bc250_display_ddi.c)) continua existindo e fica como dívida declarada, **não tratada nesta faixa** para mover uma variável só.

## 2. O que o dwm.exe fez antes de sumir (P0)

| Hora | Evento |
|---|---|
| 21:55:13.841 | `OpenResource` de superfície 1024x832 criada pelo Explorer: `GpuVa=0x3F80000000000000` |
| 21:55:14.012 | `Read LOCK falhou: hr=0x80070057` (alocação `CpuVisible=0`) |
| 21:55:14.048 | `BRIDGE GPU BLT` com a VA lixo, recusado pelo KMD (`COMPOSITE_BLT RECUSADO (resolucao)`) |
| 21:55:14.051 | `Fallback 1024x832 (0xFF202020)` — **última linha do log** |

Depois: ~4 s sem submissão, `Visible=0`, recriação das primárias, FILL de 11 MB (fence 952) sem conclusão e trava dura. Não há evento 1000 de crash do dwm.exe. **A causa da morte do DWM segue NÃO IDENTIFICADA** (H: relacionada ao ramo do fallback, apenas por proximidade temporal).

## 3. Causa da VA lixo (P0 + P1)

- **P0:** o dwm.exe ofereceu `0x000B0011`, a UMD recusou e ficou negociado `0x000B000B` (D3D11.0.7).
- **P1 (WDK 10.0.28000):**
  - `pOpenAllocationInfo` e `pOpenAllocationInfo2` são um **union** em `D3D10DDIARG_OPENRESOURCE` (`d3d10umddi.h`).
  - A versão 2 só vale a partir de `D3DWDDM2_0_DDI_MINOR_VERSION = 17`.
  - `D3DDDI_OPENALLOCATIONINFO` tem 24 bytes, e o `GpuVirtualAddress` da versão 2 fica no offset 24 (`d3dukmdt.h:587-605`).
- **Medido nas Corridas 180 e 181:** VAs `0x3F80000000000000`, `0x200FFFF` e `0xF166610`; zero sucessos dos caminhos Lock e SDMA; 16 fallbacks cinza.
- `hAllocation` e os dados privados ocupam os mesmos offsets nas duas versões; por isso a geometria saía correta.

## 4. Por que as decisões anteriores ficam

| Decisão | Motivo medido | Fica? |
|---|---|---|
| Recusar `0x000B0011` (07/09) | Aceitar sem backend: 66 ciclos de carga e descarga da DLL no dwm.exe | Sim. Aceitar exige o device WDDM 2.0 completo |
| Cinza `0xFF202020` (T3.8/T4.0) | Preto transparente virava bloco preto; Draw recusado fazia o Direct2D abortar o menu | Sim, nesta faixa. Conflito com a Lei 4 declarado; decisão do Daniel |
| Staging sem `memcpy` (T4.1) | `0xc0000005` na Corrida 180 | Sim; zero `0xc0000005` na 181 |

## 5. Como o Linux faz

- **amdgpu** (`radv_amdgpu_winsys_bo_from_fd`): importa o handle, aloca VA no próprio processo (`ac_drm_va_range_alloc`) e mapeia com `AMDGPU_VA_OP_MAP`. A VA de quem exportou nunca é usada.
- **Nosso RADV WDDM2** (`mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_bo.c:828-880`): recebe `OPENALLOCATIONINFO2`, **ignora** o `GpuVirtualAddress`, chama `MapGpuVirtualAddress` (teto `0x3F0000000` na BC-250) e depois `MakeResident`.

## 6. A mudança (T4.2)

Uma variável, só na UMD ([bc250_d3d_umd.c](../driver/umd/bc250_d3d_umd.c), `Bc250Umd10OpenResource`):

1. O elemento `[0]` é lido pela struct v1. A VA do runtime **nunca** é usada, em nenhuma interface.
2. A VA sai de `Bc250WinsysMapAllocation` neste processo, seguida de `MakeResident` e `WaitPaging`. O tamanho é arredondado para a página de 4 KiB (a alocação do kernel é granular em página; medido: 3.407.872 publicado, 3.670.016 alocado).
3. Todo passo registra status, e a ausência de tamanho é recusa com nome:
   - `OpenResource T4.2: Interface=... array=... -- GPUVA do runtime IGNORADA`
   - `OpenResource T4.2: Map=... Resident=... WaitPaging=... hr=... VA=...`
   - `OpenResource T4.2: MAPEAMENTO RECUSADO -- tamanho desconhecido`
4. Função pura `Bc250UmdOpenAllocationArrayIsInfo2` em [bc250_umd_caps_probe.c](../driver/umd/bc250_umd_caps_probe.c), usada só no log.

**Pré-condição conferida no log da 181:** `pfnMapGpuVirtualAddressCb` funciona nesta interface no dwm.exe (36 `MapAllocation`, todos com `hr=0x8000000A`, o `E_PENDING` tratado desde a B7.1).

**Dívida declarada, não tocada:** `DestroyResource` chama `Bc250WinsysDeallocate` também para recursos **abertos**, o que pede `pfnDeallocate2Cb` sobre um handle que não criamos. O comportamento vem da T4.0. A T4.2 só acrescenta a liberação da VA que ela mesma mapeou, que é correta.

## 7. Provas offline

- `driver/umd/build-umd.bat` EXIT 0, com o teste novo `TestOpenResourceIgnoresRuntimeGpuVa` (array v1 seguido de `0x3F80000000000000`, exatamente como medido).
- **Mutação verificada:** voltar a ler `pOpenAllocationInfo2[0].GpuVirtualAddress` reprova (`Assertion failed: res.Allocation.GpuVirtualAddress == 0u`). A UMD foi recompilada limpa depois da mutação.
- `driver/wdk/build-contract.bat` EXIT 0.
- `tools/build-bc250mcdm-lab-bringup.ps1 -Execute` EXIT 0.
- `tools/package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -ArmGfxWaveProbe -IAcceptSelectorWritesDuringTdr` EXIT 0 (mesmo perfil da 181).
- Auditorias: lab package PASS, safety PASS, DDI matrix PASS (estrutural), noop callbacks PASS (105 anunciadas, 0 falhas).
- A UMD **empacotada** contém as strings `OpenResource T4.2: Interface=` e `OpenResource T4.2: Map=`.
- `tools/prepare-corrida182-t42.ps1` EXIT 0.

### Hashes do candidato T4.2 (DriverVer `09/29/2026,3.327.1.0`)

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `2BBD88C8429016A5AB86E3D9CEF31AA434F1B1799106D40C6DE864A39C9A12B2` |
| `bc250mcdm.inf` | `E79FBE406FEB3638FD39FD2283D32F7AD9C517A45AE708CEF245767A5DAB8549` |
| `bc250mcdm.cat` | `EAF726C208CFBE188BF9E625BB3E21DB7053246AC568B39F8E82E3AA96587441` |
| `bc250umd.dll` empacotada | `1575E3B3D7FC7C2AAC7D4CEDF78175566EFCBFE57101889588C5A6D808F4ADC5` |
| SYS fonte (`output_lab`) | `6306A02078D20FBD027B0033B3C35B14B43080DB14EC6FB9AB24E28DC0F3C8A8` |
| UMD fonte (`driver/umd/output`) | `EC7E9EEBDB386F8630240AD395B00634F4543EE2143E964FF8C19C167473C307` |
| `vulkan_radeon.dll` (igual à 181) | `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C` |

O KMD não mudou de código nesta faixa; o hash do SYS difere do da 181 porque foi recompilado e reassinado.

## 8. Critério da Corrida 182 (escrito antes, Lei 10)

**Procedimento:** boot frio limpo; `tools\EXECUTAR_CORRIDA182_T42_600S.cmd` como Administrador; aguardar ~20 s de desktop e abrir o menu Iniciar. Se travar: reset em 10–15 s e, depois, copiar `C:\Windows\Temp\BC250_UMD_LOG_*.txt` com PowerShell elevado.

**Controles que não podem regredir:** zero `0xc0000005`; zero BugCheck.

| Desfecho | Assinatura (o que o log mostra) | Implicação (para onde leva) |
|---|---|---|
| **A** | `OpenResource T4.2: Map=0 Resident=0 WaitPaging=0` **e** `Superficie compartilhada carregada via SDMA GPU` **e** zero `Fallback ... (0xFF202020)` para as superfícies 1024x832 | A ponte de dados funciona; o próximo passo é conferir pixels do menu (foto + amostras) |
| **B** | `OpenResource T4.2: Map=` com valor diferente de 0, com `hr` registrado | O runtime recusa mapear o handle aberto nesta interface; investigar pelo `hr` |
| **C** | `Map=0` e VA plausível, mas `COMPOSITE_BLT RECUSADO` no KMD ou `BRIDGE GPU BLT LEAVE: hr` diferente de 0 | O KMD não resolve a VA recém-mapeada no espaço do processo do DWM; investigar a resolução de GPUVA por processo |
| **D** | `Map=0`, blt `hr=0`, caminho SDMA declarado, mas o menu segue cinza ou preto | Dado errado copiado (pitch, formato ou offset); conferir amostras da origem |
| **E** | Algo fora do mapa | Descrever o observado antes de julgar |

Um desfecho só ocorre com **todos** os seus sinais. A trava do FILL de 11 MB e a morte do DWM após o fallback são observadas à parte e registradas em qualquer desfecho.
