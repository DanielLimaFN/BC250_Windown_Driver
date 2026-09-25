# Corrida 107 (DX.64): resultado; critério da Corrida 108 (mesmo pacote, 600 s)

> **Corrida 107:** 24/09/2026, bind às 17:46:38.
> - Rollback às 17:49:40.
> - `RECOVERY_OK_ONLINE` às 17:49:56, **no mesmo boot** (tentativa 1).
>
> **Candidato:** lab `3.268.1.0`, SYS `7C4E6E1B…`, UMD `B6AB6788…`, RADV `FCAA11FE…`.
> **Evidência:** `docs/evidence/2026-09-24_17-46-38-DESKTOP-HQAG6T4-lab-binding/`.
> - `display-log-this-bind-FINAL.txt`: 23.501.827 bytes, `complete_at_capture=true`.
> - `umd-logs-pos-travamento/`: 8 logs.
>
> **Critério:** [doc 415 §6](415_CORRIDA106_RESULTADO_TRIANGULO_COMPOSTO_E_DX64_CRITERIO_CORRIDA107.md).
> **Relato do Daniel:** "dessa vez não bugou".

## 1. Resultado: desfecho A

### As três condições de A

1. **Zero** `SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA` e **zero** `Draw: RECUSADO` no dwm.exe (106: 2 e 2).
2. **Zero** recriação do device do DWM durante a janela.
   - Um `D3D10 CreateDevice` às 17:46:47.
   - Um único `DestroyDevice`, às 17:49:43,659, **3 s depois** de o rollback começar (17:49:40). É a desmontagem da recovery.
3. **Zero** `PRIMARY_FALLBACK` e **zero** `SpaceResolveAddress failed: status=9` (106: 101 e 303).

### Controle

- **Cópias de imagem:** 1196 `ResourceCopyImage Vk`.
  - 2 com `LayoutDst=0->2` (UNDEFINED → COLOR_ATTACHMENT, exatamente o caso que falhava).
  - 1194 com `2->2`.
  - Zero `RECUSADO`, zero `FALHOU`.
- **Eixo 1 mantido:**
  - zero `BLT_DESTINO RECUSADO`, zero escrita no scanout;
  - `BYTE_PROOF` 3/3 PASS nos flips (#1/5/10);
  - #25 e #45 caíram em presents Blt e foram pulados com motivo, sem falha.

### Visual e estabilidade

- **Visual (relato):** a tela não degradou em 180 s.
- **R1** pela 3ª corrida seguida.
- Zero TDR, zero timeout, zero GFXHUB_FAULT, zero BugCheck.
- `SDMA_RETIRE_ARCHIVE NotifiedTotal=8980 NotifyFailed=0`; SDMA Present `Submitted=1588 Completed=1588 Fallbacks=0`.
- DWM com **470** `DXGI Present` OK (106: 109 depois da 2ª recriação), 530 presents no KMD.
- `COMPOSITE_BLT` com 529 OK e **0** recusados (106: 101).
  - Nesta corrida o Claude.exe abriu device e o Edge não; a comparação com a 106 não é controlada.

### Secundárias

- **Harness:** 60/60, FPS médio **1,1** (106: 1,6), sem medição que explique a diferença.
- **Preempção:** `PREEMPT_DONE Node=0` = 0; a DX.60 continua sem exercício.
- **Laço do Explorer ativo:** 84 recusas `Misc=0x2` em três processos Explorer; sonda 36/36 `OpenCalls=1 OpenStatus=0 DestroyCalls=1` (D3D, igual à 105).
- **Um `PRIMARY_COPY` de primária zerada** (#473, linha 169.609 de 170.564), perto do `StopDevice` na desmontagem.

## 2. Veredito

**Sucesso frente ao critério da 107 (A). Avanço funcional:** pela primeira vez, uma janela D3D11 composta pelo DWM ficou na tela por toda a janela de 180 s sem degradar, com recuperação no mesmo boot.

**Continuam NÃO VALIDADOS EM HARDWARE:**
- pixels do destino por máquina;
- estabilidade além de 180 s e o travamento duro (que aparecia entre ~0,65 e ~8,5 min);
- robustez à recriação do device (camada 2 do doc 415: tokens/VAs repetidos, `status=9`, soma dupla no `PRIMARY_FALLBACK`, não exercitada porque a recriação não ocorreu);
- velocidade (~1 FPS);
- DX.60;
- textura compartilhada do Explorer;
- jogo.

## 3. Corrida 108: mesmo pacote, janela de 600 s

**Justificativa:** o travamento duro (88–102) nunca foi medido com o DWM estável. Até a 106, toda janela longa carregava o laço de recriação ou o segundo escritor do scanout. Uma janela de 600 s com o candidato da 107, **sem nenhuma mudança de código**, responde se o travamento persiste neste estado. A única variável é a duração.

**Execução:**
- `tools\EXECUTAR_CORRIDA108_DX64_600S.cmd` (CRLF), depois de **boot frio**.
- Usa o pré-voo `prepare-corrida107-dx64.ps1` (mesmos hashes) e `BC250_WATCHDOG_SECONDS=600`, que é o máximo aceito pelo guardião.
- Pré-voo PASS com o runner novo na árvore.

**Durante a corrida:** se a tela congelar, anote a hora aproximada. O rastro DX.56 (`C:\BC250_PAGING_TRACE.bin`) e a cópia pós-recovery dos logs da UMD (DX.57) cobrem a evidência.

| | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem travamento, guardião reverte sozinho, R1 | Estabilidade de 10 min medida neste estado. O travamento das 88–102 dependia de algo que as DX.61–64 removeram (candidatos: recriação do DWM, dois escritores do scanout). Próximo: velocidade, ou a textura do Explorer. |
| **B** | Travamento duro (Kernel-Power 41, sem BugCheck), com hora | Independe do DWM estável. O último registro do rastro DX.56 e os logs da UMD recuperados dizem onde. Próximo: localizar pela trilha. |
| **C** | BugCheck (0x119/0x116/outro) | Seguir o dump. Para 0x119, a DX.60 passa a ser o suspeito a medir. |
| **D** | O desktop degrada ou o DWM recria o device | A camada 2 fica exposta. O log das ~5 s anteriores ao `DestroyDevice` nomeia o gatilho. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Regras:**
- Desfecho parcial não é desfecho.
- A exige os 600 s inteiros **e** a recuperação no mesmo boot.
- Um BasicDisplay Código 0 no boot novo não é R1.

**Secundárias:**
- FPS e presents/min do DWM;
- `COMPOSITE_BLT RECUSADO`;
- contextos/min;
- `PREEMPT_DONE Node=0`;
- relato visual.
