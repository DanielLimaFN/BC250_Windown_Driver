# 712 — Corrida 270: flip pelo HUBP0 normal até o fim; trava ~1,1 s depois de o SearchApp nascer ao abrir o menu

**Data:** 12/10/2026. **Bind:** 18:40:11, lab **3.420.1.0** (SYS `87DB9853`).
**Evidência:** `docs/evidence/2026-10-12_18-40-11-DESKTOP-HQAG6T4-lab-binding/`, com:
- `display-log-this-bind-RECUPERADO.txt` e `paging-trace-this-bind-RECUPERADO.bin` (coleta automática);
- `umd-logs-pos-travamento/`;
- `paging-trace-decodificado-cauda.json`.

**Critério:** [doc 711](711_CORRIDA269_DCN1_FLIP_PELO_HUBP0_FUNCIONOU_CONTABILIDADE_DO_LATCH_E_CRITERIO_CORRIDA270.md) §5. **NÃO VALIDADO EM HARDWARE** além do descrito.

## 1. Linha do tempo (P0)

| Hora | Fonte | Evento |
|---|---|---|
| 18:40:19–21 | monitor, KMD | `[DCN1_D2]` igual ao da 269; `DcnFlip=1`, `ARMADO`; desktop sobe |
| 18:40:21–33 | KMD | flips `SET_ADDRESS` e MPO3 com readback OK e latch visto (amostras #1–#16: 8–52 ms); 202 Presents; zero recusa, TDR e fault |
| 18:40:33,74 | UMD | `StartMenuExperienceHost` e `SearchApp` carregam a UMD: o menu foi aberto |
| 18:40:34,86 | UMD | último `Present` do menu (hr=0) |
| 18:40:34,89 | UMD | `SearchApp` cria o 3º recurso, render target 2592×1088 (11,4 MB) |
| ~18:40:34,9 | rastro BEV1 | último evento: `SUBMIT_SDMA` do IB de paginação **2119** (4.672 bytes, 5 pontos de divisão), sem RETIRE; 2117 e 2118, do mesmo padrão, aposentaram |
| 18:41:46 | Windows | Kernel-Power 41 (reset pelo botão); fusível dispara no boot seguinte |

A máquina morreu **~1,1 s depois de o SearchApp nascer** e ~15 s depois do desktop, **antes da T3**.

## 2. O que a corrida diz

- **Assinatura de sempre:** processo novo nos primeiros ~1 s, paginação em curso, IB do SDMA0 recém-entregue sem RETIRE (como a 267), zero TDR/fault/WHEA.
  - Com a 270, são 5 de 6 travas com carga nova dentro do primeiro segundo de um processo novo.
  - Desta vez o processo foi o SearchApp do menu, não a T3.
- **DCN.1 até o fim:** o último Present (#202) é um flip MPO3 normal, e não há recusa, readback falho ou flip pendente anômalo nas linhas amostradas.
  - Nada no DCN mudou no instante da trava.
  - O DCN.1 não aparece como causa. Com n=1 isso não o exclui; as cópias de 11 MB por flip saíram (ver o defeito 3.1) e a trava veio igual.
- **Perguntas do critério:**
  - **P1 (rotas na tela):** só o menu chegou a ser exercitado; o resto do roteiro não aconteceu.
  - **P2 (conta) e P3 (VSYNC):** **sem resposta.** O teardown não rodou (trava), e o `[DCN1_RESUMO] PERIODICO` só sai a cada 1.024 latches; a corrida fez ~200.
  - **P4:** desfecho (b), mas sem a T3: a trava veio na abertura do menu.
  - O desfecho segue §5 do doc 711: P1 parcial (menu), P2/P3 sem medida, P4(b) antecipado. Nenhuma letra fecha inteira.

## 3. Defeitos meus achados nesta corrida

1. **~~Flip com sub-retângulo ainda copia 11 MB ao scanout do GOP~~ — CORRIGIDO: era só o log, não cópia.**
   - A linha `Blt 11059200 bytes ... -> scanout 0x0: SDMA=YES` é impressa depois do desvio da DCN.1 com `sdmaDone=TRUE`, então aparece logo abaixo de cada `DCN1_SEM_COPIA` (Presents #1–#8 lado a lado no log).
   - O sub-retângulo do #180 está em `SubRects`, não em `SrcRect` (0,0)-(0,0); `isSubWindow` era falso e o desvio aplicou.
   - **A DCN.1 tirou todas as cópias de 11 MB do SDMA0 na 270.** A pergunta "as cópias importam?" já foi respondida: a trava veio igual sem elas (P2, n=1).
   - No 3.421 a linha passa a dizer `SEM COPIA ao scanout do GOP (flip pelo HUBP0, DCN.1)`.
2. **Resumo periódico por contagem e não por tempo.** Numa trava precoce, ele nunca sai. Deve sair por tempo (por exemplo a cada 256 quadros do OTG) e a cada latch das primeiras dezenas.

## 4. Estado e próximo passo

- **Estado:** placa em Código 43 pelo fusível, com o 3.420.1.0 preso. Para voltar: `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset` e desligar.
- **Conclusão (com a correção do 3.1):** a apresentação pelo SDMA0 sai das causas suficientes da trava (P2, n=1); a DCN.1 fica como melhoria funcional.
- **Corrida 271 pronta:** lab **3.421.1.0** (SYS `CAB0136C`) = 3.420.1.0 + log de Present sem cópia correto + `[DCN1_RESUMO]` a cada 256 quadros do OTG (~4 s); portões provados por 2 mutações; `tools/EXECUTAR_CORRIDA271_DCN1_AMPLIADA_180S.cmd`, mesmo roteiro de 180 s. O critério do doc 711 §5 vale igual; desta vez P2/P3 têm resposta mesmo com trava precoce, pelo resumo periódico.
