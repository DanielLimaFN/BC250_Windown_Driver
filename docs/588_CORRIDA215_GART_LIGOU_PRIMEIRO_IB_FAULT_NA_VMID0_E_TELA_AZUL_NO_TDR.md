# 588 — Corrida 215: a GART ligou e passou na prova; o primeiro IB real deu fault na VMID 0 e o TDR terminou em tela azul

**Data:** 04/10/2026, bind às 21:52:18 · **Candidato:** lab **3.363.1.0** (D2 por GART) · **Critério:** [doc 587](587_D2_POR_GART_DA_VMID0_COMO_O_LINUX_E_CRITERIO_CORRIDA215.md) §5
**Evidência:** `docs/evidence/2026-10-04_21-52-18-DESKTOP-HQAG6T4-lab-binding/display-log-this-bind-215.txt` (642 linhas, extraídas do log vivo a partir de 5.775 bytes) e `C:\Windows\MEMORY.DMP` (21:52, 840 MB). Dump lido pelo Daniel (elevado) em `dump-215.txt`.

## 1. Medido (P0)

**A D2 por GART ligou, com os três sinais de ativação:**
- `[D2_GART] TABELA ... PTE[1]=0x44E9A2077 PTE[2]=0x44E9A3077 ... divergentes=0`;
- `[D2_GART] CONTEXTO0: status=0 ... BASE=0x00000004:6FF02001 END=0x1FF CNTL 0x007FFE80->0x007FFE01 readback=1 flushVmid0=1`;
- `SYNC_NOP TRIPOD PASS ... in 2 polls`, `TDR_RESTART PROVADO`, `[D2_ANEL_SISTEMA] ATIVO RingGpu=0x1000 WorkGpu=0x2000`;
- `[VMFLUSH_PROBE] VEREDITO ANEL: PROVADO ... Dados 0/16 divergentes`. **É a terceira perna na RAM, pela GART.**
- `StartDevice LEAVE status=0`. O inventário da GCVM depois da D2 lia `VMID00 Cntl=0x007FFE01 PtBase=0x46FF02000`.

**O primeiro IB de paginação real (FenceId 1, 8.896 bytes, dividido em 5 pontos) não andou:**
- `IB_TRIPOD TIMEOUT: WPTR=1368 RPTR=216`. O 216 é exatamente onde a sonda terminou.
- **Fault:** `Status=0x00001B3B -> Vmid=0 WalkerError=5 MappingError=1 Cid=13 ... GpuVa=0x1000`.
  - Cid 13 = **SDMA0** (`gfxhub_v2_0.c`, P1).
  - O endereço é a **página do anel na GART**, a mesma que acabara de funcionar.
  - O latch estava preso (`LATCH PRESO`), então pode ser o primeiro fault, não o último.

**TDR:**
- O quiesce correu.
- O restart reconstruiu o anel **ainda pela GART** e falhou: `SYNC_NOP TIMEOUT WPTR=40 RPTR=0`.
- Veio a tela azul: **`VIDEO_TDR_FAILURE (0x116)`, Arg3=`0xC00000A3` (`STATUS_DEVICE_NOT_READY`)** (P0, `dump-215.txt`). A pilha é `TdrResetFromTimeoutWorkItem → DXGADAPTER::Reset → TdrBugcheckOnTimeout`. Arg2=`bc250mcdm+0x241d0` é a etiqueta do adaptador, não o ponto da falha. O `DEVICE_NOT_READY` é o retorno do nosso `RestartFromTimeout`, porque o NOP de prova pela GART não retirou a fence. Confirma o §2. No boot seguinte o fusível recusou o StartDevice.

## 2. Leitura

- **Avanço de diagnóstico:**
  - a GART da VMID 0 funciona neste driver e neste chip, com prova de três pernas na RAM;
  - a AGP não funcionava (doc 586).
