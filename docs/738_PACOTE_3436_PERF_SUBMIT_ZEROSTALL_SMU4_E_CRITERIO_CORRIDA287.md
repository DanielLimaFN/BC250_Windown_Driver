# 738 — Pacote 3.436.1.0: Otimização de Latência no KMD (Amortização DISPATCH_LEVEL), DCN Zero-Stall (MPO3 Instantâneo) e Telemetria SMU.4 (P-State + Target Clock) — Critério da Corrida 287

**Data:** 13/10/2026.  
**Pacote:** lab **3.436.1.0** (SYS `FB00F1A1B93E735672268B663C71803BED225064D1D6ECDBA1974C677B34DDBB`, INF `0C74C03A230135CF9C505E1F4A1B9C0A8D5FC4490D692919C99744BC4D7A81FB`, CAT `AA53DA4EA3E5B5E64A3CAE70583E666A8D21AA130F376B81272C9034542FF408`).  
**Binários associados:** UMD `B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66`, RADV `B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D`, executável de carga `bc250-processo-novo.exe` (`89DEB11368C25549AAAD60A34EB23CDC57AE54E878C32DC0191DCF8A38255CEB`).  
**Parâmetros de configuração:** `DcnFlip = 1`, `DcnExt = 125` (0x7D: baseline provado das Corridas 274, 283, 284, 285 e 286; D12 e D19 DESATIVADOS), `DebugTrace = 1`.  
**Janela de Watchdog:** **300 s** (5 minutos contínuos).

---

## 1. Contexto e Motivação

Nas Corridas 285 e 286, a BC-250 demonstrou estabilidade absoluta ao longo de janelas de 300 segundos com DCN básico, render 3D em janela visível e telemetria SMU ativa a 1500 MHz. O operador questionou como poderíamos otimizar a performance do driver em regime e expandir ainda mais as leituras do hardware.

A pedido expresso do Daniel, combinamos três pilares funcionais para o KMD neste pacote:
1. **Opção 1 — Redução de Latência de Submissão no KMD (Desobstrução em `DISPATCH_LEVEL`):**
   - Na submissão de nós de computação e render (`SubmitVirtual Node1 GFX`), o KMD emitia múltiplas linhas de log verboso por submissão (`GATE FISICO PASS`, `FENCE COMMIT`).
   - Sob alta taxa de renderização, a formatação de strings e aquisição contínua do spinlock de log em `DISPATCH_LEVEL` roubava preciosos microssegundos da CPU.
   - Implementamos a amortização (1:128) dessas linhas secundárias através da nova Classe 18 do filtro verboso, enquanto mantivemos a linha principal `[SubmitVirtual Node1 GFX] ENTER` 100% íntegra para garantir precisão absoluta dos contadores de `GFX/s` do `monitor-corrida.py`.
2. **Opção 4 — Zero-Stall no Flip do DCN (MPO3 Instantâneo):**
   - Na rotina de temporização do VSYNC do KMD (`Bc250VsyncDpcRoutine`), a verificação de flip anterior requeria a detecção de mudança de frame counter do OTG (`currentFrame != LastOtgFrameCount`) ou aguardava até 32 ms (8 ticks de 4 ms).
   - Ao detectar `latchedNow == TRUE` (o hardware DCN sinaliza que o registrador de superfície linear do HUBP0 acabou de assumir o novo endereço), o driver agora notifica imediatamente a interrupção ao subsistema gráfico do Windows (`DxgkCbNotifyInterrupt`), eliminando até 32 ms de latência desnecessária no ciclo de apresentação.
3. **Opção 3 — Telemetria SMU.4 Expandida (P-State + Target Clock):**
   - Além do clock real (Msg `0x37`), tensão VDDGFX em mV (Msg `0x38`), versão de firmware (Msg `0x02`), versão da interface do driver (Msg `0x03`) e máscara de features (Msg `0x3D`), agora o KMD também consulta:
     - Msg `0x0C` (`PPSMC_MSG_QueryCorePstate`): estado de energia atual dos CUs da GPU;
     - Msg `0x0F` (`PPSMC_MSG_QueryGfxclk`): frequência alvo solicitada pelo gerenciador de energia do SMU.
   - Ambas as métricas são publicadas no rastro em `[SMU1]` e exibidas em tempo real no monitor e no CSV da corrida.
4. **Preservação Integral da Linha de Base:**
   - DCN Básico comprovado mantido intacto: `DcnFlip=1`, `DcnExt=125`, D12 e D19 desativados.
   - Carga T3 Animada (janela 480x320 visível, 60 FPS, 3,0 s de tela ativa, 64 MB de VRAM local).

---

## 2. O que mudou no código

### 2.1 Miniport KMD — Amortização de Logs do Node 1 GFX (`driver/wdk/bc250_display_ddi.c`)
- Expandido `BC250_VERBOSE_LOG_CLASS_COUNT` de 18u para 19u.
- Excluído o token `"GATE FISICO"` do bypass incondicional de `"PASS"` em `Bc250ShouldEmitVerboseLog`:
  ```c
  if (strstr(Message, "PASS") != NULL && strstr(Message, "GATE FISICO") == NULL) {
      return TRUE;
  }
  ```
