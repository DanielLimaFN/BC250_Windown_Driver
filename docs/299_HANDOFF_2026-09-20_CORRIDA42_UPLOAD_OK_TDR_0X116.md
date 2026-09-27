# 299 — Handoff da Corrida 42 — row pitch validado, CP_RB0 recorrente e BugCheck 0x116

> **Corrida física:** 20/09/2026, bind iniciado às 00:03:31.  
> **Candidato:** lab `3.200.1.0`, `DISPLAY_RENDER_LAB`, WDDM 2.7, schema 135.  
> **Evidência:** `docs/evidence/2026-09-20_00-03-31-DESKTOP-HQAG6T4-lab-binding/`.  
> **Critério escrito antes do ensaio:** [docs/298](298_CRITERIO_CORRIDA_42_ROW_PITCH_E_GATE_PRESERVADO.md).

## Veredito curto

**Houve avanço funcional parcial, mas a corrida falhou frente ao critério primário e terminou
em BugCheck.** A correção de cópia 2D/row pitch executou no silício e o runner preservou o gate.
Não houve descriptor bound, draw concluído, frame do harness nem pixel correto. O primeiro trabalho
GFX deixou o `CP_RB0` em `RPTR=24/WPTR=32`; cinco resets declararam recuperação, novas submissões
voltaram a travar e o Windows encerrou a sequência com `VIDEO_TDR_FAILURE (0x116)`.

### Adendo — análise elevada do `MEMORY.DMP`

Análise pública arquivada em `memory-dmp-public-analysis.txt`, SHA-256
`36CD8FD6B8788F6297D63100F748074D895CCB766E39B4E39D210D146B97BD0F`.

- `Failure.Bucket = 0x116_IMAGE_bc250mcdm.sys`;
- Arg2 `fffff803475ad770` pertence a `bc250mcdm.sys`, RVA `+0x1D770`;
- Arg3 é zero: o dump não registra um `NTSTATUS` final de operação como causa;
- a pilha é `TdrBugcheckOnTimeout -> TdrIsRecoveryRequired -> VidSchiReportHwHang ->
  VidSchWaitForCompletionEvent -> VidSchiWaitForCompletePreemption`;
- nenhum bucket de corrupção de memória, verifier, AV ou IRQL apareceu.

Isto fecha a atribuição do `0x116` ao nosso miniport e confirma que o scheduler terminou
esperando preempção/recuperação. Não identifica a função interna no RVA `0x1D770`, pois o PDB
privado exato de 3.200.1.0 não foi preservado. Não usar o PDB do candidato seguinte para
inventar esse símbolo.

O resultado não autoriza atribuir o TDR à correção de row pitch. O primeiro IB GFX tem a mesma
assinatura da Corrida 41 (`DmaVA=0x244FE4000`, 664 DW, inspeção interrompida em DW 655); naquela
corrida ele drenou até 32 e nesta parou em 24. A diferença medida é a execução do CP, não a forma
do IB nem uma falha de validação da cópia 2D.

## Sucessos medidos

- Identidade confirmada pelo binder: SYS
  `7B6E1F5832A2C7D08BAE440A55A681F142A9ACE81E4CCE8DFD54AB4649F0E901`, candidato
  `09/20/2026,3.200.1.0`.
- O gate foi preservado: **13** leituras `D3D11_LAB_PROMOTION: ARMADO` em processos novos,
  incluindo os dois `dwm.exe` e as duas inicializações do harness; **zero** leitura desarmada.
- Uma textura `2560x1080` foi copiada linha a linha com `SrcPitch=DstPitch=10240` e depois
  sincronizada com o `VkImage`. O upload Vulkan concluiu após fence com
  `RowPitch=10240`, `bufferRowLength=2560` e 11.059.200 bytes. Logo, a implementação genérica
  de row pitch rodou no hardware. O caso acolchoado específico `590x800`, porém, não apareceu
  antes do colapso e continua sem prova física direta.
- O Node 0 permaneceu ativo: são **513** linhas `IB EXECUTADO (INDIRECT)` no bind
  (10 no arquivo inicial e 503 na cauda recuperada); não houve `GFXHUB_FAULT`.
- Sete Presents do KMD chegaram ao blt por SDMA (`SDMA=YES`). Isso não equivale a quadro
  correto: as amostras permaneceram em branco.
- Cinco ciclos de TDR executaram quiesce e retornaram sucesso de restart para SDMA/MEC/GFX.
  Esse fato prova que as DDIs foram chamadas e que os readbacks de rearmamento passaram; não
  prova que o CP_RB0 voltou a executar trabalho útil, como demonstrado pelas submissões seguintes.

## Falhas e sequência causal medida

1. A primeira submissão GFX foi aceita como `Fence=2`, `NewWptr=32`, IB de 664 DW sob VMID 7.
   O CP avançou apenas até `RPTR_Reg=24` (`RPTR_Wb=20`) e a fence do KMD não mudou de 1 para 2.
   Assim, a perna 1 não convergiu e a submissão WDDM não aposentou.
2. No TDR, `IB1 Base=0x244FE4000` e `RestamDW=0`; não havia ondas em voo. Os últimos headers
   observados foram `EVENT_WRITE` no ME e opcode `0x42` no PFP. `CP_BUSY_STAT=0`, enquanto
   `CPG_BUSY=1` e `CPF_BUSY=1`. Não há evidência de shader preso nem de fault GFXHUB.
3. Após cada restart, a implementação zerou os ponteiros MMIO e validou `ACTIVE/RPTR/WPTR`,
   mas não executou um NOP/fence no próprio CP_RB0. A mensagem genérica
   `engine provada por NOP fisico` é forte demais: o NOP físico pertence ao backend SDMA; o
   bloco GFX só faz readback. As cinco submissões GFX seguintes foram aceitas, porém ficaram
   em `RPTR=0/WPTR=12`, com fence em poison. Foram **6 submits GFX aceitos e zero aposentados**.
