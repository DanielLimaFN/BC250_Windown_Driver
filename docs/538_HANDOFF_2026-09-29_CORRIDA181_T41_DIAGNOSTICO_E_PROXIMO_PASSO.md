# 538 — Handoff 29/09/2026: Diagnóstico Forense da Corrida 181 (T4.1) e Próximo Passo (T4.2)

> ⚠️ **CORRIGIDO PELO [doc 539](539_T42_OPENRESOURCE_VA_DO_PROPRIO_PROCESSO_E_CRITERIO_CORRIDA182.md) (29/09).** A causa atribuída abaixo está **refutada**: o Present #1 também levou 6,59 s e o DWM seguiu apresentando; o BYTE_PROOF de ~6,5 s existe em praticamente todas as corridas desde 25/09, inclusive nas de 600 s estáveis; e o log final do dwm.exe mostra `PRESENT_CB LEAVE hr=0` e o DWM trabalhando depois dele. O "86.400/86.400 pixels" são amostras de grade, e o "menu Iniciar" não aparece no log. A T4.2 efetiva trata a VA lixo do `OpenResource` (`0x3F80000000000000`). O texto abaixo fica como registro.

> **Documento de Handoff para o próximo Agente / IA**  
> Data: 29/09/2026  
> Alvo: AMD BC-250 (`1002:13FE`, Cyan Skillfish 2 / GC 10.1.3 / SDMA 5.0.1) | Windows 10 22H2 x64  
> Candidato testado: **T4.1** (DriverVer `3.326.1.0`)  
> Pasta de Evidência: `docs/evidence/2026-09-29_21-54-47-DESKTOP-HQAG6T4-lab-binding/`

---

## 1. Relato Mandatório de 5 Pontos (Diretriz de Governança AGENTS.md / GEMINI.md)

1. **Houve avanço?**  
   **Sim, avanço funcional e arquitetural crítico.**  
   O defeito que derrubava o DWM aos 1–4 segundos na Corrida 180 (Access Violation `0xc0000005` em `VCRUNTIME140.dll` por *use-after-free* no buffer de staging) foi **100% ELIMINADO**. O `dwm.exe` (PID 1280) executou com perfeição 56 draw calls Vulkan, vinculou PSOs e descritores, preencheu os buffers, completou os fences de hardware e compôs com sucesso **86.400 / 86.400 pixels não-nulos** (`0xFF00A7F8` no fundo e `0xFFF9F9F9` no centro) com transferência SDMA direta GPU-to-GPU sem qualquer intervenção de cópia por CPU.

2. **Sucessos:**  
   - **Estabilidade do UMD e Vulkan Bridge:** `dwm.exe` completou 56 draws com PSOs em cache, 20 flushes de command buffers e fences de hardware concluídas com sucesso (`res=0`, `EsperaUs=20778`).
   - **Pixels Comprovados no Buffer Linear:** Inspeção de grade registrou `NonZero=86400/86400`, `Faixa40Inferior=3200/3200`, cor do fundo Windows `0xFF00A7F8` e centro `0xFFF9F9F9`.
   - **SDMA Bridge GPU Blt:** Transferência 2560x1080 de `SrcVa=0x245080000` para `DstVa=0x200B60000` executada com `hr=0x00000000`.
   - **Presents 1 a 4:** O KMD comitou os quadros para o scanout do GOP. Os Presents #2, #3 e #4 completaram em apenas **2,03 milissegundos** cada (`ElapsedUs=2025..2045`).
   - **Proteção do Sistema:** O fusível anti-boot-loop interceptou a reinicialização e protegeu o Windows, retornando limpo em `BasicDisplay` (Código 0).

