# 514 — DX.98 (rascunho de desenho): tabelas de página escritas pela GPU (`DXGK_PAGETABLEUPDATE_GPU_VIRTUAL`)

> 28/09/2026, offline, só leitura. Rascunho produzido por uma frente de pesquisa; **ainda não conferido linha a linha** contra o código. Bloqueio da Lei 7: o pacote SRBM_WRITE do SDMA não tem fonte local. Tudo aqui é NÃO VALIDADO EM HARDWARE.

# DX.98 — page tables written by the GPU (`DXGK_PAGETABLEUPDATE_GPU_VIRTUAL`): design (offline, read-only)

Confidence tags: **P1** means quoted from a local source. **H** means hypothesis, or upstream recalled from memory with no local copy. **P0** is used only where earlier runs are already on file.

## 1. The WDDM contract

**P1, `d3dkmddi.h` (10.0.26100.0).**

- Lines 2055–2070 define the mode enum:
  `DXGK_PAGETABLEUPDATE_CPU_VIRTUAL, _GPU_VIRTUAL, _GPU_PHYSICAL`.
- The address is a **union**: `{ PVOID CpuVirtual; D3DGPU_PHYSICAL_ADDRESS GpuPhysical; D3DGPU_VIRTUAL_ADDRESS GpuVirtual; }`. In GPU_VIRTUAL mode there is **no CPU pointer**.
- Lines 4693–4710 define `_DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE`:
  `PageTableLevel, hAllocation, PageTableAddress, pPageTableEntries, StartIndex, NumPageTableEntries, Reserved0, Flags, DriverProtection, AllocationOffsetInBytes, hProcess, UpdateMode, pPageTableEntries64KB, FirstPteVirtualAddress`.
- `UpdateMode` arrives **per operation**.
- Line 4685 defines FLUSH_TLB: `RootPageTableAddress, hProcess, StartVirtualAddress, EndVirtualAddress`.
- Line 4880 shows that `DXGKARG_BUILDPAGINGBUFFER` carries `pDmaBufferPrivateData/DmaBufferPrivateDataSize` and `MultipassOffset`.

**P1, `referencias/windows-driver-docs/.../system-paging-process.md`.**

- The paging process's own page tables, at init and power-on: *"the PageTableUpdateMode is forced to CPU_VIRTUAL and must be completed immediately using the CPU (the paging buffer should not be used)."*
- For everything else: *"Updates of the page table entries for all other processes are done using the PageTableUpdateMode specified by the driver. These updates are done in the context of the paging process."*

**P1, `examples.md` §"Updating page table entries of a process".** The sequence in one paging buffer is:

1. VidMm maps process P's page tables into the paging-process VA.
2. UpdatePageTable on the paging process's page tables.
3. `FlushTLB(PagingProcessRootPageTable)`.
4. UpdatePageTable on P.
5. `FlushTLB(P root)`.
6. Submit in the paging context.

**Consequences:**

- **P1.** The handler must support **both** modes: CPU_VIRTUAL (init/power-on) and GPU_VIRTUAL (everything else).
- **H, strong.** In GPU_VIRTUAL mode, `PageTableAddress.GpuVirtual` is an address inside the **paging-process VA**, which is VMID8 for us. It is the same address class as `FillVirtual.DestinationVirtualAddress`, which SDMA already writes under VMID8 with the tripod proven (P0, runs of 07/09 and 10/09).
- **P1.** A FLUSH_TLB can sit **between** writes and a transfer that uses them, inside the **same** buffer. So the invalidation has to go into the stream, in order.
- `Flags.Repeat` means a single PTE repeated `NumPageTableEntries` times. The code already handles this at `bc250_mcdm_callbacks.c:704`.
- Multipass: use the same mechanism as `TransferVirtual`/`FillVirtual` (`bc250_mcdm_callbacks.c:1117-1195`), with `MultipassOffset` counting finished entries and `STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER`.

## 2. SDMA packets

**Existing emitters** (`driver/core/bc250_sdma_packet_builder.h:84-190`): `Nop, CopyLinear, CopyLinearSubWindow32, WriteLinear, ConstFill, Fence, Fence64, Gcr, Indirect, Trap, PollRegMem`. There is **no SRBM_WRITE and no PTEPDE**. The opcodes defined at `:20-29` go up to 17 (GCR), without 12 or 14.

