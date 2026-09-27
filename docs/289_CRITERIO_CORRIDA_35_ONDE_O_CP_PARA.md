# 289 — Critério da Corrida 35 — Onde o CP grafico para

> **Escrito em 19/09/2026, ANTES do ensaio.** Candidato lab **3.192.1.0**.
> **Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3) | Windows 10 Pro 22H2 build 19045
> **Pergunta única desta corrida:** *em que ponto, e esperando o quê, o `CP_RB0` para?*

---

## 1. Esta corrida NÃO é para acender a tela

Escrito antes para não haver dúvida depois: **tela preta é o resultado esperado e correto.**
Não há rasterização provada, não há pixel no scanout, e nada nesta faixa tenta produzir um.
Qualquer imagem que aparecer deve ser atribuída ao rollback/BasicDisplay até prova em contrário —
o mesmo engano já foi cometido e corrigido em 07/09 18:16 e em 10/09 11:22.

O que está em teste é **diagnóstico**. A Corrida 34 travou dentro de um IB de 1008 DW e nos
deixou com três hipóteses e nenhuma medição que as separasse. Esta corrida existe para trocar
as três hipóteses por um nome.

---

## 2. O que mudou desde a Corrida 34

| # | Mudança | Onde | Efeito esperado nesta corrida |
|---|---|---|---|
| 1 | Sonda do ponto de parada do CP | `bc250_active_miniport.c`, quiesce do TDR | 4 linhas novas de log no TDR |
| 2 | Máscara de halt do restart corrigida (`0x7` → `0x15000000`) | `bc250_tdr_recovery_contract.c` | restart com ME/PFP/CE parados agora **reprova** |
| 3 | Despejo do IB gráfico, opt-in | RADV `radv_wddm2_cs.c` | arquivos `.bin` na pasta de evidência |
| 4 | Fail-open de fence removido | RADV `vk_wddm2_monitored_fence.c` | device-lost propaga em vez de virar sucesso |
| 5 | Harness confere `Present()` e aborta | `d3d11_native_conformance_test.cpp` | para no 1º Present que falha, exit 2 |

**Aviso de leitura sobre a #5:** a contagem de quadros vai **cair** em relação aos "52" da
Corrida 34. Isso **não** é regressão — aqueles 51 quadros eram no-ops após o dispositivo removido.
O número novo é o primeiro número honesto que este harness produz.

**Aviso de leitura sobre a #2:** é possível que apareçam linhas
`[TDR_GFX_RESTART] Readback INVALIDO: status=restart_not_ready ... MeCntl=0x15000000`.
Isso é o portão **funcionando** — na Corrida 34 esse mesmo estado saía como
"rearmado e comprovado no silício". Um restart que reprova é informação; um restart que mente não é.

---

## 3. Critério, escrito antes — quatro desfechos, todos distinguíveis

A pergunta é respondida pelo cruzamento de **duas** fontes que agora existem: a sonda do KMD,
que diz onde o CP parou, e o despejo do RADV, que diz o que havia naquele ponto.

### Desfecho 1 — ONDA VIVA (o suspeito passa a ser shader/ACO)

```
[TDR_GFX_WAVES] ... WaveLevelSA0>0 ou WaveLevelSA1>0
[TDR_GFX_SE]    ... SPI=1 e/ou TA=1, CB_CLEAN=1
```

Há onda em voo que não retira. O IB está correto e o problema é a execução do shader — muito
provavelmente `s_waitcnt` sobre um fetch que nunca volta. **Próximo passo:** disassembly do
shader ACO e as descriptors de vertex buffer, não o stream PM4.

### Desfecho 2 — ZERO ONDA, ME PARADO NUM PACOTE (o suspeito passa a ser o stream ou o estado)

```
[TDR_GFX_WAVES]  ... WaveLevelSA0=0 WaveLevelSA1=0
[TDR_GFX_PACKET] ME_HEADER_DUMP=0x........ Opcode=0x.. (NOME)
[TDR_GFX_PACKET] IB1 Base=0x............ RestamDW=N
```

O pacote tem **nome**. Cruzar com o despejo:

```bash
python tools\decode-gfx-ib.py docs\evidence\<pasta>\gfx-ib-dump\bc250_ib_000_va..._dw1008.bin --stopped-at-opcode 0x<OPCODE> --remaining-dw <N>
```

`>>` marca as ocorrências do opcode; `**` marca o pacote que `CP_IB1_BUFSZ` aponta. Onde os dois
coincidem está o ponto de parada. **Próximo passo:** ler o emissor daquele pacote no RADV.

### Desfecho 3 — ESPERA NOMEADA PELO CP (o suspeito é sincronização, não desenho)

```
[TDR_GFX_CPWAIT] CP_BUSY_STAT=... SemPolling=1 ou SemFailedHolding=1 ou EopDoneBusy=1 ou CoherCntNeqZero=1
```

O CP não está travado desenhando; está esperando semáforo, EOP ou o contador de coerência drenar.
Isso muda a frente inteira para o par fence/EOP entre RADV e KMD. Repare que este desfecho pode
**coexistir** com o 2 — nesse caso o `CP_BUSY_STAT` diz *por que* o ME parou naquele pacote, e a
combinação é mais informativa que qualquer um sozinho.

### Desfecho 4 — A SONDA NÃO RESPONDE

```
[TDR_GFX_PACKET] ME_HEADER_DUMP INDETERMINADO: Read32 recusou 0x08684
[TDR_GFX_WAVES]  SQ_DEBUG_STS_GLOBAL INDETERMINADO: Read32 recusou 0x08C24
```

