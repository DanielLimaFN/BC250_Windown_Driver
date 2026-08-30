# Handoff — 2026-08-29, fim do dia

> Sucede o [doc 94](94_HANDOFF_IA_2026-08-29.md). O detalhamento técnico está em
> [doc 95](95_ICACHE_POLL_PFP_CONGELADO.md), adendos A–J, em ordem cronológica.

---

## 1. O que mudou hoje

O projeto entrou o dia com **"o CP não consome o anel, e a intermitência do PFP é fenômeno de
silício"**. Sai com o **tripé de `CLAUDE.md` fechado no CP Ring 0**, sob gate estrito,
reproduzido 59/59 com o display vivo.

```text
CP TRIPE COMPLETO : 59/59      RPTR == WPTR == 31 estrito
fence 0xBC250123 (veneno 0x43DAFFFE verificado)
4 dwords conferidos byte a byte, destino envenenado antes
HQD tripé fechado : 59/59      Service=bc250mcdm, ConfigMgrError=0, zero eventos de display
```

**A "intermitência do PFP" era regressão nossa, em três camadas.** Nenhuma era silício:

| Regressão | Efeito | Correção |
|---|---|---|
| `GRBM_SOFT_RESET` do CP removido em `771d108` | PFP travava no laço `0x218..0x21B` | tunable `CpSoftReset=1` |
| `AUTO_CTXSW_ENABLE` ligado antes do anel (upstream desliga) | intermitência do SDMA | tunable `SdmaAutoCtxSw=0` |
| Padding `0xC0001000` (PACKET3 NOP declara 2 dwords, escrevíamos 1) | `RPTR` parava um dword antes | `0x80000000` (PACKET2) |

E um quarto achado, de natureza diferente: um `WRITE_DATA` endereçando GPUVA sem page table
**falha a tradução e interrompe a execução do resto do anel**, embora o RPTR continue
avançando. Foi por isso que os writes de destino nunca chegavam. Hoje o pacote só é emitido
com `Vmid0PageTable=1`.

## 2. A receita

```text
CpSoftReset = 1        Vmid0PageTable = 0        SdmaAutoCtxSw = 0
```

Tunables de registro em `HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters`, lidos
no `StartDevice`. **Uma reinstalação do driver apaga a chave** — só o `SkipHwBringup` é
reescrito pelo `bind`. O ensaio regrava entre o `Disable` e o `Enable` de cada ciclo.

`Vmid0PageTable=0` importa: a receita **não toca no VMID 0**, então o risco de invasão do
framebuffer sai de cena. Ligá-lo é desnecessário e quebra o SDMA por cobertura de página.

## 3. O que continua aberto

**Intermitência do SDMA.** 5 falhas em 249 ciclos. Todas com `EX_IDLE_POLL_TIMER_EXPIRE`
(bit 11 de `SDMA0_STATUS_REG`) aceso na entrada; **nenhuma sem ele**. Condição necessária,
validada. O bit não cai com `HALT/UNHALT` do F32, nem com 10 ms de espera, nem com des-gating
precoce de `CLK_CTRL`. Origem fora do microengine — SMU ou power.

Não há evidência de que as mitigações reduzam a probabilidade de falha: 5 das 9 ocorrências do
latch falharam no total, e a diferença entre regimes cabe no ruído. Ver doc 95 § J3.

**Dispatch de shader: 0%.** Nenhum kernel executado. É a próxima ordem de grandeza.

**Porte para produção.** Tudo acima é imagem de **laboratório**. A produção compila o bring-up
fora por contrato, e `audit-bc250mcdm-safety.py` proíbe a macro. Levar a receita para lá é
decisão do Daniel, não mudança técnica.

## 4. Armadilhas desta sessão — todas custaram corridas

**Um controle que não pega se parece com hardware indiferente.** Três tunables seguidos não
tiveram efeito: dois por eu escrever o caminho do registro com `\b` numa string Python (virou
um backspace, criando `Services<BS>c250mcdm`), e o eco de verificação relia a mesma chave
errada — confirmava a si mesmo. Hoje o ensaio confronta **pedido contra o que o driver
reportou**, e a máscara `TunableReadOkMask` distingue ausente de zero.

**Latch e leitura fresca não são a mesma coisa.** Li `ErrorLog`/XNACK só *depois* e atribuí
falha de tradução ao ciclo. Valores idênticos em falhas com e sem a page table denunciaram:
eram residuais. Todo registrador de status agora tem par antes/depois.

**Watchdogs concorrentes brigam pelo dispositivo.** Dois armados ao mesmo tempo → o mais
antigo faz rollback no meio do binding do mais novo → `win32=2`, e depois `1072`
(`ERROR_SERVICE_MARKED_FOR_DELETE`), que só reboot resolve. O `bind` agora aborta se achar
watchdog vivo, e o ensaio recusa começar com um armado.

**`Get-ItemProperty` devolve `REG_DWORD` como `Int32`.** Qualquer valor com o bit 31 aceso
chega negativo e `[uint32]` sobre negativo **lança**. Helper `To-U32` aplicado nas 18 leituras
de registrador cru.

**Uma regra de sucesso pode ficar obsoleta pelo próprio avanço.** A assinatura "PC final prevê
o resultado, 110/110" valia só enquanto o laço esgotava 100 amostras. Com o anel sendo
consumido ele sai na 2ª, e `0x17Fx` passou a ser *passagem*, não destino.

**Um gate correto recusa quando o estado está ruim — e isso pode travar você.** Com o
dispositivo em Código 43, o `confirm` recusa (certo) e o ensaio recusa por watchdog (certo), e
não sobra saída sancionada. Falta um `abort-bc250mcdm-lab.ps1` que desarme declarando a falha.

## 5. Próximo passo sugerido

1. **Origem do `EX_IDLE_POLL_TIMER_EXPIRE`** — com o preditor na mão e o mailbox da SMU já
   mapeado no repositório. É pesquisa, não conserto.
2. **Dispatch de shader** — a ETAPA 1 do roadmap de fato.
3. **`abort-bc250mcdm-lab.ps1`** — 20 minutos, remove o impasse do item acima.

## 6. Estado da máquina ao fim

Imagem de laboratório vinculada, display servido por `bc250mcdm`, tunables da receita
persistidos. `C:\BC250_RECOVERY.cmd` disponível. Nenhum watchdog deve ficar armado — confira
antes de começar, o ensaio bloqueia se houver.
