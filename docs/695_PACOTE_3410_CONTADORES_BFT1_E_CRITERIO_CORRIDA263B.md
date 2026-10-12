# 695 — Pacote lab 3.410.1.0 (contadores BFT1) e critério da Corrida 263b

**Data:** 10/10/2026. **Natureza:** preparação offline; nada rodou na placa. **Base:** [doc 693](693_CORRIDA263_LINHA_DE_BASE_TRAVA_DENTRO_DO_22O_PROCESSO_NOVO_DESFECHO_B.md) (Corrida 263) e [doc 694](694_T5_T6_MINERACAO_DO_PROCESSO_NOVO_NA_263_E_INVENTARIO_DE_MMIO_EM_REGIME.md) (T5/T6).

---

## 1. O que é a 263b

1. **Repetição da linha de base** (doc 692 §4, teto de 3): mesma carga T3, para fechar a taxa de travas por processo novo com n=2.
2. **Medição do mecanismo que a T5 e a T6 não conseguiram medir pelo log amostrado:** o pacote só acrescenta contadores no rastro durável. **Não muda comportamento**: nenhum registrador novo, nenhum log novo, nenhuma decisão nova.

## 2. O que foi acrescentado (offline)

Registro **BFT1** no rastro durável (`C:\BC250_PAGING_TRACE.bin`):
- gravado a cada 10 tiques (~1,2 s), junto do BPG1;
- mesmo layout de 72 bytes;
- contadores cumulativos desde o carregamento.

| Palavra | Campo no decodificador | O que conta | Onde |
|---|---|---|---|
| 1 | `flush_paging` | FLUSH_TLB do processo de paginação (máscara só VMID 8) | `Bc250ActiveFlushTlbHandler` |
| 2 | `flush_user_vmid` | FLUSH_TLB de processo cuja raiz está instalada em alguma VMID (pool ou VMID 7) | idem |
| 3 | `flush_user_sem_vmid` | FLUSH_TLB de processo **sem VMID** (o caso dos filhos da T3) | idem |
| 4 | `vmids_invalidadas` | Soma das VMIDs na máscara que a regra atual manda invalidar | idem (mesma máscara do ramo 4c.4) |
| 5 | `faixas_pte_sdma1` | Faixas de PTE roteadas ao SDMA1 (P4.2) | `Sdma1RangesRouted` |
| 6 | `leituras_sq` | Leituras de `SQ_DEBUG_STS_GLOBAL` pela CPU (`GfxWaveProbe=1`) | três sítios do GFX |
| 7 | `invalidacoes_cpu_eng17` | Invalidações de TLB do GFXHUB pela CPU (instalação e desarme de VMID) | `Bc250ActiveFlushVmidTlb` |

**Arquivos alterados:**
- `driver/core/bc250_vmid_pool_contract.{h,c}`: função pura `Bc250VmidPoolMaskForRoot` (VMIDs READY com aquela raiz). O teste está em `vmid_pool_contract_test.c`, com raiz conhecida, raiz ausente, raiz zero e pool nulo.
- `driver/wdk/bc250_mcdm_callbacks.{h,c}`: contadores e o registro BFT1.
- `driver/wdk/bc250_active_miniport.c`: os pontos de contagem.
- `tools/decode-bc250-paging-trace.py`: o campo novo `flush_classes`, com o último valor e os deltas por segundo dos últimos 20 registros.

**Custo no FLUSH_TLB:** uma aritmética de caps (sem MMIO e sem log no caminho de sucesso), um spinlock curto do pool e incrementos atômicos.

## 3. Identidade do candidato

| Item | Valor |
|---|---|
| DriverVer | `10/10/2026,3.410.1.0` (fonte 3.410.0.0) |
| SYS assinado | `F65336997E34C8A4CD3CB665C47B3DEF300E7EDB9B23A09939ADC7E759740782` |
| SYS fonte (lab) | `172905025DED7CA8BD15EE98194D58E889241C0AA4410622E69696D2B1E9DF1A` |
| INF | `200C2BFD9DA1CE0235B1D70CAB6751709975EE67552801F4AA8A3C1D5B759779` |
| CAT | `D6BD6852E2CABFF914E4E2BC33BD393DF8064D83C79AF6EFB3D441A74FD0FA7B` |
| UMD, RADV, RLC, T3 | iguais à 263 (`B73F9BDA`, `B4AE2E14`, `20ACEFDB`, `768C82F7`) |

