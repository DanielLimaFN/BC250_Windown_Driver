# 665 — Corrida 251: inválida (sombra recusada) e trava no StartDevice; suspeita: a foto do clock gating. Critério da 251b

**Data:** 08/10/2026, bind às 22:31:54. **Pacote:** lab 3.396.1.0 (SYS `36A6FC4F…`), sem KD.
**Evidência:** `docs/evidence/2026-10-08_22-31-54-DESKTOP-HQAG6T4-lab-binding/`, mais o trecho de `C:\BC250_DISPLAY_LOG.txt` desde o offset 756.287.099 (394 linhas, duas instâncias). A coleta automática não gerou `coleta-automatica.json` nesta pasta.

## 1. O que aconteceu (P0)

1. **Sombra recusada:** `[SOMBRA1] RECUSADO: sombra de 11075584 bytes nao alocada (no_space)`. O Present seguiu no scanout real, então a pergunta da 251 **não foi testada** (desfecho **C**).
   - Causa (P1, código): no `Bc250SdmaPresentInit` o alocador do bootstrap ainda não existe (`TotalBytes=0`, e `Bc250VramAllocate` devolve `no_space` quando o pedido é maior que o limite 0). O próprio comentário em `bc250_active_miniport.c:~4212` registra isso desde a corrida das 15:16. **Defeito meu.**
2. **Trava dura dentro do StartDevice,** ~8 s depois do bind:
   - o monitor para às 22:32:02;
   - o log do KMD termina na publicação da geometria do segmento (`[M2_SEGMENTO3] alocacoes no segmento 3: LIGADAS`);
   - na 250 isso vinha logo antes de `[P4_SDMA1]`, `[MEC1]` e `[P3_IH]`;
   - nenhuma linha `[CG_FOTO]` nem `[REFRESH_REAL]` chegou ao disco.
3. **Depois:** o fusível disparou no boot seguinte (`State=2, Boots=2`) e o StartDevice foi recusado. A placa ficou em **Código 43** e o watchdog em `OBSERVING`, sem rollback.

## 2. Por que a suspeita é a foto do clock gating (P2/H forte, n=1)

- O log é drenado de tempos em tempos. A última linha é a última drenagem, não o último evento (doc 582). A trava cabe em qualquer ponto depois da publicação do segmento.
- Entre esse ponto e o fim do StartDevice, o código que rodou é **idêntico ao da 249/250** (SDMA1, MEC.1, IH, MMHUB), que passou dezenas de vezes. As **únicas** exceções:
  - (a) a tentativa de alocação da sombra, que falhou e não tocou em hardware;
  - (b) uma leitura a mais do contador do OTG0 na Bateria A, registrador já lido sem problema na 250;
  - (c) **a leitura de 69 registradores de clock gating**, incluindo GC seg1, MMEA/DAGB/ATC e alguns `*_CAC_CGTT`, nunca lidos antes pelo driver;
  - (d) a espera de até 1 s antes do segundo ponto do refresh.
- **A L1b no Linux deu tela verde.** Ela lia uma lista que contém esses mesmos registradores, pelo `amdgpu_regs`, na coleta com o display ativo. Duas máquinas, dois sintomas, e o mesmo ato em comum: ler essa lista.
- **Leitura (H):** ler algum registrador desse conjunto quebra o chip (trava o barramento ou corrompe o display). Qual deles é **NÃO VALIDADO**. Isso também quer dizer que "só leitura" não é inócuo neste hardware, e a governança de leitura passa a precisar de lista de registradores já lidos sem dano.
- **Não é** prova sobre a trava de sempre: esta morreu no StartDevice, antes de qualquer Present.

## 3. 251b (pronta, NÃO EXECUTADA)

Lab **3.397.1.0**. Muda em relação à 3.396.1.0:
1. **SOMBRA.1b:** a sombra é alocada do alocador do bootstrap logo antes da consulta de caps que corta o segmento (depois de TMR/quad/anel, antes de SDMA1/IH e de qualquer Present). O `g_Bc250SdmaPresent.ScanoutGpuAddress` passa para a sombra ali. A linha `ATIVO`/`RECUSADO` imprime `TotalBytes`, `CpuVisibleBytes` e o número de regiões do alocador.
2. **Foto do clock gating desligada** (`BC250_CG_FOTO_NO_BOOT 0`; a linha `[CG_FOTO] DESLIGADA` confirma).
3. **Refresh real sem espera:** os dois pontos do contador do OTG0 (Bateria A e fim do StartDevice) e o QPC; sem `KeDelayExecutionThread`.

O resto é igual à 250.

**Critério:** o mesmo do doc 664 §5, com a pré-condição atualizada:
- `[SOMBRA1] ATIVO` com MC da sombra ≠ MC do scanout;
- `[CG_FOTO] DESLIGADA`;
- `[REFRESH_REAL]` presente;
- Presents com destino na sombra;
- zero blt para o scanout depois do `ATIVO`;
- menu operado.

Se travar **antes** do desktop de novo (no StartDevice), o desfecho é **D**, e a foto deixa de ser a única suspeita.

**Pacote:**

| Artefato | Valor |
|---|---|
| Pacote | **3.397.1.0** (INF fonte 3.397.0.0) |
| SYS lab | `FDF273B9…` |
| SYS assinado | `E93667CD…` |
| INF | `C50ADEA0…` |
| CAT | `2A925F86…` |
| UMD | `B73F9BDA…` (a mesma) |
| RADV | `B4AE2E14` (a mesma) |
| Auditorias | segurança exit 0; pacote de laboratório PASS |
| Conferência no SYS assinado | `[SOMBRA1] ATIVO`, `allocator total=`, `[CG_FOTO] DESLIGADA`, `[REFRESH_REAL] quadros` |
| Guardado em | `driver\wdk\package\output_lab_3.397.1.0_251b\` |
| Lançador | `tools\EXECUTAR_CORRIDA251B_SOMBRA1B_MENU_600S.cmd` (sem KD) |

## 4. Recuperação antes da 251b

Elevado:
1. `tools\BC250_RECOVERY.cmd`
2. `tools\inspect-boot-fuse.ps1 -Reset`
3. **boot frio** (desligar e ligar, não reiniciar).

**NÃO VALIDADO EM HARDWARE:** a SOMBRA.1 (nunca ativou), qual registrador da foto faz mal, o refresh real.
