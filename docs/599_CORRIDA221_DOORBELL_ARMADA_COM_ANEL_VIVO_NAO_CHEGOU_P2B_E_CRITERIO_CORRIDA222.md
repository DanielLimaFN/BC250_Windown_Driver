# 599 — Corrida 221: a doorbell armada com o anel vivo não chegou ao SDMA0; P2b e critério da Corrida 222

**Corrida 221:** 05/10/2026 13:17:24 · lab **3.369.1.0** (SYS `03FB3C8C…`) · critério: [doc 598](598_P2_WPTR_DO_SDMA0_POR_DOORBELL_COMO_O_AMDGPU_E_CRITERIO_CORRIDA221.md) §5
**Evidência:** `docs/evidence/2026-10-05_13-17-24-DESKTOP-HQAG6T4-lab-binding/`, com a coleta automática e o teste de RAM do boot das 13:20.

## 1. O que aconteceu (P0)

**Arme da doorbell:**
- `[P2_DOORBELL] APLICADO em STARTDEVICE`:
  - `DOORBELL 0x00000000->0x10000000`;
  - `OFFSET 0x00000000->0x00000800`;
  - `BIF_RANGE 0x00000000->0x00140800` (pedido `0x00140800`);
  - `POLL 0x00403004->0x00403000`.
- **Readback confere.** Os três offsets novos existem e aceitam o valor do upstream, o que confere a aritmética da proveniência.
- **A prova falhou.** `SYNC_NOP TIMEOUT: WPTR=40 RPTR=40 FenceExp=0x1003 FenceObs=0x43DAFFFE`, com **Kicks=1**. A doorbell foi escrita, mas nem o registrador `RB_WPTR` se moveu: ela não chegou ao SDMA0.

**Recuperação, como desenhada:**
- `DESARMADA (prova falhou)` devolveu os três registradores a zero;
- `NAO ATIVA` reconstruiu o anel por MMIO, com `TDR_RESTART PROVADO`;
- a corrida seguiu sem a P2.

**Base mantida:** D2 ATIVO, `[P7_GFX_GTT] ATIVO`, `DrenoOk=1`, `ONLINE SUCESSO Anel=RAM_GART(P7)`.

**Trava dura aos ~99,6 s** (último tique 996), com a mesma assinatura:
- os últimos eventos são `SUBMIT/RETIRE_SDMA`, todos retirados;
- 6.153 IBs, zero TDR/timeout/fault/`DEVICE_REMOVED`;
- Kernel-Power 41 e EventLog 6008.

**Teste de RAM após a trava:** 8.141 MB × 2 passadas, zero erros.

## 2. Veredito: desfecho C

A doorbell não armou, e a corrida não testou a P2. A trava aconteceu com a configuração da 220 (WPTR por MMIO). Não há informação nova sobre a trava; há informação nova sobre a doorbell.

## 3. Por que não chegou: leitura do upstream (P1) e hipótese (H)

O `sdma_v5_0_gfx_resume` programa `SDMA0_GFX_DOORBELL`, `DOORBELL_OFFSET` e `nbio...sdma_doorbell_range` **com o F32 parado e o anel desligado**:
- depois de `MINOR_PTR_UPDATE=1` e do `WPTR`;
- antes do unhalt do F32 e do `RB_ENABLE`.

O amdgpu nunca liga a doorbell com o motor andando. Nós ligamos com o anel vivo.

**H:** o F32 carrega a configuração da doorbell ao subir o anel, e mudar os registradores com ele rodando não tem efeito até o próximo resume.

**Descartado como causa:**
- abertura da BAR de doorbell: a do MEC funciona na mesma BAR;
- offsets: o readback confere.

## 4. P2b (candidato 3.370.1.0)

