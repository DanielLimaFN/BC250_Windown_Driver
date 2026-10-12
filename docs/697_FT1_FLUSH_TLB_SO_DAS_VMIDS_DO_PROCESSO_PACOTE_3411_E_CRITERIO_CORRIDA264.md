# 697 — FT.1: FLUSH_TLB invalida só as VMIDs do processo. Pacote 3.411.1.0 e critério da Corrida 264

**Data:** 10/10/2026. **Natureza:** preparação offline; nada rodou na placa. **Base:** [doc 694](694_T5_T6_MINERACAO_DO_PROCESSO_NOVO_NA_263_E_INVENTARIO_DE_MMIO_EM_REGIME.md) §1.4 (achado de código) e [doc 696](696_CORRIDA263B_TRAVA_NA_SUBIDA_DO_DESKTOP_DESFECHO_D_E_O_QUE_OS_CONTADORES_BFT1_MEDIRAM.md) §3 (medida).

---

## 1. Por que

**Na 263b (medido, P0):**
- 522 de 2.611 FLUSH_TLB vieram de processos **sem VMID**.
- Cada FLUSH_TLB de processo de usuário invalidava em média **3,7 VMIDs**, inclusive as do DWM, que o GFX estava usando.

**No amdgpu (P1):**
- Só a VMID da VM cujas tabelas mudaram é invalidada.
- A invalidação vai no anel da engine que usa aquela VMID, antes do job seguinte (`gmc_v10_0_emit_flush_gpu_tlb`, `gmc_v10_0.c:359`).
- Processo sem VMID não tem tradução em TLB nenhuma.

O upstream registra que, no Navi 1x, invalidação concorrente com tradução é perigosa (`amdgpu_gmc.c:744`).

**Uma mudança só, de paridade, com o efeito já medido.**

## 2. O que mudou

**`driver/wdk/bc250_active_miniport.c`, `Bc250ActiveFlushTlbHandler`**, com a chave `BC250_FT1_FLUSH_SO_DO_PROCESSO 1`:

- **Processo de paginação:** só a VMID 8 (como antes).
- **Processo de usuário:** a máscara passa a ser a união de:
  - VMIDs do pool com essa raiz, em READY (raiz instalada) **ou INSTALLING** (raiz alvo da instalação em curso);
  - VMID 7, se a raiz instalada lá é a deste processo;
  - VMID 9 (compute), se a ligação de compute aponta para essa raiz.
- **Processo sem VMID:** máscara 0. Nenhum ponto de divisão é registrado (`Bc250PagingSplitRecordAt` ignora máscara 0).
- **Falha fechada:** se a raiz não está no segmento 1 ou os caps não estão prontos, volta à máscara antiga (todas as VMIDs) e registra `[FT1_FLUSH_POR_PROCESSO] raiz desconhecida`, nas 8 primeiras ocorrências.
- **Linha de boot:** `[FT1_FLUSH_POR_PROCESSO] chave=1 …`.

**Outros arquivos:**
- `driver/core/bc250_vmid_pool_contract.c`: `Bc250VmidPoolMaskForRoot` passa a contar o estado INSTALLING pelo `RootPhysical`. O teste ganhou esse caso.
- `tools/audit-bc250mcdm-safety.py`: portão `audit_ft1_flush_so_do_processo`. A mutação (voltar à máscara de todas as VMIDs) reprova.
- Os contadores BFT1 continuam: `vmids_invalidadas` passa a somar a máscara **efetivamente usada**.

**Por que é seguro:**
- A instalação de VMID (`Bc250GcvmInstallVmid`) invalida a VMID depois de escrever a base.
- O desarme (`Bc250GcvmUninstallVmid`) também invalida.
- Uma VMID que troca de raiz é reinstalada (com invalidação) na submissão seguinte.

Então uma tradução de um processo só pode estar em TLB numa VMID que hoje aponta para a raiz dele, e é exatamente essa VMID que a FT.1 invalida.

## 3. Identidade do candidato

| Item | Valor |
|---|---|
| DriverVer | `10/10/2026,3.411.1.0` (fonte 3.411.0.0) |
| SYS assinado | `73EDA3F6A715EE83B16C1B8AD6AC6B41D6B3381ED57155C467E9E47535F47E5C` |
| SYS fonte (lab) | `B4E43BA90734E4094C4D3258DBB8C325DC3A882B0AE9D0C20437360EF06197F2` |
| INF | `4DD9F58F725464358B1CD1791249B48B88B8D9DC58D45913F70F7E151A8226A4` |
| CAT | `D79D36A0708EF2AF6949281CD283050A37E806370FD0763B4F9C2CDB44ADC4E5` |
| UMD, RADV, RLC, T3 | iguais à 263/263b (`B73F9BDA`, `B4AE2E14`, `20ACEFDB`, `768C82F7`) |

