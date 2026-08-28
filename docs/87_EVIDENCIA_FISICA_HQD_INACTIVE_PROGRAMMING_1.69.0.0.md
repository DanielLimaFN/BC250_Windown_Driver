# MEC/HQD — Evidência Física da Programação Inativa 1.69.0.0

## 1. Resumo do Ensaio Físico

O **Candidato 1.69.0.0 (SchemaVersion 75)** foi executado no silício físico da APU **AMD BC-250** (`1002:13FE`), cumprindo integralmente a máquina de estados transacional fail-closed:

```text
[PREFLIGHT] → [ALLOC & VERIFY MC 72 KiB] → [SNAPSHOT] → [APPLY 16 HQD REGS] → [DOUBLE READBACK 16/16 MATCH] → [LIFO ROLLBACK & RESTORE] → [TEARDOWN]
```

Telemetria de registro gravada em `docs/evidence/2026-08-27_16-05-43-DESKTOP-HQAG6T4-mcdm-replace/probe-registry.json`:

| Métrica / Probe | Valor Físico Observado | Veredito |
|:---|:---:|:---|
| `HqdProgramStatus` | **`0`** (`BC250_HQD_PROGRAM_OK`) | **APROVADO** |
| `HqdProgramMemoryPassed` | **`1`** (72 KiB MC alocados, gravados e auditados) | **APROVADO** |
| `HqdProgramReadbackPassed` | **`1`** (16 de 16 registradores com correspondência exata) | **APROVADO** |
| `HqdProgramActiveZero` | **`1`** (`CP_HQD_ACTIVE` permaneceu estritamente `0`) | **APROVADO** |
| `HqdProgramRestorePassed` | **`1`** (16 registradores devolvidos ao snapshot inicial) | **APROVADO** |
| `HqdProgramMemRestorePassed` | **`1`** (Memória MC zerada e auditada) | **APROVADO** |
| `HqdProgramRestoredSelector` | **`0`** (`GRBM_GFX_CNTL` restaurado a `0x00000000`) | **APROVADO** |
| `HqdProgramPhysicalReady` | **`1`** (Prontidão física completa do gate inativo) | **APROVADO** |
| `HqdProgramMismatchMask` | **`0`** (Zero discrepâncias) | **APROVADO** |
| `HqdProgramReadCalls` | **`91`** | **AUDITADO** |
| `HqdProgramWriteCalls` | **`34`** | **AUDITADO** |

---

## 2. Comparativo Físico de Registradores Aplicados vs Readback

| Registrador | Offset BAR5 | Valor Aplicado | Readback Pass 1 | Readback Pass 2 | Status |
|:---|:---:|:---:|:---:|:---:|:---:|
| `CP_MQD_BASE_ADDR_LO` | `0xC824` | `0x00A8C000` | `0x00A8C000` | `0x00A8C000` | **MATCH (100%)** |
| `CP_MQD_BASE_ADDR_HI` | `0xC828` | `0x000000F4` | `0x000000F4` | `0x000000F4` | **MATCH (100%)** |
| `CP_MQD_CONTROL` | `0xC8AC` | `0x00000000` | `0x00000000` | `0x00000000` | **MATCH (100%)** |
| `CP_HQD_EOP_BASE_ADDR_LO` | `0xC8B8` | `0x00A9C200` | `0x00A9C200` | `0x00A9C200` | **MATCH (100%)** |
| `CP_HQD_EOP_BASE_ADDR_HI` | `0xC8BC` | `0x000000F4` | `0x000000F4` | `0x000000F4` | **MATCH (100%)** |
| `CP_HQD_EOP_CONTROL` | `0xC8C0` | `0x00000009` | `0x00000009` | `0x00000009` | **MATCH (100%)** |
| `CP_HQD_VMID` | `0xC830` | `0x00000000` | `0x00000000` | `0x00000000` | **MATCH (100%)** |
| `CP_HQD_PERSISTENT_STATE` | `0xC834` | `0x00005300` | `0x00005300` | `0x00005300` | **MATCH (100%)** |
| `CP_HQD_PIPE_PRIORITY` | `0xC838` | `0x00000000` | `0x00000000` | `0x00000000` | **MATCH (100%)** |
| `CP_HQD_QUEUE_PRIORITY` | `0xC83C` | `0x00000000` | `0x00000000` | `0x00000000` | **MATCH (100%)** |
| `CP_HQD_QUANTUM` | `0xC840` | `0x00000111` | `0x00000111` | `0x00000111` | **MATCH (100%)** |
| `CP_HQD_PQ_BASE_LO` | `0xC844` | `0xF4000200` | `0xF4000200` | `0xF4000200` | **MATCH (100%)** |
| `CP_HQD_PQ_BASE_HI` | `0xC848` | `0x00000000` | `0x00000000` | `0x00000000` | **MATCH (100%)** |
| `CP_HQD_PQ_RPTR` | `0xC84C` | `0x00000000` | `0x00000000` | `0x00000000` | **MATCH (100%)** |
| `CP_HQD_PQ_CONTROL` | `0xC868` | `0xD000890D` | `0xD000890D` | `0xD000890D` | **MATCH (100%)** |
| `CP_HQD_PQ_DOORBELL_CONTROL` | `0xC860` | `0x00000000` | `0x00000000` | `0x00000000` | **MATCH (100%)** |

---

## 3. Conclusão Técnica e Autorização do Próximo Gate

1. **Validação da Camada HQD no Silício**:
   - A capacidade do miniport em programar e auditar de forma 100% reversível e segura todos os 16 registradores de configuração do banco MEC1/P0/Q0 na APU AMD BC-250 está **comprovada fisicamente**.
2. **Autorização para o Próximo Gate (Ativação e Submissão PM4)**:
   - Com o gate inativo aprovado com status `0`, autoriza-se o desenvolvimento do **Gate de Ativação do HQD e Submissão PM4** (Candidato 1.70.0.0), no qual `CP_HQD_ACTIVE = 1` será ensaiado, seguido da emissão de pacotes PM4 (`IT_WRITE_DATA` e `IT_RELEASE_MEM`) e validação do tripé físico (`RPTR == WPTR`, scratch mutado e fence física gravada na VRAM).

> [!CAUTION]
> Enquanto a execução do tripé PM4 não gravar a fence física na VRAM com `RPTR == WPTR`, o veredito global de aceleração de compute permanece formalmente **`NÃO VALIDADO EM HARDWARE`**.