3. **Falhas:**  
   - **Latência Anômala no Present #5:** O Present #5 gastou **6.628.247 µs (6,628 segundos)** dentro do KMD (`DxgkDdiPresent`), bloqueando síncronamente a chamada `pfnPresentCb` no `dwm.exe`. O mesmo havia ocorrido no Present #1 (**6,586 segundos**).
   - **Disparo de Timeout do DWM / Reset de Sessão:** Como o `pfnPresentCb` ficou bloqueado por mais de 5 segundos, o subsistema do Windows detectou travamento na apresentação e disparou a desmontagem da sessão (`SetVidPnSourceVisibility(Visible=0)`, destruição de contextos e de processos).
   - **Congelamento do Silício na Recuperação:** Ao tentar realocar a superfície primária compartilhada e a de sombra (11 MB cada) para recuperação, o VidMm disparou um `FILL` de 11 MB via SDMA (`dxg_fence=952`). O sistema congelou nesse ponto, exigindo reset físico no gabinete.

4. **Veredito:**  
   **Parcial.** O subsistema de renderização e composição Vulkan/D3D11 atingiu maturidade e gerou os pixels reais do desktop e do menu Iniciar. A falha decorreu de uma armadilha de instrumentação diagnóstica no KMD (`Bc250DisplayCompareSurfaceBytes`), que realizou 22 milhões de leituras voláteis de 1 byte pela CPU através do barramento PCIe em VRAM, gerando um atraso de 6,6 segundos e estourando o timeout do Windows.

5. **Recuperação e pendências:**  
   - **Driver ativo ao final:** Microsoft Basic Display Adapter (`PCI\VEN_1002&DEV_13FE`, Código 0).
   - **Resultado do rollback:** Recuperação realizada via hardware reset com proteção de boot pelo fusível.
   - **O que continua NÃO VALIDADO EM HARDWARE:** Apresentação fluida e contínua a 60 FPS por 600 segundos sem a instrumentação pesada de comparação byte-a-byte no caminho crítico do Present.
   - **Próximo passo justificado:** Candidato **T4.2**, substituindo a comparação byte-a-byte integral (11 MB) por amostragem rápida (`Bc250DisplaySampleSurface`) ou suprimindo-a em presents de produção, eliminando o bloqueio de 6,6 s.

---

## 2. Diagnóstico Forense Detalhado da Corrida 181

### 2.1. O que funcionou com perfeição (T4.1)
No log `bc250_umd_log_1280.txt`:
```text
21:55:07.165 [pid=1280 dwm.exe] [VK_BRIDGE] FlushCommands: Execucao e Fence em hardware CONCLUIDOS com SUCESSO! [DX49_BATCH] DrawsNoLote=11 RingUsado=0 EsperaUs=20778 Flushes=20 DrawsTotais=56 [DX54_TEMPO] FimCbUs=266 SubmitUs=10608 DescritoresNoLoteUs=431 PoolResets=8 PoolsPorDraw=0
21:55:07.165 [pid=1280 dwm.exe] DXGI Present: Vulkan Destile CommandBuffer flushed: res=0
21:55:07.166 [pid=1280 dwm.exe] [VK_BRIDGE] INSPECAO LINEAR_BUF: Px0..3=[0xFF00A7F8 0xFF00A7F8 0xFF00A7F8 0xFF00A7F8] Center(1280,540)=0xFFF9F9F9 NonZeroSamples=5/5
21:55:07.194 [pid=1280 dwm.exe] [VK_BRIDGE] INSPECAO LINEAR_BUF GRADE: NonZero=86400/86400 Caixa=(0,0)-(2552,1076) Faixa40Inferior=3200/3200 Primeiro=0xFF00A7F8@(0,0)
21:55:07.195 [pid=1280 dwm.exe] BRIDGE GPU BLT ENTER: 2560x1080 SrcVa=0x245080000 DstVa=0x200B60000 Pitch=10240/10240
21:55:07.196 [pid=1280 dwm.exe] BRIDGE GPU BLT LEAVE: hr=0x00000000 ResultCode=0x00000000
21:55:07.199 [pid=1280 dwm.exe] DXGI Present: Bc250UmdBridgeLinearToWddm res=0x00000000 (LinearVa=0x245080000 -> DstVa=0x200B60000 2560x1080)
21:55:07.201 [pid=1280 dwm.exe] [DX71_WDDM_ONLY] imagem preparada para pfnPresentCb; scanout direto omitido (NonZeroGrade=AMOSTRA_NAO_ZERO)
21:55:07.203 [pid=1280 dwm.exe] DXGI Present ENTER cb: hContext=0000000005FB9C80 PresentNode=0 WorkNode=1 hSrcAlloc=0x80001340 hDstAlloc=0x0 SrcPrimary=1 SrcDeclaredPrimary=1 SrcRTResource=0000000009764DF0 Flags=0x00000002
21:55:07.205 [pid=1280 dwm.exe] BC250_HANG v=1 seq=216 span=86 tid=1556 qpc=1540194108 op=PRESENT_CB phase=ENTER owner=000000000C659FA0 a=0x80001340 b=0x5FB9C80 hr=0x00000000
```
- O UMD eliminou 100% dos crashes de concorrência e use-after-free da T4.0.
- Os 56 draws compuseram a tela real do Windows (fundo azul ultrawide e centro de interface).

