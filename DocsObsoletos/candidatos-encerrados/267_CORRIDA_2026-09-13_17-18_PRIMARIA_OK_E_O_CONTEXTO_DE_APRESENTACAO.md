# Corrida 13/09/2026 17:18:35 — a primária fechou, o Present não

> **Pacote:** lab `3.173.1.0` · **Evidência:** `docs/evidence/2026-09-13_17-18-35-DESKTOP-HQAG6T4-lab-binding/`
> **Desfecho:** o **2** do critério escrito antes da corrida.
> **Anterior:** [`docs/266`](266_AUDITORIA_2026-09-13_A_PAREDE_E_A_PRIMARIA_NAO_O_SCANOUT.md).

---

## 1. Estabilidade — sem regressão

| Sinal | Contagem |
|---|---|
| `TDR_QUIESCE` | 0 |
| `IB_TRIPOD TIMEOUT` | 0 |
| `IB NAO EXECUTADO` | 0 |
| `DEVICE_REMOVED` | 0 |
| `StopDevice` | 0 |
| `GFXHUB_FAULT` | 0 |

Rollback online limpo: `RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em 5 amostras.

## 2. O que a correção da primária conseguiu — P0

| Medição | 16:28 (3.170.1.0) | 17:18 (3.173.1.0) |
|---|---|---|
| `CreateAllocation` de origem UMD | `Resource=0` → *Non-resource allocation* | **7/7 `Resource=1`** |
| Lado da UMD | não existia | `SrcPrimary=1 SrcDeclaredPrimary=1 SrcRTResource=0000000006010000` |
| `SetVidPnSourceAddress` | `0xAAC000`, `hAlloc=0x08` (primária do GOP) | **`0x4B5C000`, `hAlloc=0xA8`** — alocação criada pela UMD |
| `[DdiPresent] ENTER` | **0** em toda a história do projeto | **6** |

O `dxgkrnl` só aponta o source address para uma alocação que ele aceita como primária. Os 6
Presents fecharam até o silício:

```
[DdiPresent] ENTER #1: hContext=0000000700000007 pPresent=FFFFC885539AA140 FrameBufferVirtual=FFFFDAFFC9600000
[Present #1] Source=PRESENT_SLOT SegOff=0x4B60000 Seg=1 SegBase=0xA8C000 -> VramOff=0x55EC000 (resolved=1)
[Present #1] Blt 11059200 bytes (src 0x55EC000 -> scanout 0x0): SDMA=YES
[SubmitVirtual Node0 SDMA] IB EXECUTADO (INDIRECT): ... 4 bytes em VA=0x10000; FenceId=833
```

## 3. Tela preta pelo motivo previsto

6 de 6:

```
[Present #N] AVISO: Superficie de origem ... esta com todos os pixels em zero (NonZero=0/256)
[Present #N] BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO)
```

A parede de rasterização segue inteira. Não havia pixel para apresentar.

## 4. A hipótese da primária está refutada como causa suficiente — P0

**84 de 84** `pfnPresentCb` devolveram `hr=0x80004005` (E_FAIL), **no mesmo milissegundo** da
chamada — rejeição síncrona de validação, não falha de submissão — com a primária já
corretamente declarada:

```
17:19:17.953  DXGI Present ENTER cb: ... SrcPrimary=1 SrcDeclaredPrimary=1 SrcRTResource=0000000006010000 Flags=0x00000002
17:19:17.953  DXGI Present LEAVE cb: hr=0x80004005
```

O laço de device-lost do `dwm.exe` continua: 61 `CreateDevice`/`DestroyDevice`. **O I1 pagou** —
o número que decidia a corrida agora existe.

## 5. Correção do meu próprio raciocínio

Eu escrevi que o próximo suspeito seria "o nó do contexto (COMPUTE, node 1)". **Errado como
formulado.** O `GetNodeMetadata` da própria corrida:

```
[GetNodeMetadata] Node=0, EngineType=6   (COPY)
[GetNodeMetadata] Node=1, EngineType=1   (3D)
```

Node 1 **não** é um nó de compute para o `dxgkrnl`. O que sobrevive é mais estreito e é medição.
Decodificando `DXGK_CREATECONTEXTFLAGS` (`d3dkmddi.h:1518`, P1 — bit0 `SystemContext`,
bit1 `GdiContext`, bit2 `VirtualAddressing`):

| Contexto | Node | Flags | Decodificado | Apresentou? |
|---|---|---|---|---|
| `0x04` | 0 | `0x05` | SystemContext + VirtualAddressing | — |
| `0x07` | 0 | `0x06` | **GdiContext** + VirtualAddressing | **sim, os 6** |
| todos os nossos | 1 | `0x04` | VirtualAddressing apenas | **nunca** |

**Não nomeio a causa do E_FAIL.** `0x80004005` é genérico e a corrida não a determinou. É **H**.

## 6. Ponto cego meu, declarado

`Primary token confirmed (UMD` aparece **0 vezes**, e isso **não é conclusivo**. A classe 9 do
amostrador (`[CreateAllocation]` + `[ActiveOpenAlloc]` + `[McdmCreateAllocation]` +
`[McdmCloseAllocation]`) somou **45 linhas** contra o orçamento de 16 primeiras.

É o **mesmo** defeito de contador compartilhado que corrigi na classe 12 e não corrigi na classe
9. Meu próprio I3 foi incompleto, e o buraco caiu exatamente sobre a medição que eu queria.

## 7. Faixa aplicada (offline)

### (A) I3 completado
As quatro tags da classe 9 ganharam contadores próprios (9, 15, 16, 17). A ausência de
`Primary token confirmed (UMD` passa a ser conclusiva.

### (B) Contexto de apresentação dedicado
`Bc250WinsysCreatePresentContext` / `Bc250WinsysDestroyPresentContext` novas. O Present vai por
`Winsys.PresentContext`, criado no **Node 0**; o contexto de trabalho continua no Node 1.

`SystemContext`/`GdiContext` são decididos no kernel e não estão em
`D3DDDI_CREATECONTEXTFLAGS` (`d3dukmdt.h:791`) — logo **`NodeOrdinal` é a única variável sob
nosso controle**, e o ensaio move exatamente essa uma.

**Sem fallback:** a ausência do contexto de apresentação **recusa com motivo nomeado**. Cair de
volta no contexto de trabalho reproduziria o estado já medido falhando 84/84 e misturaria as
duas variáveis — "E_FAIL de novo" deixaria de significar "falhou pelo Node 0".

O teardown destrói os **dois**: o `dwm.exe` fez 61 ciclos de device em ~3 min, e contexto de
kernel não destruído vaza um por ciclo.

## 8. Testes e mutações

`build-umd EXIT=0` · `build-contract EXIT=0` · três auditorias `PASS`.

**Seis mutações verificadas (10–15):**

| # | Mutação | Quem reprovou |
|---|---|---|
| 10 | reverter a divisão da classe 9 | `audit_present_path_is_not_mute` |
| 11 | Present voltar ao contexto de trabalho | `audit_present_path_is_not_mute` |
| 12 | remover a recusa nomeada | `audit_present_path_is_not_mute` |
| 13 | criação do contexto de apresentação escrever no de trabalho | `audit_present_path_is_not_mute` |
| 14 | teardown perder a destruição | `audit_present_path_is_not_mute` |
| 15 | **stub devolver o mesmo handle para os dois nós** | `bc250_wddm_winsys_test:435` |

A mutação 15 existe porque "o gate congelava a mentira" já apareceu cinco vezes no projeto.

**Armadilha registrada:** a primeira versão do teste criava o contexto de apresentação em
`TestLifecycle` e o destruía em `TestResidencyAndPrimaryClass` — funções diferentes. A suíte
pegou. Três asserções de contagem que congelavam "1 contexto" viraram "2 criados, 2 destruídos",
e um stub que exigia o handle `0xC001` constante passou a comparar com a **fonte**
(`context.KernelContext`), para continuar valendo se o nó do contexto de trabalho mudar.

## 9. Pacote candidato

**`3.174.1.0` — assinado, auditado e NÃO INSTALADO.**

| Campo | Valor |
|---|---|
| Perfil | `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`), WDDM 2.7, P1 Persistent Submission, schema 135 |
| SYS fonte | `638D83F2833ACD9B…` |
| SYS assinado | `957A387346637B32F4230181E830800F45500E20871ADD5DAFADB82C776B208A` |
| UMD assinada | `90F90E2A318B18104238F0C905A6855CE166133703C4C2662D32625413FFF7C1` |
| INF | `B64F03205442E2DD9E30BCBC20E3B272DA90F5A99C6474788AC2A57AF3381A20` |
| CAT | `8F34DF1951CF1C16E6235C2F596BAB27F57D4D383292C2C135F7752E3B8A99CE` |

Conferido por string — SYS: `[DdiPresent] ENTER #`, `Primary token confirmed (UMD`,
`[McdmCloseAllocation]`, `[ActiveOpenAlloc]`. UMD: `CreatePresentContext ENTER`,
`DestroyPresentContext ENTER`, `PRESENT_CONTEXT: Node=`, `PresentNode=`,
`DXGI Present RECUSADO: contexto de apresentacao ausente`.

## 10. Critério da próxima corrida, escrito antes

1. `DXGI Present LEAVE cb: hr=0x00000000` → o nó do contexto **era** a causa; o laço de
   device-lost para e `[DdiPresent] ENTER` cresce muito além de 6.
2. `hr=0x80004005` com `PresentNode=0` na linha ENTER → apresentamos pelo Node 0 e ainda assim
   falhou. **O contexto morre como suspeito**, e o próximo passa a ser a forma da cadeia de flip
   (`hDstAllocation=0`, buffers do swapchain criados um a um, sem registro de cadeia).
3. `DXGI Present RECUSADO: contexto de apresentacao ausente` → a criação no Node 0 falhou;
   `PRESENT_CONTEXT: criacao FALHOU` diz o status e o ensaio não chegou a acontecer.
4. `Primary token confirmed (UMD #N)` → fecha por medição direta o que a corrida 17:18 só
   sustenta por indício.

**Não esperar desktop.** A parede de rasterização continua inteira. Tela preta segue sendo o
resultado correto.

**Estado da máquina:** BasicDisplay Código 0, fusível inexistente, sem marcador de boot limpo.
**Boot frio continua obrigatório** — o rollback de 17:18 foi *online*, sem reboot.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