**Writing PTEs: use WRITE_LINEAR.** `Bc250SdmaEmitWriteLinear` (`bc250_sdma_packet_builder.c:184`) is the packet the repo already emits, and it has provenance.

- Header is 4 dwords, and our cap is 64 dwords per packet (`:195`). That is **32 PTEs per packet**.
- Buffer cost:
  - one leaf (512 entries, `LEAF_INDEX_BITS=9`, `bc250_gpuva_space_contract.h:128`) takes 16 packets, about 4.3 KB;
  - a full root (8192 entries) takes about 70 KB, so it needs multipass.
- `Repeat` is encoded by repeating the pair of dwords. CONST_FILL cannot do it, because the pattern is 32 bits.
- **H, upstream from memory.**
  - `sdma_v5_0_vm_write_pte` emits exactly `SDMA_OP_WRITE|SUBOP_WRITE_LINEAR, pe_lo, pe_hi, ndw-1, {lo,hi}*`.
  - `sdma_v5_0_vm_set_pte_pde` uses `SDMA_OP_PTEPDE` (10 dwords: dst, mask, addr, incr, count-1) for contiguous VRAM.
  - `amdgpu_vm_sdma_update` picks write_pte for GART/system pages and set_pte_pde for contiguous runs.
- PTEPDE is **not** in the repo. The macOS kext names it (`SDMA_SUBOP_PTEPDE_GEN`, doc 512 §2, P1 by symbol only), but `navi10_sdma_pkt_open.h` is not in `inc/linux_ref/`. Under Lei 7 it is **not** used in v1. It is an optimization for later.

**FLUSH_TLB in the stream.** H, upstream from memory, consistent with doc 344:66-69:

- `sdma_v5_0_ring_emit_vm_flush` calls `amdgpu_gmc_emit_flush_gpu_tlb`, which calls `gmc_v10_0_emit_flush_gpu_tlb`.
- That function does `emit_wreg(PAGE_TABLE_BASE lo/hi)` and then `emit_reg_write_reg_wait(INV_ENGx_REQ, INV_ENGx_ACK, req, 1<<vmid)`.
- On SDMA:
  - `emit_wreg` is `SDMA_OP_SRBM_WRITE` (byte-enable 0xF, reg, val);
  - `emit_reg_wait` is `POLL_REGMEM` with mem_poll=0 and func=3 (equal).
- The GFXHUB does not use a semaphore (`gmc_v10_0_use_invalidate_semaphore` returns true only for MMHUB). This matches doc 512: no `VM_INVALIDATE_ENG*_SEM` in the macOS kext.

**What we need for this:**

- **(a)** A new `Bc250SdmaEmitSrbmWrite`. Its encoding needs the header imported into `inc/linux_ref/` with provenance. This is the **Lei 7 blocker**.
- **(b)** `Bc250SdmaEmitPollRegMem(Address=ACK reg, Value=1<<vmid, Mask=1<<vmid, Func=3, MemPoll=0, …)`. This emitter already exists.
- **(c)** The REQ value comes from the single source `Bc250PagingBuildGfxhubTlbFlushPlanForEngine` (`driver/core/bc250_paging_contract.c`):
  - `RequestValue=(1<<vmid)|(FlushType<<16)|0x00F80000`;
  - `REQ=0xA28C+4n`, `ACK=0xA2D4+4n`.
- **(d)** An invalidation engine of its own for SDMA0.
  - Engine 17 belongs to the CPU flush (`:111-114`) and engine 0 to the graphics ring (DX.32).
  - The function accepts only 0 and 17, and only those two have `ADDR_RANGE` programmed (`bc250_gcvm_global_mmu.c`, `…_global_mmu.h:47-50`).
  - Proposal: engine 1. Program `RANGE_LO=0xA324`/`HI=0xA328` (formula `0xA31C+8n`, P1 via `gc_10_1_0_offset.h`, `mmGCVM_INVALIDATE_ENG0_REQ=0x1643`), with readback.

**Order inside the paging buffer:**

1. WRITE_LINEAR*.
2. `Bc250SdmaEmitGcr` with GL2 writeback (**H**, conservative: the UTCL2 walker must not see stale data).
3. SRBM_WRITE REQ.
4. POLL_REGMEM ACK.

**H, open risk:** whether SDMA 5.0 executes SRBM_WRITE **inside an IB**. Linux emits it only on the ring.