- Registrada a Classe 18 para throttling de logs secundários de submissão do GFX:
  ```c
  else if (strstr(Message, "[SubmitVirtual Node1 GFX] GATE FISICO") != NULL ||
           strstr(Message, "[SubmitVirtual Node1 GFX] FENCE COMMIT") != NULL) {
      classIndex = 18u;
      limit = 128u;
  }
  ```
- A linha `[SubmitVirtual Node1 GFX] ENTER` permanece sem classe (emitida normalmente ou monitorada diretamente) garantindo que `monitor-corrida.py` meça cada frame entregue.

### 2.2 Miniport KMD — Zero-Stall no Flip do DCN (`driver/wdk/bc250_display_ddi.c`)
- Em `Bc250VsyncDpcRoutine`, quando o flip MPO3 é registrado no hardware e confirmado (`latchedNow == TRUE`):
  ```c
  if (latchedNow) {
      shouldNotify = TRUE;
  } else if (currentFrame != g_Bc250DcnFlipAccounting.LastOtgFrameCount) {
      shouldNotify = TRUE;
  } else if (g_Bc250DcnFlipAccounting.TicksSinceLastVsync >= 8u) {
      shouldNotify = TRUE;
  }
  ```
- O DWM e o runtime gráfico recebem a confirmação de apresentação no instante exato da captura pelo HUBP0.

### 2.3 Miniport KMD — SMU.4 Telemetria (`driver/wdk/bc250_active_miniport.c`, `bc250_mcdm_callbacks.h`, `.c`)
- Atualizada assinatura de `BC250_MCDM_SMU_CLOCK_PROBE`:
  ```c
  typedef BOOLEAN (*BC250_MCDM_SMU_CLOCK_PROBE)(
      _Out_ UINT32 *GfxclkMhz,
      _Out_opt_ UINT32 *GfxVidMv,
      _Out_opt_ UINT32 *SmuVersion,
      _Out_opt_ UINT32 *DriverIfVer,
      _Out_opt_ UINT32 *Features,
      _Out_opt_ UINT32 *CorePstate,
      _Out_opt_ UINT32 *TargetGfxclk);
  ```
- `Bc250ActiveSmuClockProbe` executa envio seguro de mensagens via Mailbox SMN:
  - Msg `0x0C`: `CorePstate`
  - Msg `0x0F`: `TargetGfxclk`
- Log do rastro `[SMU1]`:
  ```text
  [SMU1] GFXCLK=%u MHz VDDGFX=%u mV SMU_VER=0x%08X IF_VER=0x%08X FEAT=0x%08X PSTATE=%u TARGET_CLK=%u MHz
  ```

### 2.4 Monitor e CSV da Corrida (`tools/monitor-corrida.py`)
- `LogTail` parseia `PSTATE=` e `TARGET_CLK=`.
- `clk_parts` exibe: `1500 MHz | TGT 1500 MHz | 100 mV | P-State 0 | FW 0x00580600 | ...`.
- Colunas adicionadas ao CSV: `gpu_pstate` e `gpu_tgt_mhz`.

---

## 3. Pré-condições e Checklist

| Item | Descrição | Status Esperado |
| :--- | :--- | :--- |
| **P01** | CMOS configurada em 256 MB aplicados | ✅ Obrigatório |
| **P02** | Boot frio (desligamento elétrico prévio) | ✅ Obrigatório |
| **P03** | `audit-bc250mcdm-safety.py` e `audit-bc250mcdm-lab-package.ps1` aprovados | ✅ Concluído (PASS) |
| **P04** | Driver de laboratório 3.436.1.0 compilado e empacotado | ✅ Concluído |
| **P05** | `tools\EXECUTAR_CORRIDA287_PERF_SMU4_300S.cmd` gerado (300 s, CRLF) | ✅ Concluído |
| **P06** | `tools\prepare-corrida287-perf-smu4-zerostall.ps1` gerado | ✅ Concluído |

---

## 4. Critério de Aceite e Desfechos

| Desfecho | Critério | Ação |
| :--- | :--- | :--- |
| **A (Avanço Completo)** | Janela de 300 s completa sem travamento; T3 animado rodando com fluidez visual a 60 FPS; telemetria SMU.4 exibindo P-State e Target Clock no monitor; Zero-Stall atuando no flip sem degradação ou screen tearing; rollback online limpo para BasicDisplay ao fim. | Marco de performance consolidado. Base pronta para evolução de swapchains avançadas. |
| **B (Sucesso Parcial)** | Driver estável por 300 s; telemetria SMU.4 funcionando, mas alguma mensagem do SMU retornou 0/recusa sem travar a máquina. | Ajuste pontual das mensagens SMN. |
| **C (Falha Funcional)** | TDR, BugCheck ou congelamento sob carga. | Extração de ETW/dump, conferência de causa raiz. |

---

## 5. Como Executar

1. **Desligar completamente o PC** (Menu Iniciar > Desligar; **não reiniciar**) e ligar de novo.
2. Abrir o terminal PowerShell como Administrador em `C:\Users\Daniel\Desktop\NamixWDDM`.
3. Executar o runner da corrida:
   ```cmd
   .\tools\EXECUTAR_CORRIDA287_PERF_SMU4_300S.cmd
   ```
4. Após o desktop subir acelerado, abrir um segundo terminal como Administrador e iniciar a carga T3:
   ```cmd
   .\tools\RODAR_PROCESSO_NOVO.cmd
   ```
5. Acompanhar a janela animada 3D na tela e os contadores de clock/P-State/GFX no Monitor da Corrida.
