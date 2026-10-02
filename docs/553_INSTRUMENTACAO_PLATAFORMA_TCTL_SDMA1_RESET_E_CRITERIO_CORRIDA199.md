# 553 — Instrumentação de plataforma (Tctl, SDMA1, motivo de reset) e critério revisto da Corrida 199

**Data:** 01/10/2026 · **Tipo:** offline, sem corrida · **Candidato:** lab **3.345.1.0**
**Antecede:** [doc 551](551_DECISAO_VRAM_MINIMA_256MB_E_CRITERIO_CORRIDA199.md) §3 (critério original da 199) e [doc 552](552_COLETA_LINUX_256MB_AMDGPU_ATIVO_GABARITO_E_O_QUE_MUDA.md) (coleta Linux L-B).

---

## 1. Por que a 199 mudou de candidato

O Daniel decidiu não gastar uma corrida só para testar 256 MB. A coleta Linux (doc 552) apontou três diferenças para a trava dura: SMU, SDMA1 e fences por interrupção. Também mostrou um registro de reset com `0x40080400` ("parity error"). Hoje, quando a máquina trava, não sabemos:

- a temperatura no momento da trava;
- se o SDMA1, que nunca tocamos, estava ligado;
- o motivo que o hardware registrou para o reset seguinte.

Por isso a 199 passa a levar **instrumentação que só mede**: nenhum caminho de comportamento do driver muda.

O pacote é o 3.344.1.0 mais esta faixa:

- a UMD é a mesma (`5CEC87A9…`);
- a RADV é a mesma (`5447A17D…`);
- `BC250_M2_HIDDEN_SEGMENT_ALLOCATIONS_ENABLED` continua 1, e com 256 MB o segmento 3 fica dormente por construção.

## 2. O que foi feito

### 2.1 Registro `BPS1` no rastro do KMD (`C:\BC250_PAGING_TRACE.bin`)

**Quando roda**
- `Bc250PagingTraceThread` (PASSIVE) grava um registro a cada **10 ticks (1 s)**.
- Roda **depois** do BGH1 do mesmo tick, para não esconder o batimento.

**Formato**
- Mesmo layout de 72 bytes do BGH1 (DX.90), magic `0x31535042`, checksum FNV.
- Mesma regra ANTES/DEPOIS: a fase ANTES vai a disco antes de qualquer acesso a registrador.
- Um ANTES sem DEPOIS no fim do arquivo significa que o acesso não voltou.

**Conteúdo (`BC250_MCDM_PLATFORM_SAMPLE`)**

| Campo | Registrador | Procedência |
|---|---|---|
| `Sdma1Status` | `SDMA1_STATUS_REG` 0x6214 (bit 0 IDLE) | P1, `gc_10_1_0_offset.h`, BASE_IDX 0, base 0x1260 dwords (a mesma que dá `SDMA0_F32_CNTL`=0x4A28, já provada) |
| `Sdma1F32Cntl` | 0x6228 (bit 0 HALT) | idem |
| `Sdma1RbCntl` | `SDMA1_GFX_RB_CNTL` 0x6380 (bit 0 RB_ENABLE) | idem |
| `Sdma1RbRptr/Wptr` | 0x638C / 0x6394 | idem |
| `TctlRaw` / `TctlMilliC` | SMN `0x59800` pelo par RSMU 0x38/0x3C | endereço e conversão: P1, Linux `drivers/hwmon/k10temp.c`. Par índice/dado: P0, docs/03 |

**Conversão (k10temp)**
- `temp_mC = (raw >> 21) * 125`.
- Subtrair 49000 se o bit 19 (`RANGE_SEL`) estiver ligado, ou se os bits 17:16 forem 3 (`TJ_SEL`).
- Fonte única: `Bc250PlatformTctlRawToMilliC`, em `bc250_mcdm_callbacks.h`.

**Hipótese declarada (H):** não está provado que o par RSMU da GPU alcance o endereço SMN do bloco térmico do SoC. O k10temp usa a ponte SMN da CPU. A amostra se autovalida de três formas:

- releitura do índice antes de ler o dado;
- faixa plausível de 0 a 125 °C, que o decodificador marca;
- variação com a carga, conferida na análise.

Valor fixo ou fora da faixa refuta a hipótese. Não é temperatura.