**Verificações:**
- `build-contract.bat`: EXIT 0.
- Build de laboratório: EXIT 0.
- Empacotador: EXIT 0.
- `audit-bc250mcdm-lab-package.ps1`: PASS.
- `audit-bc250mcdm-safety.py`: PASS.
- O SYS assinado contém a prova `[T2_PTE_MTYPE]` e a constante `BFT1`.
- O decodificador roda sobre o rastro da 263 (BFT1 ausente lá, como esperado).

**Backup:** `driver/wdk/package/output_lab_3.410.1.0_263b`.

**Ferramentas:**
- Preflight `tools/prepare-corrida263b-contadores-processo-novo.ps1`: boot frio pelo log do Windows; programas abertos só registrados.
- Lançador `tools/EXECUTAR_CORRIDA263B_CONTADORES_PROCESSO_NOVO_600S.cmd`.

## 4. Roteiro (Daniel)

Igual ao da 263:
1. `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset`, **desligar** (não reiniciar) e ligar.
2. Rodar o lançador como administrador.
3. Desktop aparecer: anotar a hora, abrir e fechar o menu.
4. Em outra janela, como administrador: `tools\RODAR_PROCESSO_NOVO.cmd`.
5. Programas abertos são permitidos. Se travar: reset em 10 a 15 s e deixar a coleta automática terminar.

## 5. Critério (escrito antes da corrida)

### Pré-condição

1. `[T2_PTE_MTYPE] folha padrao MTYPE=0 snoop=0 | coerente MTYPE=0 snoop=1 | pedido P6 MTYPE=3` no log do KMD.
2. `[4C_HDP_FLUSH_BYPASS] ATIVO`, `[C3_VRAM_WC]` e `[C1_GFX_DOORBELL] ATIVA`.
3. Presents no log.
4. `flush_classes` presente no `paging-trace-decodificado.json`, com `records > 0`.
5. Filhos da T3 com `ADAPTADOR 1002:13FE`.

Sem o item 4, a parte de medição é inválida, mas a taxa (parte 1) ainda conta.

### Desfechos da taxa (iguais aos do doc 692)

- **A** — 600 s sem trava, com ≥ 80 ciclos da T3.
- **B** — trava com o último evento da T3 dentro de um filho e a GPU ociosa no último tique (os três sinais).
- **C** — trava entre ciclos, sem processo novo nos 3 s anteriores.
- **D** — trava antes de a T3 começar.
- **E** — fora do mapa: TDR, BugCheck, fault, filho preso, contadores incoerentes.

**Implicação:** com a 263, a taxa fica com n=2. Se der B de novo, a hipótese do processo novo fica com 2 de 2 na T3. Se der A ou C, a taxa é menor ou a hipótese enfraquece. O teto continua 3 corridas.

### Leitura dos contadores (vale em qualquer desfecho com BFT1)

A análise compara o **último registro BFT1** antes da trava (delta do último segundo) com a mediana dos deltas da corrida:

- **M1 — invalidação de todas as VMIDs por processo sem VMID.**
  - *Assinatura:* `flush_user_sem_vmid` alto nos segundos dos filhos da T3, e `vmids_invalidadas / flush_*` maior que 1 nesses segundos.
  - *Implicação:* confirma por medição o achado P1 da T5 (doc 694 §1.4). O próximo candidato é a mudança 3 do doc 694 §2.3: invalidar só a VMID do processo, nada para processo sem VMID.
- **M2 — rota de PTE no SDMA1 concentrada na trava.**
  - *Assinatura:* delta de `faixas_pte_sdma1` no último segundo acima do percentil 90 da corrida.
  - *Implicação:* reforça a concorrência entre o SDMA1 (tradução na VMID 8) e as invalidações do SDMA0 (aviso do upstream, `amdgpu_gmc.c:744`).
- **M3 — leituras do SQ ativas na trava.**
  - *Assinatura:* delta de `leituras_sq` > 0 no último segundo.
  - *Implicação:* mantém o candidato `GfxWaveProbe=0` (doc 694 §2.3, item 2).
- **M4 — invalidação pela CPU (ENG17) na trava.**
  - *Assinatura:* delta de `invalidacoes_cpu_eng17` > 0 no último segundo.
  - *Implicação:* contradiz a 263 (zero troca de VMID pela CPU nos 12 s finais) e sobe o candidato "VMID pelo anel".
- **E — fora do mapa:** descrever antes de julgar.

M1–M4 não são excludentes: são medidas, não desfechos. **Nenhuma delas prova causa.** Só a corrida seguinte, com uma mudança, pode mostrar efeito.

**NÃO VALIDADO EM HARDWARE.**
