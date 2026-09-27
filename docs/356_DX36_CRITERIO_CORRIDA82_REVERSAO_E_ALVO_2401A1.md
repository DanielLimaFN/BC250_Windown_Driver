# DX.36 — reversão do SWITCH_BUFFER e sonda do alvo 0x2401A1000. Critério da Corrida 82

> **23/09/2026. OFFLINE; zero bind.** Parte do resultado da Corrida 81 ([docs/355](355_CORRIDA81_DX35_RESULTADO.md)).
> Critério escrito **antes** do ensaio (Leis 9 e 10). **NÃO VALIDADO EM HARDWARE.**

## 1. O que motiva (P0)

- **Corrida 81:** o par de `SWITCH_BUFFER` travou o CP. Em 5 de 5 TDRs, `ME/PFP_HEADER_DUMP=0xC0008B00` com zero ondas,
  seguidos de 0x116. A pilha conferida é `TdrBugcheckOnTimeout`, sem quadros nossos.
- **Investigação offline da escrita sem permissão em `0x2401A1000`** (logs da 80 e da 81, sentinela
  `GFX_FAULT_SENTINELA` e `GCVM_FAULT_TIMELINE`):
  - são **três** ocorrências: fence 32 da 80, fences 20 e 25 da 81;
  - `Cid=0` = **CB/DB** (`inc/linux_ref/gfxhub_v2_0.c:35`), `Rw=1`, `PermissionFaults=0x4` (escrita);
  - cada uma veio logo depois de aposentar o **mesmo IB `0x2401DC000` de 552 DW**, o primeiro IB de um contexto
    recém-instalado no pool;
  - em **todas**, o VMID do fault é igual ao VMID com que esse IB foi submetido (80: 2/2; 81: 1/1 e 1/1). **Não é
    mistura de VMID**, ao contrário do TDR 1 da 80;
  - a página cai numa alocação do heap da RADV (`Base=0x240040000`, `Size=0x3C0000`, token `0x3479380`, classe
    desconhecida para o KMD), mas **nenhum log registra com que flags DXGK ela foi mapeada**;
  - o IB aposentou, então a escrita não trava a GPU. O dano medido foi esconder a falta do TDR 2 da 80, e a linha do
    tempo da DX.35 já neutraliza isso.

## 2. O que mudou

| # | Mudança | Tipo |
|---|---|---|
| R1 | **Reversão** do par de `SWITCH_BUFFER`: a troca volta a 31 DW (a sequência da Corrida 80). O portão agora **proíbe** `SWITCH_BUFFER` na troca, e o teste trava que o dword depois do `PFP_SYNC_ME` fica intocado. | Reversão (volta ao comportamento validado na 80) |
| M1 | `[DX_TARGET_2401A1] MAP:`, somente leitura, no `UpdatePageTable`: para a página `0x2401A1000`, registra `V/Z/Seg/PFN/ReadOnly/NoExecute/CacheCoherent`, os flags crus, a PTE **realmente codificada**, o processo, a alocação e o token. Mesmo formato da `DX_TARGET_200040`, isenta de amostragem. | Medição |

**Mantidos:**
- `[GCVM_FAULT_TIMELINE]` (DX.35), que funcionou;
- sonda de ondas armada (`GfxWaveProbe=1`), retângulos sujos, pulo de ModeChange, marcador do watchdog.

**Nenhuma mudança de comportamento nova**: a 82 é **controle da reversão mais medição**.

**Verificação offline:**
- `build-contract` EXIT=0;
- mutação de teste: reemitir um `SWITCH_BUFFER` depois do `PFP_SYNC_ME` derruba a suíte;
- portões DX.36: **6 de 6 mutações reprovam**;
- auditorias de segurança, matriz de DDI, callbacks no-op e RADV: PASS;
- ferramentas de mutação DX.31, DX.33 e DX.34: 0 falhas.

**Pacote lab 3.243.1.0**, assinado, `Lab package audit: PASS`, `GfxWaveProbe=1`, NÃO INSTALADO:

| Artefato | SHA-256 |
|---|---|
| SYS assinado | `1E1575F5FED611C1069358CB843FA70EBF25BF020065B41B0D38B6DA9B61A952` (fonte `404A8D8C...`) |
| UMD | `239B046C...` (inalterada) |
| INF / CAT | `2E6FA3F7...` / `5B425889...` |
| ICD | `5988B38E...` (inalterado) |

As strings `DX_TARGET_2401A1`, `GCVM_FAULT_TIMELINE` e `TDR_GFX_WAVE_VMID` estão no SYS empacotado.

## 3. Critério da Corrida 82