**Única escrita:** o seletor `RSMU_INDEX` (0x38) recebe `0x59800`.
- Fica sob um spinlock único do SMN, como exige o docs/03.
- No `bc250mcdm`, nenhum outro caminho usa o par em tempo de execução.
- `RSMU_DATA` (0x3C) e o SDMA1 ficam **fora** da allowlist de escrita.

### 2.2 Motivo do último reset da plataforma (ferramenta da CMOS)

**Registrador** (P1, Linux `arch/x86/kernel/cpu/amd.c` + `amd-fch.h`): `FCH_PM_BASE` `0xFED80300` + `S5_RESET_STATUS` `0xC0`, 32 bits.

**Regra do Linux:**
- `0xFFFFFFFF` é leitura inválida e deve ser ignorada;
- caso contrário, ele **escreve o mesmo valor de volta (W1C)** para limpar o registrador e imprime os bits.

**O que entrou na ferramenta:**
- novo IOCTL `IOCTL_BC250CMOS_RESET_REASON` no `bc250cmos.sys`: mapeamento sem cache só durante a chamada; a resposta traz o valor **antes** da limpeza e a releitura;
- `bc250-vram.exe --motivo-reset [--limpar-reset]` decodifica os bits com a tabela do Linux (`bc250_fch_reset_reason.h`);
- `bc250-vram.ps1 -Ler` passa os dois por padrão (`-SemMotivoReset` e `-NaoLimparReset` desligam).

**Consequência:** cada leitura mostra só o que aconteceu desde a anterior. Isso inclui o Linux, que também limpa o registrador.

### 2.3 Provas offline

| Item | Resultado |
|---|---|
| `build-contract.bat` | EXIT=0; `mcdm_callbacks_test` com 5 asserções novas da conversão (os dois ramos do −49 °C, TJ_SEL com um bit só que **não** desloca, e zero) |
| Build de laboratório | SYS fonte `C1EB7507…`, bring-up confirmado |
| `tools\vram-cmos\build.bat` | teste APCB PASS, CLI e driver /W4 /WX, `bc250cmos.sys` assinado |
| Decodificador | arquivo sintético com 5 registros: Tctl 55 °C nos dois ramos, `tctl_kmd_agrees`, ANTES final detectado como `read_never_returned` |
| `audit_iplat_platform_probe` (novo) | PASS. **4 de 4 mutações reprovam:** valor do seletor ≠ 0x59800; 0x3C na allowlist de escrita; BPS1 antes do BGH1; releitura do índice removida |
| Auditoria de segurança, auditoria do pacote | PASS / PASS |

### 2.4a Candidato efetivo da 199: 3.346.1.0 (01/10, 21:45)

O 3.345.1.0 foi **recusado pelo próprio preflight** da 199 por causa de `hash divergente ou fonte UMD auditada ausente`. Depois de empacotar, eu recompilei a UMD para testar a M1b.1 dormente (doc 554). O build da UMD não é determinístico (sem `/Brepro`), então a origem auditada não pôde ser recriada.

Reempacotei como **3.346.1.0**, com o fonte atual:
- os três interruptores da M1b estão em 0;
- o texto `M1B_RADV_SISTEMA` está **ausente** do SYS (bloco eliminado pelo compilador);
- `BPS1` está presente;
- a RADV é a mesma `5447A17D…`.

| Arquivo | SHA-256 |
|---|---|
| SYS | `4ECF829F39E849BBA9136C2635B02C31FEBB423B0CAECE6A677144564AD0CC34` |
| INF | `BCAE625EF6F80CCB613B7DF2A62F67D5C032A3E4232C416ACD090F70C752AF12` |
| CAT | `EDFF42E0D8EA4E120CA221A995B550727B325D749AC508A473442E74B1C172FA` |
| UMD | `ED025FAB4363AE7F452084B0D28F33F0E2A79AA2A85C5D7336EDEED68040408D` |

A auditoria do pacote e a de segurança passaram (PASS). O preflight foi atualizado para esses hashes.

**Dado colhido na tentativa recusada (P0, 21:40):** `RESET_REASON raw=0x40080400`; depois da escrita W1C, a releitura deu `0x40000400`. **Só o bit 19 limpou; os bits 30 (paridade) e 10 ficaram.** Isso enfraquece a leitura do doc 552, segundo a qual o `0x40080400` do Linux às 14:52 seria um evento **novo**: se o bit 30 não limpa por escrita, ele pode ser antigo. Análise completa depois da corrida.