- **O que quebra é a tradução depois da prova:** a mesma página da GART traduzia e, um instante depois, deixa de traduzir para o SDMA0.
- **Candidatos (H):**
  1. alguém reescreveu o contexto 0;
  2. alguém sobrescreveu a tabela em `0xFF02000`;
  3. o caminho de tradução (UTCL2/L2) perdeu a entrada e o re-walk falhou.
- **O log não tem como separar os três:** no TDR não líamos nem os registradores nem a tabela.
- **A tela azul tem causa direta (P1, leitura de código):** o restart do TDR usava o anel onde ele estava, a GART quebrada, e devolvia falha.

## 3. Veredito pelo critério do doc 587

- **Não é A, B nem C.** A D2 ligou (os sinais de A e B) e não chegou à janela de 600 s.
- **É E, fora do mapa:** GART provada e, em seguida, fault de tradução na mesma página.
- Não houve trava dura nesta corrida, mas também não houve tempo de uso para isso significar algo.

## 4. Correção e instrumentação (candidato 3.364.1.0, offline)

- **Foto da GART** (`Bc250ActiveD2GartFoto`): os 7 registradores do contexto 0, as PTEs 0–3 da tabela (por um mapeamento UC mantido) e o `FAULT_STATUS`, comparados com o que foi programado.
  - Roda logo depois de ligar, nos primeiros 4 timeouts de IB (pelo gancho, em DISPATCH, só leitura) e no quiesce do TDR.
  - **Separa os três candidatos:** registradores divergentes, PTEs divergentes, ou ambos intactos.
- **TDR com retorno à VRAM** (`Bc250SdmaPresentRestartAfterTdrOrRevert`): se o restart pela GART falhar, o gancho desliga a GART, o anel volta para a VRAM e é provado de novo. A RAM fica retida.
  - Troca a tela azul por "a corrida segue sem a D2".
- **Portão estendido** com três invariantes. Uma mutação (TDR sem retorno) reprova.
- `build-contract` EXIT=0, build de laboratório "TODOS OS TESTES APROVADOS", auditorias PASS.

| Arquivo | SHA-256 |
|---|---|
| SYS | `02A6795A3F2A373FB68E4BBB2F77D1D792AA2A6D5640ED4B029FE538FDDE539B` |
| INF | `3672472CE96A8A286A13B7BB7281FD43CB14021195E89E7E574A61C884BB1F9B` |
| CAT | `672E06F0318B0C781268567503E8C51EC9384223066CC4F8A82D9EB48011FDAE` |

**Scripts:** `tools/prepare-corrida216-d2-gart-foto-e-volta.ps1` e `tools/EXECUTAR_CORRIDA216_D2_GART_FOTO_E_VOLTA_600S.cmd`.

## 5. Critério da Corrida 216 (Lei 10)

- **A — a foto no timeout mostra registradores divergentes.**
  - *Implicação:* alguém reprograma o contexto 0 depois da D2. Caçar o escritor (carga do PFP, MEC/RLC); o valor lido diz qual.
- **B — a foto mostra PTEs divergentes, com os registradores intactos.**
  - *Implicação:* a tabela em `0xFF02000` é sobrescrita. Mudar a tabela de lugar ou reservá-la no alocador.
- **C — registradores e PTEs intactos, e mesmo assim fault.**
  - *Implicação:* é o caminho de tradução (UTCL2/L2, invalidação). Próximo passo: comparar com a invalidação do Linux para a VMID 0, ou desistir da D2 e ir para a D1.
- **D — não há timeout:** a D2 segue ativa pela janela inteira.
  - *Implicação:* vale o critério A/B do doc 587 (trava ou não).
- **E — fora do mapa.**

**Em qualquer desfecho com timeout**, o TDR deve mostrar `TDR: anel de volta na VRAM e provado` e a corrida deve seguir sem tela azul. Se houver tela azul de novo, o retorno falhou e isso também é um resultado.

## 6. Pendências

- Recuperação, fusível e boot frio antes do bind.
- **NÃO VALIDADO EM HARDWARE:** a D2 estável além do primeiro IB, o retorno à VRAM no TDR, a H-BAR, e 600 s sem trava com 256 MB.
