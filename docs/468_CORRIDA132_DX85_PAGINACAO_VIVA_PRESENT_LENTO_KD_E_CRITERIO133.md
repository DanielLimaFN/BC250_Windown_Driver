# 468 — Corrida 132 (DX.85): deadlock resolvido, paginação saturada por ~510 ms fixos com KD ligado; DX.86 e critério da Corrida 133

> **Corrida:** 26/09/2026, bind 11:37:21, rollback online 11:48:32 (mesmo boot). **Candidato:** DX.85 / lab 3.290.1.0 (SYS `F3F491F8…`).
> **Evidência:** `docs/evidence/2026-09-26_11-37-21-DESKTOP-HQAG6T4-lab-binding` e ETW `docs/evidence/2026-09-26_11-37-16-DX83-HANG-ETW`.
> **Relato do Daniel:** "rodei, tela preta".

## 1. Relato obrigatório

**1. Houve avanço?** Sim, funcional e de diagnóstico, contra a 131. O deadlock da DX.83 sumiu e a cadeia inteira voltou a andar; a tela preta passou a ter outra causa, agora medida.

**2. Sucessos (P0):**
- Wrapper de paginação balanceado: 10.616 `ENTER` / 10.616 `LEAVE` (131: 90/89). Zero `WaitPaging TIMEOUT` nos cinco logs da UMD.
- DWM (pid 1208) vivo de 11:37:44 a 11:48:23: 35 `DXGI Present LEAVE cb: hr=0x00000000`, teardown limpo no rollback.
- KMD: 223 `[Present #`, 10 `DESKTOP REAL DETECTADO`, 4 `BYTE_PROOF PASS` com `Dest=0x0` (scanout) e `PostScanout NonZero=256/256`. O scanout recebeu pixels do desktop por SDMA.
- Zero TDR_QUIESCE, zero IB_TRIPOD TIMEOUT, zero GFXHUB_FAULT, zero BugCheck. `ChildUid=1`, 1 CommitVidPn. OTG contou 22.797 → 60.804 quadros (~59 Hz), pipeline de display vivo.
- Harness D3D11: 24 quadros `PRESENT_OK`.

**3. Falhas (P0):**
- Tela preta relatada pelo Daniel.
- `pfnPresentCb` do DWM bloqueado **88–116 s por chamada**. O primeiro Present só retornou às 11:40:27, 2m43s depois do load. Na 119 essa espera era ~437 ms.
- Harness: Presents de 10–13 s e `DXGI_ERROR_DEVICE_REMOVED` (0x887A0005) no quadro 25.
- `FLUSH_TLB` bimodal: 510 chamadas com **~509–510 ms exatos** e 26 com ~1.030 ms, somando 300 s dos 351 s gastos em `FLUSH_TLB`. Média de 16 flushes/s, contra ~270/s na 126.
- ETW, janela 524–612 s: o VidMm passou **84,6 s de 88 s em `PagingPreparation`** (p90 522 ms, máximo 1,537 s, o mesmo máximo do nosso `FLUSH_TLB`).
- Nossos callbacks de VSync: 5.340 em ~645 s (~8/s, em vez de ~60). O writer do rastro DX.56: 427 registros em 645 s (esperado ~6.450). Ou seja, lentidão geral do kernel, não só da paginação.
- 29 `NO_SPACE`, 1 `PUBLICACAO_FALHA`.

**4. Veredito frente ao critério do doc 467: B não se aplica e A não ocorreu.** Houve Presents e desktop no scanout, mas não houve desktop visível e o rastro não travou dentro de `FLUSH_TLB`. Classificação: **E — fora do mapa**. O observado foi paginação viva porém lentíssima, com espera fixa de ~510 ms por flush.

