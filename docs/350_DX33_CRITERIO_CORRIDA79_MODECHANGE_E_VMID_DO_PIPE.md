# DX.33 — ModeChange não apaga o scanout; VMID do pipe gráfico sem seleção GRBM. Critério da Corrida 79

> **23/09/2026. OFFLINE; zero bind.** Parte do resultado da Corrida 78 ([docs/349](349_CORRIDA78_DX32_RESULTADO.md)).
> Critério escrito **antes** do ensaio (Leis 9 e 10). **NÃO VALIDADO EM HARDWARE.**

## 1. O que motiva (P0, Corrida 78)

- **Scanout apagado por mode change:** as 16 `PRIMARY_COPY` da rota `SET_ADDRESS` tinham `Flags=0x1`
  (`ModeChange`, d3dkmddi.h) e origem 0/256.
  - A origem era a primária do GOP `0xAAC000` e as primárias novas do DWM, ainda não desenhadas.
  - No modelo Blt o Present escreve direto no scanout, e a cópia apagava o quadro válido:
    - 86/256 → 0/256 em #5–#8 (o 1280×720 do harness);
    - 256/256 → 0/256 em #12.
  - Na Corrida 77 havia 8 de 8 `SET_ADDRESS` com origem zero, todas `ModeChange`.
- **`COMPUTE_VMID` inútil:** leu `0xCFBB8BBC` nos dois TDRs, sob `GRBM_GFX_CNTL=0` (C3).

## 2. O que mudou

| # | Mudança | Tipo |
|---|---|---|
| V1 | Na `SET_ADDRESS` com `ModeChange=1` e sem `FlipImmediate`/`FlipOnNextVSync`, a cópia para o scanout é pulada **só se** 1024 amostras da origem são todas zero. A linha `MODECHANGE_BLANK_SKIP` registra o NZ/hash do scanout preservado. Com origem não-nula, a cópia acontece como antes. MPO1/MPO3 (flip) nunca pulam por esta regra. | **Comportamental** (uma variável) |
| M1 | `[TDR_GFX_PIPE_VMID] PipeVmid=`: leitura de `CP_ME0_PIPE0_VMID` (mm 0x1DF2 → byte 0xC148, campo `VMID[3:0]`, `gc_10_1_0_offset.h`/`sh_mask.h`) e de `CP_RB_VMID` (0xC144), sem seleção GRBM. Bits fora de [3:0] marcam `INCOERENTE:`. Somente leitura (allowlist de leitura; fora da de escrita). | Medição |

**Classificação:**
- offset e campo de `CP_ME0_PIPE0_VMID` são **P1**;
- que o registrador reflita o VMID do IB em execução no momento do travamento é **H**;
- a linha `[TDR_GFX_VMID]` (`COMPUTE_VMID`) continua saindo, só para comparação; pelo docs/349 ela **não** é
  discriminante.

**Risco declarado da V1:** uma primária com conteúdo esparso que caia fora das 1024 amostras não é copiada até o
próximo Present ou flip, e o scanout fica com o quadro anterior.

**Verificação offline:**
- `build-contract` EXIT=0 e SYS de laboratório compilado;
- auditorias de segurança, matriz de DDI, callbacks no-op e RADV: PASS;
- a ferramenta de mutação DX.31 passa;
- portão novo `audit_dx33_modechange_skip_and_pipe_vmid`: **8 de 8 mutações reprovam**:
  - sem amostragem densa;
  - sem exigir `ModeChange`;
  - `SET_ADDRESS` aceitando flip;
  - MPO3 podendo pular;
  - MPO1 podendo pular;
  - offset errado;
  - sem a marca `INCOERENTE`;
  - registrador fora da allowlist.

**Pacote lab 3.240.1.0**, assinado, `Lab package audit: PASS`, NÃO INSTALADO:

| Artefato | SHA-256 |
|---|---|
| SYS assinado | `BC6F4FCEC0A3C2F5D0F1829A185CDD417AA2BC3F3B94AAB96662FC01A3D4A207` (fonte `81DF760F...`) |
| UMD | `239B046C...` (igual à 77 e à 78) |
| INF / CAT | `F099B3F5...` / `F2237EB0...` |
| ICD | `5988B38E...` (inalterado) |

As strings `MODECHANGE_BLANK_SKIP` e `TDR_GFX_PIPE_VMID` foram conferidas no SYS empacotado.

## 3. Critério da Corrida 79

### 3.1 Sinais
- **SKIP-ATIVO:** linhas `MODECHANGE_BLANK_SKIP`, e **zero** `PRIMARY_COPY ... END Route=SET_ADDRESS` com
  `SrcNZ=0/256` e `DstAfterNZ` menor que `DstBeforeNZ`.
