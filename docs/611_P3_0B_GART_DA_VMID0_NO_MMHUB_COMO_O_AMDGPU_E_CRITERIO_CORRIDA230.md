# 611 — P3.0b: GART da VMID 0 no MMHUB, como o amdgpu, e critério da Corrida 230

**Origem:** a Corrida 229 (doc 610) mediu o MMHUB como o firmware deixa (P0):

| Registrador | Valor | Leitura |
|---|---|---|
| `CONTEXT0_CNTL` | `0x007FFE80` | VMID 0 desligada |
| Tabela de página | 0 | sem tabela |
| `VM_L2_CNTL` | `0x00080602` | L2 desligado |
| `MX_L1_TLB_CNTL` | `0x501` | sem `ADVANCED_DRIVER_MODEL` |
| `AGP_BOT`/`AGP_TOP` | `0`/`0` | AGP de 16 MB em MC 0 |

O IH é cliente do MMHUB, e o amdgpu põe o anel dele em VA da VMID 0 (doc 609). Sem a GART no MMHUB, não há IH.

**Achado novo, que reforça o motivo:** com `AGP_BOT=AGP_TOP=0`, a abertura AGP cobre MC `0..0xFFFFFF`. O anel do IH ficaria em VA `0x80000`, dentro dela, e o endereço iria direto para a RAM física baixa. O amdgpu, com a AGP desligada (P0 da coleta L-B), escreve `BOT=0xFFFFFF` e `TOP=0`.

## 1. O que o amdgpu faz (P1)

`gmc_v10_0_gart_enable` (`gmc_v10_0.c:1041`):
1. liga a GART no GFXHUB, depois no MMHUB (`mmhub_v2_0_gart_enable`);
2. chama `set_fault_enable_default(true)` nos dois hubs;
3. faz `flush_gpu_tlb(VMID 0, MMHUB)`.

**Os dois hubs usam a mesma tabela,** `adev->gart.bo`.

**`mmhub_v2_0_gart_enable` (`mmhub_v2_0.c:426`):**
- tabela e faixa do contexto 0;
- abertura AGP fechada e abertura de sistema sobre o FB;
- endereço padrão e endereço de fault na página dummy;
- `MX_L1_TLB_CNTL` com `SYSTEM_ACCESS_MODE=3`, `ADVANCED_DRIVER_MODEL=1` e `MTYPE=UC`;
- L2 ligado (`CNTL`..`CNTL5`);
- `CONTEXT0_CNTL` com `ENABLE=1`, `DEPTH=0` e `RETRY=0`;
- abertura de identidade fechada;
- contextos 1..15;
- faixa de invalidação dos 18 engines.

**O flush do MMHUB** (`gmc_v10_0_flush_vm_hub`):
1. adquire o semáforo do engine 17 (ler o `SEM` adquire);
2. escreve o `REQ` da VMID 0 (`0x00F80001`);
3. espera o bit 0 do `ACK`;
4. solta o semáforo escrevendo 0.

## 2. O que foi escrito (candidato 3.377.1.0)

**Núcleo:** `driver/core/bc250_mmhub_gart.c` e `.h`.
- 34 registradores, na ordem do upstream.
- Ciclo completo: snapshot, plano pela Lei 6, aplicação, readback dos 34, flush, e rollback em ordem inversa se qualquer passo falhar.
- `Bc250MmhubGartRestore` devolve o snapshot no teardown.
- Offsets e campos vêm de `inc/linux_ref/mmhub_2_0_0_offset.h` e `_sh_mask.h`, conferidos contra a foto da 229.

**Valores esperados a partir do estado da 229** (teste offline com esses números):