**5. Recuperação e pendências:** `RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em 5 amostras, mesmo boot, `CLEAN_BOOT_REQUIRED` armado. Desktop visível, 600 s estáveis, 60 FPS e jogos: **NÃO VALIDADOS EM HARDWARE**.

## 2. Onde está a espera de ~510 ms
- O rastro DX.56 aponta `FLUSH_JOURNAL_WAIT` (estágio 44) como o único estágio em que a thread de paginação aparece parada entre snapshots, inclusive com lote **M=1** e N≈230. Um lote de uma faixa não custa 510 ms por aritmética: o contexto é pool não-paginado, logo cacheável.
- **Achado de configuração (P0):** o boot tem `DEBUG DEBUGPORT=NET HOST_IP=192.168.0.169 … NOVGA` (plano 460, após a 129). O logger do KMD chamava `DbgPrintEx(DPFLTR_ERROR_LEVEL)` em toda linha emitida, incluindo as duas `BC250_HANG` de cada `FLUSH_TLB`. Com KD habilitado, isso vai ao transporte de depuração.
- **Causalidade: H forte, não P0.** O binder nunca gravou `SystemStartOptions`, então não há prova de que 118–128 rodaram sem KD. O ETL de kernel (circular) perdeu a janela do bloqueio e só guardou os últimos ~2 min. Também não se sabe se havia host WinDbg conectado na 132.
- Ruído descartado: ~85 processos `git.exe`/`conhost.exe` em 88 s (polling de git do app Claude, que também carregou a nossa UMD, pid 11412). Não explica a paginação.
- A leitura "o Present do KMD levou 115 s", tirada da posição das linhas no arquivo, **não vale**: o logger desvia linhas para um anel em RAM durante a paginação e as drena depois, então a posição no arquivo não é ordem temporal.

## 3. DX.86 (offline)

| Alteração | Arquivo |
|---|---|
| `DbgPrintEx` removido de `Bc250DisplayLog`; o arquivo de log continua sendo a evidência | `driver/wdk/bc250_display_ddi.c` |
| Portão `audit_dx86_no_dbgprint_in_driver`, provado por mutação | `tools/audit-bc250mcdm-safety.py` |
| Preflight recusa bind com `DEBUG` no boot, salvo `BC250_ALLOW_KERNEL_DEBUG=1`, e imprime `SystemStartOptions` sem a chave | `tools/prepare-corrida133-dx86.ps1` |

UMD, RADV e o resto do KMD iguais à DX.85. Build de contrato e de laboratório EXIT 0; auditorias PASS. O pacote foi conferido: o formato `"[BC250] %s"` do `DbgPrintEx` existe no SYS da 132 e não existe no novo.

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` (assinado, 3.291.1.0) | `C1BE8BBFBAED798C859AB5F4F344486202B3DFA8CFD993061EC9CA8CC6CFEC3F` |
| `bc250mcdm.sys` (fonte lab) | `064E566AE90C6588BEFE7EB0E21F1B50C04A128BF8374D5C784A562B0F6195AD` |
| `bc250umd.dll` (igual à 132) | `3A9731939A970F7386EA75E69277D11764BBEF3178AFC9D98343161A4B6F2A0A` |
| `bc250mcdm.inf` | `A2ED930DE9C2D4049E69858DDF2A489C55B79BEEAF308BD3CBCB6A1D849984B5` |
| `bc250mcdm.cat` | `9BDEE59AA995A713276E14C1C92AC7403400339EE68295454D2A241E7F274F39` |

## 4. Critério da Corrida 133 (Lei 10)
**Pré-requisitos, feitos pelo Daniel:** `bcdedit /debug off` elevado e boot frio. Runner: `tools\EXECUTAR_CORRIDA133_DX86_600S.cmd`. **Teto:** uma corrida.

Variáveis que mudam contra a 132: KD desligado e `DbgPrintEx` fora do log. Todo o resto é igual, inclusive o ETW.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | `FLUSH_TLB` sem o platô de ~510 ms (menos de 10 chamadas ≥ 400 ms); `PagingPreparation` do VidMm abaixo de 50% da janela; `pfnPresentCb` do DWM p90 < 2 s; Daniel vê desktop | KD + DbgPrint era a causa da lentidão e da tela preta das 132. Próximo passo: estabilidade de 600 s. |
| **B** | Platô de ~510 ms some, mas o DWM continua preso no Present (> 10 s) ou a tela continua preta | A lentidão da paginação não era a única trava do Present. Investigar a cadeia de flip/present no dxgkrnl pelo ETW (`QueuePacket`/`DmaPacket` do contexto do DWM). |
| **C** | Platô de ~510 ms persiste com KD desligado | A espera fixa é nossa. Instrumentar o estágio 44 por dentro (PublishBatch, Replace, cópias) com QPC. |
| **D** | Preflight recusa, ou `SystemStartOptions` ainda com `DEBUG` | Ensaio não aconteceu; corrigir a configuração de boot. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Desfecho parcial não é desfecho: A exige os quatro sinais.