4. O estado interno também preservou pendências antigas: os avisos cresceram até
   `7 submissao(oes) pendente(s)`. O restart avança `LastRetiredFence`, mas não há nesta rota
   prova de purga/reconciliação completa das entradas WDDM abortadas.
5. O DWM recebeu `vkWaitForFences = -4` (`VK_ERROR_DEVICE_LOST`). A alocação seguinte retornou
   `0x88760870`, que é `D3DDDIERR_DEVICEREMOVED`, não falta de VRAM.
6. A telemetria nova fechou o Desfecho E em duas ocorrências tardias:
   - `Batch=864 FailedIndex=529 Map=1 Base=0x1611000 Size=0x1000`, slot 0 com
     `Cap=1024 Mapped=495 Reusable=529 Live=495`;
   - `Batch=721 FailedIndex=530 Map=1 Base=0xC12000 Size=0x1000`, slot 1 com
     `Cap=1024 Mapped=494 Reusable=530 Live=494`.
   Em ambos os casos, `Journal NO_SPACE` ocorreu depois de PTEs de hardware já escritas e o
   flush devolveu `STATUS_IO_DEVICE_ERROR`. Isso é defeito real de publicação/transação, mas
   surgiu depois de múltiplos TDRs e não explica o primeiro stall do CP_RB0.
7. O harness terminou processo com Exit 0, porém o runner corretamente converteu o ensaio em
   falha (Exit 3): não houve nenhuma linha `PRESENT_OK` ou `PRESENT_FALHOU`. Portanto foram
   **zero frames medidos**, e não 60.
8. O Windows registrou `VIDEO_TDR_FAILURE (0x116)`:
   `0x116 (ffffd305de0b5460, fffff803475ad770, 0, d)`, relatório
   `d5b3083a-3da8-41a5-9c45-4cbe58c95460`. `C:\Windows\MEMORY.DMP` tem 876.936.171 bytes,
   data 00:05:37. A sessão atual não possui acesso de leitura ao arquivo (`Win32 error 5`),
   logo a pilha e o módulo de `Arg2` ainda não foram extraídos. Não inventar essa atribuição.

## Métricas frente ao critério

| Medição | Resultado |
|---|---:|
| Upload Vulkan aceito / recusado | 1 / 0 |
| Upload WDDM `pfnLockCb` falhou | 16 (`0x80070057`) |
| Descriptor bound / recusado | 0 / 0 |
| Draw com fence | 0 |
| Submits GFX aceitos / aposentados | 6 / 0 |
| `GFX_TRIPE` com `NAO_CONVERGIU` | 16 linhas de polling |
| TDR reset/restart concluídos | 5 ciclos |
| `GFXHUB_FAULT` | 0 |
| `PUBLICACAO_FALHA` | 2 |
| Presents KMD com SDMA | 7 |
| Frames do harness | 0 |
| Pixel correto/readback conclusivo | 0 |
| BugCheck | `0x116` |

## Recuperação

O reboot é comprovado pelo Event Log e pela mudança de `boot_ticks` entre o estado armado e o
watchdog retomado. Depois do boot, o fusível recusou um segundo StartDevice (`State=2`, `Boots=2`),
impedindo loop. O watchdog fez rollback e terminou em `RECOVERY_OK_ONLINE`; BasicDisplay ficou
em Código 0 em cinco amostras entre 00:05:58 e 00:06:03. LocalDumps do DWM foi restaurado e não
coletou dump de processo. Isso é recuperação, não sucesso do nosso display.

## Falha de coleta encontrada e corrigida offline

O arquivo inicial `display-log-this-bind.txt` parou em 92.390 bytes, antes dos TDRs. O log
cumulativo preservava mais dados. A ferramenta `recover-bc250mcdm-lost-tail.ps1` ainda tratava
`archive.bytes` como offset absoluto mesmo quando o manifesto dizia **delta delimitado**; isso
explica também arquivos recuperados gigantes de corridas anteriores. O cálculo foi corrigido
para `start_offset + bytes` no formato atual. Foram arquivados 656.463 bytes em
`bc250_display_log-tail-recovered.txt`, SHA-256
`C1C76B598EB81C814C92885D1CC495A4845D07795222BAB54627E1288009A134`.

## Próximo passo justificado

**Não repetir o bind DWM atual.** Antes de nova corrida física:

1. abrir o `MEMORY.DMP` em sessão elevada e arquivar `!analyze -v`, `.bugcheck`, `kv` e
   `lmvm bc250mcdm`; isso fecha a atribuição do `Arg2`;
2. tornar o restart GFX honesto: depois de reprogramar CP_RB0, submeter um NOP mais
   `RELEASE_MEM`, exigir `RPTR==WPTR` e fence sobre poison, e falhar o restart se qualquer
   perna não fechar;
3. reconciliar explicitamente as fences/entradas WDDM abortadas pelo reset, sem carregar
   pendências para o device seguinte;
4. criar ensaio físico estreito do CP_RB0/restart antes de recolocar DWM e harness em carga;
5. separar a correção do journal: lotes maiores que os slots reutilizáveis precisam ser
   particionados ou reservados atomicamente antes de escrever PTEs de hardware. Não repetir
   `Hardware PTEs written -> SUCCESS` seguido de falha de publicação parcial.

Desktop visível pelo nosso driver, descriptor, draw, frame do harness, pixel correto e
recuperação TDR com apresentação sobrevivendo continuam **NÃO VALIDADOS EM HARDWARE**.