### 3.1 Sinais
- **REVERTIDO:** trocas com `PM4DW=31`, nenhum `ME/PFP_HEADER_DUMP=0xC0008B00`, e `SUBMIT SUCESSO` depois das trocas.
- **ALVO:** linhas `[DX_TARGET_2401A1] MAP:` e, se houver, faults em `0x2401A1000` na TIMELINE/sentinela, casados pelo
  processo (`hProc`).
- **ONDA-VMID:** em cada TDR, `WaveVmid` × `CARIMBO -0`/`-1`, como na 80.
- **Controles:**
  - autoteste 5/5 nas três variantes, com NÃO MEDIDO por device perdido não contando como FAIL;
  - zero BugCheck;
  - zero `PREAMBLE_CNTL`;
  - `Eng=0`;
  - boot novo com marcador do watchdog.
- **Visual (Daniel):** o que aparece, se fica, e se responde.

### 3.2 Desfechos da reversão
- **R-OK:** REVERTIDO e nenhum TDR com header `0xC0008B00`. A reversão fecha.
- **R-FALHA:** algum TDR com header `0xC0008B00`. O binário não é o que se pensa; conferir hash e string antes de ler o
  resto.

### 3.3 Desfechos da sonda do alvo (M1), com assinatura e implicação separadas

**P1 — Mapeada somente leitura pelo WDDM**
- Assinatura: a linha do alvo com `V=1 ReadOnly=1`, no mesmo `hProc` do fault.
- Implicação: a alocação foi mapeada com `Protection.Write=0` (RADV `RADEON_FLAG_READ_ONLY` ou a nossa UMD) e o CB/DB
  escreve nela. Próximo: identificar qual BO/recurso da RADV é, e por que um render target ou um DB caiu num BO
  read-only.

**P2 — Mapeada gravável, e a PTE sai sem o bit de escrita**
- Assinatura: `V=1 ReadOnly=0`, e `EncodedPte` sem o bit 6 (`WRITEABLE`, `amdgpu_vm.h`).
- Implicação: defeito **nosso** de codificação da PTE. Correção no KMD.

**P3 — Página zero (reserva sem BO)**
- Assinatura: `Z=1` (ou `V=0` com `Zero`) no mesmo `hProc`.
- Implicação: o CB/DB escreve numa faixa reservada da RADV (`MapGpuVirtualAddress` com `Protection.Zero=1`) ainda sem
  BO. É um defeito de sequência na RADV ou na nossa UMD, não no KMD.

**P4 — Mapeada gravável, PTE com escrita, e o fault ainda ocorre**
- Assinatura: `V=1 ReadOnly=0`, `EncodedPte` com bit 6, e fault no mesmo `hProc`.
- Implicação: a tabela vista pela GPU não é a que codificamos (outra raiz ou TLB velho). Voltar aos carimbos de raiz.

**P5 — Sem dado**
- Assinatura: nenhuma linha `[DX_TARGET_2401A1]` e/ou nenhum fault em `0x2401A1000`.
- Implicação: descrever. Se não houver fault, o fenômeno não se repetiu; se houver fault sem a linha, a página foi
  mapeada por um caminho que não passa pelo `UpdatePageTable` logado.

### 3.4 Desfechos dos TDRs
- **T0** sem TDR;
- **T1** `WaveVmid` = `-1`;
- **T2** `WaveVmid` = `-0`;
- **T3** sem leitura;
- **T4** VMID fora dos dois.

Mesma definição do docs/352. Cada TDR é classificado. Usar a TIMELINE para nomear a falta própria de cada TDR.

### 3.5 Transversais
- **F — Regressão de controle:** autoteste FAIL (não NÃO MEDIDO), BugCheck, `PREAMBLE_CNTL`.
  - Um 0x116 **sem** header `0xC0008B00` indica outra causa: conferir a pilha pelo dump.
- **H — Host:** bind no mesmo boot de um rollback, ou watchdog sem marcador.
- **G — Fora do mapa:** descrever antes de julgar.

Desfecho parcial não é desfecho (Lei 10.3).

### 3.6 Execução
- O marcador de boot frio está armado (boot de 09:41:55).
- **Desligamento completo**: `shutdown /s /t 0`, esperar apagar e ligar pelo botão.
- Preflight PASS, depois `tools\EXECUTAR_D3D11_NATIVO.cmd` elevado, **sem `-Force`**.
- Anotar o que aparece, se fica e se responde.
- Se houver tela azul, gerar o dump no PowerShell elevado com **caminho absoluto**:
  `> C:\Users\Daniel\Desktop\NamixWDDM\docs\evidence\dump-0x116-2026-09-23b.txt`.
- Teto: uma corrida.
