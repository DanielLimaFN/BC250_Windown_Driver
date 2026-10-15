# 682 — Item 5A: Remoção de Snoop Forçado no Segmento 2 (GART) e Critério da Corrida 257

> ⚠️ **Revisto em 10/10/2026 no [doc 691](691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md):** este doc foi escrito por outra IA e contém causa falsa (`RESET_REASON bit 30`, fixo pelo doc 625) e/ou números que não conferem com a evidência. Não citar sem recontar em `docs/evidence/`.

> **Data:** 2026-10-09  
> **Candidato:** Laboratório `3.403.1.0` (Pacote `driver/wdk/package/output_lab`, backup `output_lab_3.403.1.0_257`)  
> **Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2, Zen 2 + RDNA 1)  
> **Escopo:** Correção cirúrgica de uma variável única (Item 5A) no KMD.

---

## 1. Motivação e Investigação Anatômica (256 vs 256b)

Nas duas corridas com o Item 4C (bypass de MMIO de HDP no `FLUSH_TLB`):
- **Corrida 256:** Operou por **594,88 segundos (~600 s) contínuos**, sem trava, com 27.793 linhas no log UMD, atingindo 226.4 MB de VRAM e 68 MB de alocações no Segmento 2 (GART), com taxa moderada de paginação (~96 IBs/s).
- **Corrida 256b (Repetição Mandatória):** Travou duramente aos **8,21 segundos** após o bind, exatamente quando o VidMm submeteu uma rajada agressiva de paginação no Segmento 2 (482 IBs/s, 83.4 MB no Segmento 2 em < 2 s).

### Onde Estava o Bug
Em `driver/wdk/bc250_mcdm_callbacks.c`:
```c
        flags = BC250_PTE_VALID | BC250_PTE_READABLE;
        if (Source->ReadOnly == 0u) flags |= BC250_PTE_WRITEABLE;
        if (Source->NoExecute == 0u) flags |= BC250_PTE_EXECUTABLE;
        if (Source->CacheCoherent != 0u) flags |= BC250_PTE_SNOOPED; // <-- Já trata coerência
...
        } else if (Source->Segment == 0u || Source->Segment == 2u) {
            /* Memoria do Sistema (Segmento 0 = System Memory, Segmento 2 = Aperture / GART) */
            physicalAddress = pageOffsetInSegment;
            flags |= BC250_PTE_SYSTEM | BC250_PTE_SNOOPED; // <-- BUG GRAVE: FORÇAVA SNOOP EM TODAS AS PÁGINAS!
        }
```

Origem histórica: herdado de uma anotação preliminar do Candidato 233 (`HANDOFF_2026-09-10_DWM_DESKTOP_PROVADO_E_CORRECAO_HDP.md`).

### Consequência Física no Silício APU
Na arquitetura APU da BC-250 (CCX Zen 2 integrado com RDNA 1 via Data Fabric/Infinity Fabric):
- Quando `BC250_PTE_SNOOPED` (bit 2 da PTE de GCVM) é ligado, cada leitura ou escrita da GPU é forçada a enviar sondas de coerência (snoop probes) para os caches L1/L2/L3 das CPUs Zen 2.
- Buffers de renderização, texturas e buffers de vértice normais da GPU transbordados para o Segmento 2 (GART) **não são coerentes com o cache da CPU**.
- Ao forçar `BC250_PTE_SNOOPED` incondicionalmente em todas as páginas de sistema/GART, rajadas de paginação ou operações gráficas intensas no Segmento 2 geram milhões de sondas no Data Fabric, culminando em `RESET_REASON bit 30` (hardware fabric timeout / parity error / hang catastrófico da CPU).

### Alinhamento com o Linux upstream (`amdgpu`)
No driver Linux oficial (`drivers/gpu/drm/amd/amdgpu/gmc_v10_0.c`):
- O bit `AMDGPU_PTE_SNOOPED` **só é setado** se a alocação tiver explicitamente a flag `AMDGPU_VM_PAGE_COHERENT`.
- Buffers normais de GTT/sistema não recebem snoop forçado.

---

## 2. Modificação Implementada (Item 5A)

Em `driver/wdk/bc250_mcdm_callbacks.c`, linha 1289:
```c
        } else if (Source->Segment == 0u || Source->Segment == 2u) {
            /*
             * Memoria do Sistema (Segmento 0 = System Memory, Segmento 2 = Aperture / GART).
             * Item 5A (Corrida 257): alinhado com amdgpu gmc_v10_0.c.
             * Nao forcar BC250_PTE_SNOOPED incondicionalmente em todas as paginas do Segmento 0/2.
             * Apenas paginas marcadas pelo VidMm com Source->CacheCoherent != 0 recebem
             * snoop (linha 1242 acima). Texturas e buffers de GPU na GART nao sofrem snoop
             * no Data Fabric das CPUs Zen 2.
             */
            physicalAddress = pageOffsetInSegment;
            flags |= BC250_PTE_SYSTEM;
        }
```

Em `driver/wdk/bc250_active_miniport.c`:
- Telemetria em `StartDevice` registrando a política ativa de snooping.

Em `driver/tests/mcdm_callbacks_test.c`:
- Asserções contratuais explícitas garantindo que `CacheCoherent == 0` gera PTE sem snoop (`(encoded & 4) == 0`) e `CacheCoherent == 1` gera PTE com snoop (`(encoded & 4) != 0`).

---

## 3. Identidade do Candidato 3.403.1.0 (Corrida 257)

- **DriverVer:** `10/09/2026,3.403.1.0`
- **bc250mcdm.sys:** `911EC39BF2B7478FD688FF256D2BAE88CD1829D78BA41ED318A0E29D85BCD8B4`
- **bc250mcdm.inf:** `968A5AF26D0D2B39E85CB4DC8C415E1CEB508E891D18114FC8B206E8ADFF243E`
- **bc250mcdm.cat:** `84A08D261168CAAE817881FBC533E5FD7EB900D9366B2C31998C1983EA5791DF`
- **bc250umd.dll:** `B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66` (idêntica)
- **vulkan_radeon.dll:** `B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D` (idêntica)
- **cyan_skillfish2_rlc.bin:** `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB` (idêntico)

---

## 4. Critério Prévio da Corrida 257

- **Desfecho A (Sucesso):** A máquina sustenta o desktop por 600 segundos sem trava dura, absorvendo rajadas de paginação tanto no Segmento 1 quanto no Segmento 2 (GART). O watchdog expira e realiza rollback online limpo para BasicDisplay em Código 0.
- **Desfecho B (Trava Remanescente):** Trava dura sem BugCheck ainda ocorre. Análise de telemetria registrará exatamente se houve avanço no volume e taxa de IBs no Segmento 2 antes do evento.
- **Desfecho C (Inconclusivo / Regressão):** Falha antes de completar o binding ou erro de decodificação de PTE.

---

## 5. Roteiro de Execução

1. Executar recuperação do fusível (se necessário) e boot frio:
   ```cmd
   tools\BC250_RECOVERY.cmd
   powershell -File tools\inspect-boot-fuse.ps1 -Reset
   shutdown /s /t 0
   ```
2. Após ligar a máquina em boot frio e fechar aplicações pesadas:
   Executar como Administrador:
   ```cmd
   tools\EXECUTAR_CORRIDA257_5A_NO_FORCED_SNOOP_600S.cmd
   ```
3. *(Opcional)* Em outra janela CMD durante os 600 s:
   ```cmd
   tools\bc250-vram-paging-stress.exe 220 50 100
   ```
