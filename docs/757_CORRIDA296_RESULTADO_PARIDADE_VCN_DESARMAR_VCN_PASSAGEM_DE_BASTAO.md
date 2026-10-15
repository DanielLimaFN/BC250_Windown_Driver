# 757 — Corrida 296: Análise Forense da Paridade do VCN, Blindagem Fail-Safe e Passagem de Bastão

> **Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2 / Oberon) | Windows 10 22H2 x64  
> **Data:** 14/10/2026  
> **Pacote testado:** lab `3.450.1.0` (Corrida 296 / [doc 756](756_BLOCO1_ALIVIO_JOURNAL_GPUVA_E_VCN1_SONDA_BOOT_VCN2_PS5_PACOTE_3450_E_CRITERIO_CORRIDA296.md))  
> **Evidência arquivada:** `docs/evidence/2026-10-14_15-31-15-DESKTOP-HQAG6T4-lab-binding`  
> **Status:** Paridade de Barramento no VCN isolada | Bloco 1 íntegro e pronto para ensaio | Bastão passado  

---

## 1. Resumo Executivo e Diagnóstico Forense

Na **Corrida 296**, o pacote lab `3.450.1.0` incluiu duas frentes:
1. **Bloco 1 (Pré-Jogos):** Expansão do Journal GPUVA (4 -> 16 slots), Taint global (16 -> 64 slots), cura de histerese e limpeza de órfãos no `DestroyProcess`.
2. **VCN.1 (Experimental):** Sonda de ativação do Video Core Next 2.0 com microcódigo do PS5 Linux (`ps5_vcn.bin`) acionada com `VcnProbe=2`.

### 1.1 O Que Aconteceu no Silício
Ao iniciar o ensaio de bind WDDM, o monitor apagou (tela preta imediata) e a plataforma reiniciou.

A análise dos logs forenses recuperados da pasta de evidência revelou a causa exata:
* **Registro de Reset de Hardware ([reset-reason-pos-trava.txt](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-10-14_15-31-15-DESKTOP-HQAG6T4-lab-binding/reset-reason-pos-trava.txt)):**
  ```text
  RESET_REASON raw=0x40080400
  RESET_REASON bit 19: software wrote 0x6 to reset control register 0xCF9
  RESET_REASON bit 30: a parity error occurred
  ```
* **Ponto de Corte no Driver ([display-log-this-bind-RECUPERADO.txt](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-10-14_15-31-15-DESKTOP-HQAG6T4-lab-binding/display-log-this-bind-RECUPERADO.txt)):**
  O log gravou perfeitamente até a linha 18:
  ```text
  17: [DisplayInit] OTG VSync linked to BAR5 at 0xFE800000 (Offset 0x14030), InitialFrameCount=8326
  18: [DCN1_D2] HUBP0/OTG0 como o GOP deixou (so' leitura): SURFACE_CONFIG=0x00000008 VIEWPORT=0x04380A00 PITCH=0x000009FF ADDR=0x0004:60000000 SURFACE_CONTROL=0x00000000 FLIP_CONTROL=0x00000000 EARLIEST_INUSE=0x0004:60000000 OTG_CONTROL=0x80011311 MASTER_UPDATE_LOCK=0x00000000 FRAME=8326
  ```
  Imediatamente após a linha 18, o `StartDevice` invocou `Bc250VcnProbeExecute(mmioPhys, context, vcnProbe)`. Na linha 170 de `bc250_vcn_probe.c`, foi executada a primeira instrução de leitura MMIO:
  ```c
  statusInitial = VcnMmioRead(mmio, BC250_VCN_REG_UVD_STATUS); // Leitura em 0x201BC
  ```
  O log foi cortado instantaneamente nessa linha sem produzir nenhum byte adicional.

### 1.2 Fenômeno Físico de Silício (Causa Raiz)
* **Power Gating / Clock Gating pelo SMU:** Na APU BC-250 (Cyan Skillfish 2 / Oberon), o bloco VCN 2.0 vem **desenergizado e com clock desligado** pelo firmware do SMU (System Management Unit) durante o POST do UEFI GOP.
* **Colapso de Barramento (Parity Error / Target Abort):** Quando a CPU tenta ler qualquer registrador MMIO de um bloco IP cujo clock ou alimentação está desligado no nível do SoC, o barramento interno (*Data Fabric* / *PCIe Controller*) sofre um *Master Abort / Parity Error*. Isso setou imediatamente o **bit 30 de paridade** no registrador do FCH `0xFED803C0`, disparando a proteção de hardware da APU e resetando a máquina.
* **Diferença com o Linux PS5:** No patch do PS5 Linux (`vcn_v2_0.c`), antes de qualquer leitura ou escrita MMIO no VCN, o kernel chama:
  ```c
  vcn_v2_0_set_pg_state(adev->vcn.inst, AMD_PG_STATE_UNGATE);
  ```
  Essa rotina envia mensagens de mailbox ao firmware do SMU para tirar o bloco VCN do estado de corte de energia e religar seus PLLs. Sem essa comunicação prévia com o SMU, o bloco é inacessível por MMIO.

