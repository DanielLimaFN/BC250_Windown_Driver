# DX.61–63: recovery no mesmo boot, Blt para o destino, seguimento da alocação; critério da Corrida 105

> **Data:** 24/09/2026. Faixa **offline**, sem bind.
> **Base:** [Corrida 104](412_HANDOFF_2026-09-24_CORRIDA104_DX60_E_CONTINUIDADE.md), evidência em `docs/evidence/2026-09-24_15-35-24-DESKTOP-HQAG6T4-lab-binding/`.
> **Teto:** uma corrida de 180 s, depois de boot frio.
> **Variável comportamental do driver:** uma só, a DX.63.
> - A DX.61 muda apenas o guardião.
> - A DX.62 é somente instrumentação.

## 1. Item 1: por que a recovery da 104 pediu reboot (DX.61)

### Fatos

**Journal do guardião (P0):**
- 15:38:26,42: `ROLLBACK_STARTED`.
- 15:38:35,97: `ROLLED_BACK` ("PnP ainda não confirmou BasicDisplay"), seguido de `RECOVERY_REBOOT_REQUESTED`.

**Kernel-PnP/Configuration (P0):**
- 15:38:30: evento 400 (configurado) e evento 410 (iniciado) para `PCI\VEN_1002&DEV_13FE…`, com `Serviço: BasicDisplay`.
- Nenhum outro evento de PnP do dispositivo até o reboot.

**Consequência (P0):** o dispositivo estava no driver certo ~6 s antes de o guardião desistir. A recovery funcionou, e quem reprovou foi o veredito de 5 amostras do guardião.

**Tempo do veredito (P2):**
- O veredito caiu entre ~15:38:31 e 15:38:36.
- Esse intervalo é curto demais para 5 consultas CIM que estourassem o timeout de 2 s: cinco estouros mais 4 s de espera dariam ≥ 14 s.
- Então pelo menos uma amostra **voltou** com valor diferente de BasicDisplay/0, ou com `device_count` ≠ 1.

**Qual amostra reprovou não se sabe, e não dá para recuperar**, porque a tentativa 2 sobrescreveu três artefatos:
- `safe-bind-pnp-samples.json`;
- `safe-bind-recovery.txt`;
- `C:\BC250_RECOVERY.log`.

**Defeito secundário:** a tarefa de prazo entrou 0,24 s depois, registrou `BLOCKED` e **sobrescreveu o `state.json`** da instância que executava a recovery.

### Correção (em `tools/watch-bc250mcdm-safe-bind.ps1`)

1. **Amostras em journal append-only:** `safe-bind-pnp-samples.jsonl`, uma linha por amostra, com tentativa, rodada, boot, serviço, código, `Status` e erro.
2. **Saída da recovery por tentativa:** `safe-bind-recovery-attempt<N>.txt` e `bc250-recovery-log-attempt<N>.txt`.
3. **Janela de assentamento:** rodadas de veredito por até 45 s depois da recovery.
   - O critério **não afrouxa**: exige uma rodada inteira de 5 amostras consecutivas em BasicDisplay/Código 0.
   - Uma amostra ruim anula a rodada inteira.
   - Sem rodada saudável no prazo, o guardião segue para o reboot como antes.
4. **A instância secundária `BLOCKED` só escreve no journal** (`-JournalOnly`).

### Verificação

- Auditoria PASS com o portão `audit_dx61_safe_bind_recovery_evidence`.
- `tools/test-dx61-safe-bind-recovery-mutations.py`: **7/7** mutações reprovadas, inclusive "critério afrouxado para 4 amostras" e "código ≠ 0 aceito".
- `test-bc250-safebind-offline.ps1`: PASS.

**NÃO VALIDADO EM HARDWARE.**

## 2. Item 2: os dois `BYTE_PROOF FAIL` (#10 e #45) (DX.63)

### Classificação (P0 por log e código)

**Quem apresenta:**
- Os 60 presents `Blt=1` da corrida são do harness (`hCtx=0x64B`), com `SrcRect=DstRect=(0,0)-(1280,720)` e **destino resolvido** (`Spread SLOT_DST @ 0x4ECC000`, a superfície de redirecionamento).
- Os flips do DWM são `hCtx=0x4C` e `Flags=0xC`.

**O defeito (P0 por código):** `bc250_display_ddi.c` ignorava o destino. Todo present com `SrcRect` menor que o modo era copiado **para o scanout**, numa posição centralizada calculada pelo KMD: `DstXY=640,180`, visível em `[Present #60] Blt 2D 1280x720 … -> scanout 0x0 DstXY=640,180`.

**Resultado: dois escritores do scanout ao mesmo tempo.**
- Os presents são concorrentes: o `[Present #11]` (Blt) começa na linha 18901, antes do `BYTE_PROOF` do #10 na linha 19609.
- **#10:** `FirstMismatch=0x1C2A00` = 1.845.760 bytes = linha 180, x = 640 px. É **exatamente o canto superior esquerdo** da janela centralizada.
- **#45:** `FirstMismatch=0x3D84DC` = linha 393, x = 1847, dentro da mesma área (x 640–1919, y 180–899). O Present #44 (Blt) estava em voo.

