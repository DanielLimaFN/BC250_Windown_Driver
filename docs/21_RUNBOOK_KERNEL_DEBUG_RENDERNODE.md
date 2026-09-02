# Runbook: Diagnóstico por Kernel Debugging (KD / WinDbg) do RenderNode WDDM

**Data:** 2026-09-01  
**Hardware:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2)  
**Objetivo:** Capturar com precisão cirúrgica a stack trace e o status interno no `dxgkrnl.sys` que causa a desmontagem (`StopDevice` / `RemoveDevice` / Código 43) logo após o estágio de `QueryChildRelations` (`V5PostStartStage=3`).

---

## 1. Pré-Requisitos e Ambiente

1. **WinDbg** (WinDbg Preview / WinDbg clássico) instalado na máquina host ou local.
2. **Símbolos Públicos Microsoft** configurados para o `dxgkrnl.sys`, `ntoskrnl.exe`, `pci.sys`.
3. **PDB Local do Driver** preservado em `driver/wdk/output_lab/bc250mcdm.pdb`.
4. **Script de Automação:** [`tools/windbg_diagnose_rendernode.wds`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/windbg_diagnose_rendernode.wds).
5. **Recuperação Manual:** [`tools/BC250_RECOVERY.cmd`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/BC250_RECOVERY.cmd).

---

## 2. Métodos de Conexão KD

### Opção 1: Kernel Debugging Remoto via Rede (KDNET — Recomendado)
Se você estiver conectado a partir de uma máquina de desenvolvimento secundária (Host) para o BC-250 (Target):

1. No BC-250 (Target), abra o PowerShell como Administrador:
   ```cmd
   bcdedit /debug on
   bcdedit /dbgsettings net hostip:<IP_DA_MAQUINA_HOST> port:50000 key:1.2.3.4
   shutdown /r /t 0
   ```
2. No Host, abra o WinDbg:
   * **File** -> **Attach to Kernel** -> **Net** -> Port `50000`, Key `1.2.3.4`.

### Opção 2: Local Live Kernel Debugging (Na própria máquina)
Para depuração ao vivo com o sistema em execução (read-only live capture):
1. Habilitar depuração local no Windows:
   ```cmd
   bcdedit /debug on
   bcdedit /bootdebug on
   ```
2. Iniciar WinDbg local como Administrador:
   ```cmd
   windbg.exe -kl
   ```

---

## 3. Executando o Script de Diagnóstico no WinDbg

Com o WinDbg conectado ao kernel:

1. Carregue o script automatizado de rastreamento:
   ```text
   $$< C:\Users\Daniel\Desktop\NamixWDDM\tools\windbg_diagnose_rendernode.wds
   ```
2. O script executará as seguintes operações:
   - Apontará para o servidor de símbolos da Microsoft e o PDB local;
   - Abrirá o arquivo de log em `docs/evidence/windbg_rendernode_trace.log`;
   - Registrará breakpoints nas DDIs do driver (`StartDevice`, `QueryChildRelations`, `QueryInterface`, `StopDevice`);
   - Registrará breakpoints em funções internas do `dxgkrnl` (`DxgkStopDevice`, `CAdapter::Stop`, `CAdapter::Remove`).
3. Digite `g` no WinDbg para liberar a execução do sistema.

---

## 4. Disparando a Inicialização do RenderNode

No PowerShell do BC-250 (em console ou via SSH/RDP):

1. Certifique-se de que o sistema está em KMDOD limpo:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\bind-bc250mcdm-lab.ps1" -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -DisableWatchdog -IAcceptNoAutomaticRollback
   ```
2. Prepare a ativação do RenderNode:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\prepare-bc250mcdm-rendernode.ps1" -Execute -IAcceptDisplayRenderBootRisk
   ```
3. Reinicie ou force o reinício do dispositivo no Gerenciador de Dispositivos:
   ```powershell
   # O WinDbg irá capturar cada breakpoint no carregamento do driver
   ```

---

## 5. O Que Analisar na Saída do WinDbg

No momento em que `bc250mcdm!Bc250DxgkDdiStopDevice` ou `dxgkrnl!CAdapter::Stop` for atingido:

1. **Inspecione a Call Stack Completa**:
   ```text
   kP 30
   ```
2. **Observe o Chamador Imediato dentro do `dxgkrnl`**:
   - Qual função do `dxgkrnl` chamou `Stop`? (Ex: `dxgkrnl!CAdapter::StartFailed`, `dxgkrnl!CVidPnMgr::*`, `dxgkrnl!CDisplayManager::*`, `dxgkrnl!DXGDEVICE::*`).
3. **Inspecione o Status de Falha**:
   - Olhe os registradores `rax` / `eax` ou variáveis locais no frame do chamador:
   ```text
   r
   dv
   ```
4. **Feche o Log**:
   ```text
   .logclose
   ```

---

## 6. Procedimento de Rollback de Emergência

Se o sistema ficar instável ou a tela perder sinal:
1. Pressione `Power` para desligamento limpo ou execute via linha de comando:
   ```cmd
   C:\BC250_RECOVERY.cmd
   ```
2. O script restaura o driver para o Basic Display Driver do Windows em Código 0.