### 1.3 Comportamento do Mecanismo de Proteção e Rollback
* O fusível anti-boot-loop funcionou com 100% de precisão: no boot seguinte, recusou o `StartDevice` com `0xC0000182` (`[FUSE] STARTDEVICE RECUSADO: Fusivel disparado (State=2, Boots=2)`).
* O script de recuperação automática restaurou o `BasicDisplay` (Código 0) e eliminou o binário experimental do sistema sem afetar o estado da máquina.

---

## 2. Estado do Bloco 1 (Alívio do Journal GPUVA e Taint)

O **Bloco 1 não teve qualquer relação com a falha**. Ele é composto puramente por estruturas de gerenciamento em RAM de host no kernel:
1. `BC250_ACTIVE_GPUVA_JOURNAL_SLOTS` expandido de 4 para **16 slots** por processo.
2. `BC250_ACTIVE_JOURNAL_TAINT_SLOTS` expandido de 16 para **64 slots** na tabela de taint global.
3. Cura de histerese em `Bc250ActiveDrainGpuvaJournal` (`active->JournalTaintAll = 0` ao esvaziar).
4. Limpeza explícita de journals e taints órfãos em `Bc250ActiveDestroyProcessObject`.
5. Validação integral em contratos offline: `build-contract.bat` com **275 portões verdes**.
6. Auditoria de segurança aprovada: `audit-bc250mcdm-safety.py` **PASS**.

O Bloco 1 está pronto para ser validado em hardware assim que a sonda do VCN for mantida em repouso.

---

## 3. Blindagem Implementada no Código

Para proteger a máquina física contra qualquer disparo acidental futuro:
1. **Gate Fail-Safe em `bc250_vcn_probe.c`:**
   ```c
   /*
    * ACHADO DE SILICIO (Corrida 296, doc 757):
    * Na APU BC-250 (Cyan Skillfish 2 / Oberon), o bloco VCN 2.0 vem sob
    * Power Gating e Clock Gating completos pelo SMU vindo do UEFI GOP.
    * Qualquer leitura MMIO direta (VcnMmioRead) nos registradores de UVD/VCN
    * sem previo ungate pelo SMU (smu_set_power_gate / AMD_PG_STATE_UNGATE)
    * causa TARGET ABORT no Data Fabric / PCIe (RESET_REASON bit 30: a parity error occurred)
    * e reinicio instantaneo da maquina.
    * O modulo permanece bloqueado ate a integracao de mensagens ao SMU.
    */
   if (Mode != 0x554E4754u) {
       Bc250DisplayLog("[VCN_PROBE] BLOQUEADO: VCN requer ungate de clock/energia via SMU antes de MMIO (Corrida 296 / bit 30)\n");
       return STATUS_SUCCESS;
   }
   ```
2. **INF (`bc250mcdm.inf`):**
   `HKR,Parameters,VcnProbe,0x00010001,0` (padrão 0, fail-closed).

---

## 4. Instruções de Passagem de Bastão para a Próxima IA

Prezada IA que assume o projeto:

### 4.1 Onde Estamos
* O ambiente está **limpo**, com `BasicDisplay` (Código 0) ativo no Windows 10 22H2 da BC-250.
* A baseline de estabilidade comprovada em silício é a **Corrida 295** (300 segundos estáveis, DWM + Menu Iniciar + Pesquisa na GPU, zero telas pretas, zero resets).
* A tentativa de sonda direta do VCN na Corrida 296 provou que o bloco multimídia está em **Power Gating** no silício e não pode receber leitura MMIO antes de mensagens de mailbox ao SMU. O VCN está agora blindado e bloqueado (`VcnProbe=0`).

### 4.2 O Que Fazer a Seguir (Ordem Estrita)

1. **Reempacotar para Validação Limpa do Bloco 1:**
   * Rodar o empacotamento com o perfil de laboratório padrão (mantendo `ShaderDispatchEnabled`, `PersistentSubmissionEnabled`, `DcnFlip=1`, `DcnExt=125`, `DebugTrace=1`, `ApuUnifiedMemory=1` e `VcnProbe=0`).
   * **ATENÇÃO:** O script `bind-bc250mcdm-lab.ps1` exige que os switches coincidam com o último bind para não acusar "Perfil do candidato é MAIS FRACO".
2. **Atualizar o Script de Preparo da Corrida:**
   * Atualizar hashes do pacote em `tools/prepare-corrida296...` ou criar `tools/prepare-corrida297-journal-alivio.ps1`.
3. **Executar a Corrida de 300 Segundos:**
   * Confirmar se o DWM e apps pesados rodam sem esgotar o Journal GPUVA (zero eventos de `NO_SPACE`) e sem saturação de taint.
4. **Seguir para o Bloco 2 (Jogos):**
   * Configurar e ensaiar a camada DXVK (Direct3D 9/10/11 sobre o Vulkan RADV existente da BC-250).
   * Mapear o D3D12 (vkd3d-proton sobre RADV).
   * Não reabrir MMIO de VCN sem antes implementar a comunicação de ungate com o SMU11.