**Verificações:**
- `build-contract.bat`: EXIT 0.
- Build de laboratório: EXIT 0.
- Empacotador: EXIT 0.
- `audit-bc250mcdm-lab-package.ps1`: PASS.
- `audit-bc250mcdm-safety.py`: PASS.
- O SYS assinado contém `[FT1_FLUSH_POR_PROCESSO] chave=` e `[T2_PTE_MTYPE]`.

**Backup:** `driver/wdk/package/output_lab_3.411.1.0_264`.

**Ferramentas:**
- Preflight `tools/prepare-corrida264-ft1-flush-por-processo.ps1`.
- Lançador `tools/EXECUTAR_CORRIDA264_FT1_FLUSH_POR_PROCESSO_600S.cmd`.

**Defeito meu, pego antes do pacote:** dois `\n` de log viraram quebra de linha real no fonte (escape no script de edição). A suíte reprovou na compilação e eu corrigi pelo Edit. Também um `\b` da regex do portão virou caractere de controle; corrigido.

## 4. Roteiro (Daniel)

Igual ao da 263:
1. `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset`, **desligar** e ligar.
2. Rodar o lançador como administrador.
3. Quando o desktop aparecer: menu (Win, 10 s, Esc).
4. Como administrador: `tools\RODAR_PROCESSO_NOVO.cmd`.
5. Se travar: reset em 10 a 15 s e deixar a coleta automática terminar.

## 5. Critério (escrito antes da corrida)

### Pré-condição

1. `[FT1_FLUSH_POR_PROCESSO] chave=1` e `[T2_PTE_MTYPE] … 0/0 | 0/1 | 3` no log; a base 4C, C3 e C1 ativa; Presents.
2. **A FT.1 pegou, pelos contadores BFT1:** em todo intervalo, `vmids_invalidadas − flush_paging ≤ 16 × flush_user_vmid`. Ou seja, FLUSH_TLB de processo sem VMID não soma VMID nenhuma. Em particular, nos intervalos com `flush_user_vmid = 0`, `vmids_invalidadas = flush_paging`.
3. No máximo 8 linhas `raiz desconhecida`. Se aparecer alguma, registrar quantas e o `Seg`.

Sem o item 2, a FT.1 não está em efeito e a corrida não mede a mudança.

### Desfechos

- **A — 600 s sem trava, com a T3 completando ≥ 80 ciclos.**
  - *Assinatura:* rollback `RECOVERY_OK_ONLINE`, T3 com `motivo=COMPLETO` ou ≥ 80 ciclos com código 0, zero TDR/fault/BugCheck.
  - *Implicação:* sinal a favor da FT.1. Na linha de base (263) foi 1 trava em 22 processos; passar 80 sem trava teria chance de ~2–3% se nada mudou e o risco por processo fosse constante (H, n=1). **Repetição obrigatória** (264b) antes de chamar de efeito. A FT.1 fica de qualquer forma, por paridade.
- **B — trava com o último evento da T3 dentro de um filho e a GPU ociosa** (os três sinais).
  - *Implicação:* a invalidação desnecessária não é causa suficiente. A FT.1 fica por paridade. Próximo da lista do doc 694 §2.3: `GfxWaveProbe=0` ou o poll do MEC sem o seletor GRBM.
- **C — trava entre ciclos**, sem processo novo nos 3 s anteriores.
  - *Implicação:* igual a B, e enfraquece a hipótese do processo novo.
- **D — trava antes da T3** (subida do desktop).
  - *Implicação:* a FT.1 não evita a trava da subida (já vista em 256b, 262 e 263b). Ler os contadores do último intervalo.
- **E — fora do mapa.** Inclui o sinal de **invalidação faltando**: fault do GFXHUB, TDR do GFX, imagem corrompida ou lixo em janela.
  - *Implicação:* a FT.1 deixou de invalidar algo necessário. Voltar a chave para 0 e analisar o fault.

**Regra (Lei 10):** desfecho parcial não é desfecho. A sem os ≥ 80 ciclos não é A.

### Teto

- 264 e, se der A, 264b.
- Se der B, C ou D, a FT.1 fica e a próxima corrida muda outra variável. Não se repete a FT.1 sozinha.

**NÃO VALIDADO EM HARDWARE.**
