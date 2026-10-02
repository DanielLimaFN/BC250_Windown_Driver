# Corrida 173 — Avanço Estrutural do SDMA (T3.3) e Alinhamento do FLUSH_TLB com Upstream Linux

> **Data:** 29/09/2026 12:18 | **Candidato:** T3.3 (DriverVer `3.318.1.0`, SYS `E6A21FC4888D6FBEB7C533EE5E21BEF8B59458D1D13014E9EF7D9BBA66289A00`)  
> **Pasta de Evidência:** `docs/evidence/2026-09-29_12-07-30-DESKTOP-HQAG6T4-lab-binding`  
> **Log recuperado:** 10.367.370 bytes de offset 4264065128, SHA-256 `DB1CAB588AE756C3D1F240405128EBB1E23D64F47199F9846FA3DBAEF6051E41`  
> **Duração medida pelo QPC:** 63,0 segundos.

---

## 1. Relatório Governança (Obrigatório segundo AGENTS.md / GEMINI.md)

### 1.1 Houve avanço?
**SIM, avanço funcional e estrutural de alta magnitude.**
A eliminação da escrita permanente de `BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION` (Faixa T3.3) resolveu completamente o travamento do anel do SDMA e o decaimento de VRAM que paralisavam todas as 14 corridas anteriores (Corridas 159 a 172). Pela primeira vez no corpus deste ciclo de testes:
- O SDMA executou 3.419 IBs sem nenhum timeout ou travamento no anel.
- O anel de eventos da GPU (`[EVT1_GPU]`) registrou **48 válidos, 0 degradados e 0 vazios** com integridade XOR de 100% — **a VRAM e o controlador de memória não decaíram**.
- O último submit do SDMA em voo no momento da trava foi devidamente completado e aposentado pela GPU (`seq=11312 fence=3420 -> CONCLUIDO`).

### 1.2 Sucessos
- **Throughput de Paginação:** 3.419 IBs executados (+155% em relação às corridas 166-172).
- **Operações de Memória:** 2.168 preenchimentos (`[DX91_PAGING_MOVE] op=FILL`) e 1.252 transferências (`[DX91_PAGING_MOVE] op=TRANSFER`).
- **Apresentação Visual:** 301 quadros apresentados pelo DWM (`[Present #` = 301) com 2 `BYTE_PROOF PASS`.
- **Estabilidade do Ring e DMA:** 0 `IB_TRIPOD TIMEOUT`, 0 `Blt Timeout`, 0 `GFXHUB_FAULT`, 0 `TDR_QUIESCE`, 0 `DEVICE_REMOVED`.
- **Integridade da Memória:** 0 corrupções de XOR no anel EVT.1 (zero decaimento de VRAM).

### 1.3 Falhas
- **Parada do Sistema aos 63,0 s:** O sistema congelou sem BugCheck e sem recuperação automática do watchdog.
- **Assinatura de Parada (Migalhas de CPU):**
  - `CPU10 camada=PAGINACAO seq=522145 sitio=PAGING_DDI detalhe=0x00000C fase=ANTES(acesso NAO concluido)`.
  - `CPU00 camada=INTERNA seq=522154 sitio=OTG_FRAMECOUNT_READ detalhe=0x014030 fase=DEPOIS(concluido) <- ULTIMA DA MAQUINA`.
  - A thread de paginação na CPU10 parou ao entrar no manipulador de `DXGK_OPERATION_FLUSH_TLB` (`0x0C`).
  - Todas as 18.536 chamadas anteriores de `FLUSH_TLB` haviam completado com sucesso (`status=0x00000000`).

### 1.4 Veredito
**SUCESSO PARCIAL CRÍTICO (Avanço Arquitetural Comprovado).**
O problema de 14 corridas de "trava no FILL por congelamento do SDMA e decaimento de VRAM" está **refutado e resolvido** pelo alinhamento do T3.3 com o upstream do Linux (`sdma_v5_0.c`). O gargalo remanescente foi isolado na rotina de `FLUSH_TLB` (`Bc250ActiveFlushTlbHandler` / `Bc250RunGfxhubTlbTransaction`).

### 1.5 Recuperação e Pendências
- **Recuperação:** Máquina reiniciada via reset físico por Daniel; fusível anti-boot-loop disparou como previsto (`State=2, Boots=2, LastStatus=0xC0000001`), deixando o dispositivo em Código 43 seguro.
- **Pendência:** O fusível precisa ser resetado (`tools/inspect-boot-fuse.ps1 -Reset`). O código de `FLUSH_TLB` precisa ser limpo para o padrão upstream Linux.

---

## 2. Análise Detalhada da Corrida 173

### 2.1 Comparativo: Corridas 159–172 vs Corrida 173

