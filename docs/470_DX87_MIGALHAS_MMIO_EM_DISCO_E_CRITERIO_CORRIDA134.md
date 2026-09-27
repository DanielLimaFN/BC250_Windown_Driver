# 470 — DX.87: migalhas MMIO gravadas em disco (sem depender de WinDbg) e critério da Corrida 134

> **26/09/2026, offline, sem corrida.** O Daniel não pode usar WinDbg agora; o break-in proposto no doc 469 fica adiado. **NÃO VALIDADO EM HARDWARE.**

## 1. Por que
- A trava dura da 133 (~5m51s) não pôde ser localizada (doc 469). É a mesma classe das 108–112, 115, 128 e 129.
- As migalhas DX.66 ficam só numa página de VRAM, e essa página **não sobreviveu ao reset** nas 115/128/129/133 ("ANTERIOR AUSENTE"). Só sobreviveu na 131, que não foi trava dura.
- O escritor do rastro DX.56 grava em disco a cada ~100 ms e às vezes vive depois da trava da paginação: 12,3 s na 129. Na 128 e na 133 morreu no mesmo instante.

## 2. O que mudou

| Alteração | Arquivo |
|---|---|
| Toda migalha (ANTES/DEPOIS) também grava um espelho em pool não-paginado, mesmo layout; a migalha deixou de depender da página de VRAM para existir | `driver/wdk/bc250_mmio_breadcrumb.h` |
| O escritor do rastro grava, depois de cada registro de paginação, um registro `BCR1` com os 96 slots do espelho (32 CPUs × 3 bancos), com checksum, no mesmo flush | `driver/wdk/bc250_mcdm_callbacks.c` |
| O decodificador lê `BCR1` e lista os acessos em fase ANTES no último registro, com CPU, banco, sítio, offset e em quantos registros seguidos o acesso continuou em andamento | `tools/decode-bc250-paging-trace.py` |
| Portão `audit_dx87_crumbs_reach_disk`, provado por mutação | `tools/audit-bc250mcdm-safety.py` |
| O runner da 134 roda `bc250-hang-etw.ps1 -Recover` antes, para a captura pendente não recusar o bind | `tools/EXECUTAR_CORRIDA134_DX87_600S.cmd` |

Nenhum acesso MMIO muda de valor, ordem ou número. O custo é uma escrita em RAM por migalha e ~1,2 KB a mais por tick no arquivo.

**Verificação:** decodificador testado com arquivo sintético (acesso preso detectado em 3 registros seguidos) e com o arquivo real da 133 (compatível, `mmio_crumbs: null`). `build-contract` e build de laboratório EXIT 0; auditorias PASS. Binário conferido: a constante `BCR1` aparece no SYS novo e não no da 133, e `"[BC250] %s"` continua ausente.

| Artefato (3.292.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `F5C9B85EF1C2B52BCAADDFEE358F398F1E032E3DF390B63BE9DBC514D547A702` |
| `bc250mcdm.sys` fonte lab | `1B513CCCC6D00849D68E6D2D2197F3623152D0546AC79EA124C304058664E5B6` |
| `bc250umd.dll` (igual à 133) | `3A9731939A970F7386EA75E69277D11764BBEF3178AFC9D98343161A4B6F2A0A` |
| `bc250mcdm.inf` | `9D6F1B284D72AF0176C69D3DCA3367960B8F41B7491E1F2552AA7E98E1E1A48C` |
| `bc250mcdm.cat` | `A3F78F708151B15A180AE7BDE8F492BA01AB323BCF65D9A9E2B4756A23BF040F` |

## 3. Critério da Corrida 134 (Lei 10)
**Pré-requisitos (Daniel):**
1. `bcdedit /debug off` elevado. O boot de 19:26 voltou a ter `DEBUG DEBUGPORT=NET` e o preflight recusa.
2. Boot frio.
3. `tools\EXECUTAR_CORRIDA134_DX87_600S.cmd` como administrador.

Se travar: anotar a hora, tentar Ctrl direito + Scroll Lock ×2, esperar ~1 min e **reset pelo botão**. **Teto:** uma corrida.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Trava; o último registro `BCR1` tem um acesso em fase ANTES presente em ≥ 2 registros seguidos (≥ ~100 ms parado) | O acesso nomeado (CPU, sítio, offset) é o suspeito direto. Próximo: ler o sítio no código, conferir o registrador no catálogo docs/03 e decidir se pode ser evitado ou protegido. |
| **B** | Trava; o escritor morre no mesmo tick e o último `BCR1` só tem acessos em fase ANTES vistos num único registro | A máquina para em menos de ~100 ms. O dado localiza só o conjunto de acessos em curso; comparar com as corridas seguintes para achar sítio recorrente. |
| **C** | Trava; nenhum acesso em fase ANTES no último `BCR1` | A trava não está num acesso instrumentado. Ampliar a cobertura das migalhas (MMIO fora do transporte e da SDMA). |
| **D** | 600 s sem trava | Registrar. Uma corrida não prova estabilidade; repetir com o mesmo critério antes de concluir. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Controle a não regredir: `FLUSH_TLB` p50 ~1,4 ms e Present do DWM p90 < 2 s, como na 133.
