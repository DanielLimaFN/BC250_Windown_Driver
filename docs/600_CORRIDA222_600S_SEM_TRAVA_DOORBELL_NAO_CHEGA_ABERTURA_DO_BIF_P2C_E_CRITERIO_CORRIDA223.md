# 600 — Corrida 222: 600 s sem trava dura; a doorbell não chega porque a abertura do BIF está desligada; P2c e critério da Corrida 223

**Corrida 222:** 05/10/2026 14:09:52 · lab **3.370.1.0** (SYS `1E24C811…`) · critério: [doc 599](599_CORRIDA221_DOORBELL_ARMADA_COM_ANEL_VIVO_NAO_CHEGOU_P2B_E_CRITERIO_CORRIDA222.md) §5
**Evidência:** `docs/evidence/2026-10-05_14-09-52-DESKTOP-HQAG6T4-lab-binding/`. Inclui `display-log-this-bind-FINAL.txt` (36,8 MB), os logs `-FINAL` da UMD e `safe-bind-final.json`.

## 1. Resultado (P0)

**P2b: desfecho C.**
- `APLICADO NA RECONSTRUCAO (F32 parado): DOORBELL=0x10000000 OFFSET=0x800 BIF_RANGE=0x00140800 readback=confere; prova=0 Kicks 0->1`;
- `SYNC_NOP TIMEOUT: WPTR=0 RPTR=0 FenceObs=0x43DAFFFE`.

A doorbell foi programada com o F32 parado e mesmo assim não chegou. A hipótese da posição (doc 599 §3) está **refutada**.

**Recuperação, como desenhada:**
- `DESARMADA`, `NAO ATIVA`;
- reconstrução por MMIO provada (`TDR_RESTART PROVADO`, 2 nesta corrida, ambos no StartDevice);
- D2, P7 e `DrenoOk=1` mantidos.

**600 s sem trava dura (E: fora do mapa da P2).**
- O watchdog levou a janela até o fim e fez o rollback online: `RECOVERY_OK_ONLINE`, BasicDisplay em Código 0 em 5 amostras.
- Contadores:
  - 3.106 IBs (WDDM `Accepted=3106`, `NotExecuted=0`);
  - 28.509 submissões do SDMA0, todas retiradas (`Completed=28509`, `Fallbacks=0`, `HostCpuCopies=0`);
  - 11.445 Presents;
  - zero TDR, timeout, fault, `INSIDE_IB`, `NO_SPACE`, `DEVICE_REMOVED` ou BugCheck.
- Recusas: 90 `DIRTY_RECTS` (bissecção desligada, esperado) e 3 `DescribeAlloc` sem geometria.

**Primeira prova do teardown completo**, em ordem:
1. `[P7_GFX_GTT] DESLIGADO: PTEs de volta na dummy e RAM do anel devolvida`;
2. `[GfxEngine] OFFLINE`;
3. `[P2_DOORBELL] DESARMADA (teardown)`;
4. `[D2_ANEL_SISTEMA] TEARDOWN: ... RAM liberada`;
5. `Global GFXHUB MMU restored`.

Isso fecha a pendência "teardown da P7" (doc 597) e prova a LC.1 no teardown, sem laço.

## 2. Leitura: os 600 s não são atribuíveis

A configuração efetiva é a mesma da 221: P2 desarmada, D2 + P7 + P1, WPTR por MMIO. A 221 travou aos ~99,6 s.

As três corridas de 256 MB que fecharam 600 s (201, 216 e 222) tinham configurações diferentes entre si. **A trava é intermitente, e uma corrida sem trava não prova causa.**

Uma diferença de carga, não de driver: a 222 teve 3.106 IBs em 600 s, contra 6.153 em ~100 s na 221. A taxa de paginação foi bem menor.
- **H:** a trava depende do volume de paginação ou de uso. Não dá para separar o roteiro do Daniel da sorte com estes dados.

## 3. Por que a doorbell não chega (P0 + P1)

**Nenhuma doorbell funcionou nesta placa, nem a do MEC.** Na própria 222, todo `SubmissionPublishWptr` mostra:
- o MEC andando pelo WPTR escrito por MMIO **antes** da doorbell (`Step 3 MMIO WPTR: Write=12 Readback=12`, e `Rptr=12` já no Step 4);
- `Step 4 DbHit: ... HitObserved=0` em todos os toques.

A premissa "a doorbell do MEC funciona na mesma BAR" (doc 598/599) era **falsa**. Eu a tirei de leitura de código, não de medição.

