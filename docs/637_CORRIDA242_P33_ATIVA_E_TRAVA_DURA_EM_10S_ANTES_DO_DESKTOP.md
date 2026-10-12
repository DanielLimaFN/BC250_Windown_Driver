# 637 — Corrida 242: P3.3 ativa e provada; trava dura em ~10–15 s, antes do desktop

**07/10/2026, bind 14:21:51, boot seguinte 14:23:11.** Lab 3.386.1.0 (SYS assinado `382C007B…`) + RADV `B4AE2E14…`. Evidência: `docs/evidence/2026-10-07_14-21-51-DESKTOP-HQAG6T4-lab-binding/`. Critério: [doc 635](635_241B_ENCADEAMENTO_NO_WDDM2_E_P33_RPTR_DO_IH_POR_DOORBELL_CRITERIOS_241B_E_242.md) Parte 2.

## 1. Medido (P0)

### P3.3 armou e provou

```
[P3_3_IH_DOORBELL] ATIVO: DOORBELL_RPTR=0x100002F0 BIF_IH_DOORBELL_RANGE=0x00020BC0
prova: RPTR publicado pela doorbell=0x20 == IH_RB_RPTR lido=0x20, escritas pela doorbell=1
```

- Foi a primeira vez que o RPTR do IH passou pela doorbell.
- Readback dos dois registradores conferido.

### Trava dura logo no começo

- **Fim do log do KMD:** cobre só ~7,2 s depois do StartDevice (QPC 1.231.471.613 → 1.302.976.781).
  - Nesse intervalo: 1.276 IBs executados, `CommitVidPn` e dois `SetVidPnSourceAddress`.
  - Zero Present, zero composição, zero TDR, fault ou timeout.
  - As últimas linhas mostram uma rajada de `FILL_POR_COPIA` e `operation=12` (paginação de alocações novas).
- **DWM:** o log termina às 14:22:02.218, ~11 s depois do bind, no meio da montagem do primeiro quadro (`PsSetSamplers`), sem nenhum Present. O log do Explorer vai até 14:22:07.
- **Rastro de paginação:** 77 registros, último tique 62, ou seja ~7–8 s de vida do escritor.
- **Tela:** o Daniel viu tela preta, e o desktop nunca apareceu. Nas corridas anteriores com este caminho o desktop sempre foi desenhado antes da trava.
- **Plataforma:** `RESET_REASON=0x40080400`, igual ao de sempre (bits 30/10 fixos, bit 19 do botão); eventos 41/6008; RAM 7.591 MB × 2 passadas sem erro; coleta automática OK.
- **Estado final:** placa em **Código 43** pelo fusível no boot seguinte. O guardião ficou em `OBSERVING` e não fez rollback.

### O que não dá para medir

Só houve **uma** linha de regime do IH (ISR=1, `escritas_doorbell=1`), impressa antes de qualquer IB. A linha seguinte só sairia 10 s depois, e a máquina morreu antes. Por isso não se sabe se o IH continuou entregando interrupções com o RPTR pela doorbell.

## 2. Veredito pelo critério

- **A não ocorreu.** A exige `escritas_doorbell` crescendo nas linhas de regime, e não houve segunda linha. Pela Lei 10.3, desfecho parcial não conta.
- **B não ocorreu:** a P3.3 ficou ativa.
- **C não foi medido.**
- **Desfecho E (fora do mapa):** trava dura muito cedo, antes do primeiro quadro.

**Leitura:**
- Nas travas com 256 MB, o tempo até travar vai de 45 a 584 s, com média de ~350 s. Uma trava em menos de 15 s seria rara se nada tivesse mudado (≲5 % com taxa constante).
- A única mudança em relação à 241b é a P3.3. Isso faz da P3.3 a principal suspeita (H), mas com n=1.
- Mecanismo possível (H): o IH, com `RPTR_REARM=1` (MSI), depende do RPTR que recebe. Se a doorbell não atualiza o RPTR de forma confiável fora da prova, as interrupções param, e a conclusão volta à rede de 10 ms do timer. Isso explica um IH mudo, mas **não** explica, por si só, uma trava dura da máquina. Não está provado.

## 3. Próximo

1. **Recuperação:**
   - `BC250_RECOVERY.cmd` elevado;
   - desligar com `shutdown /s /t 0`;
   - ligar pelo botão.
2. **Repetição da 242** com o mesmo pacote (teto de 2 do doc 635):
   - **trava cedo de novo:** a P3.3 é revertida (chave `BC250_P33_IH_RPTR_BY_DOORBELL 0`) e volta a ser divergência declarada, com o motivo registrado;
   - **chega ao desktop:** a 242 original fica como ocorrência rara e o critério do doc 635 volta a valer.
3. A repetição sai na mesma hora e responde à pergunta principal. Instrumentar a linha de regime do IH a cada 1 s nos primeiros 30 s pediria um pacote novo; por isso não faço agora.

**NÃO VALIDADO EM HARDWARE:**
- se a P3.3 causa a trava precoce;
- se o IH segue entregando interrupções em regime com a doorbell.
