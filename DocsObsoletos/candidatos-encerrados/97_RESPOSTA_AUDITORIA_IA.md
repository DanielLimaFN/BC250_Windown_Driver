# Resposta à auditoria independente — 2026-08-29

> Para a IA/engenheiro que produziu a auditoria do status consolidado. **Concordo com o
> diagnóstico**: verifiquei os nove pontos contra o arquivo vivo e todos procediam. Este
> documento confirma, registra o que já foi corrigido, e acrescenta o que a auditoria não
> tinha como saber — incluindo **duas correções ao plano proposto** que evitam ciclos de
> hardware perdidos.

---

## 1. A auditoria está certa — e os nove pontos já foram corrigidos

| Ponto | Verificado | O que foi feito |
|---|---|---|
| 1 — cabeçalho chamava o PFP de "gargalo único" | ✅ procedia | Revisão promovida a **4.0**; cabeçalho reescrito |
| 2 — mesma prova como `3/3`, `39/39` e `59/59` | ✅ procedia | Unificado em **59/59**, com nota de que 3/3 e 39/39 eram etapas do mesmo dia |
| 3 — "Não provados" listava `RPTR==WPTR` do CP | ✅ procedia | Removido; trocado por dispatch de shader, IH/TDR, robustez do 3.24 e **produção** |
| 4 — 2.0.4 dizia "NÃO VALIDADO" por causa do `15/16` | ✅ procedia | Corrigido — o `15/16` é anterior à troca do padding por PACKET2 |
| 5 — seção 4 documentava o gate antigo | ✅ procedia | Virou **HISTÓRICO**, com o gate vigente no topo em formato antes × depois |
| 6 — seções 5 e 6 contradiziam F4d e F7 | ✅ procedia | Ambas marcadas como **HISTÓRICO**, com a correção no topo |
| 7 — pendências com itens concluídos | ✅ procedia | Seção 9 reescrita: 6 dos 13 fechados, sobraram os reais |
| 8 — revisão ainda "3.1" | ✅ procedia | Agora **4.0** |
| 9 — três fontes de status divergentes | ✅ procedia | Registrado como P1; `AGENTS.md` e `CLAUDE.md` ainda têm a barra antiga |

Um item que a auditoria não pegou e também estava velho: o veredito dizia
`fila de compute MEC/HQD (tripé, 26x)`. São **59/59**.

---

## 2. Duas correções ao plano proposto

### 2.1 `s_endpgm` sozinho é uma armadilha, não um primeiro marco

O plano propõe *"primeiro dispatch mínimo, inicialmente `s_endpgm`, mas com prova adicional de
que uma wave realmente iniciou"*. A ressalva está certa — mas com `s_endpgm` puro **não existe
prova adicional possível pelo tripé**, porque o shader não produz efeito observável nenhum. O
`RPTR==WPTR` e a fence são escritos pelo CP quer a wave rode, quer não.

Isso não é hipotético aqui: **hoje o CP fecha o tripé estrito 59/59 sem nunca ter despachado
uma wave.** Um teste com `s_endpgm` passaria exatamente igual e não provaria nada de novo.

**Recomendo pular esse degrau** e ir direto ao shader que **grava um padrão** — `v_mov_b32` +
`global_store_dword` + `s_endpgm`, com o destino envenenado antes. Aí a prova é o próprio
dado, e não há como o CP produzi-lo sozinho.

Se quiserem mesmo um degrau intermediário, use um observável **lateral** (amostrar
`GRBM_STATUS2`/`SPI_BUSY` durante o dispatch, ou contadores de wave) — mas isso dá mais
trabalho do que já ir ao shader que escreve.

### 2.2 GFXOFF não é suspeito — não gastem um ciclo com ele

A auditoria lista corretamente que a guarda de GFXOFF do HQD é stub (`GfxOffApplicable=0`,
`GuardPassed=1` são literais). **Corrijam, porque é `STATUS_SUCCESS` sem backend e o
`CLAUDE.md` proíbe** — mas tratem como higiene, não como suspeito.

Evidência de que o bloco GFX **não** está desligado: `CP_PFP_PC` muda **44 a 63 vezes em 100
amostras** em todo ciclo bom. Um bloco power-gated pela SMU não move o PC. Já verifiquei ao
investigar a intermitência; não repitam.

---

## 3. O que a auditoria não tinha como saber — e economiza ciclos

### 3.1 Um pacote PM4 que falha derruba TUDO que vem depois no mesmo anel

**Este é o achado mais importante para a fase de dispatch.**

O anel tinha um `WRITE_DATA` endereçando um GPUVA sem page table. Ele falha a tradução — e o
CP **para de executar o resto do anel**, enquanto o `RPTR` continua avançando até o `WPTR`,
porque a *busca* continua. Resultado: `RPTR==WPTR` perfeito, fence gravada (o pacote da fence
vinha antes), e o destino intocado.

Gastei três ciclos de hardware perseguindo isso como se fosse endereço, `COUNT` ou coerência.
Era ordem.

**Consequência direta**: quando adicionarem `DISPATCH_DIRECT`, se qualquer pacote anterior
falhar vocês verão `RPTR==WPTR` e nenhum resultado — e a tentação será culpar o shader.
**Ponham o dispatch e a verificação o mais cedo possível no anel**, e desconfiem de qualquer
pacote que endereça memória traduzida.

### 3.2 Um `WRITE_DATA` com payload múltiplo foi aceito e não escreveu

`PACKET3_WRITE_DATA` com `COUNT=6` (4 dwords de payload) foi consumido pelo CP e **não
escreveu nada**. Quatro `WRITE_DATA` de um dword, nos mesmos endereços, escreveram. Não
investiguei a fundo porque a ordem (3.1) resolveu o caso — mas se precisarem de payload
múltiplo, **não presumam que funciona**.

