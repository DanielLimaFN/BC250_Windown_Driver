# Passagem de bastão — Desbloqueio do HQD_IB_FETCHER sob VMID 9 (3.118.1.0)

> **Data:** 2026-09-09  
> **Pacote:** laboratório `3.118.1.0`, assinado e auditado, **NÃO INSTALADO**  
> **Estado:** implementação, contratos e auditorias offline concluídos com sucesso; aguardando ensaio físico após boot frio  
> **Veredito:** **NÃO VALIDADO EM HARDWARE**

---

## 1. Origem da mudança — Resultados Físicos da Corrida 3.117.1.0

A corrida **3.117.1.0** (18:25:22) produziu o maior avanço microarquitetural de compute até o momento:
1. **O dispatch e a doorbell funcionaram no silício**:
   - `WPTR` MMIO avançou de `0 -> 12`.
   - O toque de doorbell em BAR2 funcionou: `HqStatus0` passou de `0x40008040` para `0x00008040` (o bit 30 `QUEUE_IDLE` caiu de 1 para 0).
2. **O Command Processor Fetcher (CPF) buscou e decodificou o anel**:
   - `CP_CPF_BUSY_STAT` saltou de `0x28060000` para `0x68460000`.
   - Decodificação oficial GFX10 (`gc_10_1_0_sh_mask.h`):
     - Bit 30 (`0x40000000`): `HQD_PQ_BUSY = 1`
     - Bit 18 (`0x00040000`): **`HQD_IB_FETCHER_BUSY = 1`** (o fetcher de Buffer Indireto engajou!).
     - Bit 17 (`0x00020000`): `HQD_PQ_FETCHER_BUSY = 1`.
   - O descritor PM4 `IT_INDIRECT_BUFFER` (`0xC0023F00, 0x00011000, 0x00000000, 0x09800011`) foi lido pelo CPF, que direcionou a busca para `VA = 0x11000` sob **VMID 9**.
3. **O Bloqueador Medido**:
   - O `HQD_IB_FETCHER` congelou aguardando leitura de memória via GCVM VMID 9 (`CP_CPC_STALLED_STAT1 = 0x00040000`, `MEC2_WAIT_ON_RCIU_READ`).
   - `RPTR` permaneceu em `0`, a Fence 18 não foi produzida, os 5 canários de VRAM não foram alterados e o TDR disparou após 2 segundos.
   - A tela preta ocorreu porque o `dwm.exe` recebeu `E_NOTIMPL` da UMD para D3D10/11, e o watchdog restaurou o `BasicDisplay` (Código 0) de forma limpa.

---

## 2. O que foi Implementado no Candidato 3.118

### A. Protocolo Upstream de Drenagem (Dequeue Handshake)
Local: `driver/wdk/bc250_submission_engine.c` (`Bc250SubmissionEngineRetargetDrainedIbVmid`)
Conforme especificado no kernel Linux upstream (`kgd_gfx_v10_hqd_destroy`), desativar uma HQD gravando `ACTIVE = 0` diretamente desestabiliza a malha RCIU. A desativação agora cumpre o handshake completo:
1. Requisita drenagem de pipeline: `BC250_HQD_MMIO_DEQUEUE_REQUEST = 1u`.
2. Aguarda até `BC250_SUBMISSION_ACTIVE_POLLS` pela inatividade (`ACTIVE == 0`).
3. Limpa a requisição: `BC250_HQD_MMIO_DEQUEUE_REQUEST = 0u`.
4. Grava e confirma `BC250_HQD_MMIO_ACTIVE = 0u`.

### B. Sincronização Estrita do MQD em VRAM
Local: `driver/wdk/bc250_submission_engine.c`
- Atualiza `engine->Mqd.CpHqdVmid = targetValue` (`0x900`).
- Copia a struct completa para a VRAM com barreira de memória:
  ```c
  if (engine->Buffers.MqdVirtual != NULL) {
      RtlCopyMemory((PVOID)engine->Buffers.MqdVirtual, &engine->Mqd, sizeof(engine->Mqd));
      KeMemoryBarrier();
  }
  ```
- Isso elimina a inconsistência em que o microcódigo do MEC encontrava o MQD em VRAM ainda configurado com o VMID 0.

### C. Rearme de DOORBELL_ENABLE e Invalidação de Cache HDP
Local: `driver/wdk/bc250_submission_engine.c`
- Imediatamente após reativar a HQD (`ACTIVE = 1u`):
  - Rearma o bit `DOORBELL_ENABLE` (`0x2`) em `CP_HQD_PQ_STATUS`.
  - Emite `BC250_HQD_MMIO_HDP_READ_CACHE_INVALIDATE = 1u` para assegurar que o MEC leia o MQD atualizado em VRAM.

### D. Telemetria Fora de DISPATCH_LEVEL
Local: `driver/wdk/bc250_submission_engine.c`
- A chamada `Bc250DisplayLog("[MEC_VMID_RETARGET] ...")` foi movida para logo após `KeReleaseSpinLock(&engine->Lock, irql)`, garantindo execução em `PASSIVE_LEVEL` e gravação direta no arquivo de log em disco.

---

## 3. Identidade Criptográfica do Pacote 3.118.1.0

Localização: `driver/wdk/package/output_lab`  
Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Artefato | SHA-256 |
|---|---|
| **SYS de laboratório** (`bc250mcdm.sys`) | `76D4FFE3BC38441E506AC357B67FA74F2E5BB9C5F55B5FE13925DA7FF640A091` |
| **UMD de laboratório** (`bc250umd.dll`) | `2A25BE2C6D807917BDCA2D8A03BDDEA94FB3DD018B312EC1FDAF1AD5D460969B` |
| **INF de laboratório** (`bc250mcdm.inf`) | `9941465561E4394781A8D7E6857B94B64EF585A6B3596CDE66B4CF1253BF6F81` |
| **CAT de laboratório** (`bc250mcdm.cat`) | `F5189A10E7B71308C9E6F1887AF21D091B96199FBF9026A32E9CE85C6837F6AE` |

---

## 4. Resultados das Auditorias Offline

- `test-mec-vmid-retarget-safety.py`: **PASS** (6/6 mutações)
- `test-mec-cursor-reconciliation-safety.py`: **PASS** (5/5 mutações)
- `test-wgp-policy-safety.py`: **PASS** (4/4 mutações)
- `audit-bc250mcdm-safety.py`: **PASS** (suíte completa de segurança)
- `build-contract.bat`: **EXIT=0** (suíte completa de contratos unitários WDK)
- `audit-bc250mcdm-lab-package.ps1`: **PASS** (pacote 3.118.1.0 assinado e íntegro)

---

## 5. Instruções para o Ensaio Físico (Daniel / Regra 10)

> [!CAUTION]
> **Boot Frio Obrigatório:** O watchdog da corrida anterior registrou `C:\BC250_REQUIRE_CLEAN_BOOT.json`.
> Reinicie a máquina no Windows com o `BasicDisplay` ativo antes de executar o comando abaixo.

Após reiniciar o computador, abra o PowerShell como Administrador e execute:

```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod
```

### Critério Físico de Aceite (Tripé Node 1):
- `RPTR` físico avança de `0` para `12` (`RPTR == WPTR == 12`).
- `P1RetiredTotal` avança de `17` para `18`.
- Fence 18 é escrita sobre o poison `0x43DAFFFE`.
- Os 5 canários em VRAM (`0xBC250C01`..`05`) são gravados e conferidos byte a byte.