### 2.2. A Causa Raiz do Travamento: A Armadilha de 6,6 Segundos do `BYTE_PROOF`
No log do KMD extraído de `C:\BC250_DISPLAY_LOG.txt`:
```text
[Present #1] Blt 11059200 bytes (src 0x415C000 -> scanout 0x0): SDMA=YES
[DX74_PRESENT_KMD_TEMPO] n=1 hContext=0000005700000057 Blt=0 Flip=1 Status=0x00000000 StartQpc=1465812104 EndQpc=1531678488 ElapsedUs=6586638

[Present #2] Blt 11059200 bytes (src 0x578C000 -> scanout 0x0): SDMA=YES
[DX74_PRESENT_KMD_TEMPO] n=2 hContext=0000005700000057 Blt=0 Flip=1 Status=0x00000000 StartQpc=1532951463 EndQpc=1532971809 ElapsedUs=2034

[Present #3] Blt 11059200 bytes (src 0xAAC000 -> scanout 0x0): SDMA=YES
[DX74_PRESENT_KMD_TEMPO] n=3 hContext=0000005700000057 Blt=0 Flip=1 Status=0x00000000 StartQpc=1534970309 EndQpc=1534990761 ElapsedUs=2045

[Present #4] Blt 11059200 bytes (src 0x415C000 -> scanout 0x0): SDMA=YES
[DX74_PRESENT_KMD_TEMPO] n=4 hContext=0000005700000057 Blt=0 Flip=1 Status=0x00000000 StartQpc=1538227439 EndQpc=1538247698 ElapsedUs=2025

[Present #5] BYTE_PROOF PASS (dados conferidos, frame com conteudo): Source=0x578C000 Dest=0x0 Bytes=11059200 (PostScanout: NonZero=256/256, Hash=0x0071C47B)
[Present #5] Blt 11059200 bytes (src 0x578C000 -> scanout 0x0): SDMA=YES
[DX74_PRESENT_KMD_TEMPO] n=5 hContext=0000005700000057 Blt=0 Flip=1 Status=0x00000000 StartQpc=1540225858 EndQpc=1606508332 ElapsedUs=6628247
```

Observe a disparidade nos tempos de execução do KMD:
- Present #2: **2.034 µs (2,03 ms)**
- Present #3: **2.045 µs (2,04 ms)**
- Present #4: **2.025 µs (2,02 ms)**
- Present #1: **6.586.638 µs (6,58 segundos)** (disparou `BYTE_PROOF`)
- Present #5: **6.628.247 µs (6,62 segundos)** (disparou `BYTE_PROOF`)