**Conclusão:** a conferência do flip pegou o blt concorrente.
- Não é corrupção da cópia (P2: a cópia do flip em si não foi reconferida isoladamente).
- **H:** isso também explica o relato "visível por um tempo, depois começou a bugar". A janela do harness aparecia **no centro**, fora da posição real, disputando a área com o quadro composto pelo DWM.

### Correção (a única variável comportamental)

**Todo present `Flags.Blt` copia `SrcRect` da origem para `DstRect` da alocação de destino** que o dxgkrnl entrega, conforme o contrato do `DXGKARG_PRESENT`. O caminho é `Bc250DisplayBltToDestination`:
- geometria da origem e do destino pelo registro de alocação: `hDeviceSpecificAllocation` → registro `OPEN_ALLOCATION` → `ReferencedObject` → geometria declarada pela UMD;
- mesmo formato, 32 bpp (D3DDDI 21/22);
- retângulos do mesmo tamanho (sem escala) e dentro das duas superfícies;
- destino resolvido pelo dxgkrnl, que nunca pode sobrepor o scanout.

**O que muda em volta:**
- **Falha fecha:** `BLT_DESTINO RECUSADO: motivo=…`, e o scanout **não é tocado**.
- A supressão de quadro em branco não se aplica ao Blt: ela protegia o scanout, e preto é conteúdo válido.
- O `BYTE_PROOF` do scanout é pulado para o Blt, com o motivo registrado.
- A cópia centralizada para o scanout fica inalcançável para Blt.

**Verificação:**
- Portão `audit_dx63_blt_present_goes_to_destination`.
- Mutações 6/6 dentro da suíte `tools/test-dx62-dx63-mutations.py` (12/12 no total).

## 3. Item 3: textura compartilhada do Explorer (DX.62)

### O que a 104 já prova (P0)

**As duas alocações são idênticas no KMD.** A aceita (DWM, `Bind=0x8`, `Misc=0`) e a recusada (Explorer, `Bind=0xA8`, `Misc=0x2`) devolvem os mesmos valores em todos os campos:
- `KmdStatus=0`, `Resource=1`;
- `Flags=0x1` (CpuVisible);
- prioridade `0x78000000` (NORMAL);
- `SegSet=0x1`, `Preferred=0x1`, `Align=4096`;
- `Size=3407872`, `hResource=0`.

O `hResource=0` também aparece na aceita, então **não discrimina**.

**BIND_PRESENT sozinho não é a causa (P2):** o DWM criou 18 texturas BGRA com `bindFlags=0xA8` com sucesso. O `Misc` delas não é impresso nas linhas de sucesso, daí P2.

**Nenhuma** criação com `Misc=0x2` teve sucesso na corrida: 97 ocorrências, todas recusadas, todas 1024×832.

**A primeira recusa leva 46 ms dentro do `pfnAllocateCb`.** O KMD aceitou, e algo depois desfez.

### Lacuna encontrada (P0 por código)

- `Bc250LogTextContains` diferencia maiúsculas de minúsculas.
- As saídas de falha do `OpenAllocation` ("Device lookup failed", "ABI parse failed", "Size/Alignment mismatch", "Duplicate token", `LEAVE: result=0xC…`) não continham token isento.
- Com isso, a classe 9 as amostrava fora (16 primeiras, depois 1 em 1024).
- O `DestroyAllocation` não registrava nada.
- **Uma recusa nossa no `OpenAllocation`, que o dxgkrnl chama no dispositivo criador depois do `CreateAllocation`, seria invisível.**

### Instrumentação (zero mudança de comportamento)

1. A consulta DX.59 vai para a **versão 2**, com os campos `OpenCalls`, `OpenLastStatus`, `DestroyCalls` e `DestroyLastStatus`.
   - O KMD anota, por token, cada `OpenAllocation` que cita a alocação sondada.
   - O KMD anota, pelo handle publicado, cada `DestroyAllocation` dela.
2. Toda saída de falha do `OpenAllocation` e do wrapper `Bc250McdmOpenAllocation` passa a conter `RECUSADO`. O `LEAVE` com falha vira `RECUSADO LEAVE`.
3. `DestroyAllocation` com falha registra `[ActiveDestroyAlloc] RECUSADO`.
4. A UMD imprime os campos novos no `DX59_ALLOC_QUERY`.

**Verificação:**
- Portão `audit_dx62_allocation_followup`.
- Mutações 6/6 na suíte `test-dx62-dx63-mutations.py`.

