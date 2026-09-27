# 208 — VMID9 dedicado ao MEC: candidato offline 3.95.1.0

**Data:** 08/09/2026. **Nenhum bind, armamento de registro ou ensaio físico nesta sessão.**
**NÃO VALIDADO EM HARDWARE.** Continuação do handoff 207.

## Evidência e limite da conclusão

O log integral de 15:43 confirma a instalação da raiz do harness em VMID8,
a submissão Node1 e a posterior troca de raiz pelo Node0. O código também
compartilhava esse VMID. O instante da busca de instruções pelo MEC e o page fault
atribuído a essa busca não estão medidos nesse trecho. Portanto, a colisão é um
defeito concreto; sua responsabilidade pelo stall permanece **H**, a discriminar
com o ensaio abaixo. A expressão de causa definitiva do doc 207 é excessiva.

## Alteração entregue

- Node0 permanece em VMID8. O IB virtual MEC usa `BC250_GCVM_COMPUTE_VMID=9`.
- `SetRootPageTable` de contexto Node1 publica os metadados sem escrever VMID8.
  A raiz física de VMID9 é instalada imediatamente antes da submissão MEC.
- O invólucro de submissão Node1 mantém `ObjectLock` entre a leitura do contexto,
  instalação da raiz e publicação do IB. As alterações de raiz/paging do Node0
  usam o mesmo lock, mas o trabalho GPU continua assíncrono.
- Antes da programação, `Bc250SubmissionEnginePoll` observa a fence física.
  `Bc250GcvmComputeCanSwitch` exige `LastSubmittedFence == LastRetiredFence`;
  valor futuro/corrompido não autoriza troca. Não há loop de espera novo.
- Snapshot dos sete registros VMID9; contexto previamente habilitado por outro
  proprietário é recusado. CNTL é desabilitado antes da troca, bits externos à
  máscara catalogada são preservados, cada escrita tem readback e o flush exige
  ACK de VMID9 na engine17. Falha tenta restaurar a baseline; restore incompleto
  conserva o snapshot e fecha novas instalações.
- O callback de flush usado para atualizações VidMm invalida VMID8 e VMID9.
  O instalador compute usa um callback específico VMID9.
- O teardown restaura VMID9 depois do dreno do tracker e antes de desmontar o
  anel. Falha de restauração retorna erro e preserva os recursos nesse ponto.
- As duas atribuições antigas de VMID8 ao compute foram removidas. O callback
  de paging também deixou de mudar o VMID do anel quando fecha a admissão.
- A telemetria prévia `VMID8_DISPUTE_PROBE` foi substituída por `VMID9_BIND`,
  com raiz, fences submetida/aposentada, status e prontidão após readback/ACK.
  `PUBLICADO NO ANEL MEC` continua distinguindo publicação de execução.

Os endereços P1 e a fórmula SOC15 estão na seção VMID9 do catálogo 03, conferidos
contra `gc_10_1_0_offset.h` do Linux e as máscaras locais. Nada foi escrito em DCN.

## Limitações explícitas

Este candidato admite **um lote virtual compute pendente por vez**. Enquanto a
fence anterior não aposenta, retorna `STATUS_GRAPHICS_ALLOCATION_BUSY`, inclusive
para outra submissão da mesma raiz. Ainda não é um alocador de VMIDs por processo,
nem uma solução de filas concorrentes para jogos. A reação do scheduler a essa
recusa em carga contínua não foi medida; não usar essa restrição como prova de
escalabilidade. O ensaio atual usa uma submissão Node1.

O reset/TDR medido anteriormente cobre SDMA; recuperação de hang MEC não foi
implementada aqui. `D3D10_11NativeReady=false` permanece no manifesto, portanto
este candidato não entrega desktop acelerado. O critério do projeto continua
sendo jogos na saída da própria placa, sem fallback.

## Verificação offline

