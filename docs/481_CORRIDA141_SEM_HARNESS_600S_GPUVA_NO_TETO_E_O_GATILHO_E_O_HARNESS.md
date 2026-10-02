# 481 — Corrida 141 (DX.89b sem o harness): 600 s sem trava, GPUVA preso no teto de 1.024, e o gatilho da trava é o harness sobre o estado grande

> **Corrida:** 27/09/2026. Bind às 17:22:27 após boot frio; rollback online às 17:32:28–17:32:43 (`RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em 5 amostras). **Candidato:** DX.89b / 3.295.1.0, o mesmo das 139/140, **sem o harness D3D11** (`BC250_SKIP_D3D11_HARNESS=1`).
> **Evidência:** `docs/evidence/2026-09-27_17-22-27-DESKTOP-HQAG6T4-lab-binding` (log `-FINAL` de 77,5 MB; logs `-FINAL` da UMD); rastro a partir do offset 83.614.496.
> **Relato do Daniel:** "desktop visível".

## 1. Relato obrigatório
**1. Houve avanço?** Sim. Funcionalmente, pela primeira vez houve 600 s com a textura compartilhada funcionando e o Explorer sem quedas. Em diagnóstico, a pergunta das 139/140 foi respondida.

**2. Sucessos (P0, exceto o visual):**
- 600 s sem trava; desktop visível.
- Paginação 112.084/112.084; 1.955 Presents no KMD, 5 `BYTE_PROOF PASS`; DWM com 1.416 Presents `hr=0`.
- Zero TDR, fault, `InsideIb`, `Blt Timeout`, `IB_TRIPOD TIMEOUT`, `DEVICE_REMOVED` ou `StopDevice`.
- **Explorer: uma instância (pid 4908) a janela inteira, zero quedas no Event Log.** 4 texturas compartilhadas com `AllocateHr=0` e `Flags=0`.

**3. Falhas:**
- **N atingiu o teto de 1.024 faixas por volta de 5 min e ficou preso ali até o teardown.** Houve 446 `NO_SPACE`.
- A partir das 17:26:24, **460 `vkAllocateMemory` do Explorer falharam com `res=-2`** (`VK_ERROR_OUT_OF_DEVICE_MEMORY`). O Explorer está vivo, mas passou a ter recursos recusados. A ligação dessas falhas com o teto é temporal (P2); o mecanismo dentro da RADV não foi lido.
- 2 `Draw: RECUSADO` (`SRV_ALEM_DO_SUBRECURSO_CARREGADO`, gradiente 1024x1) e 4 `Upload LOCK falhou`.
- 1 LiveKernelEvent 193/804 (recorrente e não decodificado). TextInputHost caiu às 17:32:35, durante o rollback.

**4. Veredito frente ao critério do doc 480 §5: C.**
- Assinatura: 600 s sem trava, com N crescendo até o fim. N parou em 1.024 porque bateu no teto, não porque estabilizou; por isso **não é A**.
- Implicação: o crescimento sozinho não trava a máquina. **N passou de 1.000 sem o harness**, contra ~780 com o harness nas 139/140. Logo, a trava exige o harness sobre esse estado.

**5. Recuperação e pendências:**
- BasicDisplay Código 0 no mesmo boot; `CLEAN_BOOT_REQUIRED` armado.
- **NÃO VALIDADOS EM HARDWARE:** a causa da trava com o harness e o Explorer sem recursos recusados.

## 2. O que isto estabelece

| Corrida | Textura compartilhada | Harness | N | Resultado |
|---|---|---|---|---|
| 136 | recusada | sim | 243–360 | 600 s |
| 139 | aceita | sim | 290 → ~780 | trava ~6 s depois do início do harness |
| 140 | aceita | sim | 290 → ~770 | trava ~6 s depois do início do harness |
| **141** | **aceita** | **não** | **16 → 1.024 (teto)** | **600 s** |

- **(P0)** Um N alto não trava por si só.
- **(P0)** O harness sobre um N baixo não trava (136).
- **(H)** A trava vem do harness submetendo trabalho enquanto o journal é grande. Nas duas travas o último estágio do rastro foi o **commit do journal (44), com N≈775**, na thread de paginação.
  - Candidato a ler: a interação entre esse commit e o caminho de submissão do Node 1 do harness (gate do journal por processo, `Node1JournalGatePerProcess=1`, e o snapshot de GPUVA fixado na submissão).
  - Uma espera circular em IRQL elevado entre duas CPUs explicaria a máquina inteira parar sem nenhum acesso MMIO em andamento. Isso não está provado.

## 3. Problema de produto que apareceu
Um Explorer normal esgota as 1.024 faixas em ~5 min. Depois disso a UI passa a ter alocações recusadas. O teto voltou a 1.024 na DX.85 por custo de commit. Aumentá-lo, ou reduzir faixas por recurso (hoje cada recurso gasta uma alocação WDDM e uma memória Vulkan própria), é trabalho necessário. Precisa ser medido depois de resolver a trava com o harness.

## 4. Próximo passo (offline, sem corrida)
1. Ler o caminho `SubmitCommandVirtual` do Node 1: gate do journal, snapshot e cobertura. Ler também o commit do journal no `FLUSH_TLB` (estágio 44): locks, IRQL e custo O(N). Procurar espera cruzada.
2. Ler por que `vkAllocateMemory` devolve `-2` na RADV do repositório de Mesa, e confirmar se é o teto de GPUVA.
3. Só depois, propor candidato e critério da Corrida 142.

## 5. Leitura offline (27/09, depois da 141)
**(a) O store de snapshot do GPUVA não trava (P1, `driver/core/bc250_gpuva_snapshot_contract.c`).**
- O leitor (`Bc250GpuvaSnapshotAcquire`, chamado da submissão, até DISPATCH) e o publicador (`Bc250GpuvaSnapshotPublishBatch`, dentro do commit do `FLUSH_TLB`, sob o push lock `ObjectLock`) tentam no máximo 16 vezes e desistem com `BUSY`. Nenhum dos dois espera em laço pelo outro.
- A espera cruzada nesse ponto está **descartada**.

**(b) As duas travas terminam com a mesma sequência no KMD (P0, logs das 139 e 140):**
1. `SubmitVirtual Node1 GFX` do harness: IB de 288 DW em `VA=0x240030000`, troca VMID 1→3, `VMID_SWITCH_VM_FLUSH` #28/#29, `WPTR_READBACK PASS`, `SUBMIT SUCESSO`;
2. `COMPOSITE_BLT OK` 1280x720 com prova 5/5;
3. `Present #N` em ~670 µs;
4. flush de TLB de VMID do pool;
5. fim.

Nenhum erro, recusa ou timeout. Os logs da UMD param no mesmo instante em todos os processos. A última gravação BCR1 não mostra acesso a registrador nem submissão em andamento, mas é feita a cada 100 ms e pode anteceder a trava.

**(c) As falhas `-2` do Explorer não vêm do KMD.**
- Zero recusas do KMD na 141.
- Na RADV (`winsys/wddm2/radv_wddm2_bo.c`), falhas de `CreateAllocation`, `MapGpuVirtualAddress` e residência viram todas `VK_ERROR_OUT_OF_DEVICE_MEMORY`. O stderr da RADV se perde no Explorer.
- A DX.59 mostra `SegSet=0x1 Preferred=0x1`: os recursos só podem morar no segmento local (~245 MB).
- **H:** o VidMm esgota o segmento local e não tem para onde despejar. Isso é o item "memória dinâmica / segmento de sistema" da checklist.

## 6. Próximo passo proposto
**DX.90, somente instrumentação, sem mudar comportamento.**
- **O quê:** a thread do rastro (PASSIVE, 100 ms, grava direto em disco) passa a ler, a cada período, só registradores com procedência no doc 03: `GRBM_STATUS` (0x08010), `CP_STAT` (0x08680), `CP_RB0_RPTR` (0x08700) e `CP_RB0_WPTR` (0x0C150). Cada leitura fica envolvida por migalha própria, e o resultado vai num registro novo, gravado junto do BPX6/BCR1. Também lê a fence gráfica em memória.
- **Pergunta que responde:** o que o GPU estava fazendo nos 100–300 ms antes da trava.
  - anel gráfico parado com o CP ocupado → trava de GPU vindo antes da trava da máquina;
  - leitura em andamento que nunca retorna → o barramento parou de responder;
  - tudo ocioso e normal → o problema está do lado da CPU.
- **Risco:** 40 leituras de registrador por segundo, somente leitura. `GRBM_STATUS` e `CP_STAT` não dependem de `GRBM_GFX_INDEX`.

**Depois disso:** declarar o segmento de memória do sistema no `SupportedSegmentSet` dos recursos, que é a frente de produto do teto e das falhas `-2`.
