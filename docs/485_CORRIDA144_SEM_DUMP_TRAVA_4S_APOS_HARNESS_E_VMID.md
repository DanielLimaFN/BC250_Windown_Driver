# 485 — Corrida 144 (repetição da 143 para dump): trava 4 s depois do harness, sem dump; GPU ociosa de novo

> **Corrida:** 27/09/2026. Bind às 20:26:12; reset manual antes das 20:30:27 (Kernel-Power 41). O rollback online às 20:36:13 ocorreu no boot seguinte. **Candidato:** DX.90 / 3.296.1.0, harness atrasado 150 s.
> **Evidência:**
> - pasta `docs/evidence/2026-09-27_20-26-05-DESKTOP-HQAG6T4-lab-binding`;
> - cauda do KMD a partir do offset 3.371.799.628 (131.915 linhas até o reboot);
> - rastro a partir do offset 106.030.832 (1.447 ticks).
>
> **Sem `MEMORY.DMP` novo** (o existente é de 26/09 21:53).
> **Relato do Daniel:** "deu tela preta, tive que reiniciar". Ainda não se sabe se o Ctrl+Scroll Lock foi tentado.

## 1. Relato obrigatório
**1. Houve avanço?** Sim, de diagnóstico: a trava reproduziu pela 4ª vez com a mesma assinatura. O dump, objetivo da corrida, **não** veio.

**2. Sucessos (P0):**
- **Último BGH1 = DEPOIS completo** no tick da última paginação (1.447). O anel gráfico estava ocioso, com fence 0x2E0 == última submetida, `CP_STAT=0` e `GRBM_STATUS=0xA0003028`. É a assinatura da 143.
- Até a trava: 834 Presents, zero TDR, fault ou `DEVICE_REMOVED`.

**3. Falhas:**
- Trava dura **4,3 s depois do início do harness** (harness em 169,6 s, fim do log em 173,9 s), com 1 `COMPOSITE_BLT` do harness.
- Paginação: **ENTER 24.651 contra LEAVE 24.650**; um `FLUSH_TLB` ficou em andamento, na CPU 1 pelo BCR1.
- O último estágio do rastro é o commit do journal (44), com **N=1.024**.
- Nenhum dump foi gerado.

**4. Veredito frente ao critério do doc 484 §4: inconclusivo entre C e "não tentado".** O desfecho C exige que o Ctrl+Scroll Lock tenha sido tentado sem efeito, e isso não foi informado. A assinatura do rastro repete a da 143: GPU ociosa, MMIO e VRAM respondendo.

**5. Recuperação e pendências:**
- BasicDisplay Código 0 (rollback online às 20:36:13); `CLEAN_BOOT_REQUIRED` armado.
- **NÃO VALIDADO EM HARDWARE:** a causa.

## 2. O que as seis corridas com harness dizem (P0 por log)

| Corrida | Explorer crescido quando o harness começou | Falhas de publicação no teto | VMID do harness | Trava |
|---|---|---|---|---|
| 136 | não (textura compartilhada recusada, N 243–360) | — | 4 | não |
| 139 | sim (N~780) | 0 | 3 | ~6 s depois |
| 140 | sim (N~770) | 0 | 3 | ~6 s depois |
| 142 | não (Explorer recém-nascido, N ~300–480) | 5.090 | 5 | não |
| 143 | sim (N~830) | 5.586 | 3 | ~15 s depois |
| 144 | sim (N 1.024) | 718 | 2 | ~4 s depois |

- **Falhas de publicação no teto não são a causa.** A 141 (sem harness) teve 23.400 falhas e a 142 teve 5.090, sem trava; 139 e 140 travaram com zero.
- **O que se repete nas quatro travas:** harness começando com o Explorer crescido; a GPU ociosa e o MMIO/VRAM vivos 100 ms antes; o último estágio no commit do journal; o `pfnPresentCb` do harness sem retorno (139, 143).
- **Correlação nova (H, n=6):** nas travas o harness recebeu VMID 3, 3, 3 e 2; nas que não travaram, 4 e 5. Pode ser coincidência da ordem de alocação do pool.

