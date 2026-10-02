# 542 — Corrida 184: colisão de tokens fechada, menu lento por flush a cada draw, T4.5 e critério da Corrida 185

> 30/09/2026. Corrida 184: bind às 09:01:37, candidato T4.4 (**3.329.1.0**). Evidência: `docs/evidence/2026-09-30_09-01-37-DESKTOP-HQAG6T4-lab-binding/`.
> Faixa T4.5 offline, candidato **3.330.1.0**. **Nada da T4.5 foi validado em hardware.**

---

## 1. Relato da Corrida 184

1. **Houve avanço?** **Sim, funcional.** Os ícones e os textos do menu aparecem. O Daniel relata ~15 s até aparecerem e uma travadinha na hora em que surgem.
2. **Sucessos:**
   - **Critério da 184 fechou no desfecho A:**
     - `size/alignment mismatch` 172 → **0**;
     - `STATUS_OBJECT_NAME_COLLISION` no `OpenAllocation` 2.406 → **0**;
     - `CreateResource RECUSADO` no menu 172 → **0**;
     - `T4.4 TOKEN_SEQUENCE retomada em` diferente de 0 nos processos que recarregam a DLL;
     - cargas da DLL no StartMenuExperienceHost: 177 → **3**.
   - **Estabilidade:** um único dwm.exe (pid 1284) de 09:01:47 a 09:11:29; zero eventos 1000 de qualquer processo. KMD: 592,4 s, 3.877 IBs, zero TDR, zero BugCheck, zero trava; rollback online em Código 0.
3. **Falhas:**
   - **Lentidão do menu (medida):** em cada uma das 3 aberturas, o 2º Present do menu sai 4,4–5,4 s depois do 1º. Detalhe na §2.
   - **1 `Blt Timeout` falso** (2 linhas no KMD). Dívida do doc 540.
4. **Veredito:** **parcial.** O critério da 184 foi cumprido; a fluidez do menu não.
5. **Recuperação e pendências:** BasicDisplay em Código 0. NÃO VALIDADO EM HARDWARE: menu fluido, 60 FPS e jogos. Próximo passo: Corrida 185 com a T4.5.

## 2. Por que o menu demora (P0 por contagem, com a mecânica lida no código)

**O ciclo de cada abertura:** ao ser escondido, o menu chama `CloseAdapter`; ao abrir, recarrega a DLL, cria a ponte Vulkan (~0,04–0,14 s) e o device do zero.

**Entre o 1º e o 2º Present de cada abertura:**

| Carga | Intervalo | Draws | Flushes | Tempo em flush | Espera de paginação |
|---|---|---|---|---|---|
| 09:02:46 | 5,44 s | 126 | 127 | 1,58 s | 0,84 s |
| 09:04:19 | 4,36 s | 140 | 141 | 1,39 s | 0,26 s |
| 09:09:38 | 4,43 s | 143 | 144 | 1,55 s | 0,14 s |

- **Um flush por draw.** Todos os draws saem com `Lote=1 Direto=0 FlushAgora=1`.
- **Causa, por leitura de código:**
  - O flush era decidido por `readbackActive = (rtv->pResource && DrawReadbackCount < 3)`.
  - Mas `Bc250UmdReadbackDrawTarget` só lê, e só **incrementa o contador**, em alvos com pelo menos 1024x512.
  - Os alvos do menu são 640x640: o contador fica em 0 para sempre, e todo draw esvazia a fila e espera a GPU.
  - No dwm.exe (2560x1080) o contador chega a 3 e o efeito some, por isso só aparece no menu.
- **O resto do intervalo (H):** ~4.900 linhas de log gravadas com escrita direta no disco. ~800 delas são despejo do código dos pipelines (`VK_PIPELINE_IR`), repetido a cada abertura porque o teto de 32 pipelines é por carga da DLL.

## 3. A mudança (T4.5), uma variável, só na UMD

- **Fonte única de elegibilidade:** `Bc250UmdDrawReadbackTargetQualifies` decide as duas coisas: se a leitura roda e se o draw precisa de flush.
- **Efeito esperado:** alvo pequeno volta a juntar os draws no lote até o Present (DX.77); a leitura de diagnóstico e a sua semântica ficam iguais.

**Provas offline:**
- `build-umd.bat` e `build-contract.bat` EXIT 0.
- Portão novo `audit_draw_flush_matches_readback`, com **duas mutações verificadas**: flush com o predicado antigo, e leitura com checagem de tamanho própria.
- A primeira versão do portão reprovava o próprio código correto, porque a regex pegava a declaração `readbackActive = FALSE`. Corrigido antes das mutações.
- Pacote 3.330.1.0 assinado; auditorias lab package, safety, DDI matrix, noop e node1 PASS; `prepare-corrida185-t45.ps1` EXIT 0.
- A T4.5 não adiciona strings. A recompilação é provada pelo hash da UMD fonte (`C7011188…` → `B97FC137…`).

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado (**idêntico ao da 182–184**) | `2BBD88C8429016A5AB86E3D9CEF31AA434F1B1799106D40C6DE864A39C9A12B2` |
| `bc250mcdm.inf` | `F6C92B1D1CF82EA4CB50F280CA706A5E274F4272E31558CF9D16DC96A682AEA9` |
| `bc250mcdm.cat` | `61A11C94E753615B955C83A92B3BA6AA88EE52D8E3061BFFFC005A2098DCA026` |
| `bc250umd.dll` empacotada | `9863938BCC08EB442D329007ACB1DCE3126B4FDB6914D1988AE2A9811BF29D75` |
| UMD fonte | `B97FC13729D5CDB49EAB610E643A3012D2F6E78025BC7F584F0A62CB939E11FF` |

## 4. Dívidas declaradas (próximas faixas, uma por vez)

1. **Log pesado:** escrita direta no disco a cada linha, e despejo do código dos pipelines repetido a cada abertura do menu.
2. **Cache de pipelines por carga da DLL:** o menu recompila de 6 a 10 pipelines por abertura, 0,7–1,3 s cada vez.
3. **Blt Timeout falso no KMD** (doc 540).
4. **Drenos síncronos na destruição de recursos** (doc 540).
5. **Dívidas do doc 541:** volta de 16 bits do token, token PRIMARY sem PID, `hAllocation` do dxgkrnl tratado como handle nosso.

## 5. Critério da Corrida 185 (escrito antes, Lei 10)

**Procedimento:** boot frio; `tools\EXECUTAR_CORRIDA185_T45_600S.cmd`; o mesmo roteiro (abrir e fechar o menu várias vezes).

**Controles que não podem regredir:** desfecho A da 184 (zero mismatch, zero `NAME_COLLISION`); um dwm.exe; zero BugCheck e zero trava.

| Desfecho | Assinatura (log do StartMenuExperienceHost em `umd-logs-appcontainer/`) | Implicação |
|---|---|---|
| **A** | Flushes entre o 1º e o 2º Present de cada abertura caem de ~140 para **menos de 20**; draws com `FlushAgora=0` e `Lote` > 1; intervalo 1º→2º Present **abaixo de 3 s** | Flush por draw fechado; o Daniel compara o tempo dos ícones; o próximo alvo é o log (dívida 1) |
| **B** | Flushes caem, mas o intervalo segue acima de 4 s | O tempo restante não era flush; medir log e compilação de pipeline na mesma janela |
| **C** | Flushes continuam ~1 por draw com `Direto=0` | Há outra condição de flush; ler `flushNow` de novo |
| **D** | Draws com `Direto=1` passam a dominar | O caminho direto (buffer sem shadow) força o flush; investigar esses recursos |
| **E** | Algo fora do mapa | Descrever antes de julgar |
