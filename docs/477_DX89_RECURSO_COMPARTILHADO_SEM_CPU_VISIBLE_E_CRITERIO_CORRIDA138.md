# 477 — DX.89: recurso compartilhado sem CPU_VISIBLE (hipótese a) e critério da Corrida 138

> **26/09/2026, offline, sem corrida.** Branch `p7-desktop-estabilidade`. **NÃO VALIDADO EM HARDWARE.**

## 1. O que muda (uma variável)
- **UMD:** recurso criado com `D3D10_DDI_RESOURCE_MISC_SHARED` deixa de pedir `BC250_ALLOCATION_FLAG_CPU_VISIBLE`, e o KMD passa a devolver `FlagsWddm2.CpuVisible=0` para essa alocação. Recursos comuns da classe `RESOURCE` continuam com `CPU_VISIBLE`.
  - `bc250_wddm_winsys.h`: campo `Shared` em `BC250_WINSYS_RESOURCE_BINDING`.
  - `bc250_wddm_winsys.c`: regra `(tokenClass == RESOURCE && !sharedResource)` e linha `Allocate SHARED: recurso compartilhado sem CPU_VISIBLE`.
  - `bc250_d3d_umd.c`: `binding.Shared = (MiscFlags & D3D10_DDI_RESOURCE_MISC_SHARED)`.
- **KMD:** não muda (SYS idêntico ao da DX.88, `0DDA3953…`). A consulta DX.59 já devolve à UMD, sem amostragem, as flags da alocação (`Flags=`), prioridade, segmentos, alinhamento e tamanho. É por ela que se confere o efeito.
- **Risco declarado:** se algum caminho precisar travar pela CPU uma alocação compartilhada (`Bc250WinsysUploadAllocation` usa `pfnLockCb`), o lock falha e o erro sai nomeado no log da UMD. A textura do Explorer vem sem dados iniciais (`InitData=0`).

## 2. Verificação offline
- Teste novo em `bc250_wddm_winsys_test.c`: recurso comum da classe `RESOURCE` com `CPU_VISIBLE`; o mesmo com `Shared=TRUE` sem `CPU_VISIBLE` e com `hResource` preservado. `WDDM WINSYS CONTRACT: PASS`.
- **Mutação:** sem o `&& !sharedResource`, a suíte reprova na asserção nova (linha 697).
- Portão `audit_dx89_shared_resource_not_cpu_visible`; auditoria de segurança e do pacote PASS; `prepare-corrida138-dx89.ps1` PASS.

| Artefato (3.294.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado (igual à DX.88) | `0DDA39530EB3285A976FD764753FE118F284740CCAD45F8464706EE7FAFD278B` |
| `bc250umd.dll` | `1972A9693011891BF507EADDBFE30C363434E342934125303488087282C556A7` |
| `bc250mcdm.inf` | `B7473AE99113D302AD9D0EEABBF2E36FC49D17826B4DA4664F84939E13671FFC` |
| `bc250mcdm.cat` | `A824CDA84CE286586D6D0A04EFF78DD4AE06298696A21549E23D57E0CE512060` |

## 3. Critério da Corrida 138 (escrito antes; Lei 10)
Runner `tools\EXECUTAR_CORRIDA138_DX89_600S.cmd`, com boot frio antes. O Daniel usa o desktop durante a janela (Explorador de Arquivos, janelas, menu Iniciar), como na 137.

Sinais a medir: `Allocate SHARED` e `DX59_ALLOC_QUERY` (com `Flags=0x00000000` para a textura compartilhada) nos logs da UMD; `Allocate LEAVE hr=` dessas alocações; quedas do Explorer no Event Log; `AdapterAllocation` de 3.407.872 bytes no ETW durante o bind.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | `Allocate SHARED` presente, a textura 1024x832 compartilhada sai com `hr=0x00000000`, zero quedas de Explorer com uso real, 600 s sem trava | Hipótese (a) confirmada: `CpuVisible` era o motivo. Seguir para abertura do recurso compartilhado em outro processo (`OpenResource`). |
| **B** | `Allocate SHARED` presente e `DX59 Flags=0x00000000`, mas a recusa `E_INVALIDARG` continua | Hipótese (a) refutada. Testar a (b): segmento de memória do sistema no conjunto da alocação compartilhada. |
| **C** | A alocação passa, mas o Explorer ainda cai, ou surge erro novo (lock, abertura) | O próximo obstáculo do recurso compartilhado aparece nomeado no log; investigar ali. |
| **D** | Trava ou BugCheck | Usar `BCR1`/dump; a mudança é só na UMD. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Controle a não regredir: DWM apresentando, desktop visível, `FLUSH_TLB` p50 ~1,5 ms.