- **Fallback that is already viable:** FLUSH_TLB writes, through the serial encoder→submit channel (`bc250_mcdm_callbacks.c:867-900`, `Bc250PagingTakeProof`), a list of split offsets.
- `SubmitCommandVirtual` then emits `INDIRECT(seg0) + ring flush + INDIRECT(seg1)…`, which is Linux's model (flush on the ring).

## 3. What depends on CPU-written tables and has to change

1. **`bc250_mcdm_callbacks.c:720` (header check).** It rejects `UpdateMode != CPU_VIRTUAL` and `CpuVirtual==NULL`. Split it into two branches:
   - CPU (unchanged);
   - GPU: requires `GpuVirtual != 0`, 8-byte alignment, `pDmaBuffer`, and budget/multipass.
   The fallback write loop at `:783-799` becomes CPU-only.
2. **`bc250_mcdm_callbacks.c:2728` (GPUMMUCAPS).** Set `PageTableUpdateMode` from the tunable `PageTableUpdateGpu`: default 0, fail-closed, `present==1 && value==1`, read before the QAI. This is the same pattern as `TdrInduceHang`/`SdmaPlanoAFallback`, and must be read on **both** sides of `#if UNSAFE_STARTDEVICE`. Three past defects came from a field written in only one branch (A13/B4/ContextAbi).
3. **`bc250_active_miniport.c:15174` `Bc250ActiveUpdatePageTableHandler`.**
   - Staging into the journal (`Bc250GpuvaJournalStageLeafUpdate`, around `:15419`) uses `pPageTableEntries`, not VRAM, so it **stays**.
   - The write at `:15455-15488` (`destination[...] = encoded`) becomes a WRITE_LINEAR emit.
   - The PDE tripwire (`:15478`) checks the encoding before the write, so it stays.
   - The DX.17 readback of index 4096 (`:15530+`) reads VRAM **right after** the write. With GPU writes it would read the old value. **Move it to retire.**
4. **`Bc250ActiveFlushTlbHandler` (`:14623`).**
   - Today it calls `Bc250ActiveFlushMappedGfxhubTlb` (`:17201`): HDP flush plus CPU MMIO invalidation of **every** VMID in the pool, at build time.
   - In GPU mode it should emit REQ/ACK for **the VMID of the flushed root** into the paging buffer instead. VMID8 when `RootPageTableAddress` is the paging root; otherwise the VMID the pool resolves for `hProcess`. A root with no VMID installed needs no flush: install at submit already invalidates (DX.95).
   - `canCommitJournal`/`Bc250GpuvaJournalCommitFlush` keep running at build time, but become **"VidMm intent"**.
   - **H, strong:** without hardware scheduling, VidSch only calls `SubmitCommandVirtual` for a user context after that context's wait on the paging fence is satisfied. So user consumers of the snapshot (`NODE1_IB` walk at `:13476/:14152`, present resolve) see PTEs that have already landed.
   - Add `ExecutedGeneration`: the generation published at build travels in the paging proof and is marked executed in `Bc250ActiveSdmaRetireAndNotify`.
5. **Paging submit walks (`:13091-13150`).**
   - `ValidateGpuVaRange("IB")` covers the paging buffer VA, which is mapped by the paging process's system page table (CPU, at init). It **stays**.
   - `ValidateGpuVaRangeEnds("SRC"/"DEST")` reads VRAM under VMID8 **before** the IB runs. If the same buffer has just remapped the scratch area, it reads the old PTE, refuses, and the path goes `STATUS_IO_DEVICE_ERROR`, then 0x119.
   - **Change:** for a paging submission whose proof says "contains its own UpdatePageTable on the paging root", validate SRC/DEST against the **staged model** (entries emitted in that same buffer), and mark the VRAM walk as INCONCLUSIVO, never as refusal.