- **O arme com o anel vivo foi removido.** O portão proíbe que ele volte.
- **`Bc250SdmaPresentP2ProgramLocked`:** RMW e readback dos três registradores, chamado **dentro de `Bc250SdmaPresentRestartAfterTdr`**, no lugar onde a reconstrução desligava a doorbell. A posição é a do upstream: F32 haltado, antes do unhalt e do `RB_ENABLE`.
- **A ativação reconstrói o anel.** `Bc250SdmaPresentEnableDoorbell` marca a doorbell como pedida e chama a reconstrução (soft reset e reprogramação integral, o mesmo caminho que a D2 usa).
- **Prova isolada, igual à da P2:**
  - o poll da shadow do WPTR fica desligado durante a reconstrução;
  - o NOP precisa retirar com pelo menos uma doorbell escrita;
  - depois o poll volta para `0x00403004`, com a shadow igual ao WPTR.
- **Falha:** a doorbell deixa de ser pedida e é desarmada, e o anel é reconstruído de novo por MMIO. É uma recursão só, sem laço.
- **TDR em regime:** a reconstrução programa a doorbell de novo e prova de novo, como o resume do amdgpu.

**Provas offline:**
- `build-contract` EXIT=0;
- laboratório "TODOS OS TESTES APROVADOS (0 falhas)";
- auditoria de segurança PASS;
- auditoria do pacote PASS;
- strings `APLICADO NA RECONSTRUCAO`, `[P2_DOORBELL] PEDIDA` e `ATIVA:` no SYS assinado.

**Portão `audit_p2_sdma0_doorbell` revisado: 17 mutações, todas reprovadas.** Inclui:
- doorbell desligada;
- doorbell movida para depois do `RB_ENABLE`;
- prova sem isolar o poll;
- prova sem doorbell escrita;
- laço com o restart;
- TDR perdendo a P2;
- ativação sem reconstruir;
- volta do arme com o anel vivo.

| Arquivo | SHA-256 |
|---|---|
| SYS | `1E24C811CB4D18D0F89B12727E51E595C3D8451C76D759CD0E5094D134DDE72F` |
| INF | `B81A0FE7CED1292513BB9F525CAB60E9CADE4ABD526C44B2249FF488F4F3789F` |
| CAT | `819BDA0AA02764B3FEDD86450182BA10136D336AECFD3C5C6BF528167F77E17E` |

**Scripts:** `tools/prepare-corrida222-p2b-doorbell-na-reconstrucao.ps1` e `tools/EXECUTAR_CORRIDA222_P2B_DOORBELL_NA_RECONSTRUCAO_600S.cmd`. O dry-run passou em hashes e manifesto e parou na CMOS, como esperado.

## 5. Critério da Corrida 222 (Lei 10)

**Pré-condição de validade:**
- `[P2_DOORBELL] PEDIDA`;
- `[P2_DOORBELL] APLICADO NA RECONSTRUCAO ... readback=confere; prova=1`, com os Kicks avançando;
- `[P2_DOORBELL] ATIVA:`;
- a base mantida: D2, P7, `DrenoOk=1`.

Desfechos:

- **A — 600 s sem trava dura**, com a doorbell ativa.
  - *Implicação:* a publicação do WPTR por MMIO era causa, ou parte dela. Repetir para confirmar.
- **B — trava igual**, com a doorbell ativa.
  - *Implicação:* a P2 sai das causas suficientes. Próximo: P1b (HDP por MMIO em cada submissão).
- **C — `NAO ATIVA` de novo**, com prova=0 e readback=1.
  - *Implicação:* a hipótese da posição cai. Os próximos suspeitos, ainda sem fonte local, são o `RCC_DOORBELL_APER_EN`/selfring e uma faixa de doorbell própria do Cyan Skillfish. Primeiro pedir a coleta Linux com `umr` desses registradores.
- **D — ativa, mas o SDMA0 para depois:** timeout ou TDR.
  - *Implicação:* falta algo da publicação em regime. O TDR reprograma e prova de novo.
- **E — fora do mapa:** descrever antes de julgar.

## 6. Pendências

- **Daniel:** recuperação, fusível, CMOS `-Ler` a menos de 15 min, boot frio e `tools\EXECUTAR_CORRIDA222_P2B_DOORBELL_NA_RECONSTRUCAO_600S.cmd`.
- **NÃO VALIDADO EM HARDWARE:**
  - a P2b;
  - a desmontagem da P7;
  - 600 s sem trava com 256 MB.
