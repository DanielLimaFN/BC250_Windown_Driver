# Corrida 98 revisada (DX.53): resultado

> **Data:** 24/09/2026, bind às 09:24:30 e rollback do guardião às 09:29:32.
> **Candidato:** lab `3.259.1.0`, SYS `2D95145B…B593`, UMD `8D4EE96D…A3CB`,
> RADV `FCAA11FE…`.
> **Evidência:** `docs/evidence/2026-09-24_09-24-30-DESKTOP-HQAG6T4-lab-binding/`.
> - Log do KMD: `display-log-this-bind-FINAL.txt`, 31.788.927 bytes, até o `RemoveDevice`.
> - Logs da UMD: `bc250_umd_log_*-FINAL.txt`.
>
> **Critério:** [doc 397 §5](397_DX53_VMID_DESARMADO_ANTES_DE_DEVOLVER_E_CRITERIO_CORRIDA98R.md).

## 1. Recuperação: primeira janela inteira desde a 93

- **Guardião:** `ROLLBACK_STARTED` às 09:29:32 por prazo, ou seja, sem confirmação, como previsto. Terminou em `RECOVERY_OK_ONLINE`, **sem reboot**, com BasicDisplay em Código 0 em 5 amostras.
- **Encerramento do KMD:** o fim do log é um encerramento ordenado:
  - `[SdmaPresent] Teardown Complete. TripodValidated=1, Submitted=1428, Completed=1428, Fallbacks=0, HostCpuCopies=0`;
  - `[StopDevice] VMID8 uninstalled`, seguido de `Global GFXHUB MMU restored … Regs=26`;
  - `[FUSE] Desligamento limpo registrado: fusivel em CLEAN`;
  - `RemoveDevice LEAVE status=0`.
- **Contra 94–97:** nas quatro houve travamento duro com Kernel-Power 41. O fusível recusou o boot seguinte (`[FUSE] STARTDEVICE RECUSADO` no log recuperado da 97).
- **Falhas de engine:** zero TDR_QUIESCE, zero IB_TRIPOD TIMEOUT, zero GFXHUB_FAULT e zero DEVICE_REMOVED (P0).

## 2. Controle: a DX.53 rodou como desenhada (P0)

| Sinal | 98R |
|---|---|
| `[DX53_VMID] DESARME ANTES DA DEVOLUCAO` | **157** |
| `[VMID_POOL] DESARMADO … status=0 Readback=1 Flush=1` | **157/157**, nos VMIDs 01 (1), 02 (137), 03 (5), 04 (11) e 05 (3) |
| `[VMID_POOL] LIBERADO … DEPOIS do desarme` | **157** |
| `LIBERADO` sem "DEPOIS" | 0 |
| `QUARENTENA` / `RETIDO EM INSTALACAO` | 0 / 0 |
| `Medido=1` | 157/157 |
| `PendentesNoRastreador` | **0 em 157/157**; `Reservadas=0` |
| `ANEL … criado` (lote) | 0: lote desligado, como na 97 |
| `DX50_BISSECCAO_DESLIGADO` | 19: dirty rects desligados, como na 97 |

**A outra janela da auditoria não ocorreu:** a do trabalho em voo no instante do desarme. Nas 157 medições sincronizadas com a ISR não havia fence pendente. É P0 para esta corrida.

## 3. Carga: equivalente à 97 (P0)

| Medida | 97 (até travar) | 98R (janela inteira) |
|---|---|---|
| `event=KMD_CONTEXT_CREATE` | 1202 em ~4,1 min (**~293/min**) | 1384 em ~4,8 min (**~290/min**) |
| Ciclos do laço do Explorer (DestroyDevice) | 156 em 4,0 min (**~39/min**) | 169 em 4,75 min (**~36/min**) |
| `DXGI Present LEAVE hr=0` por minuto (dwm) | ~110–134 | 87 / 112 / 120 / 70 / 16 (min 09:25…09:29; o último é parcial) |
| `COMPOSITE_BLT OK` | 583 | 475 |
| `SpaceResolveAddress status=9` (falha da ponte) | 0 | 0 |
| Harness D3D11 | 60/60 | **60/60 PRESENT_OK**, 1,2 FPS médio |
| Selftests RADV | 5/5 ×3 | **5/5 ×3** |

**Duração até o travamento em cada corrida:**
- 95: ~1,5 min (bind 23:30:42, fim ~23:32:17);
- 96: ~4,4 min (log do Explorer para às 23:56:38);
- 97: ~4,2 min (para às 00:18:21);
- 98R: ~5,0 min sem travar, com a carga medida até 09:29:25.

## 4. DX.52 respondeu (P0 da descrição)

O recurso que o Explorer não consegue criar, sempre o mesmo:

```
CreateResource RECUSADO desc: Dim=3 1024x832 Mips=1 Array=1 Fmt=87 Samples=1
Bind=0xA8 Misc=0x2 Usage=0 CpuAccess=0x0 TotalSize=3407872 RowPitch=4096 hr=0x80070057
```

**Decodificação (P1, `d3d10umddi.h`):**
- Texture2D;
- `DXGI_FORMAT_B8G8R8A8_UNORM` (87);
- Bind = SHADER_RESOURCE (0x08) | RENDER_TARGET (0x20) | **PRESENT (0x80)**;
- Misc = **`D3D10_DDI_RESOURCE_MISC_SHARED`**.

**Conclusão:** é uma superfície de apresentação **compartilhada**, que outro processo abre. A falha é `E_INVALIDARG` no `pfnAllocateCb`, com `hRTResource` preenchido.

**No KMD:**
- zero `[CREATE_ALLOC_RECUSADO]`, então o KMD não recusou nada;
- isso casa com o doc 396, onde o mesmo token saía com `CreateAllocation Result=0`.

**A recusa é do dxgkrnl, especificamente para recurso SHARED:**
- **P2:** o critério da 98 original, desfecho B, previa "dxgkrnl recusa antes de chamar o KMD". Aqui o KMD chega a aceitar.
- **H:** a causa exata. Candidatos a conferir offline: o contrato de alocação compartilhada (dados privados no nível do recurso e flags de compartilhamento na chamada da UMD) e o `DescribeAllocation`/`OpenAllocation` no processo que abre.

## 5. Fora do mapa (descrito antes de julgar)

- **O shell caiu duas vezes no primeiro minuto:**
  - `Explorer.EXE` às 09:24:49 e `explorer.exe` às 09:25:23;
  - as duas com `0xc0000602` (fail-fast) em `KERNELBASE.dll+0x12d862`, cada uma seguida de Winlogon 1002 ("o shell parou … e foi reiniciado");
  - nas duas, o log da UMD do processo termina numa destruição de device, 5–7 s antes do crash, sem nenhuma chamada nossa no intervalo;
  - depois disso, a instância seguinte (pid 9988) rodou o laço por 4 min sem cair.
- **SearchApp.exe caiu duas vezes:** `0xc000027b`, também no mesmo offset do KERNELBASE.
- **Contra 96 e 97:** nas janelas delas não há evento 1000/1002 de Explorer.
- **LiveKernelEvent 0x193/804:** houve três, a 1–2 s de cada queda. É a mesma assinatura que aparece também fora de corridas (por exemplo, 04:31 e 05:16), então não é específica.
- **Atribuição:** **H**. É compatível com o Explorer desistir pelo `E_INVALIDARG` repetido (mesmo offset do fail-fast histórico do DWM), mas a 97 teve o mesmo laço sem queda. **Não é atribuível à DX.53 por esta evidência.**
- **Aplicativos no nosso driver:** Claude.exe, msedge e msedgewebview2 abriram device D3D11 pelo nosso driver (3 CreateDevice cada), como na 97.

## 6. Veredito frente ao critério

**Desfecho A, assinatura cumprida:**
- janela inteira sem travamento, com o guardião revertendo sozinho;
- carga equivalente (~290 contra ~293 contextos/min);
- harness 60/60.

Nenhum dos sinais que definem B, C ou D apareceu (zero quarentena).

**Confiança da implicação ("a janela do VMID era a causa"): P2, não P0.**
- **Amostra:** é uma corrida.
- **Margem:** as corridas anteriores travaram entre ~1,5 e ~4,4 min. A 98R foi até ~5,0 min, só ~0,6 min além do travamento mais tardio (96).
- **O que já está provado:** a correção eliminou uma corrida de dados real (P0 de código), e a primeira janela limpa desde a 93 veio exatamente com ela.
- **O que falta:** para dizer que o travamento acabou, é preciso janela bem maior que a pior margem anterior.

## 7. Recuperação e pendências

- **Driver ao final:** BasicDisplay em Código 0 (recovery online), fusível removido, sem reboot.
- **Boot frio:** continua obrigatório antes do próximo bind (estado de engine).
- **NÃO VALIDADO EM HARDWARE:**
  - estabilidade além de 5 min;
  - estabilidade com o lote religado (o lote causava o piscar, doc 393);
  - desktop sem piscar. O operador não descreveu o desktop, só o rollback; zero falhas de ponte no KMD, como na 97.
- **Próximo passo recomendado:**
  1. **Replicação com janela de 600 s, sem mudança de código.** É a única variável que transforma P2 em P0. Se mudar o laço do Explorer agora, a carga que abre a janela some e a replicação deixa de ser possível. Se travar entre 5 e 10 min, a DX.53 era necessária mas não suficiente.
  2. **Em paralelo, offline:** investigar a recusa do recurso SHARED/PRESENT 1024×832 (causa do laço do Explorer e provavelmente das duas quedas do shell).