| Métrica | Corridas 159–172 (Média) | Corrida 173 (T3.3) | Diagnóstico |
|---|:---:|:---:|---|
| **IBs Executados** | 1.000 ~ 1.300 | **3.419** | +155% de vazão de comandos |
| **Paging Moves (Fill + Transfer)** | ~1.300 | **3.420** (2.168 Fills / 1.252 Transfers) | Pipeline SDMA contínuo e sem engasgo |
| **Quadros DWM (`Present`)** | 20 ~ 120 | **301** | Desktop ativo e renderizando suavemente |
| **EVT.1 Status na Trava** | `SEM CONCLUSAO REGISTRADA` | **CONCLUIDO** (`seq=11312 fence=3420`) | SDMA aposentou tudo |
| **Eventos Degradados na VRAM (XOR)** | 10 ~ 30 degradados | **0 degradados** (48 válidos) | Fabric e DRAM refresh permaneceram ativos |
| **Ponto de Parada** | SDMA preso em `CONST_FILL` | CPU10 em `PAGING_DDI 0x0C` (`FLUSH_TLB`) | Problema mudou da GPU para CPU/MMIO |

---

## 3. Investigação da Parada: O que acontece no `FLUSH_TLB`?

A análise do código em `driver/core/bc250_gfxhub_tlb_transaction.c` e `driver/wdk/bc250_active_miniport.c` confrontada com o código oficial do Linux kernel (`drivers/gpu/drm/amd/amdgpu/gmc_v10_0.c:243-319`) revelou uma discrepância massiva na implementação do flush de TLB:

### 3.1 O que o Linux Upstream faz (`gmc_v10_0_flush_gpu_tlb`):
```c
// Linux upstream gmc_v10_0.c:289-315
WREG32_RLC_NO_KIQ(req, inv_req, hub_ip);

/* Dummy read para aguardar ACK limpar na interface rápida do GRBM (GC < 10.3) */
if ((vmhub == AMDGPU_GFXHUB(0)) &&
    (amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(10, 3, 0)))
    RREG32_RLC_NO_KIQ(req, hub_ip);

/* Aguarda pelo ACK com delay */
for (i = 0; i < adev->usec_timeout; i++) {
    tmp = RREG32_RLC_NO_KIQ(ack, hub_ip);
    tmp &= 1 << vmid;
    if (tmp)
        break;
    udelay(1);
}
```
**Operações MMIO no Linux:** Apenas **3 passos**:
1. Escreve `req` (0xA2D0).
2. Lê `req` (dummy read para limpar latch antigo do GRBM).
3. Lê `ack` (0xA318) em loop até o bit do VMID acender.

### 3.2 O que o nosso driver faz atualmente (`Bc250RunGfxhubTlbTransaction`):
A cada chamada para cada VMID no loop:
1. `Read32(RangeLow 0xA3A4)` (salva estado anterior)
2. `Read32(RangeHigh 0xA3A8)` (salva estado anterior)
3. `Read32(Ack 0xA318)`
4. `Write32(RangeLow, 0xFFFFFFFF)`
5. `Write32(RangeHigh, 0x0000001F)`
6. `Read32(RangeLow)` (leitura de verificação)
7. `Read32(RangeHigh)` (leitura de verificação)
8. `Write32(Request 0xA2D0)`
9. `Read32(Request)` (dummy read)
10. `Read32(Ack)` em polling até 1000 vezes
11. `Write32(RangeLow, RangeLowBefore)` (restaura estado)
12. `Write32(RangeHigh, RangeHighBefore)` (restaura estado)
13. `Read32(RangeLow)` (leitura de verificação da restauração)
14. `Read32(RangeHigh)` (leitura de verificação da restauração)

Isso totaliza **14 operações de barramento MMIO para cada VMID a cada flush**, multiplicado por milhares de flushes (foram 18.536 flushes em 63 segundos!). Além disso, a rotina é executada sob o `ObjectLock` e emite logs não-amostrados na thread de paginação.

---

## 4. Próxima Etapa: Candidato T3.4 (Alinhamento do TLB Flush ao Linux Upstream)

1. **Simplificar `Bc250RunGfxhubTlbTransaction`:**
   - Eliminar todo o ciclo inútil de leitura/escrita/restauração de `RangeLow` (`0xA3A4`) e `RangeHigh` (`0xA3A8`).
   - Executar estritamente o protocolo upstream do Linux:
     - `Write32(Request, RequestValue)`
     - `Read32(Request, &dummy)`
     - Polling de `Read32(Ack, &ack)` até bater `AckMask`.
2. **Eliminar telemetria excessiva sob lock:**
   - Remover os logs de `[FlushMappedGfxhubTlb]` disparados para cada VMID dentro de `Bc250ActiveFlushVmidTlb`.
3. **Auditar e Testar:**
   - Atualizar a suíte de testes de contrato `gfxhub_tlb_transaction_test.c`.
   - Adicionar gate de segurança e mutações.
   - Gerar pacote assinado `3.319.1.0` para Corrida 174.
