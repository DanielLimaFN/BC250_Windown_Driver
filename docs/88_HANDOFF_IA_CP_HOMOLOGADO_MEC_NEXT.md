# HANDOFF DE ENGENHARIA — PASSAGEM DE BASTÃO PARA A PRÓXIMA IA

> **Data**: 2026-08-27  
> **Target**: AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 11 x64  
> **Versão de Saída Homologada**: **Candidato 1.83.0.0 / SchemaVersion 90**  
> **Próxima Missão**: **Candidato 1.84.0.0 — Fechamento do HDP Flush e Status 0 no Async Compute (MEC1 / HQD)**

---

## 1. ESTADO CONSOLIDADO DO SILÍCIO (EVIDÊNCIA FÍSICA)

O driver ativo no Windows 11 (`bc250mcdm.sys`) atingiu o tripé de validação de silício para as duas engines primárias:

```text
BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS (Exit Code 0)
  published INF: oem19.inf
  device service/code: bc250mcdm / 43
  driver hash matches package: True
  TLB: status=0 vmid=8 engine=17 ackBefore=00000100 ackAfter=00000100 polls=1 restore=1
  GCVM inert: backing=0 vmid8=0 pt=000000045FBBC000 readback=1/1 restore=1/1
  PSP ring: status=0 pa=7E412000 createAck=80020000 destroyAck=800C0000 restore=1
  PSP SDMA FW: status=0 bytes=33536 type=9 fence=1/1 polls=3 response=0 checksum=1486248B destroy/restore=1/1
  PFP direct: status=0 mc=000000F40FAAE000 staged=65792dw/0mismatch JT=128 version=94 JT_STAT=FF0303
  PFP physical: PC=00000000->000018ED->00000284 transitions=2/4 HALT=15000000->00000000->15000000 fault=00000000->00000000 restore=1/1
  CP triad PCs: PFP=00000284 ME=00000000->000004A1 CE=00000000->000005CB
  CP PM4 ring: active=0->1->0 RPTR/WPTR=0->15/16 fence=43DAFFFE->BC250123
  SDMA F32: status=0 pc=455->512 transitions=15 readback/restore=1/1
  SDMA ring: status=0 RPTR-mmio/wb/WPTR=68/52/68 fence=43DAFFFE->BC250123 data=4dw/0mismatch polls=1 restore=1/1
```

### Provas Irrefutáveis Gravadas no Hardware
1. **SDMA 0 Engine**: `status=0`, `RPTR=68/68`, fence física `0x43DAFFFE -> 0xBC250123`, dados 4/4 DWORDs sem mismatch.
2. **CP Ring 0 (Command Processor)**: `status=0`, `RPTR=15/16`, fence física `0x43DAFFFE -> 0xBC250123`, `PhysicalReady = 1`.
3. **Microengines do CP**: `JT_STAT = 0x00FF0303` (MEC=0xFF, CE=0x03, PFP=0x03), PCs de produção (`PFP=0x0284`, `ME=0x04A1`, `CE=0x05CB`).
4. **Auditoria de Produção**: `tools/audit-bc250mcdm-failclosed-binding.ps1` retorna **PASS** com Exit Code 0.

---

## 2. DESCOBERTAS CRÍTICAS DE HARDWARE EM `referencias/bc250-gfx1013-fix`

O repositório em [`referencias/bc250-gfx1013-fix`](file:///c:/Users/Daniel/Desktop/NamixWDDM/referencias/bc250-gfx1013-fix) revelou informações vitais para a próxima IA:

1. **Guarda de GFXOFF (`COMPUTE_GFXOFF_GUARD`)**:
   * O hardware BC-250 congela filas de computação assíncrona se o GFXOFF estiver ativo.
   * **Obrigatório**: Antes de ativar o anel HQD/MEC, enviar `PPSMC_MSG_DisallowGfxOff = 0x15` no SMN `0x03B10A20`. Reativar com `PPSMC_MSG_AllowGfxOff = 0x14` apenas na limpeza.
2. **Bug de Threadgroup no ACE (`has_async_compute_threadgroup_bug`)**:
   * O chip falha dispatches que utilizam `PARTIAL_TG_EN` com dimensões de threadgroup.
   * **Obrigatório**: Forçar os dispatches de compute assíncrono para o modo **thread-dimension**.
3. **Registradores de Doorbell do MEC**:
   * Os registradores de Doorbell do MEC são `mmCP_MEC_DOORBELL_RANGE_LOWER` (byte `0x77F0`) e `mmCP_MEC_DOORBELL_RANGE_UPPER` (byte `0x77F4`).
   * Quando `props.UseDoorbell = 0u`, o hardware aceita o `WPTR` diretamente por MMIO em `mmCP_HQD_PQ_WPTR_LO` sem depender da abertura BAR2.
4. **Unlock de 40 CUs (Duggasco)**:
   * Sequência: `CC_GC_SHADER_ARRAY_CONFIG = 0`, `SPI_PG_ENABLE_STATIC_WGP_MASK = 0x1F`, `RLC_PG_ALWAYS_ON_WGP_MASK = 0x1F`. Salto de 302 tok/s para 466 tok/s.

---

## 3. PLANO EXATO PARA O CANDIDATO 1.84.0.0 (PRÓXIMA IA)

O ensaio físico do Candidato 1.83.0.0 comprovou que o banco de registradores do HQD programa perfeitamente e ativa (`CP_HQD_ACTIVE: 0 -> 1 -> 0`). Para conquistar o **`status=0`** na computação assíncrona, a próxima IA deve aplicar a receita comprovada no Ring 0:

1. **Flush de Coerência HDP Obrigatório**:
   - Em `Bc250ComputeRunActiveHqdSubmissionTransaction` (`driver/core/bc250_compute_mqd_contract.c`), logo após gravar os 16 DWORDs no `Buffers->RingVirtual` e antes de escrever o `WPTR`:
     ```c
     /* Flush HDP e invalidação L2 idênticos ao Ring 0 (bc250_pfp_direct_load.c:899) */
     (void)Bc250HqdActiveWrite(Interface, Evidence, 0x000086D0u, 1u); /* HDP_MEM_COHERENCY_FLUSH_CNTL */
     (void)Bc250HqdActiveWrite(Interface, Evidence, 0x00028208u, 3u); /* GCVM_L2_CNTL2 */
     (void)Bc250HqdActiveWrite(Interface, Evidence, 0x00028210u, 1u); /* GCVM_INVALIDATE_ENG0_REQ */
     ```
2. **Ativação de WPTR Polling no MEC**:
   - Em vez de zerar `CP_PQ_WPTR_POLL_CNTL` (`0x0000788Cu`), programar `0x00403004u` (polling ativo no shadow memory WPTR) exatamente como no Ring 0.
3. **Build, Assinatura e Ensaio**:
   - Bump para `08/27/2026,1.84.0.0` nos arquivos `bc250mcdm.inf`.
   - Compilar (`driver\wdk\build-contract.bat`), assinar (`tools\build-bc250mcdm-package.ps1`) e orientar o usuário a rodar `tools\replace-bc250mcdm.ps1`.

