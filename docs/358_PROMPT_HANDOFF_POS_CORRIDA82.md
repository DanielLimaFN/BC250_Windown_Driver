# Prompt de handoff — depois da Corrida 82 (23/09/2026)

Cole o texto abaixo na outra IA.

---

Você vai continuar o driver WDDM da AMD BC-250 (GC 10.1.3) no Windows 10 22H2:
- KMD `bc250mcdm.sys`;
- UMD `bc250umd.dll` (D3D11 → Vulkan);
- RADV em `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm`.

Repositório: `C:\Users\Daniel\Desktop\NamixWDDM`, branch `p6-rendernode-wddm`. **Há muito trabalho não commitado
(faixas DX.32 a DX.36): não faça commit, reset nem checkout sem o Daniel pedir.**

## Leia, nesta ordem
1. `CLAUDE.md`: leis, regras de relato de corrida e vocabulário P0/P1/P2/H.
2. `BASE_DE_AVANCO.md`: só as entradas do topo, de 23/09 (Corridas 76 a 82 e faixas DX.31 a DX.36).
3. `docs/357_CORRIDA82_DX36_RESULTADO.md`: estado atual e próximos passos.
4. `docs/356`, `docs/354`, `docs/355`: critério da 82, critério e resultado da 81 (o `SWITCH_BUFFER` que travou o CP).
5. `docs/353`, com a correção no topo, e `docs/352`: Corrida 80, a sonda de ondas e o VMID da onda.
6. Se precisar de contexto do display: `docs/348` a `docs/351`.

## Onde estamos (23/09, depois da Corrida 82)
- **Classe de TDR com mecanismo fechado (P0, 3 de 3):**
  - depois de uma troca de VMID no anel gráfico, as ondas do dispatch do IB novo rodam com o **VMID do IB anterior**
    (`[TDR_GFX_WAVE_VMID]`);
  - elas buscam o shader na tabela errada; `[GCVM_FAULT_TIMELINE]` registra a falta `Cid=9` na página do PC;
  - resultado: `FATAL_HALT` + `XNACK_ERROR`, 240 ondas presas e TDR.
- **Já tentado e descartado para esse problema:**
  - DX.29 (espera no PFP);
  - DX.30 (flush de VM no anel);
  - DX.32 (engine de invalidação 0);
  - DX.35 (par de `SWITCH_BUFFER`, que **travou o CP** e levou ao 0x116; revertido, e agora proibido por portão).
- **Próximo candidato**, lido do upstream (`gfx_v10_0_ring_emit_cntxcntl`): `CONTEXT_CONTROL` na troca, com
  `load_cs_sh_regs`.
  - Fonte: `torvalds/linux` v6.6, `drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c`, `amdgpu_ib.c` e `amdgpu_vm.c`. Baixe de
    novo; a cópia da sessão anterior era temporária.
- **Segunda frente (não causa TDR):** o IB `0x2401DC000` (552 DW) renderiza em `CB_COLOR0_BASE=0x2401A0000`, um VA que
  acabou de ser remapeado de uma alocação gravável para uma somente leitura. H: reuso de VA do render target na UMD ou
  na RADV. Investigar offline pelos logs da UMD.
- **Display:** o desktop do DWM chega ao monitor e fica, mas com partes faltando. O cursor é apresentado por retângulos
  sujos.
- **Pacote atual:** lab 3.243.1.0 (já rodado na 82). O próximo candidato precisa subir o `DriverVer` em
  `driver/wdk/package/bc250mcdm.inf` (fonte está em 3.243.0.0 → use 3.244.0.0).

## Como seguir
1. Resolva por leitura de código, dos logs em `docs/evidence/` e do upstream antes de propor uma corrida. A corrida é
   cara, e **quem roda é o Daniel**, com `tools\EXECUTAR_D3D11_NATIVO.cmd`, elevado e sem `-Force`. Você compila,
   analisa e prepara.
2. **Uma variável comportamental por corrida**, salvo quando o Daniel aprovar mais de uma.
3. **Antes de cada corrida**, escreva o critério num doc novo (`docs/359_...`):
   - assinatura e implicação em campos separados;
   - um desfecho "fora do mapa";
   - um desfecho de reversão se a mudança puder travar o anel;
   - "desfecho parcial não é desfecho".
4. **Todo portão novo** em `tools/audit-bc250mcdm-safety.py` precisa ser verificado por mutação: altere o código e
   confirme que a auditoria reprova. Quando couber, faça também uma mutação que derrube a suíte de teste.
5. **Build e pacote:**
   - `driver\wdk\build-contract.bat` (tem de dar EXIT=0);
   - `tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup`;
   - `tools\package-bc250mcdm-lab.ps1 -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -ArmGfxWaveProbe -IAcceptSelectorWritesDuringTdr`;
   - `tools\audit-bc250mcdm-lab-package.ps1`;
   - mais as auditorias: `audit-bc250mcdm-safety.py`, `audit-mcdm-ddi-matrix.py`, `audit-ddi-noop-callbacks.py`,
     `audit-radv-wddm-safety.py --mesa-root <mesa>` e `test-dx31-primary-copy-mutations.py`.
   - **O empacotador não compila:** confira por string no SYS empacotado que o código novo está lá.
6. **Depois de cada corrida**, relate ao Daniel as 5 seções obrigatórias:
   1. Houve avanço?
   2. Sucessos.
   3. Falhas.
   4. Veredito frente ao critério.
   5. Recuperação e pendências.

   Grave o resultado num doc e no topo da `BASE_DE_AVANCO.md`.
7. **Para analisar uma corrida:**
   - pasta mais nova em `docs/evidence/`, `display-log-this-bind-FINAL.txt`;
   - se `complete_at_capture=false` ou não houver FINAL (BugCheck), rode
     `tools\recover-bc250mcdm-lost-tail.ps1 -MaxFolders 1` e junte o log com a cauda resgatada;
   - dump de tela azul: peça ao Daniel para rodar o `cdb` elevado, com **caminho absoluto** de saída.

## O que NÃO fazer
- Não declarar sucesso sem prova física (Lei 2). Sem WARP, sem fallback de CPU, sem `STATUS_SUCCESS` sem backend.
- Não escrever registradores de DCN. Não usar offset de registrador sem procedência (`inc/linux_ref/` e `docs/03`).
- Não reintroduzir `SWITCH_BUFFER` na troca de VMID sem CE/preâmbulo; o portão DX.36 proíbe.
- Não pedir corrida sem boot frio. O marcador `C:\BC250_REQUIRE_CLEAN_BOOT.json` existe para isso: não o apague.
  Desligamento completo (`shutdown /s /t 0`), não reiniciar.
- Não usar `-Force` no binder.
- Não commitar, não fazer push, não descartar alterações sem o Daniel pedir.
- Não usar heredoc do Bash para conteúdo com barra invertida ou aspas: ele corrompe. Use Write/Edit ou um script em
  arquivo.
- Não repetir afirmações de docs antigos sem conferir. Exemplo: `FATAL_HALT` já era conhecido desde a Corrida 45.
- Não chamar hipótese de causa; classifique tudo como P0/P1/P2/H.
- Use they/them para pessoas.
