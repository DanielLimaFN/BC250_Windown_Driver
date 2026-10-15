# 618 — P6 (MTYPE_UC nos BOs coerentes da RADV) e EOP do CP (conclusão do GFX pela interrupção), e critério da Corrida 234

**Origem:** a Corrida 233 (doc 617) tirou a P3 inteira das causas suficientes da trava dura. Os próximos itens do inventário de paridade (doc 593 §4, PLANO_MESTRE §3.3) são a P6 e o EOP do CP. Por decisão do Daniel, os dois vão **juntos** nesta corrida.

## 1. P6 — MTYPE_UC na folha dos BOs coerentes da RADV

**Como o Linux faz (P1, `inc/linux_ref/PROVENANCE_P6_CP_EOP.txt`):**
- `radv_amdgpu_bo_va_op` mapeia todo BO com `RADEON_FLAG_GL2_BYPASS` com `AMDGPU_VM_MTYPE_UC`.
- `gmc_v10_0_get_vm_pte` leva esse MTYPE para a PTE: `3 << 48`.
- O default de todo o resto é NC (0).

**Na nossa RADV, quem tem `GL2_BYPASS`:**
- tipos de memória `DEVICE_COHERENT`/`UNCACHED_AMD`;
- eventos;
- semáforo de gang;
- o BO de IB concatenado (`radv_wddm2_cs.c`);
- o destino linear do blit de apresentação.

Até aqui o KMD não sabia disso e codificava todas as folhas como NC.

**O que muda:**

| Onde | Mudança |
|---|---|
| RADV (`radv_wddm2_bo.c`) | declara `BC250_ALLOCATION_FLAG_RADV_GL2_BYPASS` (`0x20`) no blob BCA1 quando o BO tem `GL2_BYPASS` |
| Contrato de alocação | a flag entra em `RADV_ONLY_FLAGS`: só vale em token da RADV e é recusada em classe da UMD |
| Registro de objetos | campo `AllocationAbiFlags`, gravado no `CreateAllocation` |
| `Bc250GcvmEncodePte` | flag de software `BC250_PTE_MTYPE_UC` → bits 50:48 = 3. O PDE continua recusando qualquer coisa além de VALID/SYSTEM/SNOOPED |
| `Bc250McdmEncodeVidMmPteWithLevelEx` | aceita só esse bit extra, e só na folha, em qualquer segmento (o Linux não olha o domínio) |
| UpdatePageTable `GPU_VIRTUAL` | o `WRITE_LINEAR` pergunta ao miniport (`Bc250ActivePteLeafFlagsQuery`, ObjectLock compartilhado, sem log sob o lock) se a alocação tem a flag |
| UpdatePageTable `CPU_VIRTUAL` | aplica a mesma regra pelo registro já lido |

- **Chave:** `BC250_P6_MTYPE_UC_COHERENT=1`.
- **Telemetria:**
  - `[P6_MTYPE] folha UC #n: hAlloc Token Bytes AbiFlags alocacoesGL2` (8 primeiras + 1 em 1024);
  - `[P6_MTYPE] UpdatePT #n com MTYPE_UC: ... folhasUC totalUC`.

**Acoplamento declarado:** a RADV nova (`C6BEC54B…`) só funciona com KMD 3.381+. Um KMD anterior recusa a flag `0x20` (`ABI_PRIVADA_RECUSADA_PELO_PARSE`) e as alocações da RADV falham. Para voltar a um candidato antigo, voltar também a RADV, cuja cópia está em `build\src\amd\vulkan\vulkan_radeon.dll.pre-p6-80EBB248`. O BasicDisplay não usa a RADV.

## 2. EOP do CP — conclusão do anel gráfico pela interrupção

**Como o Linux faz (P1, `gfx_v10_0.c` v6.6):**
- `gfx_v10_0_ring_emit_fence` emite `RELEASE_MEM` com `INT_SEL=2` quando a fence pede interrupção.
- `gfx_v10_0_set_gfx_eop_interrupt_state` liga `CP_INT_CNTL_RING0.TIME_STAMP_INT_ENABLE`.
- `gfx_v10_0_eop_irq` recebe o vetor cliente `0x14`, fonte `0xB5`, com me 0 e pipe 0, e chama `amdgpu_fence_process(gfx_ring[0])`.

**O que muda (chave `BC250_CP_EOP_IRQ=1`):**
- **Arme** (`Bc250ActiveCpEopIrqArm`):
  - só depois de `[P3_IH] ATIVO`;
  - snapshot do `CP_INT_CNTL_RING0` (`0xC1A8`, catálogo docs/03);
  - muda só o bit 26 (Lei 6);
  - readback;
  - se divergir, restaura e não arma.
- **Submit do GFX:** `RELEASE_MEM` com `INT_SEL=2` só com o arme conferido; sem ele, `INT_SEL=0` como antes. A forma provada do pacote não muda, só o campo INT_SEL.
- **ISR do IH:** conta `CP_EOP` de me 0/pipe 0 pelo `ring_id`. O ISR do miniport enfileira o **mesmo KDPC do timer do GFX** (`RetirementTimerDpc` → `Bc250ActiveRetireAndNotify`, como o `PreemptCommand` já faz). Nenhuma notificação ao dxgkrnl sai do ISR.
- **Desarme** antes de o IH sair: o bit volta ao snapshot. Linha `[CP_EOP] DESARMADO` com os contadores.
- **Timer:** o periódico de 1 ms continua igual, porque também atende o MEC, que não tem interrupção aqui. **Diferença declarada:** o amdgpu não tem esse timer.
- **Telemetria na linha `[P3_IH] regime`:**
  - `eop_gfx`: vetores do anel gráfico;
  - `eop_emitidos`: RELEASE_MEM com INT_SEL=2;
  - `dpc_gfx_por_irq`.