| Registrador | Antes | Depois |
|---|---|---|
| `CONTEXT0_CNTL` | `0x007FFE80` | `0x007FFE01` |
| `MX_L1_TLB_CNTL` | `0x501` | `0x1D59` (igual ao alvo provado no GFXHUB) |
| `VM_L2_CNTL` | `0x00080602` | `0x00080E01` (igual ao GFXHUB) |
| `AGP_BOT` | `0` | `0xFFFFFF` |
| `SYSTEM_APERTURE` | `0x3D0000..0x3D0400` | `0x3D0000..0x3D03FF` (fim do FB, como o amdgpu) |
| tabela | — | a mesma PA da D2 (BASE com VALID) |

**Não muda:** `FB_LOCATION`, `FB_OFFSET` e os contextos 1..15.

**Miniport** (`bc250_active_miniport.c`):
- **Chave** `BC250_P3B_MMHUB_GART_ENABLED 1`.
- **`Bc250ActiveMmhubGartEnable`** roda no `Bc250ActiveP3ArmIh`, depois da foto "firmware" e antes da foto "depois da P3.0b".
  - Exige a D2 ligada e a página dummy, em PASSIVE.
  - Só marca a GART como ligada com status OK.
  - Loga antes e depois de cada campo, e o `PROTECTION_FAULT_STATUS`.
- **`Bc250ActiveFlushMmhubTlb`** faz semáforo, `REQ`, `ACK` e liberação, com teto de 1000 × 1 µs e sob o `TlbLock`. Os timeouts do semáforo são contados.
- **`Bc250ActiveMmhubGartDisable`** fica no `Bc250ActiveD2GartDisable`, na ordem IH → MMHUB → GFXHUB.
- **A P3.1 passa a exigir a GART do MMHUB ligada** antes de `Bc250SdmaPresentIhArm`. As PTEs do IH invalidam os dois hubs.
- **O IH continua desligado** (`BC250_P3_IH_ENABLED 0`): uma diferença por corrida.

**Transporte:**
- a escrita no MMHUB tem lista própria, `Bc250MmioTopologyMmhubP3bWriteAllowed`;
- essa lista vem de uma fonte única, `Bc250MmhubGartIsWritable`;
- a leitura ganha o mesmo conjunto mais o `ACK`.

**Diferenças declaradas em relação ao upstream:**

| Item | Upstream | Aqui | Motivo |
|---|---|---|---|
| `L2_CNTL3/4/5` | parte de `_DEFAULT` | parte do valor lido | não temos `mmhub_2_0_0_default.h` |
| Contextos 1..15 | programados | não programados | nenhum cliente do MMHUB usa VMID ≠ 0 aqui |
| Faixa de invalidação | 18 engines | engines 0 e 17 | igual ao que já fazemos no GFXHUB |
| `SYSTEM_APERTURE_DEFAULT` | `mem_scratch` | página dummy | igual ao GFXHUB |

## 3. Provas offline

- **`mmhub_gart_test`** (novo, no `build-contract`) cobre:
  - os offsets;
  - o FB fora do conjunto de escrita;
  - o caminho bom com os valores da 229;
  - a restauração byte a byte;
  - readback divergente, flush falho e escrita recusada, cada um seguido de rollback;
  - recusas de argumento.
- **Builds e auditorias:** `build-contract` EXIT=0, SYS de laboratório confirmado, e as três auditorias PASS.
- **Portão** `audit_p3_ih_waits_for_mmhub_gart`, reescrito, com **8 mutações reprovadas**:
  1. IH sem conferir a GART do MMHUB;
  2. FB no conjunto escrito;
  3. AGP aberta;
  4. flush sem soltar o semáforo;
  5. D2 desligada sem desligar o MMHUB;
  6. MMHUB direto na lista de escrita;
  7. GART marcada como ligada antes do status;
  8. sem rollback.
- **Strings no SYS:** `[P3B_MMHUB] resultado: LIGADA`, `[P3B_MMHUB] DESLIGADA` e `FOTO %s:`.

**Pacote** 3.377.1.0, perfil `DISPLAY_RENDER_LAB` (RenderNode=1, PersistentSubmission, GfxWaveProbe=1), auditoria PASS:

