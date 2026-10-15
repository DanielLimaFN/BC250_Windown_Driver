# Doc 758 — Corrida 297: vkcube real pela GPU e pelo DWM, memtest_vulkan sem erro, e 0x116 por TDR do GFX que não volta

> **Data:** 14/10/2026, bind às 16:43:16, BugCheck às 16:48:21 (uptime 9:28), reboot às 16:48:46.
> **Candidato:** lab **3.451.1.0** (SYS `B29BFF54`, UMD `4BA5929A`, RADV `0EE338DB`), [doc 757](757_CORRIDA296_TRAVA_NO_STARTDEVICE_VCN_REMOVIDA_ICD1_WSI1_VULKAN_COMO_DRIVER_OFICIAL_PACOTE_3451_E_CRITERIO_CORRIDA297.md).
> **Evidência:** `docs/evidence/2026-10-14_16-43-16-DESKTOP-HQAG6T4-lab-binding/` (log do KMD recuperado, anel do dump, `dump-analyze-RECUPERADO.txt`, logs da UMD em `umd-logs-pos-travamento/`).
> **Operador:** vkcube e vkcubepp abriram; memtest_vulkan rodou e gravou `memtest_vulkan.log`; GPUbench abriu. Depois a máquina travou e reiniciou.

## 1. vkcube e vkcubepp: GPU (P0 pelo log)

| | vkcube (pid 13040) | vkcubepp (pid 3264) |
|---|---|---|
| Janela | 16:44:11 → 16:44:26 | 16:44:27 → 16:44:35 |
| RADV no processo | `is_bc250=1 (device_id=0x13fe)` | igual |
| WSI.1 | `device D3D11 da BC-250 (nível 0xa100) e swapchain DXGI na janela` | igual |
| Texturas | `[WSI1] captura … alias=1`, 3 imagens 500×500, recriadas em 509×506 no redimensionamento | 3 imagens, `alias=1` |
| Presents do D3D11 | 161, todos `hr=0x00000000` | 145, todos `hr=0x00000000` |
| Ritmo | ~11 quadros/s | ~16 quadros/s |

- Os dois acharam a placa sozinhos, sem variável de ambiente: o ICD.1 funciona (P0).
- **O cubo foi desenhado pela GPU.** A RADV não tem caminho de CPU: o único dispositivo dela é a BC-250, os shaders são compilados pelo ACO para a ISA da placa e enviados ao anel do GFX. A cópia para a janela é feita por `vkCmdCopyImage` na UMD (GPU), e o Present passa pelo DWM.
- **NÃO VALIDADO EM HARDWARE:** os pixels do cubo (só o operador viu a janela) e o FPS, baixo porque a CPU espera a GPU duas vezes por quadro (risco declarado no doc 757).
- O redimensionamento funcionou: novas texturas no tamanho novo, sem recusa.

## 2. memtest_vulkan: GPU, sem erro (P0 pelo log do próprio programa)

- Achou um único dispositivo: `DevId=0x13FE 9GB AMD BC-250 Graphics`. Não existe Vulkan por CPU instalado nesta máquina, então ele rodou na BC-250.
- 712 iterações em ~66 s, todas `Passed`: 646,9 GB escritos e 1.293,8 GB conferidos na última janela de 30 s, a 64–74 GB/s de escrita e 48–61 GB/s de conferência.
- O memtest confere cada byte contra o padrão esperado. **Zero erros** é prova de dados na escala de gigabytes: compute + memória do segmento 2 (os "9GB" são o heap de sistema pela GART, não a VRAM de 256 MB).
- O log para em 712 iterações (~16:45:47; o arquivo foi gravado às 16:46). O teste padrão é de 5 min: ele foi fechado ou parou antes do fim. A trava veio 2,5 min depois.

## 3. GPUbench: CPU (P0 pelo log do programa)

- É um teste **OpenGL** (`OPENGL32.DLL`). O `gpubench.log` diz `GL_RENDERER: GDI Generic`, `OpenGL Driver Model: Software`, `GL_VERSION: 1.1.0`: é o OpenGL por software da Microsoft.
- O nosso driver não tem OpenGL (não há ICD de OpenGL), então qualquer programa OpenGL roda na CPU. O log foi regravado às 16:51, depois do reboot, já no BasicDisplay.

## 4. A trava: BugCheck 0x116 (P0 pelo dump e pelos logs)