Os offsets derivados de `gc_10_1_0_offset.h` não são legíveis nesta placa, ou a allowlist recusou.
A pergunta fica **SEM RESPOSTA** e o defeito é da instrumentação, não do silício — as linhas dizem
isso explicitamente em vez de publicar zero como se fosse medição.

**Nenhum destes quatro desfechos era distinguível na Corrida 34.** Os quatro produzem informação.

---

## 4. Medições de controle (o que precisa continuar igual)

Se qualquer uma destas regredir, a faixa introduziu defeito e isso manda no veredito:

| Controle | Valor da Corrida 34 |
|---|---|
| `NODE1_STOP_GCVM_FAULT` | `STATUS=0x00000000 ADDR=0` |
| `GFXHUB_FAULT` | zero ocorrências |
| Node 0 SDMA | `IB_TRIPE_PROVADO`, 5 canários, `Divergentes=0` |
| `GATE FISICO PASS` no 1º IB gráfico | presente |
| BugCheck | zero |
| Rollback | online, `BasicDisplay Código 0` |

---

## 5. Preparação obrigatória antes do bind

1. **Pacote:** 3.192.1.0 ja esta construido, assinado e auditado (ver secao 7). NAO INSTALADO.
2. Fusível de boot: **INEXISTENTE** na última verificação (próximo boot assume `CLEAN`).
   Conferir com `powershell -ExecutionPolicy Bypass -File tools\inspect-boot-fuse.ps1`;
   se estiver `TRIPPED`, rodar com `-Reset`.
3. **Boot frio obrigatório.** O estado de engine não cai com desbind — só com reinício a frio.
4. Recuperação à mão, se algo travar: `tools\BC250_RECOVERY.cmd` elevado.

---

## 6. O que a corrida NÃO responde

- **Não fecha o Marco 3.** Não há pixel na tela, por construção.
- **Não prova rasterização.** O harness D3D11 não lê um pixel de volta; ele conta Presents que o
  runtime aceitou. Isso está dito no próprio rodapé do harness, e nenhuma saída dele pode ser
  citada como prova de rasterização (Lei 2).
- **Não valida a máscara de halt corrigida como *cura*.** Ela faz o restart parar de mentir; não
  faz o CP voltar a executar. Se o restart reprovar, a divida "o nosso reset deixa o CP halted"
  fica **aberta e agora visível**, que é o ponto.
- **Não exercita o TDR induzido** nem qualquer caminho novo de escrita em registrador. A sonda é
  somente leitura, travada pelo portão `audit_gfx_stall_probe_is_read_only`.

---

## 7. Pacote — PRONTO, assinado e auditado, NAO INSTALADO

**PACOTE LAB 3.192.1.0**, `BuildKind=MCDM_LAB_BRINGUP`, perfil `DISPLAY_RENDER_LAB`
(`RenderNode=1`, `RenderOnlyLab=0`), contrato WDDM 2.7, modo **P1 PERSISTENT SUBMISSION**,
schema 135, `SkipHwBringup=0`, `ShaderDispatch=1`, `CpSoftReset=1`.

| Artefato | SHA-256 |
|---|---|
| SYS (pacote, assinado) | `E48CBB12A4836A42552AAD874698FFE58A6FA47829F4A642B01B53036ADDD42D` |
| UMD | `372FEBCFD24C7B2217A0413B3DFEDF35A9D475D02DADF5CAADD8787DF412D678` |
| INF | `DE5A191677BB96767BAF78F547E9471904A5F0390787504D7B646EED819FE111` |
| CAT | `C0B0758F01633F7539E6B033E525E4F47E41A8C14DD3365882FFA8648FD502CC` |
| `vulkan_radeon.dll` (ja em `C:\BC250_LIB\`) | `0cdc4971d778ab7054d2147d126f59d450175859151bfc6a7b80ee28c247dfc9` |

`audit-bc250mcdm-lab-package.ps1`: **PASS**.

**Binarios conferidos POR STRING** — o empacotador copia, nao compila, e ja assinou binario de
candidato anterior por esse motivo. No SYS **assinado** estao as seis marcas da faixa:
`TDR_GFX_PACKET`, `TDR_GFX_WAVES`, `TDR_GFX_SE`, `TDR_GFX_CPWAIT`,
`ME_HEADER_DUMP INDETERMINADO` e `SQ_DEBUG_STS_GLOBAL INDETERMINADO`, mais os nomes de opcode
`DRAW_INDEX_AUTO` e `PREAMBLE_CNTL` da tabela nova de decodificacao PM4.

A UMD e' a MESMA do 3.191.1.0 (`SourceUmdSha256` identico) — nenhuma fonte de UMD foi tocada
nesta faixa, e isso e' proposital: as variaveis que se movem sao a sonda do KMD e o RADV.

**Armadilha registrada nesta sessao:** `build-bc250mcdm-lab-bringup.ps1` gera apenas a IMAGEM
(`driver\wdk\output_lab\bc250mcdm.sys`); quem produz o pacote assinado e'
`package-bc250mcdm-lab.ps1`. Rodar so' o primeiro deixa o `output_lab` do pacote no candidato
ANTERIOR, e a auditoria acusa `BLOCKED: hash divergente`. Foi exatamente o que aconteceu antes
deste bloco ser escrito. A sequencia correta e':

```bash
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode
powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-lab-package.ps1
```

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
