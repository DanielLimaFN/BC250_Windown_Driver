> ⚠️ **DESATUALIZADO — substituído por
> [docs/324](324_PASSAGEM_DE_BASTAO_2026-09-22_CORRIDAS_59_A_63.md).**
> A faixa que este documento propõe foi implementada e medida na **Corrida 59**:
> o flush passou a cobrir os VMIDs do pool, com ACK em todos e zero falhas — e
> **a taxa de fault não mudou**. A hipótese está **refutada como causa
> suficiente**. Além disso, a conclusão de granularidade `<<20` citada aqui foi
> **refutada por proveniência** (ver docs/324 §3). Mantido como registro.

# Passagem de bastão — pós-Corrida 58: invalidar os VMIDs do pool

> Documento autocontido para a próxima IA. Leia este arquivo inteiro; consulte somente
> `docs/316` para os detalhes da corrida e as funções citadas abaixo. Não é necessário reler
> a BASE inteira nem os handoffs anteriores.

## Objetivo vigente

Fazer o primeiro trabalho **offline** que discrimine/corrija o fault gráfico restante sem
afrouxar prova: garantir que alterações de page table publicadas pelo VidMm invalidem também
os VMIDs dinâmicos usados pelo `CP_RB0`, então preparar candidato e critério para uma única
corrida física. Não declarar pixel, desktop ou D0.7 antes da prova correspondente.

## Estado físico confirmado

Última corrida: 21/09/2026 17:55:20, lab 3.219.1.0, SYS
`5E53951F2A1F89D91C3B3815C9615F6870DC519B053D292617832F72C46FBC8C`.
Evidência: `docs/evidence/2026-09-21_17-55-20-DESKTOP-HQAG6T4-lab-binding/`.

1. A arena candidata de shader `0x200040000` **não perde execução**: 25/25 atualizações com
   64 páginas/0x40000 bytes deram `NoExecute=0`, `EncodeStatus=0`, PTE baixa `0x31`.
   Não mexer nesse bit às cegas.
2. Seis TDRs, seis restarts provados. Três sem latch; três faults de escrita em
   `0x2469A6000`, `Cid=8`, `Rw=1`, `WalkerError=6`, `MappingError=1`,
   `PermissionFaults=0x5`.
3. No terceiro fault, VMID 2, a janela de publicação ficou `INALTERADA`. A raiz
   `VIVA_READY` caminhou para PTE `0x0000000458F72071`: válida e R/W/X na imagem CPU-visível.
4. Produto ainda falha: primeiro Present `0x887A0005`, remoção `0x887A0007`, 0/600; zero
   pixel provado. Rollback terminou `RECOVERY_OK_ONLINE`; BasicDisplay Código 0.

Resultado completo: `docs/316_HANDOFF_2026-09-21_CORRIDA58_ALVO_EXECUTAVEL_FAULT_DE_ESCRITA.md`.

## Achado offline que define a próxima faixa

`Bc250ActiveFlushTlbHandler` chama `Bc250ActiveFlushMappedGfxhubTlb`. Esse helper invalida
somente:

```text
VMID 8  — paging
VMID 7  — usuário legado
VMID 9  — compute
```

Mas, desde a Corrida 54, `Bc250ActiveGfxEngineSubmit` codifica no
`PACKET3_INDIRECT_BUFFER` o `poolVmid` do contexto — na Corrida 58 foram usados VMIDs 1–4.
`Bc250GcvmInstallVmid` invalida o VMID correto **na instalação inicial**, porém uma
`UpdatePageTable`/`FlushTlb` posterior não invalida nenhum VMID dinâmico já instalado.

Isso é uma hipótese de causa muito forte e coerente com a medição: a CPU caminha a PTE nova,
enquanto a GPU pode conservar tradução/permissão antiga no VMID 2. Ainda não é causa provada
no silício; deve ser congelada como critério e testada.

## Arquivos e símbolos — leitura mínima

1. `driver/wdk/bc250_active_miniport.c`
   - `Bc250ActiveFlushTlbHandler`
   - `Bc250ActiveFlushMappedGfxhubTlb`
   - `Bc250ActiveFlushVmidTlb`
   - bloco do pool em `Bc250ActiveSubmitCommandVirtual`
   - `Bc250ActiveGfxEngineSubmit`
2. `driver/core/bc250_vmid_pool_contract.h/.c`
   - `Entries[]`, `InUseMask`, `InstallState`, `InstalledRootPhysical`
3. `driver/core/bc250_gcvm_vmid8_transaction.c`
   - `Bc250GcvmInstallVmid` e seu `FlushVmid(Context, Vmid)`
4. Testes:
   - `driver/tests/vmid_pool_contract_test.c`
   - `driver/tests/gfxhub_tlb_transaction_test.c`
   - `driver/tests/active_miniport_test.c`