## 3. Próximo passo proposto
1. Saber se o Ctrl+Scroll Lock foi tentado. Se foi e não gerou dump, isso é o desfecho C: todas as CPUs presas com interrupções bloqueadas.
2. **DX.91 (instrumentação):** o BGH1 passa a gravar:
   - contadores da nossa ISR e da nossa DPC;
   - `GCVM_L2_PROTECTION_FAULT_STATUS` (0x0A130, doc 03, somente leitura);
   - o VMID do último submit gráfico.

   Uma **tempestade de interrupções** (por exemplo, faults de GCVM de um VMID reciclado) prenderia uma CPU em DIRQL e pararia as outras no próximo IPI. As linhas de log disso ficariam no anel em RAM e se perderiam. O contador de ISR saltando nos últimos ticks mostraria isso.

## 4. Corrida 145: atalho de dump com qualquer Ctrl (decisão do Daniel, 27/09)
- **Fonte:** Microsoft Learn, "Forcing a System Crash from the Keyboard".
  - `crashdump\Dump1Keys=0x22`: Ctrl direito (0x02) ou esquerdo (0x20).
  - `Dump2Key=0x7D`: índice 125 da `keyToScanTbl`, scan 0x46 = Scroll Lock.
  - A doc exige `CrashOnCtrlScroll=0` com o atalho alternativo.
- **"Scroll Lock uma vez" não é configurável:** o Windows sempre exige a segunda tecla duas vezes.
- **Limitação documentada:** com teclado **USB**, o atalho **não funciona se a máquina parar em IRQL alto**. Se não gerar dump de novo, isso é compatível com CPU presa em IRQL elevado (desfecho C), e não prova defeito do atalho.
- **Arquivos:**
  - `tools\arm-crash-keys-any-ctrl.ps1`: o Daniel roda elevado; vale depois de reiniciar; `-Undo` desfaz;
  - `tools\prepare-corrida145-dx90.ps1`: confere o atalho novo e recusa se não estiver armado;
  - `tools\EXECUTAR_CORRIDA145_DX90_DUMP_QUALQUER_CTRL_600S.cmd`: mesmo pacote e harness atrasado 150 s.
- **Critério:** igual ao do doc 484 §4.

## 5. Variante com WinDbg (KDNET), pedida pelo Daniel em 27/09
- O boot já está com `DEBUG DEBUGPORT=NET HOST_IP=192.168.0.169`. O runner `tools\EXECUTAR_CORRIDA145_DX90_COM_WINDBG_600S.cmd` define `BC250_ALLOW_KERNEL_DEBUG=1`, o pedido explícito que o preparo exige desde a DX.86.
- A lentidão da 132 com KD foi atribuída ao `DbgPrint`, removido na DX.86 e travado por portão. O KD ainda muda o tempo do ensaio.
- **Na trava:** Debug > Break no host e `$<C:\BC250_KD\windbg-corrida145-trava.txt` (`tools\windbg-corrida145-trava.txt`). O script roda `!running -ti`, `!irql`, `!dpcs`, `!ready`, `!stacks 2` de `bc250mcdm`/`dxgmms2`/`dxgkrnl` e `.dump /m`, com log.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Break funciona e `!running -ti` mostra uma CPU dentro de `bc250mcdm` | O ponto de parada é nomeado. |
| **B** | Break funciona, sem quadro nosso; CPUs em dxgkrnl/dxgmms2/KeFlushTb/IPI | O mecanismo está no SO/dxgkrnl; a pilha diz qual. |
| **C** | Break não responde em 1 min e o Ctrl+Scroll Lock também não | Todas as CPUs presas com interrupções bloqueadas; próximo: DX.91 (ISR/DPC). |
| **D** | Sem trava em 600 s | Pode ser efeito do KD no tempo; não conclui. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |
