# Auditoria 13/09/2026 — a parede é a primária, não o scanout

> **Escopo:** revisão da corrida 16:28:27 (pacote 3.172.0.0) e do caminho de Present nos dois lados.
> **Modo:** OFFLINE, zero corridas físicas.
> **Veredito:** o diagnóstico de [`docs/265`](265_PASSAGEM_DE_BASTAO_2026-09-13_16-33.md) **não se
> sustenta na própria evidência dele**. A parede está uma etapa antes, e é defeito nosso.

---

## 1. O que a corrida 16:28:27 provou, e vale

| Fato | Confiança |
|---|---|
| BugCheck `0x10E` eliminado; zero tela azul, zero reboot, zero TDR em 180 s | P0 |
| 3.757 submissões indiretas no Node 0 sob VMID 8, todas com tripé provado | P0 |
| Rollback online limpo pelo watchdog (`RECOVERY_OK_ONLINE`), BasicDisplay em Código 0 | P0 |

Isso é **estabilidade sob carga**. É real e não está em disputa.

## 2. O que ela não provou, contra o que o 265 afirmou

### 2.1 Zero `DxgkDdiPresent` na corrida inteira

Não há **uma** linha `[Present #N]` em 5,4 MB de
`display-log-this-bind-FINAL.txt`. A ausência é **conclusiva**, e por três razões
independentes:

- `[Present #...]` não casa com nenhuma classe de amostragem de
  `Bc250ShouldEmitVerboseLog` — cai no `else { return TRUE; }`, que emite sempre;
- `FrameBufferVirtual` é não-NULL (`[DisplayInit] FrameBuffer mapping: … Virt=FFFFBAF97B800000`),
  então o corpo de apresentação não foi pulado por essa guarda;
- o ramo de recusa contém `REJECT`, que é isento de amostragem por construção.

**O caminho de transferência para o scanout do GOP é código morto nesta corrida.** O 265
propõe consertar código que nunca executou.

### 2.2 Não existe quadro composto

**1787 de 1787** `COMPOSITE_BLT` terminam em `Prova=ZERO_INCONCLUSIVO`, precedidos por:

```
[Bc250Escape] COMPOSITE_DATA_PROOF INCONCLUSIVO_ZERO: Match=5/5; zero contra zero NAO prova pixel
```

Os 5.165 escapes moveram **zeros** entre buffers. Contar operação como progresso é a **quinta**
ocorrência da classe "o gate carimbava o falso sucesso" neste projeto (A15,
`mcdm_callbacks_test`, C1, `sdma_blt_executed`).

### 2.3 Nenhum destino foi o scanout

Os sete `DstMc` distintos da corrida são todos backbuffers do DWM:

```
1134  DstMc=0xF40208C000      57  DstMc=0xF400CDC000      19  DstMc=0xF400B2C000
 521  DstMc=0xF400B8C000      19  DstMc=0xF400CEC000      18  DstMc=0xF40153C000
                              19  DstMc=0xF400B9C000
```

O scanout é `0xF400000000`. Zero blts para lá.

### 2.4 O `dwm.exe` está em laço de device-lost, 90 vezes

O padrão no log da UMD (pid 1232) é rígido e se repete a ~1 Hz:

```
DXGI Present → (≈200 linhas destruindo todo recurso) → DestroyDevice → CloseAdapter
             → bc250umd.dll CARREGADA → CreateDevice → NODE1_SUBMISSION_READY → …
```

**O Present falha.** E é `Flags=0x00000002` = **`Flip`** (`dxgiddi.h:71`), não `Blt`.

### 2.5 Não há rasterização — e o próprio log declara isso

```
SHADER_TRANSLATOR: TokenStream -> SPIR-V 1996 bytes (estagio=1).
Traducao NAO e' pipeline: descriptors, render target, anel grafico e Draw seguem ausentes.
```

O `Draw` é substituído por cópia 1:1 de superfícies que ninguém preencheu. Daí o zero universal.

---

## 3. Causa raiz do Present falhar

> **Confiança: H alta.** Código e log concordam. **Não é P0** porque o `hr` do `pfnPresentCb`
> nunca foi registrado — defeito de instrumentação corrigido nesta mesma faixa.

A UMD e o KMD discordam do **nosso próprio contrato de alocação**:

| Lado | O que faz | Consequência |
|---|---|---|
| UMD `Bc250WinsysAllocate` | `memset(&args,0,…)` e **nunca** atribui `hResource` (`d3dumddi.h:3457`), `Flags.Primary` nem `VidPnSourceId` (`d3dukmdt.h:554`, `:549`) | a primariedade existe só no token privado |
| dxgkrnl | só liga `DXGKARG_CREATEALLOCATION.Flags.Resource` quando a UMD passa `hResource` | `Flags.Resource == 0` sempre |
| KMD `bc250_active_miniport.c` | o único ramo que marca primária está atrás de `Flags.Resource != 0u` | **ramo inalcançável**; `Bc250AllocationIsPrimary` nunca é consultado |

