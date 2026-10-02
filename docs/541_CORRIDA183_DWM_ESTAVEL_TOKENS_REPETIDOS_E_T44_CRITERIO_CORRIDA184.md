# 541 — Corrida 183: dwm.exe estável, tokens de alocação repetidos entre recargas da DLL, T4.4 e critério da Corrida 184

> 30/09/2026. Corrida 183: bind às 08:26:48, candidato T4.3 (**3.328.1.0**). Evidência: `docs/evidence/2026-09-30_08-26-48-DESKTOP-HQAG6T4-lab-binding/`.
> Faixa T4.4 offline, candidato **3.329.1.0**. **Nada da T4.4 foi validado em hardware.**

---

## 1. Relato da Corrida 183

1. **Houve avanço?** **Sim, funcional.** O dwm.exe durou a corrida inteira sem crash, contra um crash por use-after-free na 182. O Daniel relata que não travou; os bugs visuais continuam.
2. **Sucessos:**
   - **Critério da 183 fechou no desfecho A:**
     - um único dwm.exe (pid 1288) de 08:26:58 a 08:36:41;
     - zero eventos 1000 com `bc250umd.dll`;
     - 17 de 17 `OpenResource T4.2: Map=0`;
     - 5 cargas `via SDMA GPU`;
     - zero fallback cinza;
     - zero `COMMAND_BUFFER_AUSENTE_APOS_CARGA_DE_SRV`.
   - **KMD:** 592,8 s, 11.488 IBs, 6.678 FILLs, zero `IB_TRIPOD TIMEOUT`, **zero `Blt Timeout`** (1 na 182), zero TDR, zero BugCheck, zero trava. Rollback online em Código 0.
   - **Processos do shell usam a nossa UMD:** StartMenuExperienceHost (pid 6992), ShellExperienceHost, SearchApp e TextInputHost gravaram logs em `umd-logs-appcontainer/`. O StartMenuExperienceHost gravou 1.102 draws.
3. **Falhas:**
   - **Bugs visuais (relato):** causa principal identificada na §2 (P0 por contagem casada entre UMD e KMD).
   - **Fluidez:** mediana de 72 ms por quadro ativo no dwm.exe (825 flushes síncronos). Dívida do doc 540.
   - **Explorer caiu às 08:36:57** (`c0000005` dentro do próprio `Explorer.EXE`, +0x1f43cc), **7 s depois de o watchdog iniciar o rollback** (08:36:50). Atribuído à remoção do adaptador pelo rollback (H); não ocorreu durante o ensaio.
4. **Veredito:** **parcial.** Critério da 183 cumprido (DWM estável); a composição correta do shell não.
5. **Recuperação e pendências:** BasicDisplay em Código 0. NÃO VALIDADO EM HARDWARE: composição sem artefatos, fluidez, 60 FPS e jogos. Próximo passo: Corrida 184 com a T4.4.

## 2. Causa dos bugs visuais: tokens de alocação repetidos (P0)

**Correção de rota durante a análise:** a primeira busca não encontrou logs do menu Iniciar e levantou a hipótese de renderização por WARP. Ela está **retirada**: os logs de AppContainer ficam em `umd-logs-appcontainer/` com o nome do pacote no prefixo (doc 516), e a busca usava outro padrão.

**Medido:**

| Evidência | Valor |
|---|---|
| Cargas da `bc250umd.dll` no StartMenuExperienceHost (pid 6992) | **177** |
| Emissões do token `0x524553001B50000D` nesse processo (em 2.648 alocações) | **176** |
| `CreateResource RECUSADO` no menu: textura 32x32 BGRA compartilhada (`Misc=0x2`, `Bind=0xA8`), `hr=0x80070057`, repetida ~1/s a partir de 08:33:52 | **172** |
| `[ActiveOpenAlloc] RECUSADO: size/alignment mismatch rec=1638400/4096 req=8192/4096` no KMD | **172** |
| `[ActiveOpenAlloc] RECUSADO LEAVE ... result=0xC0000035` (`STATUS_OBJECT_NAME_COLLISION`) no device `0xEC8`, que é do dwm.exe (faz `COMPOSITE_BLT`) | **2.406** |

**Mecanismo:**

1. O token de alocação da UMD é `classe | (PID & 0xFFFF) << 16 | (sequência & 0xFFFF)` ([bc250_d3d_umd.c](../driver/umd/bc250_d3d_umd.c), `Bc250UmdNextResourceToken`). A sequência era uma variável estática da DLL.
2. O runtime descarrega e recarrega a DLL no mesmo processo (177 vezes no menu). A cada carga a sequência volta a 1, e os tokens se repetem enquanto alocações das cargas anteriores continuam vivas no KMD.
3. Em `DxgkDdiOpenAllocation`, o `hAllocation` recebido é o handle do dxgkrnl, não o nosso. O KMD então acha a alocação por **token** (`Bc250McdmObjectFindAllocationByToken`) e encontra a **antiga**:
   - no processo que cria, o tamanho diverge (1.638.400, a 640x640, contra 8.192, a 32x32) e a criação da superfície compartilhada falha;
   - no dwm.exe, a alocação "nova" resolve para uma que o device já abriu, e `Bc250McdmObjectOpenAllocation` devolve `DUPLICATE_OWNER`.
4. **Resultado:** superfícies do shell que o DWM não consegue abrir, e uma superfície do menu que nunca é criada. Partes da tela somem ou ficam velhas (ligação com o que o Daniel vê: H forte; não há prova por pixel).

**Como o Linux evita a classe:** no amdgpu o identificador de um BO é emitido pelo kernel (handle GEM e dma-buf), nunca por um contador do espaço do usuário. Aqui o token é da UMD, então ele precisa ser único por processo durante toda a vida do processo, não só durante uma carga da DLL.