6. **Paging-process tables.** Init and power-on still arrive as CPU_VIRTUAL (P1). The branch from item 1 covers this. Log `Mode=` on the first 8 operations to prove both modes occurred.
7. **SetRootPageTable (`:4721`, DX.95 `:4826-4851`).** It only records the root, and the VMID is installed at submit with a CPU flush (`:13007-13020`). **Unchanged.** Before the switch, confirm that install at submit happens with the SDMA ring idle, or serialize it on `TlbLock`: CPU engine 17 and SDMA engine 1 are different engines, which is fine.
8. **Tripod.** The paging IB tripod stays as it is: RPTR==WPTR, fence on poison, canaries (`Bc250PlanDataProofCanaries`).
   - **New leg 3 for PTEs (Lei 2):** at retire, the CPU reads **1 sampled PTE per buffer** (the first and last written) and compares it with `encoded`, which the proof carries. Log `PTE_GPU_PROOF match/diverge`.
   - Zero on zero, as with an invalid PTE, is INCONCLUSIVE. Prefer sampling a valid PTE.
9. **HDP flush before invalidation** (in `FlushMappedGfxhubTlb`). It exists for CPU writes through the BAR. It does not apply on the GPU path.
10. **Logging on the paging thread** (memory note: logging on that thread stalls VidMm). The new UpdatePT/FlushTLB paths log only to the ring buffer or sampled output, never as file I/O.

## 4. Risks and staged plan

**Risks:**

- **(R1)** SRBM_WRITE refused inside an IB (H). The ring-split fallback is ready.
- **(R2)** GpuVirtual is not VMID8 VA as assumed (H). The first bind proves it or refutes it: fault on VMID8 against a WRITE_LINEAR destination.
- **(R3)** GL2/UTCL2 coherence (H), handled with GCR.
- **(R4)** Large paging buffers or multipass bugs, which lead to 0x10E/0x119.
- **(R5)** Invalidation engine 1 without a programmed range. That would mean an ACK that never arrives, then POLL timeout, then TDR.
- **(R6)** Caps cannot be changed after bind. The switch is all-or-nothing per bind, so the tunable is mandatory for rollback.

**Offline, before any run:**

- **(a)** `sdma_packet_builder_test`: WriteLinear with 32 PTEs, Repeat, and multipass boundaries. Also SrbmWrite/PollRegMem REQ/ACK against the plan's values, once the header has provenance.
- **(b)** `active_miniport_test`: UpdatePT with `UpdateMode=GPU_VIRTUAL` emits N dwords, **does not** touch a CPU pointer, and resumes by `MultipassOffset`. CPU_VIRTUAL stays identical.
- **(c)** FlushTLB in GPU mode emits REQ/ACK **after** the writes and makes **zero** CPU MMIO calls (stub counter).
- **(d)** An SRC/DEST walk with staged in-buffer mappings does not refuse.
- **(e)** Audit gates:
  - no CPU PTE write in the GPU branch;
  - the tunable is read on both sides of the `#if`;
  - mutations proving each gate.
- **(f)** Engine 1: program its range with readback in `bc250_gcvm_global_mmu.c`, with a test.

**Smallest hardware step (one run, cold boot):** package with `PageTableUpdateGpu=1`, everything else identical to the last stable candidate. Criterion written beforehand (Lei 10):

- **A — pass:**
  - GPUMMUCAPS log `PageTableUpdateMode=1`;
  - `[UpdatePT] Mode=0` only during init and `Mode=1` afterwards;
  - `PTE_GPU_PROOF match` on ≥ 95% of samples, with 0 diverge;
  - 0 `IB_TRIPOD TIMEOUT`, 0 GFXHUB_FAULT/SENTINELA on VMID8;
  - CommitVidPn and a visible desktop for 180 s (the Run 118–126 baseline);
  - zero CPU writes to PTEs after init (counter).
- **B — invalidation in the IB refused:** POLL_REGMEM on ACK times out or `ERR_LOG`≠0 right after SRBM_WRITE, with the PTE_GPU_PROOF of the writes matching. That means R1; activate the ring split.
- **C — wrong address space:** GFXHUB fault with ADDR in the `GpuVirtual` range of the WRITE_LINEAR under VMID8, and 0 matches. That means R2.
- **D — coherence:** PTE_GPU_PROOF matches, but a fault or dummy-page access occurs in the next user IB. That means R3 (GCR/HDP).
- **E — something outside the map:** describe what is observed before judging it.

**Stop condition:** 2 runs without A trigger rollback to CPU_VIRTUAL (tunable 0) and a return to DX.97. That result is still information about the `FLUSH_TLB` stall.

**Afterwards:** add PTEPDE_GEN (once the header has provenance) to cut the cost of large mappings, then measure 600 s.

Everything above is **NÃO VALIDADO EM HARDWARE**.