## 4. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.266.1.0`, SYS `3C7ACDEF6332AFF5A0B65A4B83E1B9DA444DD7995842343766C93E1673C0B099`, INF `710EDEE6…05AB`, CAT `DE83CCD0…86E3`. Auditoria do pacote PASS. Marcas no SYS: `BLT_DESTINO RECUSADO`, `RECUSADO LEAVE`, `[ActiveDestroyAlloc] RECUSADO`, `[McdmOpenAllocation] RECUSADO` |
| UMD | `3B70D7F2A3DDB549BF64CFE573FB1BB9304651EFDB2C43B56DD411BC7B9FAE2D`; marca `OpenCalls=%u OpenStatus` |
| RADV | DX.42 `FCAA11FE…` |
| Guardião | DX.61, copiado para `C:\BC250_SAFE_GUARD` no bind |
| Pré-voo | `tools/prepare-corrida105-dx61-63.ps1`: PASS (hashes, manifesto, auditoria de segurança, auditoria do pacote) |
| Execução | `tools\EXECUTAR_CORRIDA105_DX61_63.cmd` (CRLF), elevado, **180 s**, depois de **boot frio** |

**Verificações offline:**
- `build-umd` EXIT=0, build de laboratório EXIT=0, `build-contract` EXIT=0;
- auditoria de segurança PASS;
- mutações: DX.61 7/7, DX.62/63 12/12.

## 5. Critério da Corrida 105, escrito antes

**Controle:**
- log do KMD com ≥ 1 `[Present #N] BLT_DESTINO OK` **ou** `BLT_DESTINO RECUSADO`;
- **zero** `Blt 2D … -> scanout 0x0 DstXY=`;
- `safe-bind-pnp-samples.jsonl` presente na pasta.

**Eixo 1: apresentação (DX.63)**

| | Assinatura | Implicação |
|---|---|---|
| **A** | `BLT_DESTINO OK` nos presents do harness **e** zero `BYTE_PROOF FAIL` nos flips conferidos (#1/5/10/25/45 que forem flips) **e** harness 60/60 | O segundo escritor era a causa dos `BYTE_PROOF FAIL`. Relato visual: a janela deve aparecer onde o DWM a compõe, não no centro. Se ela sumir, a composição do DWM a partir da superfície de redirecionamento é a próxima parede. |
| **B** | `BLT_DESTINO RECUSADO motivo=X` | A resolução do destino tem a lacuna nomeada em X. O present devolve `GPU_EXCEPTION_ON_DEVICE`, e o harness pode perder o device ou reprovar presents. Próximo: corrigir X. |
| **C** | `BLT_DESTINO OK` **e** `BYTE_PROOF FAIL` em algum flip | Existe outro escritor do scanout, ou a cópia do flip falha de fato. Próximo: decodificar o `FirstMismatch` contra as escritas concorrentes. |

**Eixo 2: textura compartilhada (DX.62).** A assinatura está nas linhas `DX59_ALLOC_QUERY … Misc=0x2`.

| | Assinatura | Implicação |
|---|---|---|
| **D1** | `OpenCalls≥1` com `OpenStatus≠0` (e linha `[ActiveOpenAlloc] RECUSADO…` no KMD) | A recusa é **nossa**, no `OpenAllocation`, e a linha nomeia a verificação. Próximo: corrigir essa verificação para o caso compartilhado. |
| **D2** | `OpenCalls=0` e `DestroyCalls≥1` | O dxgkrnl rejeita entre o Create e o Open. É contrato de flags/recurso compartilhado. Próximo: trace ETW do DxgKrnl ou comparação de contrato, sem mudar código por palpite. |
| **D3** | `OpenCalls≥1`, `OpenStatus=0`, `DestroyCalls≥1` | A rejeição é posterior ao Open. Próximo: o que o runtime faz com recurso compartilhado depois do Open. |
| **D0** | nenhuma linha `Misc=0x2` | O Explorer não tentou; não há informação sobre o eixo 2. |

**Eixo 3: recuperação (DX.61)**

| | Assinatura | Implicação |
|---|---|---|
| **R1** | `RECOVERY_OK_ONLINE` com `attempt_number=1` e `boot_ticks` igual a `armed_boot_ticks` | Recuperação no mesmo boot. |
| **R2** | `RECOVERY_REBOOT_REQUESTED` | O jsonl nomeia a amostra e o valor que reprovou. Próximo: classificar esse estado. |

**E — fora do mapa:** qualquer outra coisa (BugCheck, travamento, 0x119, TDR, laço novo). Descrever antes de julgar.

**Regras:**
- Desfecho parcial não é desfecho. A exige as três condições.
- O eixo 2 só vale com a linha `DX59_ALLOC_QUERY` contendo os campos novos.
- Um BasicDisplay Código 0 no boot novo não é R1.

**Secundárias:**
- relato visual do Daniel: onde a janela do harness aparece, se pisca, se o desktop fica;
- FPS do harness (104: 1,2);
- `PREEMPT_DONE Node=0` (DX.60 continua sem exercício);
- 0x119.

**Depois da corrida:** as 5 seções e a tabela-checklist.