Confirmado no log da corrida:

```
[CreateAllocation] ENTER: NumAllocations=1, Flags=0x00000000 (Resource=0), hResource=0000000000000000
[CreateAllocation] Non-resource allocation: Handle=…, Token=0x5245530004D00016, Size=11059200, CpuVisible=0
```

Há **um único** `Primary token confirmed` na corrida, e é o `Resource=1` do bind — handle
`0000000800000008`, o mesmo do `CommitVidPn` e do `SetVidPnSourceAddress 0xAAC000`. Vem do
caminho de display, não do DWM.

**Cadeia:** alocação não-primária → `Flip` não pode ser honrado → `pfnPresentCb` falha →
DWM perde o device → recria → repete → `DxgkDdiPresent` nunca é alcançado.

---

## 4. Faixa aplicada (offline)

### (A) Vínculo de recurso — `driver/umd/bc250_wddm_winsys.{c,h}`, `bc250_d3d_umd.c`

- `Bc250WinsysAllocateForResource` nova, com `BC250_WINSYS_RESOURCE_BINDING`
  (`RuntimeResource` / `VidPnSourceId` / `Primary`).
- `Bc250Umd10CreateResource` passa `hRTResource.handle`.
- **Fonte única de primariedade:** a classe do token manda; vínculo que **discorde** dela falha
  fechado. Sem isso voltaríamos a ter duas verdades — a classe de defeito que a faixa corrige.
- **Fail-closed:** primária sem `hRTResource` é **recusada antes do callback**. Suceder calado ali
  produziria de novo uma "primária" que o kernel nunca veria como tal.
- **Desalocação simétrica:** criada com recurso, devolvida com recurso (`d3dumddi.h:4068`),
  senão o objeto de recurso do kernel fica órfão.

### (B) I1 — `Bc250DxgiPresent` registra o `hr`

Era `return device->pDXGICallbacks->pfnPresentCb(...)` direto. O número que decidia a corrida
nunca foi gravado. Agora `ENTER cb` (contexto, alocações, primariedade, flags) e `LEAVE cb: hr=`.

### (C) I2 — `Bc250DxgkDdiPresent` deixa de ser mudo

Tinha **três** saídas mudas: `hContext == NULL`, `pPresent == NULL` e todo o corpo sob
`if (FrameBufferVirtual != NULL)`. A entrada agora é **incondicional e anterior a toda guarda**,
com contador próprio (8 primeiras + 1 em 256). A ausência da linha passa a ser conclusiva.

### (D) I3 — amostragem desagregada

`[McdmQAI]`, `[McdmCreateDevice]` e `[McdmCreateContext]` dividiam **um** contador. A rajada de
QAI no bind (17 linhas medidas) esgota sozinha as 16 primeiras da classe, e por isso as 90
criações de device do DWM não deixaram **uma** linha. Agora são as classes 12, 13 e 14.

### (E) KMD — a primária de display não é sobrescrita

Até esta faixa o ramo era inalcançável e o único escritor de
`g_Bc250DisplayState.hPrimaryAllocation` era a primária do GOP. Agora as do `dwm.exe` também
chegam lá. O campo só é preenchido enquanto vazio; as demais são contadas e nomeadas
(`Primary token confirmed (UMD #N)`), nunca silenciadas.

### Efeito colateral declarado

Com `Flags.Resource` passando a 1 para recursos de render, o ramo
`else if (Flags.Resource == 0u && info->FlagsWddm2.CpuVisible)` deixa de aplicar-se a eles e
`CpuVisible` fica `FALSE` — o que **reforça** a correção do `0x10E` em vez de afrouxá-la.
`ResourceMap` usa shadow de CPU (`calloc`), não `pfnLockCb`, então não há regressão de mapeamento.

---

## 5. Testes e mutações

`build-umd EXIT=0` · `build-contract EXIT=0` · três auditorias `PASS`.

Portões novos: `audit_primary_allocation_is_declared_to_the_kernel` e
`audit_present_path_is_not_mute`.

**Nove mutações verificadas:**