### 2.4 Pacote 3.345.1.0 (assinado, auditado, NÃO INSTALADO; substituído pelo 3.346.1.0)

| Arquivo | SHA-256 |
|---|---|
| SYS | `1DEC8F9FB45C0B25151966AD188C7F3D9408CEA4170FD0159E0CA04DBD660546` |
| INF | `487D6294F7462D4BA69A50609A2DA918DBC601415D65184F03852B4AA06B8426` |
| CAT | `380A0FD0FCB891A531255C5AFD2FC2F725C885D9BEE9BEF05FEA7ED37993BED3` |
| UMD | `5CEC87A9…` (igual ao 3.344) |

Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7. A sonda não tem string de log. O binário foi conferido pelo magic `BPS1` e pela constante `0x00059800`, ambos presentes no SYS assinado.

## 3. Corrida 199 — critério revisto (escrito antes, Lei 10)

### 3.1 Preparação (Daniel)

1. `tools\BC250_RECOVERY.cmd` elevado.
2. `tools\inspect-boot-fuse.ps1 -Reset`.
3. **Boot frio:** desligar da tomada e ligar de novo.
4. `tools\EXECUTAR_CORRIDA199_UMA256_600S.cmd`.

O passo 4 faz o resto:
- lê a CMOS e o motivo de reset (lê e limpa);
- o preflight **recusa** se a última leitura não mostrar `UMA_SIZE=0256 MB` aplicado pela ABL, ou se tiver mais de 15 min;
- confere os hashes e roda o bind de 600 s.

**Depois de uma trava:** apertar o botão de reset, como no roteiro, e no boot seguinte rodar **primeiro** `tools\vram-cmos\bc250-vram.ps1 -Ler`. Essa leitura é a medida do motivo de reset da trava. Copiar também `C:\BC250_PAGING_TRACE.bin`.

### 3.2 Valores esperados

São os do doc 551 §3, mais estes:

| Linha | Esperado |
|---|---|
| `-Ler` antes do bind | `RESET_REASON raw=` presente. O valor é o do boot anterior; registrar, não julgar |
| `decode-bc250-paging-trace.py` → `platform_sample` | `records` ≈ 2 × segundos de vida do driver; `tctl_samples` > 0 |
| Tctl | entre 30 e 100 °C e variando com a carga, ou então marcado como implausível (refuta a H do §2.1) |
| SDMA1 | um estado constante (o KMD não o toca). Esperado F32 HALT=1 e RB_ENABLE=0; outro valor é **achado** |

### 3.3 Desfechos

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A — mínimo sobe e não trava** | Doc 551 desfecho A completo **e** 600 s sem trava **e** BPS1 contínuo até o rollback | 256 MB funciona; frente M1b (falta de memória medida aqui é a base). Temperatura e SDMA1 viram linha de base |
| **B — trava com calor** | Trava dura; últimos BPS1 DEPOIS com Tctl plausível ≥ 95 °C, ou subindo continuamente até o fim | Temperatura sobe a suspeito principal; refrigeração e a falta de gerência de relógios pela SMU (o Linux gerencia) entram antes de código |
| **C — trava com erro de fabric/memória** | Trava dura; Tctl plausível < 90 °C; `-Ler` seguinte com bit 27 (sync flood por erro não corrigido), 30 (paridade) ou 31 (sync flood por software) | Acesso inválido a memória ou fabric; aponta para endereço que montamos (PTE, MC, IB). Cruzar com o último BPX6/BCR1 |
| **D — a própria sonda não voltou** | Último registro do arquivo é um BPS1 **ANTES** sem DEPOIS | O acesso SMN/SDMA1 travou o barramento. A instrumentação é suspeita e sai do próximo candidato; não conclui nada sobre a trava original |
| **E — fora do mapa** | Qualquer outra coisa: trava sem BPS1, Tctl fixo, SDMA1 ativo, bits de reset diferentes, BugCheck | Descrever o observado antes de julgar |

Os desfechos B–D exigem trava; sem trava vale A ou E.

Lei 10.3: C exige os **três** sinais (trava, Tctl < 90 e bit de erro). Trava com bit 19 apenas (CF9=0x6) significa reset por software/botão: ainda não é C.

**Risco declarado:** a escrita no seletor RSMU é nova em tempo de execução. O desfecho D existe para separá-la da trava original.

**NÃO VALIDADO EM HARDWARE.**