- **SCANOUT-PRESERVADO:** depois de um `MODECHANGE_BLANK_SKIP` com `DstNZ>0`, a amostra seguinte do scanout (o
  `DstBeforeNZ` da próxima `PRIMARY_COPY` ou o Spread SCANOUT do próximo Present) continua não-nula.
- **PIPE-VMID:** em cada TDR, `PipeVmid` comparado com o VMID do `CARIMBO -0` (IB travado) e do `CARIMBO -1`
  (IB anterior).
- **Controles:**
  - autoteste 5/5 nas três variantes; NÃO MEDIDO por device perdido não conta como FAIL;
  - zero BugCheck;
  - zero `PREAMBLE_CNTL`;
  - `Eng=0` nas transições;
  - harness ≥ 1 quadro.
- **Visual (Daniel):** se o quadro do harness e/ou o desktop **ficam** na tela, em vez de piscar e sumir; e se o mouse se
  mexe.

### 3.2 Desfechos do display (V1)

**A — O scanout deixa de ser apagado**
- Assinatura: SKIP-ATIVO e SCANOUT-PRESERVADO, e o Daniel vê imagem persistente (quadro do harness ou desktop), mais
  tempo visível que na 78.
- Implicação: a rota `SET_ADDRESS` era o apagador. Próximo: TDRs e desktop sustentado.

**B — O pulo acontece, mas a tela continua preta**
- Assinatura: SKIP-ATIVO, com scanout não-nulo nas amostras do KMD, e o Daniel vê preto.
- Implicação: o desacordo passa a ser entre a VRAM do scanout e o monitor, não da cópia. Investigar a saída (leitura de
  DCN, somente leitura) antes de mexer em cópia.

**C — O pulo acontece e não havia o que preservar**
- Assinatura: SKIP-ATIVO só com `DstNZ=0`, porque não houve Present com conteúdo antes das trocas de modo.
- Implicação: V1 não foi testada no caso que importa; não concluir.

**D — V1 prejudica**
- Assinatura: o Daniel vê um quadro velho congelado onde antes via o novo, **ou** um `MODECHANGE_BLANK_SKIP` seguido
  de Present que não atualiza o scanout.
- Implicação: reverter V1 antes de interpretar o resto.

### 3.3 Desfechos dos TDRs (M1; nenhuma mudança de comportamento no anel)

**T0 — Sem TDR.** Descrever; não atribuir à DX.33, que não mexe no anel.

**T1 — O pipe roda com o VMID anterior**
- Assinatura: TDR de 240 ondas pós-troca, com `PipeVmid` sem `INCOERENTE:` e igual ao VMID do `CARIMBO -1`.
- Implicação: o dispatch do anel gráfico não troca de VMID junto com o `INDIRECT_BUFFER`. Próximo, por leitura upstream:
  `CONTEXT_CONTROL`/estado CS na troca.

**T2 — O VMID do pipe é o certo**
- Assinatura: `PipeVmid` coerente e igual ao VMID do `CARIMBO -0`.
- Implicação: não é VMID errado. Investigar `ACQUIRE_MEM`/visibilidade e o shader de cópia.

**T3 — Leitura sem utilidade**
- Assinatura: `PipeVmid` `INCOERENTE:`/`INDETERMINADO:`, ou igual a nenhum dos dois.
- Implicação: nem este registrador responde. Parar de buscar o VMID por MMIO no TDR; próximo, marcador escrito pela
  própria GPU no IB.

**T4 — Classe diferente.** Descrever antes de julgar.

### 3.4 Transversais

**F — Regressão de controle**
- Assinatura: autoteste com FAIL (não NÃO MEDIDO), BugCheck, `PREAMBLE_CNTL` ou transições sem `Eng=0`.
- Implicação: atribuir antes de ler o resto.

**G — Fora do mapa:** descrever antes de julgar.

Desfecho parcial não é desfecho (Lei 10.3). Display (A–D) e TDR (T0–T4) são julgados separadamente.

### 3.5 Execução
- A marca `C:\BC250_REQUIRE_CLEAN_BOOT.json` **está armada**. Fazer boot frio completo (`shutdown /s /t 0`, sem Fast
  Startup) e conferir preflight PASS.
- Depois rodar `tools\EXECUTAR_D3D11_NATIVO.cmd`, elevado, **sem `-Force`**.
- Anotar a imagem (o que aparece, se fica ou pisca) e se o mouse se mexe.
- Teto: uma corrida.