5. Auditoria:
   - `tools/audit-bc250mcdm-safety.py`

Não editar DCN. Não rearmar `GfxWaveProbe`; a Corrida 45 mediu `selector_stuck`.

## Implementação recomendada

### 1. Snapshot do conjunto que realmente precisa de flush

Adicionar ao contrato do pool uma consulta pura para obter máscara dos slots que estejam:

- ocupados por contexto;
- `InstallState == READY`;
- `InstalledRootPhysical != 0`.

No miniport, copiar essa máscara sob `VmidPoolLock` e **soltar o lock antes de qualquer
MMIO/poll**. `Bc250ActiveFlushVmidTlb` adquire `TlbLock` e pode fazer até 1000 polls; nunca
segurar `VmidPoolLock` durante isso.

### 2. Invalidar fixos + pool, sem duplicação

`Bc250ActiveFlushMappedGfxhubTlb` deve continuar cobrindo 7/8/9 e acrescentar todos os bits
READY do pool. O pool já exclui 0/7/8/9, mas a rotina deve construir uma máscara explícita,
recusar bits fora de 1–15 e registrar:

```text
[FlushMappedGfxhubTlb] FixedMask=... PoolReadyMask=... RequestedMask=...
[FlushMappedGfxhubTlb] VMIDxx status=... AckObserved=... Polls=...
```

Falha em qualquer VMID mantém o retorno global falso; não transformar flush parcial em
sucesso. Preservar a invalidação UTCL1 do SDMA existente.

### 3. Testes que precisam falhar antes da correção

- pool com VMID 2 e 3 READY: ambos devem chegar ao callback de flush, junto de 7/8/9;
- slot apenas alocado/`NEVER`, `INSTALLING` ou `FAILED`: não entra na máscara;
- `READY` sem raiz: não entra;
- falha do callback em um VMID: resultado global falha;
- snapshot da máscara ocorre sem manter o lock durante callback/MMIO;
- mutação que volte a deixar só 7/8/9 deve reprovar a auditoria.

Preferir helper testável no contrato em vez de fazer a regra existir apenas no miniport.

## Sequência de trabalho

1. Implementar somente a cobertura de flush dos VMIDs READY.
2. Rodar testes direcionados e `driver\wdk\build-contract.bat`.
3. Rodar `python tools\audit-bc250mcdm-safety.py` e `git diff --check`.
4. Fazer mutação verificada do novo portão e restaurar.
5. Subir a versão do candidato (próxima esperada: 3.220.1.0), empacotar e auditar.
6. Escrever o critério da Corrida 59 **antes** de pedir ao Daniel para rodar.
7. Parar e entregar comando/dry-run; a corrida física pertence ao Daniel.

## Critério físico sugerido para a Corrida 59

Controle mínimo:

- identidade/hash do novo candidato;
- pelo menos um `PoolReadyMask` não zero;
- cada VMID da máscara com ACK de invalidação antes de submissão GFX posterior;
- carga D3D11/RADV comparável;
- TDR/restart, fault, tripé, pixel e rollback relatados separadamente.

Desfechos:

- **A — hipótese confirmada funcionalmente:** flush dinâmico ocorreu/teve ACK, o fault
  `0x2469A6000` desapareceu sob carga equivalente e a submissão que antes parava aposentou.
  Pixel continua exigindo prova própria.
- **B — flush exercitado, mesmo fault:** hipótese refutada como causa suficiente; preservar
  a telemetria e investigar hierarquia/PDE ou o consumidor `Cid=8`.
- **C — flush dinâmico não foi exercitado:** inconclusivo; não atribuir ausência de fault à
  correção.
- **D — flush falhou/sem ACK:** falha da implementação ou do caminho físico; não submeter
  como se estivesse coerente.
- **E — BugCheck/TDR sem restart/outro endereço:** relatar como inesperado antes de inferir.

## Estado do plano

- Placar do catálogo: 22/51 (43%).
- Trabalho ativo: 22/48 (46%), 26 abertos.
- Bloqueador: DX.5.
- D0.7 continua aberto; perna 3 nunca fechou.
- D1 primeiro pixel continua bloqueada.
- D3, o maior bloco, continua 0/8.
- Dependências `C:\BC250_LIB\vulkan_radeon.dll` e
  `C:\BC250_LIB\libvkd3d-shader-1.dll` continuam externas; D2.2 permanece aberto.

## Cuidados com o worktree

O worktree contém toda a cadeia não commitada das Corridas 52–58 e arquivos novos. Não usar
`git reset`, `git checkout --`, limpeza recursiva nem sobrescrever mudanças existentes. Antes
de editar, conferir `git status --short`; preservar tudo que não pertence à faixa do flush.

Esta passagem de bastão não implementou a correção. Ela apenas registrou a análise da
Corrida 58 e isolou a próxima faixa offline.