### 3.3 Padding de um dword tem que ser PACKET2

`0xC0001000` é PACKET3 NOP e declara **dois** dwords (`COUNT = n-2`). Escrever um só faz o CP
parar um dword antes do `WPTR` — foi a origem do `RPTR=15/16` que parecia característica do
silício e sustentava o gate afrouxado. Padding de um dword é `0x80000000` (PACKET2).

### 3.4 O latch do SDMA: três remédios já descartados

Concordo integralmente com *"não assumir causalidade apenas porque o bit é condição
necessária"* — está registrado assim em `docs/95` §J3, com os números por regime de mitigação,
que **não** sustentam melhoria.

Já tentados, **nenhum limpa o bit**: `HALT/UNHALT` do F32; espera de ~10 ms
(`ExIdleClearPolls=10000` esgotados); des-gating precoce de `CLK_CTRL` (`0x000C0100` na
entrada). A origem é externa ao microengine. Comecem por SMU/power, não por reset.

### 3.5 Armadilhas de medição que custaram horas

| Armadilha | Como evitar |
|---|---|
| Tunable que não pega **se parece com hardware indiferente** | Confrontar **pedido × o que o driver reportou**; `TunableReadOkMask` distingue "ausente" de "zero" |
| Caminho de registro escrito com `\b` numa string | Virou `Services<BS>c250mcdm`, e o eco de verificação relia a **mesma chave errada** — confirmava a si mesmo |
| `Get-ItemProperty` devolve `REG_DWORD` como `Int32` | Bit 31 aceso vira negativo e `[uint32]` **lança**. Usar o helper `To-U32` |
| Ler latch/erro só **depois** | `ErrorLog`/XNACK são latches; sem par antes/depois atribui-se ao ciclo errado. Aconteceu e gerou uma conclusão errada publicada |
| Dois watchdogs armados | O mais antigo faz rollback no meio do binding do mais novo → `win32=2`, depois `1072` (`ERROR_SERVICE_MARKED_FOR_DELETE`), que **só reboot resolve** |
| Reinstalar o driver | A chave `Parameters` é recriada: os tunables somem e só `SkipHwBringup` é reescrito pelo `bind` |
| Regra de sucesso que **envelhece com o próprio avanço** | "PC final prevê o resultado, 110/110" valia só enquanto o laço esgotava 100 amostras; com o anel consumido ele sai na 2ª |

---

## 4. Sobre a ordem: a proposta de vocês é melhor que a minha

Eu havia proposto fases lineares (1→5). **A divisão em duas trilhas paralelas é melhor** e eu
adoto:

- **Trilha de estabilidade**: soak do SDMA + `EX_IDLE_POLL_TIMER_EXPIRE` + recuperação
- **Trilha de funcionalidade**: programar shader → primeiro `DISPATCH_DIRECT` → buffer correto

Uma nuance a favor da trilha de estabilidade que não é apenas caracterizar instabilidade: o
soak do SDMA exercita **a mesma superfície de IOCTL e UMD** que o dispatch vai precisar
(`IOCTL_BC250_ALLOC_VRAM`, `IOCTL_BC250_SDMA_COPY`, readback para o host). Fazê-lo primeiro
entrega o encanamento de dados testado antes de o shader depender dele.

E concordo que o `abort-bc250mcdm-lab.ps1` vem antes de tudo: ~20 minutos, e remove o impasse
Código 43 × watchdog que já travou a bancada duas vezes.

---

## 5. Benchmarks falsos: removidos

Removidos em 2026-08-29, com as linhas de compilação retiradas do `build-contract.bat`:

- **`tools/bc250-benchmark.c`** — imprimia `"%.2f GFLOPS em Silicio Real"` calculado como
  `2·N³ / tempo` de um GEMM **que nunca foi despachado**; e a "verificação" imprimia uma
  **constante** como amostra da GPU, sem ler o buffer, seguida de
  `"Integridade: 100% VERIFICADA E APROVADA!"` incondicional.
- **`tools/bc250_silicon_compute_benchmark.c`** — topologia e banda como **strings fixas**
  (`"1536 Stream Processors @ 2.0GHz"`, `"448 GB/s"`), "kernel" de duas instruções nunca
  despachado.

**Mantido**: `tools/bc250_validator_benchmark.py`. Ele **mede de verdade** — poison, readback,
contagem de divergências byte a byte, e tempo por `perf_counter` com cópia de aquecimento. É a
base natural da trilha de estabilidade. Dele removi apenas a barra de status **hardcoded**
(`"FENCE VRAM PROVADA!"` e percentuais fixos), que não vinha de medição nenhuma.

Critério para os próximos: **um número que não veio de uma leitura do silício não é um
resultado.**

---

## 6. Definição de pronto para o próximo marco

Concordo com a formulação de vocês; deixo explícita:

```text
[PREFLIGHT: CP em HALT no PC 0, SDMA sem EX_IDLE, tunables conferidos]
 -> [SNAPSHOT dos registradores tocados]
 -> destino ENVENENADO
 -> ISA carregada; COMPUTE_PGM_LO/HI + RSRC1/RSRC2 + NUM_THREAD_X/Y/Z programados
 -> DISPATCH_DIRECT  (cedo no anel — ver 3.1)
 -> RELEASE_MEM / fence
 -> RPTR == WPTR estrito
 -> fence física conferida contra o veneno
 -> buffer de saída conferido BYTE A BYTE contra gabarito da CPU
 -> [ROLLBACK completo + verificação do restore]
```

Enquanto as três pernas não fecharem **no mesmo ciclo**, o veredito é **`NÃO VALIDADO EM
HARDWARE`** — para compute shader e para o miniport de produção.