## 3. A mudança (T4.4), uma variável, só na UMD

- **Contador único:** os três contadores (comando, prova e recurso) viraram um só, `g_Bc250UmdTokenSequence`.
- **Retomada entre cargas:**
  - no `DLL_PROCESS_ATTACH`, a sequência é retomada de `BC250_UMD_TOKEN_SEQUENCE` no ambiente **do próprio processo**;
  - no `DLL_PROCESS_DETACH` por `FreeLibrary` ela é gravada de volta. Com o processo terminando (`Reserved != NULL`) não há próxima carga, então nada é gravado.
  - O ambiente sobrevive ao descarregamento da DLL e não é visível a outros processos; o PID já está no token.
- **Log:** nova linha no attach, `T4.4 TOKEN_SEQUENCE retomada em 0x...` (0 = primeira carga no processo).
- **Funções puras** `Bc250UmdTokenSequenceParse` e `Bc250UmdTokenSequenceFormat` em [bc250_umd_caps_probe.c](../driver/umd/bc250_umd_caps_probe.c).
- **Formato do token e KMD sem mudança.** O SYS assinado é **idêntico** ao das Corridas 182 e 183.

**Provas offline:**

- `build-umd.bat` EXIT 0 com o teste `TestTokenSequenceSurvivesDllReload`. Ele carrega uma **cópia** da DLL compilada (estáticos e `DllMain` próprios), com o ambiente em `00001234`; troca o ambiente para lixo no meio; e exige `00001234` depois do `FreeLibrary`.
- **Duas mutações verificadas**, ambas reprovando na mesma asserção: sem a retomada (o detach grava `00000000`) e sem a gravação (sobra o lixo).
- UMD recompilada limpa depois das mutações; `build-contract.bat` EXIT 0.
- Pacote 3.329.1.0 assinado; auditorias lab package, safety, DDI matrix, noop e node1 submission PASS.
- A UMD empacotada contém as strings da T4.2, T4.3 e T4.4; `prepare-corrida184-t44.ps1` EXIT 0.

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado (**idêntico ao da 182/183**) | `2BBD88C8429016A5AB86E3D9CEF31AA434F1B1799106D40C6DE864A39C9A12B2` |
| `bc250mcdm.inf` | `F8B119A85FA124C84F13C27CF1570452ED793A2E84593F2B9745EA43669BD739` |
| `bc250mcdm.cat` | `135408B2A12C90F83CC118B949265CF8825EBA9121447D5C8F8E9DFA3A13579B` |
| `bc250umd.dll` empacotada | `BE03EC02AA8B63E259D0E5EAD0176EB6A5889E8F5408B7F9DB9A126203B5AE16` |
| UMD fonte | `C70111885DEDD8A7825606D5EAAB5B44FDCB22CF79DBD45D0A53EC6D385318B7` |

## 4. Dívidas declaradas, não tocadas nesta faixa

1. **Volta de 16 bits:** a sequência ainda dá a volta depois de 65.536 alocações no mesmo processo. No ritmo medido do menu (2.648 em ~9 min) isso leva ~3,7 h. A correção definitiva é o KMD emitir o identificador (como o GEM no Linux) ou o token ganhar mais bits, e mexe no ABI.
2. **Tokens de classe PRIMARY** são `classe | sequência de 32 bits`, sem PID: dois processos que criem primárias podem colidir. Hoje só o dwm.exe cria primárias.
3. **`OpenAllocation` usa o `hAllocation` do dxgkrnl como se fosse handle nosso** antes de cair na busca por token. Hoje só não acerta porque os espaços de numeração raramente coincidem. Precisa de uma faixa própria.
4. **Dívidas do doc 540:** Blt Timeout (comparação `==` depois do lock solto; 0 ocorrências na 183), log com write-through, drenos síncronos, BYTE_PROOF.

## 5. Critério da Corrida 184 (escrito antes, Lei 10)

**Procedimento:** boot frio; `tools\EXECUTAR_CORRIDA184_T44_600S.cmd` como Administrador; o mesmo roteiro (menu Iniciar várias vezes).

**Controles que não podem regredir:** desfecho A da 183 (um dwm.exe, zero crash com `bc250umd.dll`); zero BugCheck; zero trava.

| Desfecho | Assinatura (logs do KMD, da UMD e de AppContainer) | Implicação |
|---|---|---|
| **A** | `T4.4 TOKEN_SEQUENCE retomada em` com valor **diferente de 0** em processos com mais de uma carga; **zero** `size/alignment mismatch`; `NAME_COLLISION` em `ActiveOpenAlloc` caindo de 2.406 para **menos de 10** | Colisão de tokens fechada; o Daniel compara os artefatos; o próximo alvo é a fluidez |
| **B** | Retomada diferente de 0, mas `NAME_COLLISION` continua na casa das centenas | Há outra fonte de abertura duplicada (por exemplo o mesmo device reabrindo a mesma alocação legitimamente); investigar a regra `DUPLICATE_OWNER` do KMD |
| **C** | `T4.4 TOKEN_SEQUENCE retomada em 0x00000000` em **toda** carga de um processo que carrega mais de uma vez | O ambiente não persiste entre cargas nesse tipo de processo (AppContainer?); a retomada precisa de outro armazenamento |
| **D** | `size/alignment mismatch` continua com a retomada funcionando | A colisão vem de outro processo com o mesmo `PID & 0xFFFF`, ou da volta de 16 bits (dívida 1) |
| **E** | Algo fora do mapa | Descrever antes de julgar |