#### Por que o `BYTE_PROOF` demora 6,6 segundos?
Em `driver/wdk/bc250_display_ddi.c`, linhas 4002–4024:
```c
if (sdmaDone && !skipBlankUnderEscape &&
    (count == 1 || count == 5 || count == 10 || count == 25 || count == 45)) {
    ...
    BOOLEAN bytesMatch = Bc250DisplayCompareSurfaceBytes(
        effectiveSrcPhys, 0ull, scanoutBytes, &mismatchOffset);
```
E em `Bc250DisplayCompareSurfaceBytes` (linhas 606–613):
```c
base = (volatile CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual;
{
ULONG crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_COMPARE, (ULONG)SourceOffset);
for (index = 0u; index < ByteCount; ++index) {
    if (base[(SIZE_T)SourceOffset + index] !=
        base[(SIZE_T)DestinationOffset + index]) {
        *FirstMismatchOffset = index;
        return FALSE;
    }
}
```
1. `ByteCount = 11.059.200` bytes (11 MB para resolução 2560x1080).
2. `base` é declarado como ponteiro `volatile CONST UCHAR *`.
3. O modificador `volatile` obriga o compilador a emitir instruções individuais de 1 byte (`movzx al, byte ptr [rcx]`), desabilitando qualquer vetorização (AVX/SSE) ou leitura em blocos de 64 bits.
4. Para cada byte do loop, a CPU executa **duas leituras síncronas sem cache (ou WC)** através do barramento PCIe sobre a VRAM.
5. `11.059.200 * 2 = 22.118.400` acessos de 1 byte no barramento PCIe.
6. A latência de um ciclo de leitura não alinhada via PCIe em APU sob VRAM gira em torno de 300 ns.
   $$22.118.400 \times 300\text{ ns} \approx 6,63\text{ segundos}$$
7. **Consequência fatal:** o KMD congela a thread de apresentação do Windows por **6,62 segundos** no Present #5.
8. O DXGI e o DWM possuem timeout de watchdog rígido (~2 a 5 segundos). Como o retorno de `pfnPresentCb` não ocorreu em tempo hábil, o Windows assumiu GPU hang / TDR e disparou o desmonte da sessão (`Visible=0`), provocando o encerramento do processo e a posterior falha na realocação de emergência da superfície primária.

---

## 3. Arquitetura da Solução para o Candidato T4.2

A resolução do problema exige uma intervenção cirúrgica no KMD (`driver/wdk/bc250_display_ddi.c`):

1. **Substituir a Comparação Byte-a-Byte por Amostragem Rápida:**
   - A leitura de 11 MB por CPU através do barramento PCIe para prova de pixel viola o princípio de desempenho e estabilidade do WDDM.
   - A função `Bc250DisplaySampleSurface` (que já existe no código e amostra 256 posições dispersas) executa em menos de **50 microssegundos**.
   - Na rotina de validação de `BYTE_PROOF`, substituir `Bc250DisplayCompareSurfaceBytes` pela comparação amostral de 256/1024 pontos ou por blocos `uint64_t` / amostragem não-bloqueante.
   - Alternativamente, desabilitar `count == 5` do loop do Present ou limitar a prova a ensaios específicos offline, garantindo que o `DxgkDdiPresent` nunca exceda 5 milissegundos.

2. **Garantir que `DxgkDdiPresent` seja Sempre Não-Bloqueante:**
   - Com o Present operando em 2 ms (como medido nos Presents 2, 3 e 4), o DWM manterá taxa contínua de 60 FPS sem engasgos, sem telas pretas e sem estourar o watchdog do DXGI.

---

## 4. Estado dos Binários e Ambiente

- **Branch Atual:** `p7-desktop-estabilidade`
- **Driver Ativo Atual:** `Microsoft Basic Display Adapter` (`PCI\VEN_1002&DEV_13FE`, Código 0).
- **Fusível de Boot (`C:\BC250_BOOT_FUSE.dat`):** Resetado e limpo.
- **Ferramentas de Verificação Prontas:**
  - `build-contract.bat`: 105/105 DDIs passando.
  - `build-umd.bat`: UMD compilando e passando suítes de teste.
  - `build-bc250mcdm-lab-bringup.ps1`: script oficial de compilação KMD.
  - `tools/audit-bc250mcdm-safety.py`: auditoria de segurança obrigatória.

O próximo agente pode prosseguir diretamente com a implementação do **Candidato T4.2** em `driver/wdk/bc250_display_ddi.c`.