- `VIDEO_TDR_FAILURE (116)`, `Arg3=0xC00000A3` (`STATUS_DEVICE_NOT_READY`): o Windows pediu um TDR, o nosso reset não trouxe o GFX de volta, e o Windows desistiu.
- **Quem travou:** o GFX, durante a composição do desktop, ~2,5 min depois de o último teste fechar. O vkcube e o memtest não estavam rodando.
  - Último trabalho: `VMID_SWITCH_VM_FLUSH #6248` da VMID 1 para a 6 (fence 12839). A VMID 1 é a do `dwm.exe`; as VMIDs 5 e 6 têm a mesma raiz e pertencem a um processo criado no começo do bind (provavelmente o Explorer, P2).
  - Dois page faults nos últimos segundos, os únicos da corrida:
    - VMID 5, cliente `CB/DB` (escrita de render target), `PermissionFaults=0x7` em `0x20193A000`;
    - VMID 1 (DWM), cliente `TCP` (leitura de textura), página não mapeada em `0x2030B7000`.
  - O CP ficou parado num `WAIT_REG_MEM` esperando a fence do anel (`3226`); a memória tinha `3225`.
  - O DWM entrou no último Present às 16:48:21.733 e não saiu.
- **Por que virou tela azul:** o soft-reset do GFX (GFXRST.1) rodou e o RLC voltou, mas a prova do reinício falhou: `TDR_GFX_RESTART_PROOF FALHA: RPTR=0 WPTR=12 … Polls=2000` e `o CP_RB0 nao executou NOP+RELEASE_MEM`. O driver devolveu `DEVICE_NOT_READY` (correto, sem mentir), e o Windows deu 0x116. É a mesma classe da Corrida 289 e do GFX que trava depois de troca de VMID (doc 630).
- O SDMA0 voltou normalmente (`TDR_RESTART PROVADO`).
- Recuperação: depois do reboot o fusível recusou o StartDevice, e hoje a placa está em BasicDisplay, Código 0.

## 5. Relato

1. **Houve avanço?** Sim, funcional. Pela primeira vez apps Vulkan baixados (vkcube, vkcubepp, memtest_vulkan) acharam a BC-250 sozinhos e rodaram na GPU; o memtest conferiu terabytes de dados sem um erro.
2. **Sucessos:** ICD.1 (descoberta pelo adaptador), WSI.1 (161 + 145 Presents S_OK, texturas com alias, redimensionamento), memtest sem erro, runtime da Khronos 1.4.357 instalado.
3. **Falhas:** 0x116 aos ~5 min, com o GFX travado na composição do desktop depois de dois page faults e o reinício do GFX que não volta. FPS baixo no vkcube (~11–16).
4. **Veredito:** parcial. O desfecho A do doc 757 exige os sinais de log (presentes) e o cubo visto girando (o operador disse "abriram"; falta confirmar que o cubo girava). A trava não está ligada aos testes Vulkan pelo tempo (P2).
5. **Pendências:** pixels do cubo NÃO VALIDADOS; causa dos dois faults não medida; reinício do GFX no TDR quebrado.

### Checklist da 297

| Item | Estado |
|---|---|
| vkcube/vkcubepp acham a BC-250 sem configuração (ICD.1) | ✅ |
| Swapchain DXGI da BC-250 na janela (WSI.1) | ✅ |
| Texturas importadas pela GPUVA (`alias=1`) | ✅ |
| Presents do D3D11 S_OK | ✅ 161 + 145 |
| Redimensionamento | ✅ |
| Cubo visível e girando | 🟡 operador: "abriram" |
| FPS | 🟡 ~11–16 (CPU espera a GPU) |
| memtest_vulkan na BC-250, zero erros | ✅ |
| GPUbench (OpenGL) na GPU | ❌ roda na CPU: não temos OpenGL |
| Desktop estável até o fim | ❌ 0x116 aos ~5 min |
| TDR do GFX recupera | ❌ CP_RB0 não executa depois do soft-reset |
| TDR do SDMA0 recupera | ✅ |

## 6. Tabela viva do DCN

Sem mudança nesta corrida (o DCN não foi alterado). Prontos: D01–D05, D08, D10, D15, D32–D33. Abertos: D06/D07, D11, D12–D14, D18–D23, D25–D29, D31.

## 7. Próximo

1. **TDR do GFX que volta** (o que transforma 0x116 em travadinha recuperada): depois do soft-reset, reprogramar o anel do GFX como `gfx_v10_0_cp_gfx_resume`/`cp_gfx_start` do amdgpu e provar com NOP + RELEASE_MEM. É o critério final do projeto ("TDR recuperando sem perder o display").
2. **Os dois faults:** registrar qual alocação contém `0x20193A000`/`0x2030B7000` (dona, tamanho, se foi liberada) no momento do fault, para saber se é uso depois de liberar.
3. **FPS do vkcube:** tirar as duas esperas da CPU por quadro (fence compartilhada entre a RADV do app e a UMD).
4. **OpenGL** não existe; jogos OpenGL rodam na CPU. Fora do escopo até decisão do Daniel (o caminho seria Zink sobre a RADV).