| Arquivo | SHA-256 |
|---|---|
| SYS | `8B6D60B5FA58BDE31167131AB853E70C8182962163B3D988F381B0480EA1E848` |
| INF | `1B11A1D9EA35F6CF74FD1F43B62CBA6139E059145BF2DD19FD65852EA1075061` |
| CAT | `3F1D8D48962333029B1D3E4B1C8DD00EDD2F54D2EE80D01B618610DD68F6ADE7` |
| UMD | `8782A5E5…` (inalterada) |

**Scripts:** `tools/prepare-corrida230-p30b-gart-no-mmhub.ps1` e `tools/EXECUTAR_CORRIDA230_P30B_GART_MMHUB_600S.cmd`. O preflight a seco parou só na CMOS.

## 4. Critério da Corrida 230

É a primeira escrita no MMHUB, e o display é cliente do MMHUB. O que se mede é a GART ligada com o display intacto. **Abrir o menu e a Pesquisa como na 201–228** (na 229 o menu não foi aberto).

**Pré-condição:**
- StartDevice `status=0`, P1b ativa;
- `[P3_IH] recurso de interrupcao #1: MENSAGEM (MSI)`;
- `[P3_MMHUB] FOTO firmware: 17/17`.

**Desfechos:**

- **A — GART do MMHUB ligada e display intacto**
  - *Assinatura:* todos os sinais abaixo.
    - `[P3B_MMHUB] resultado: LIGADA (34/34 conferidos, ACK da VMID 0 no MMHUB)`;
    - `PROTECTION_FAULT_STATUS=0x00000000`;
    - a foto "depois da P3.0b" com `CONTEXT0_CNTL=0x007FFE01`, `MX_L1_TLB_CNTL=0x00001D59`, `VM_L2_CNTL=0x00080E01` e `AGP_BOT=0x00FFFFFF`;
    - o desktop aparece como na 229;
    - no teardown, `[P3B_MMHUB] DESLIGADA ... falhas=0`.
  - *Implicação:* próxima corrida, a P3.1 (IH com `TRAP` e prova por vetor), com a chave do IH em 1 e mais nada.
- **B — falha fechada**
  - *Assinatura:* `resultado: NAO LIGADA`, com `passo` e `reg`.
  - *Implicação:*
    - passo 5 (readback): um campo não aceita o valor; comparar com o catálogo.
    - passo 6 (flush): o `ACK` não veio; olhar `semTimeouts` (semáforo ou power gating) antes de repetir.
- **C — display afetado**
  - *Assinatura:* `LIGADA` seguida de tela preta, corrompida ou congelada logo depois do bind, ou `PROTECTION_FAULT_STATUS` diferente de zero.
  - *Implicação:* o DCN depende de algo que mudou. Suspeitos, nesta ordem:
    1. o fim da abertura de sistema (`0x3D0400` → `0x3D03FF`);
    2. `SYSTEM_ACCESS_MODE`/`ADVANCED_DRIVER_MODEL`.

    Bisseção com um campo por corrida.
- **D — teardown sujo**
  - *Assinatura:* `DESLIGADA` com `falhas` diferente de zero, ou o rollback não devolve o BasicDisplay.
  - *Implicação:* rever a ordem de restauração.
- **E — fora do mapa**
  - *Assinatura:* descrever o observado antes de julgar.
  - *Implicação:* a definir.

**A trava dura continua possível:** nada da conclusão por interrupção está ativo. Se vier, conta como amostra com uso normal, e vale para comparar com a 228.

## 5. Pendências

**NÃO VALIDADO EM HARDWARE:**
- toda a P3.0b;
- o comportamento do DCN com a GART do MMHUB ligada;
- a P3.1.

**Próximas etapas:**
1. P3.1: IH e `TRAP`.
2. P3.2: conclusão pela interrupção.
3. P3.3: RPTR do IH por doorbell.
