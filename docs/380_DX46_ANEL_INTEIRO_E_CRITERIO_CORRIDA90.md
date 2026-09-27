# DX.46 — anel gráfico no tamanho inteiro; critério da Corrida 90

> 23/09/2026. Faixa **offline**, sem bind. Base:
> [Corrida 89 e o achado do anel pela metade](379_CORRIDA89_DX45B_RESULTADO_E_ANEL_PELA_METADE.md).
> Teto: **uma** corrida, depois de desligamento completo.

## 1. A variável única

**`Bc250GfxRingDeriveBufsz`** (`driver/core/bc250_gfx_ring_contract.c`):
- passa a `RB_BUFSZ = log2(RingBytes/8)`, `RB_BLKSZ = RB_BUFSZ - 2`;
- procedência: Linux v6.6 `gfx_v10_0.c:6106–6108` (P1). O anel do CP tem
  2^(RB_BUFSZ+1) DW;
- para o anel real de 64 KB, `RB_BUFSZ` sobe de **12 para 13**, e
  `CP_RB0_CNTL` planejado vai de `0x50000A0C` para **`0x50000B0D`**;
- o bring-up (`Bc250GfxRingTxnApply`) e o restart do TDR usam a mesma fonte.

**Fallback do restart:** era um `0x50000A0C` fixo. Agora deriva de `RingDwords`
pela fonte única; o `0x50000B0D` só entra se a derivação falhar.

**Nada mais muda.** DX.43, DX.44, DX.45b, UMD e RADV ficam como estão.

## 2. Verificação offline

- **Testes (`gfx_ring_contract_test`):**
  - a âncora antiga (8192 B → (9, 7)), que congelava o erro, virou 8192 → (10, 8);
  - casos novos: 4096 → (9, 7), 65536 → (13, 11), 1 MB → (17, 15);
  - invariante `2^(RB_BUFSZ+1) == RingBytes/4` de 4 KB a 1 MB;
  - o CNTL calculado para 8 KB = `0x5000080A` e **≠** `0x50000709`.
- **Portões:**
  - o antigo, que **exigia** reproduzir `0x50000709`, foi **invertido**;
  - portão novo `audit_dx46_gfx_ring_full_size`: proíbe o "-1" e o
    `0x50000A0C`, e exige os casos de 64 KB e a invariante.
- **Mutações:**
  - `tools/test-dx46-gfx-ring-size-mutations.py`: **6/6 reprovam**;
  - no nível da suíte, recolocar o "-1" **derruba o `build-contract`** em
    `gfx_ring_contract_test` linha 71, e o arquivo foi restaurado;
  - B.1 30/30, DX.43 7/7, DX.44 6/6, DX.45 11/11, DX.31 PASS.
- **Builds:** `build-contract` e build lab com EXIT=0.
- **Binário:** o SYS empacotado contém `0x50000B0D` e não contém
  `0x50000A0C`; o SYS da 89 contém o oposto.

## 3. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.251.1.0`. SYS `BE60274C4CCBFBD7EB273BDECE7939FAF180B6434FA44F6629FD9F8C9FE3F16C`, INF `D3CD963F…2984`, CAT `6F53B1CB…A691`. Auditoria do pacote PASS; strings DX.43/44/45b presentes. |
| UMD / RADV | inalteradas: `45BB28C3…`, DX.42 `FCAA11FE…` |
| Pré-voo | `tools/prepare-corrida90-dx46.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA90_DX46.cmd`, elevado, depois de **desligamento completo** (marcador armado no boot das 16:10:19) |

## 4. Critério, escrito antes

**Controle:** o log de bring-up tem que trazer `CntlPlanned=0x50000B0D` e
`[GfxRing] CNTL DECODIFICADO: RB_BUFSZ=13 RB_BLKSZ=11`. Se não trouxer, o
candidato não é o esperado e a corrida é **inconclusiva**.

| | Assinatura | Implicação |
|---|---|---|
| **A** | O WPTR passa de **8192** e o submit que cruza **conclui** (fence escrita, sem TDR ligado a ele); `GFX_TRIPE` com `RPTR_Reg` igual ao WPTR reduzido por 64 KB, sem `NAO_CONVERGIU` por volta de 8192 | O anel pela metade era a causa dos TDRs das 87/89. Se o WPTR também passar de **16384** sem TDR, a volta real do anel fica provada. |
| **B** | TDR no submit que leva o WPTR além de 8192, com o CNTL novo confirmado | A correção não basta; o mecanismo é outro. Reler o CNTL decodificado e o `RPTR` no TDR. |
| **C** | O WPTR **não** chega a 8192 | A variável não foi exercitada: **inconclusivo** para a pergunta principal. Registrar as outras classes de TDR. |
| **D** | O CP recusa o anel no bring-up (motor não fica ONLINE) ou há TDR na primeira submissão | O CNTL novo quebrou o bring-up. Reverter. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Perguntas secundárias:**
- DX.43, 5ª amostra: TDR com ondas no VMID anterior;
- `[PREEMPT_COMMAND]`;
- `[CP_RESET_DETECTED]` (A1/A2/F/G do docs/378);
- guardião durável.

**Função:** harness (base 16/60 na 87 e 13/60 na 89), desktop e device-lost.
Qualquer TDR, travamento ou menos de 60/60 é falha funcional.

Desfecho parcial não é desfecho. Exemplo: A exige o submit que cruza 8192
**concluído**, não só o WPTR acima de 8192.