## 3. Provas offline

- **Suíte e auditorias:** `build-contract` EXIT=0. Testes novos:
  - `gcvm_contract_test`: MTYPE 3<<48 e o PDE recusando;
  - `mcdm_callbacks_test`: folha UC/NC, bit estranho ignorado, PDE sem efeito;
  - `allocation_abi_contract_test`: GL2 só na RADV, `0x40` segue desconhecido;
  - `mcdm_object_contract_test`: o campo de flags.
- **Auditoria de segurança PASS.** Portões novos `audit_p6_mtype_uc_for_gl2_bypass` e `audit_cp_eop_irq`. **13 mutações reprovaram** (6 da P6, 7 do EOP), com os fontes conferidos por hash antes e depois.
- **Auditoria da RADV:** `audit-radv-wddm-safety.py` PASS. A RADV foi recompilada pelo ninja e só o objeto do BO mudou (a mudança é só em `.c`).
- **Pacote 3.381.1.0:** perfil `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, auditoria do pacote PASS. Strings `[P6_MTYPE]` e `[CP_EOP]` presentes no SYS assinado.

| Arquivo | SHA-256 |
|---|---|
| SYS | `9B9FE1B857CC4443602D9470645038528AC5FFB8B82FC8827243CA776143348A` |
| INF | `70C6E94B29B1AB742AE4F2DEC989EA8C5CB9C5BD1B3CE7B68AE318A0D14C5C96` |
| CAT | `95280FECF242524C5C41B3CB9BF256666EDDF9D8983FA0C39A7DDF2E28DA526C` |
| UMD | `8782A5E5…` (inalterada) |
| RADV (`vulkan_radeon.dll`, build) | `C6BEC54BFB2494077143EB4A58E1732956FC72E87F443047D59A537F516044CB` (o EXECUTAR copia para `C:\BC250_LIB` e confere) |

**Scripts:** `tools/prepare-corrida234-p6-mtype-e-eop-do-cp.ps1` e `tools/EXECUTAR_CORRIDA234_P6_MTYPE_E_EOP_DO_CP_600S.cmd`. O preflight a seco passou por todos os hashes e parou só na CMOS.

## 4. Critério da Corrida 234

**Pré-condição:**
- PSP OK, StartDevice `status=0`;
- `[P3B_MMHUB] LIGADA`, `[P3_IH] ATIVO`;
- `external_libs` do `_last_lab_binding.json` com a RADV `C6BEC54B…`.

**Uso:** boot frio, CMOS em 256, menu e Pesquisa como nas anteriores.

**Atribuição limitada:** são duas diferenças. Cada mecanismo tem a sua própria linha no log, então os dois são verificáveis em separado. Mas um efeito sobre a trava **não** é atribuível a só um deles.

- **A — os dois mecanismos em regime**
  - *Assinatura:* todos os sinais abaixo.
    - `[CP_EOP] ARMADO`;
    - `[P3_IH] regime` com `eop_gfx` crescendo junto com `eop_emitidos` e `dpc_gfx_por_irq` > 0;
    - linhas `[P6_MTYPE] folha UC` e `[P6_MTYPE] UpdatePT ... com MTYPE_UC`;
    - zero `GFXHUB_FAULT`, zero TDR;
    - desktop normal;
    - teardown com `[CP_EOP] DESARMADO`.
  - *Implicação:* P6 e EOP do CP fecham como mecanismo. Se a trava vier igual, os dois saem das causas suficientes e o próximo da paridade é a **P4** (SDMA1).
- **B — o EOP não chega**
  - *Assinatura:* `[CP_EOP] RECUSADO`/`NAO TENTADO`, ou `eop_emitidos` crescendo com `eop_gfx=0`.
  - *Implicação:* o CP não entrega o vetor. Suspeitos:
    1. os campos de evento da nossa forma provada de `RELEASE_MEM`, que difere da do Linux;
    2. o bit zerado por reset do CP.
    
    A conclusão segue pelo timer, sem regressão. Comparar `CP_INT_CNTL_RING0` antes e depois na linha do arme.
- **C — a P6 não chega**
  - *Assinatura:* nenhuma linha `[P6_MTYPE]` com a RADV rodando.
  - *Implicação:* a RADV em `C:\BC250_LIB` não é a nova (conferir `external_libs`), ou o `hAllocation` do UpdatePageTable não resolve no registro. Ler as linhas `[ActiveUpdatePT]`.
- **D — regressão**
  - *Assinatura:* qualquer um destes:
    - `CreateAllocation` recusado com `ABI_PRIVADA_RECUSADA_PELO_PARSE`;
    - `GFXHUB_FAULT` ou TDR do GFX novos;
    - imagem corrompida;
    - `BUSY_YIELD` em rajada ou `0x133` (tempestade de interrupção).
  - *Implicação:* desligar a chave correspondente (`BC250_P6_MTYPE_UC_COHERENT` ou `BC250_CP_EOP_IRQ`) e repetir com uma só.
- **E — fora do mapa**
  - *Assinatura:* descrever o observado antes de julgar.
  - *Implicação:* a definir.

**A trava dura continua possível.** Se vier, conta como a primeira amostra com P6 e EOP do CP.

## 5. Pendências

**NÃO VALIDADO EM HARDWARE:**
- a P6;
- o EOP do CP;
- o teardown do IH e da GART do MMHUB em regime;
- a P3.3 (RPTR do IH por doorbell).
