# Critério da Corrida 68 — O IB gráfico deixa de ser cumulativo (faixa DX.22)

> **Escrito em 22/09/2026, ANTES do ensaio** (Lei 9). **Estado:** NÃO VALIDADO EM HARDWARE.
> **Pacote do driver:** lab **3.229.1.0**, o mesmo da Corrida 67 (SYS `F2A3F42C...`, UMD
> `A24F3A3F...`). **Muda só o ICD:** `vulkan_radeon.dll` construído do fork com a DX.22,
> sha256 `45F937536A4EF368F66AC55BE34557776732F8302BC1EE824A62996C10B169B3`. O
> `tools\EXECUTAR_D3D11_NATIVO.cmd` copia essa DLL de `build\` para `C:\BC250_LIB` no passo 1b.

---

## 0. O que a Corrida 67 mediu

- `gfx-ib-dump/` com 32 arquivos: todos os IBs do DWM na **mesma VA** `0x244f24000` (o BO do CS
  do preâmbulo), crescendo 392 DW por submissão, e o IB *n* com prefixo byte a byte igual ao IB
  *n-1*. `tools/check-gfx-ib-dump-cumulative.py` reprova essa pasta com **18 pares de replay**.
- Causa no fonte (P1): o ramo "BC-250 UNIFICADO" de `radv_wddm2_cs.c` copiava o CS principal para
  dentro do preâmbulo e deixava o `cdw` dele crescido. A GPU reexecutava todos os draws antigos,
  com descriptor sets e shaders já liberados.
- 6 faults `Cid=10` (SQC data) em `VA>>8`, 6 TDRs, e `vkCreateDevice` falhando com `res=-3` nas
  duas tentativas depois do TDR.

## 1. O que a DX.22 muda

1. O IB concatenado é montado num **BO próprio** de um pool por contexto e IP (até 32 slots). O
   preâmbulo e os CS da aplicação **não são escritos**.
2. Um slot só volta a ser usado depois que uma fence sinalizada **após** a submissão dele retira. Sem
   fence conhecida e sem slot livre, a submissão é **recusada** com `CONCAT RECUSA` e
   `VK_ERROR_DEVICE_LOST` — reescrever IB que a GPU pode estar lendo é pior que falhar.
3. CS que cresceu para vários IBs encadeados **não** é concatenado (o ramo antigo copiava só o
   último buffer e perderia o começo); vai para o encadeamento, com `CONCAT PULADA`.
4. O despejo de IB passa a gravar o **buffer realmente submetido** (o slot), não o preâmbulo.

Verificação offline: build do fork EXIT=0; `audit-radv-wddm-safety.py` PASS com portão novo; o
fonte anterior reprova em 7 pontos; mutação (prender o slot antes de a submissão ser aceita)
reprova. `check-gfx-ib-dump-cumulative.py`: reprova a 67, inconclusivo na 66 (sem despejo),
aceita reuso legítimo de slot (caso sintético).

## 2. Perguntas

1. Os IBs deixam de conter draws de submissões anteriores?
2. Sem o replay, os faults `Cid=10` em `VA>>8` e `Cid=9` em `0x200040000` desaparecem?
3. Sem TDR, o DWM passa do 8º draw e chega a apresentar?

**Previsão registrada (H):** sim para 1 e 2. Para 3 não há previsão — é a primeira vez que o DWM
rodaria draws sem replay.

## 3. Desfechos (Lei 10 — assinatura e implicação separadas)

### A — Replay eliminado e faults somem
- **Assinatura:** `check-gfx-ib-dump-cumulative.py` sai **0** (`SEM CUMULATIVO`) sobre pelo menos
  10 despejos **e** zero `[TDR_GCVM_FAULT]` de qualquer `Cid` **e** zero `TDR_QUIESCE` **e** algum
  processo `dwm.exe` com `VkDraws` acima de 8 no log da UMD. As quatro condições são exigidas.
- **Implicação:** o replay era a causa da classe `Cid=10 VA>>8` / `Cid=9`. DX.5 pode ser fechada
  pela regra da §1 do plano, e o próximo alvo é D0.7 e D1.6 (pixel não-nulo).

### B — Replay eliminado, parede seguinte nomeada
- **Assinatura:** as quatro de A, **e** o Present recusado ou um draw posterior falhando com motivo
  nomeado no log.
- **Implicação:** a parede do replay caiu; a seguinte está nomeada pela própria linha.

### C — Replay eliminado, fault continua
- **Assinatura:** checker sai **0**, **e** aparece `[TDR_GCVM_FAULT]` com `Cid=10` em endereço que
  resolve pela caminhada `VA<<8`.
- **Implicação:** o replay não era a única fonte. O próximo suspeito é o **conteúdo** do descriptor
  set no instante do draw (o VS lê o V# do CB em `set+64`, `[VK_PIPELINE_IR]` da 67). Não trocar de
  hipótese sem ler a memória do set.

### D — O ICD novo não entrou em jogo
- **Assinatura:** checker sai **1** outra vez, **ou** o hash de `vulkan_radeon.dll` registrado em
  `binding-provenance.json` (external_libs) é diferente de `45F93753...`, **ou** aparece
  `CONCAT RECUSA` / `submit RECUSADO` em `conformance-stderr.tmp`.
- **Implicação:** o ensaio não aconteceu como desenhado (DLL não copiada, ou pool recusou). Corrigir
  a implantação antes de ler qualquer outra coisa.

### E — Algo fora do mapa
- **Assinatura:** qualquer observação que não caiba em A–D — BugCheck, regressão dos controles
  (upload, aposentadoria GFX, `RECOVERY_OK_ONLINE`), `CONCAT PULADA` inesperada, IB **vazio** ou
  menor que o preâmbulo, avanço não previsto.
- **Implicação:** descrever o observado antes de julgá-lo.

**Desfecho parcial não é desfecho** (Lei 10.3).

## 4. Pendência conhecida que esta corrida não resolve

`vkCreateDevice` com `res=-3` depois de um TDR (Corrida 67, dwm.exe e explorer.exe). Se houver TDR,
esperar a mesma falha — ela é o item 2 da fila, não desta faixa.

## 5. Execução

Boot frio, depois `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`. Na análise, rodar primeiro:

```bash
python tools/check-gfx-ib-dump-cumulative.py docs/evidence/<pasta da corrida>/gfx-ib-dump
```