**Upstream (P1):** `nv_common_hw_init` chama `nbio_v2_3_enable_doorbell_aperture(adev, true)`, que liga `RCC_DEV0_EPF0_RCC_DOORBELL_APER_EN.BIF_DOORBELL_APER_EN`, antes dos motores. `nv_common_hw_fini` desliga.
- Registrador: `0x00C0` idx2, ou seja **`0x3780`**, bit 0.
- **Nós nunca ligamos.** Sem a abertura, o BIF descarta toda escrita na BAR de doorbell.

## 4. P2c (candidato 3.371.1.0)

- **Arme:** `Bc250SdmaPresentP2ProgramLocked` liga primeiro `BIF_DOORBELL_APER_EN` (RMW) e depois programa os três registradores da P2b. O readback exige o bit da abertura.
- **Desarme e teardown:** a abertura volta ao valor lido antes, como o `hw_fini`.
- **Logs:** `APER_EN=... (antes ...)` em `APLICADO NA RECONSTRUCAO` e em `DESARMADA`.
- **Proveniência e catálogo:** `inc/linux_ref/PROVENANCE_P2_DOORBELL.txt` (com `nv.c`) e docs/03.
- **Provas offline:**
  - `build-contract` EXIT=0;
  - laboratório "TODOS OS TESTES APROVADOS (0 falhas)";
  - auditoria de segurança PASS;
  - auditoria do pacote PASS;
  - string `APER_EN` no SYS assinado.
- **Portão `audit_p2_sdma0_doorbell`: 21 mutações, todas reprovadas.** As 4 novas são:
  - abertura não ligada;
  - readback ignorando a abertura;
  - desarme sem devolver a abertura;
  - offset errado.

| Arquivo | SHA-256 |
|---|---|
| SYS | `806424F86A4DC847E0B14F92B3252B4D01C1A4A0B14FC3C5172A121A540AC6E3` |
| INF | `63A8973BC887613072BB7BDB1537EB4C6F0FDFA191C788076967140A27F8232B` |
| CAT | `952EAE8E709586AB3AE955DE8E0C543E52E6644FA5A3FA52AE151EFC01CAC832` |

**Scripts:** `tools/prepare-corrida223-p2c-abertura-de-doorbell.ps1` e `tools/EXECUTAR_CORRIDA223_P2C_ABERTURA_DE_DOORBELL_600S.cmd`.

**Efeito colateral declarado:** com a abertura ligada, a doorbell do MEC (índice `0x110`) também passa a chegar. O MEC já recebe o mesmo WPTR por MMIO no passo 3, então a doorbell repete um valor que ele já tem.
- **H:** isso é inofensivo, como no amdgpu, que usa as duas.
- Um `DbHit ... HitObserved=1` no MEC passa a ser prova independente da abertura.

## 5. Critério da Corrida 223 (Lei 10)

**Pré-condição de validade:**
- `APLICADO NA RECONSTRUCAO ... APER_EN=0x...1 ... readback=confere; prova=1`, com os Kicks avançando;
- `[P2_DOORBELL] ATIVA:`;
- a base mantida: D2, P7, `DrenoOk=1`.

**Sinal independente:** `HitObserved=1` em algum `DbHit` do MEC.

Desfechos:

- **A — 600 s sem trava**, com a doorbell ativa.
  - *Implicação:* compatível com a P2, mas a 222 mostrou que 600 s acontecem sem ela. Só repetições (≥2 seguidas) contam.
- **B — trava dura igual**, com a doorbell ativa.
  - *Implicação:* a P2 sai das causas suficientes. Próximo: P1b.
- **C — `NAO ATIVA` com `APER_EN` ligado no readback.**
  - *Implicação:* falta outra peça do roteamento. O próximo suspeito é o selfring/`DOORBELL_FENCE`, e entra a coleta Linux com `umr` dos registradores BIF.
- **C' — `NAO ATIVA` com `APER_EN` que não fica ligado.**
  - *Implicação:* o registrador está travado pelo firmware ou o offset está errado. O log traz o valor.
- **D — ativa, mas o SDMA0 para depois:** timeout ou TDR.
  - *Implicação:* falta algo da publicação em regime. O TDR reprograma e prova de novo.
- **E — fora do mapa:** descrever antes de julgar. Inclui efeito no MEC.

## 6. Pendências

- **Daniel:** recuperação, fusível, CMOS `-Ler` a menos de 15 min, boot frio e `tools\EXECUTAR_CORRIDA223_P2C_ABERTURA_DE_DOORBELL_600S.cmd`.
- **NÃO VALIDADO EM HARDWARE:**
  - a P2/P2c;
  - a causa da trava dura;
  - 600 s sem trava de forma repetível.