| # | Mutação | Quem reprovou |
|---|---|---|
| 1 | remover `args.hResource` | `bc250_wddm_winsys_test:552` |
| 2 | zerar `info.Flags.Primary` | `bc250_wddm_winsys_test:553` |
| 3 | desalocação assimétrica | `bc250_wddm_winsys_test:561` |
| 4 | afrouxar a recusa de primária sem recurso | `bc250_wddm_winsys_test:591` |
| 5 | reverter a divisão de amostragem | `audit_present_path_is_not_mute` |
| 6 | remutar a saída `hContext NULL` do `DdiPresent` | `audit_present_path_is_not_mute` |
| 7 | voltar ao `return` direto no Present da UMD | `audit_present_path_is_not_mute` |
| 8 | `CreateResource` sem vínculo | `audit_primary_allocation_is_declared_to_the_kernel` |
| 9 | KMD sobrescrever a primária de display | `audit_primary_allocation_is_declared_to_the_kernel` |

**Armadilha registrada:** a primeira passada da mutação 4 reprovou pelo `assert` **errado** — a
allowlist de token do stub disparava antes do meu invariante. Falha não-atribuível é portão
fraco. O stub foi corrigido para que quem reprove seja `AllocateCalls não mudou`, que nomeia a
causa.

---

## 6. Esta faixa não acende a tela, e isso é esperado

Ela remove a **parede de apresentação**. A **parede de rasterização** continua inteira: sem
pipeline gráfico, sem descriptors, sem render target binding, sem `Draw` PM4, nenhum pixel é
produzido por ninguém.

**Tela preta segue sendo o resultado correto.** Qualquer imagem que aparecer deve ser atribuída
ao rollback/BasicDisplay até prova em contrário — o mesmo engano já cometido e corrigido em
07/09 18:16 e em 10/09 11:22.

## 7. Critério da próxima corrida, escrito antes

1. `[CreateAllocation] Primary token confirmed (UMD #1)` **e** `[DdiPresent] ENTER #1` aparecem
   → a cadeia de apresentação fechou; o laço de device-lost tem de parar, e
   `DXGI Present LEAVE cb: hr=0x00000000` confirma pelo lado da UMD.
2. `DXGI Present LEAVE cb: hr=` com `hr != 0` e **nenhum** `[DdiPresent] ENTER` → o dxgkrnl ainda
   recusa, e **agora o `hr` nomeia o motivo**. A hipótese da primária cai, e o próximo suspeito
   passa a ser o nó do contexto — `Winsys.KernelContext` é de **COMPUTE (node 1)**.
3. Nenhuma das duas → a coleta falhou; defeito de binder, não de driver.

**Informação nova em qualquer desfecho**, o que não era verdade na corrida 16:28.

---

## 8. Pacote candidato

**`3.173.1.0` — assinado, auditado e NÃO INSTALADO.**

| Campo | Valor |
|---|---|
| BuildKind | `MCDM_LAB_BRINGUP` |
| Perfil | `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`) |
| Contrato WDDM | 2.7 · modo P1 `PERSISTENT SUBMISSION` · schema 135 |
| SYS fonte | `529B645B7FAA72FBC75C67F5263A375A08EA223B046A10776271F9AB05989545` |
| SYS assinado | `8D07EBB44AAD4F0E19369CC695DA36FA87FD5CEA25069E18D590B1DE532D0CD2` |
| UMD assinada | `AEB6A452C9AC66A726FDCC0473D29E6E3FAC53A86ED28F1F31D7A78FE1939C03` |
| INF | `AE3E88EB74CF672BBF8601685B1896F981D033C97C56943A9C2B14A818C364AD` |
| CAT | `9480733D0FC526C237E3A4C925AF5CD019066BB82F63F8316272756826CDD674` |

Lab package audit **PASS**. Os binários foram **conferidos por string** — o empacotador não
compila, só copia:

- SYS assinado: `[DdiPresent] ENTER #`, `RECUSADO: FrameBufferVirtual NULL`,
  `Primary token confirmed (UMD`
- UMD assinada: `DXGI Present ENTER cb:`, `DXGI Present LEAVE cb: hr=`, `Allocate ENTER: Token=`,
  `primaria sem hRTResource`

> ⚠️ **Correção de registro:** o [docs/265](265_PASSAGEM_DE_BASTAO_2026-09-13_16-33.md) chama a
> corrida 16:28:27 de "pacote 3.172.0.0". **É falso** — o INF daquele `output_lab` diz
> `DriverVer=3.170.1.0`, e o SYS instalado era `59E08349…`, o mesmo que
> `tools/_last_lab_binding.json` registra. O `DriverVer` fonte foi para **3.173.0.0** (candidato
> 3.173.1.0) para não restar dúvida de identidade.

**Estado da máquina no empacotamento:** BasicDisplay em Código 0, fusível **inexistente** (próximo
boot assume `CLEAN`), sem `C:\BC250_REQUIRE_CLEAN_BOOT.json`. **Ainda assim o boot frio é
obrigatório:** o rollback de 16:28 foi *online*, sem reboot, então o estado de engine da corrida
anterior continua vivo e não cai com desbind.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