Evidências: `docs/evidence/2026-09-08_VMID9_ISOLATION_OFFLINE/`.

| Verificação | Resultado |
|---|---|
| Build contratual completo, incluído no build de laboratório | EXIT=0 |
| Build KMD de laboratório | EXIT=0 |
| `gcvm_vmid8_transaction_test` | PASS: VMID9 não altera VMID8; pending sem escrita; troca após retirement; restore; falha de escrita/flush; contexto ocupado |
| `mmio_topology_transport_test` | PASS: sete endereços read/write, readback e vizinho recusado |
| `paging_contract_test` | PASS: VMID9 request `0x00F80200`, ACK `0x200` |
| `audit-bc250mcdm-safety.py` | PASS, incluindo gate novo de isolamento compute |
| `test-compute-vmid-audit.py` | Baseline PASS; quatro mutações recusadas |
| Mutação compilada que ignora retirement | Reprovada pelo contrato, exit/linha 161 |
| `audit-umd-node1-submission.py` | PASS |
| `audit-bc250mcdm-lab-package.ps1` | PASS |

Os fixtures offline simulam o transporte para verificar contratos; não são prova
de execução da GPU. Não foi revalidado o fork externo RADV nesta sessão.

Durante o desenvolvimento, o primeiro link MMIO acusou dependência externa do
helper de allowlist; o helper passou ao header como inline. O novo teste MMIO
também exigiu atualizar a contagem total de acessos (sete leituras e 14 escritas
adicionais). O build final contém ambas as correções.

## Artefatos

INF fonte: **3.95.0.0**. Pacote lab: **3.95.1.0**, assinado e auditado em
`driver/wdk/package/output_lab/`, **NÃO INSTALADO**.

- SYS fonte: `EC7E1EFD775562C9B96C877F838FD2638A53D8C594B2D93F4B576CEE7D0D7204`
- SYS assinado: `3F166D48BB41B37421DD7A83E59032F99A490611655C5C42037266499991A5DD`
- UMD: `35FBD78186D300D84F25DB76D1AF1C3025C7FECA3211CA422D35619B116AF214`
- CAT: `00FA959EBF6641A7591018E60788964CC7A21F3CC1E673F14D1051BED3C50346`
- INF: `4BBBF001D30C564D254B86CA34F2C05383647B088B8E1300CCA369559AC3A33E`

O manifesto integral está arquivado na pasta de evidência. O pacote anterior
3.94.4.0 foi preservado em `package_previous_3.94.4.0/`, com manifesto separado.
Os defaults de promoção GPUVA e do harness seguem desarmados. A UMD não mudou.

## Próximo ensaio físico — executado pelo Daniel

**Teto: uma corrida**, boot frio e preflights do binder existentes. Ensaio nomeado:
`Node1Tripod` (`tools/test-b7-umd-node1-tripod.ps1`), integrado pelo binder com
`-RunUmdNode1Tripod`, sob o watchdog existente. O comando de bind do doc 207
continua sendo a interface operacional, agora com o pacote 3.95.1.0.

PASS exige: `VMID9_BIND Status=0 Ready=1` com raiz do harness, publicação MEC,
RPTR alcançando WPTR, fence exata da GPU sobre poison e dados byte a byte. Deve
haver atividade SDMA concorrente no log para testar o cenário de colisão. Sem
essa concorrência, o tripé pode provar compute, mas não fecha esse cenário.
Arquivar log integral KMD, UMD, probes, resultado do harness e hashes na pasta
de evidência produzida pelo binder.

Se instalação/readback/ACK de VMID9 falhar, ou a GPU continuar parada com VMID9
corretamente instalado, **não repetir o pacote**. Minerar a corrida e revisar a
hipótese/estágio discriminado. Em caso de sucesso, fechar somente o tripé Node1;
desktop, Vulkan e jogos continuam com critérios próprios em BASE_DE_AVANCO.

**NÃO VALIDADO EM HARDWARE.**
