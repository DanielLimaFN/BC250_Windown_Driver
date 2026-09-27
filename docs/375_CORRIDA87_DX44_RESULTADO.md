# Corrida 87 — DX.44: harness 16/60, preempção pedida depois de um reset silencioso do CP

> 23/09/2026. Bind às 15:13:55; rollback às 15:15:56.
> - Recuperação online **incompleta**: reboot automático, e no segundo boot
>   `RECOVERY_OK_ONLINE` às 15:17:12.
> - Não houve BugCheck: o `MEMORY.DMP` é das 09:42.
>
> **Candidato:** lab `3.248.1.0`, SYS `CD60A95F…`, conferido em
> `binding-provenance.json`; UMD inalterada; RADV DX.42 `FCAA11FE…`.
>
> **Critério:** [docs/374](374_DX44_PREEMPCAO_FALANTE_E_CRITERIO_CORRIDA87.md).
>
> **Evidência:** `docs/evidence/2026-09-23_15-13-55-DESKTOP-HQAG6T4-lab-binding/`.
> - FINAL `complete_at_capture=false`, com contagens coerentes.
> - Coleta RADV pós-rollback **presente** (`post-recovery-collection.json`, 317
>   IB, 11 fluxos).

## 1. Houve avanço?

**Sim, funcional.**

- **Harness 16/60**, o melhor da série. O recorde anterior era 12/60, na
  Corrida 78; a 86 fez 2/60.
- **Foto do Daniel às 15:14, antes do rollback (P0 de horário):** a janela
  "BC-250 Direct3D 11 Nativo (bc250umd.dll)" mostra o triângulo colorido
  renderizado, composto sobre um desktop **íntegro** (ícones, barra de tarefas,
  outras janelas).
  - É a primeira imagem da série com desktop inteiro e o quadro D3D11 no
    lugar.
  - O desktop estava travado, sem entrada.
  - É observação visual, não prova byte a byte da tela inteira. O log tem 2
    `BYTE_PROOF PASS` de tela cheia (256/256).

| Medida | 85 | 86 | **87** |
|---|---|---|---|
| Trocas com aquecimento DX.43 | 0 | 106 | **123** |
| TDRs com ondas no VMID anterior | 4 | 0 | **0** |
| Faltas `Cid=9` | 4 | 0 | **0** |
| TDRs | 4 | 1 | **1** |
| Harness | 0/60 | 2/60 | **16/60** |
| Autotestes RADV 5/5 | 1/3 | 3/3 | **3/3** |

## 2. Sucessos

- **Segunda amostra da DX.43 (P0):** 123/123 trocas com `Emitido=1`, **zero**
  TDRs de VMID anterior e **zero** faltas de busca de instrução. Somando as
  duas corridas: 229 trocas sem a classe que antes aparecia ~1 vez a cada 13
  trocas.
- **A DDI de preempção falou (P0):** `[PREEMPT_COMMAND] #1
  PreemptionFenceId=1 Node=1 Engine=0 … SEM BACKEND`.
  - Foi a **única** chamada da corrida, 7 linhas antes do único TDR.
  - Zero recusas.
- **Controles:**
  - 414+ identidades de preâmbulo sem prefixo zero;
  - restart GFX/SDMA provado;
  - `TDR_FENCE_RECONCILE PASS` (`Abortadas=2 Committed=2`);
  - coleta RADV completa, com a correção do wrapper exercitada.

## 3. Falhas

**O único TDR não é ocioso. É uma classe nova (P0):**

| Momento | RPTR | WPTR | SQ | Fence observada |
|---|---|---|---|---|
| submit da fence 314 (VMID 1) | 8188 | 8196 | `0x00F00F01` | 313 |
| submit da fence 315 (VMID 4, com aquecimento) | **5** | 8248 | 0 | 313 |
| TDR (snapshot antes do nosso quiesce) | **5** | 8248 | 0 | 313 |

- Entre o submit da 314 e o da 315, o CP foi **resetado e parado**:
  - no TDR, `CP_ME_CNTL=0x15000000` (`CE_HALT|PFP_HALT|ME_HALT`), que é o
    valor de power-on desta placa;
  - RPTR voltou para 5;
  - a fence 314 nunca terminou.
- **Nenhum log nosso nesse intervalo** toca o CP. Os escritores de
  `CP_ME_CNTL` no driver são o bring-up e o quiesce do TDR.
- O ME/PFP estavam no `RELEASE_MEM`, zero ondas, sem falta latchada.
- A varredura das corridas 84–87 acha esse reset silencioso **só aqui** (1 vez).
- A preempção veio **depois** do reset, como tentativa do agendador antes do
  TDR. **Não é a causa deste TDR.**

**Outras falhas:**
- **Harness:** o quadro 17 falhou com `DXGI_ERROR_DEVICE_REMOVED`, motivo
  `0x887A0020`.
- **Display:** desktop travado; 15 `DIRTY_RECTS_RECUSADOS` contra 1 aceito.
- **Recuperação:** a online ficou incompleta e pediu reboot automático (a da
  86 foi online).
- **Render target:** uma falta de escrita CB, a frente ainda aberta.

## 4. Veredito frente ao critério

- **Pergunta 1: desfecho C, com todos os sinais.**
  - Há linhas `[PREEMPT_COMMAND]` e **não houve TDR ocioso** (o TDR tem
    `Committed=2` e `RPTR≠WPTR`).
  - A preempção é pedida e ignorada; nesta corrida ela seguiu um travamento, em
    vez de causá-lo.
  - A causa do TDR ocioso da 86 continua aberta. A dívida da Lei 4 (DDI sem
    backend) está confirmada em uso real.
- **Pergunta 2: segunda amostra da DX.43, zero TDRs de VMID anterior.** Reforça
  o desfecho A da 86.
- **Fora do mapa (E):** reset silencioso do CP, com halt de power-on e RPTR≈0,
  sem ação nossa.
- **Função: falha.** 16/60, device removed, desktop travado.

## 5. Recuperação e pendências

- **Recuperação:** reboot automático do watchdog, depois `RECOVERY_OK_ONLINE`
  às 15:17:12, BasicDisplay em Código 0. **Desligamento completo antes de outra
  corrida.**
- **NÃO VALIDADO EM HARDWARE:**
  - quem reseta o CP;
  - causa do TDR ocioso da 86;
  - desktop íntegro e interativo;
  - 60/60.

**Próximos passos (offline, a decidir):**
1. **Reset silencioso do CP.**
   - H1: power gating ou GFXOFF do bloco gráfico pelo RLC/SMU perdendo o
     estado do CP. Isso casa com o valor de power-on.
   - H2: reset pelo firmware depois de erro do CP.
   - Medição barata: a cada submit, ler `CP_ME_CNTL` (0x86D8, já catalogado) e
     acusar `CP_RESET_DETECTED` quando o RPTR recuar sem wrap ou o ME estiver
     parado.
   - Antes disso, levantar no upstream (Linux `amdgpu` e SMU do Cyan
     Skillfish) se o GFXOFF é ligado nesta família e qual mensagem o desliga.
2. **Preempção:** implementar a conclusão para `DMA_BUFFER_BOUNDARY` (drenar o
   nó e reportar `DMA_PREEMPTED`). Hoje ela é pedida e fica sem resposta.
3. **Display/entrada:** o desktop trava com imagem íntegra. Os retângulos sujos
   seguem recusados; investigar se o travamento é o DWM parado por
   device-lost ou ausência de Present.
