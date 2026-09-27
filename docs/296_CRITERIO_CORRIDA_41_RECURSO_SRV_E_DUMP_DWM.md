# 296 — Critério da Corrida 41 — Qual recurso é recusado e por que o DWM falha?

> **Escrito em 19/09/2026, ANTES do ensaio.** Candidato lab **3.199.1.0**, sonda de onda
> **ARMADA**, WER LocalDumps do `dwm.exe` **opt-in**.
> **Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3) | Windows 10 Pro 22H2 build 19045.
> **Pergunta única:** *a SRV recusada recebeu uma DDI de escrita antes da recusa, e a pilha do
> `0xe0464645` atribui o fail-fast ao nosso caminho ou a outro caminho?*
>
> Assinatura e implicação são campos separados. Um resultado parcial não conta. O dump é uma
> medição independente do resultado A–D e nunca será usado por proximidade temporal para inventar
> causalidade.

---

## 1. De onde viemos

A Corrida 40 (19/09/2026 17:38:38, lab 3.198.1.0) provou no silício seis uploads de
textura do `dwm.exe`, dois descriptor sets completos e um draw gráfico com fence concluída.
Foram 137 submissões GFX, zero TDR, zero BugCheck, zero `GFXHUB_FAULT` e rollback
`RECOVERY_OK_ONLINE`.

A parede atual é mais estreita: o segundo pipeline do `dwm.exe` foi recusado duas vezes com
`SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA`. A linha não identificava o recurso. Assim, “ainda não
foi escrito” e “a escrita chegou mas não transitou a imagem” continuavam indistinguíveis.

Três crashes do `dwm.exe` (`0xe0464645`, `KERNELBASE+0x12d862`) ocorreram depois da coleta final.
A correlação temporal com a recusa não prova causa. A coleta da Corrida 40 encerrou 22 s após o
bind, embora o watchdog só tenha encerrado 104 s depois; a evidência arquivada perdeu os três
crashes e mais 29 submissões GFX.

**Tela preta continua sendo o resultado esperado. Nenhum texel foi lido de volta.**

---

## 2. O que mudou nesta faixa

| Mudança | Efeito observável |
|---|---|
| Coleta final presa ao prazo UTC absoluto do watchdog, não ao término do harness | um harness que morre cedo não encurta a janela física |
| Sink da UMD compara `%TEMP%` e `C:\Windows\Temp` já resolvidos | evita a segunda escrita quando os dois destinos são o mesmo arquivo; a causa em runtime ainda será medida |
| Contador `TextureWriteDdiCount` por recurso na fonte única `Bc250UmdSyncTextureToVkImage` | conta `UpdateSubresourceUP`/`Unmap` que chegaram ao recurso, sem alegar que o upload concluiu |
| Recusa SRV imprime ponteiro, dimensões, formato, `BindFlags`, `Usage`, contador e `VkLayout` legível | permite correlacionar a recusa com a história daquele recurso |
| LocalDumps exclusivo de `dwm.exe`, default desarmado e restaurado pelo binder/guardião | captura o fail-fast sem deixar configuração WER global armada após a janela |

O limite `GfxIbInspect=4096 DW` não mudou. IBs maiores continuam obrigatoriamente
`INDETERMINADO`; não houve relaxamento do veredito.

---

## 3. Critério primário — recurso SRV

### A — o recurso recusado não recebeu escrita instrumentada

| Campo | Critério |
|---|---|
| **Assinatura** | mesma linha contém `motivo=SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA`, `SRV_RESOURCE=0x...`, `TextureWrites=0` e `VkLayout=UNDEFINED` |
| **Implicação** | nenhuma chamada de `UpdateSubresourceUP`/`Unmap` chegou à fonte única para **esse recurso** antes da recusa. Isto favorece a hipótese de ordem, mas não prova que a ordem do DWM esteja correta nem exclui um caminho ainda não instrumentado. |

### B — a escrita chegou, mas a imagem continuou indefinida

| Campo | Critério |
|---|---|
| **Assinatura** | mesma linha de recusa nomeia um `SRV_RESOURCE`, traz `TextureWrites>0` e `VkLayout=UNDEFINED` |
| **Implicação** | ao menos uma DDI de escrita chegou ao helper para esse recurso, mas não estabeleceu layout de conteúdo. Procurar, pelo mesmo ponteiro, a recusa/pré-condição exata do bridge; não chamar isso de upload concluído. |

### C — a parede de descriptor deixa de ocorrer

| Campo | Critério |
|---|---|
| **Assinatura** | durante a coleta completa não há recusa do segundo pipeline; aparecem ao menos dois novos `[VK_DESCRIPTOR] BOUND` do `dwm.exe` e os draws correspondentes chegam a `DRAW fence concluida` |
| **Implicação** | a parede observada na Corrida 40 foi ultrapassada. Isso é avanço funcional do quadro, não prova de pixel, rasterização correta ou desktop visível. |

### D — regressão de estabilidade

| Campo | Critério |
|---|---|
| **Assinatura** | qualquer `NAO_CONVERGIU`, TDR, `GFXHUB_FAULT`, BugCheck, timeout novo, falha de rollback, ou o mesmo recurso alterna metadados impossíveis sem destruição/recriação registrada |
| **Implicação** | a regressão manda no veredito, mesmo que A, B ou C também apareça. Registrar a sequência completa antes de atribuir causa. |

### E — algo fora do mapa

| Campo | Critério |
|---|---|
| **Assinatura** | qualquer resultado não coberto integralmente por A–D |
| **Implicação** | **descrever antes de julgar**. Não encaixar parcialmente o observado numa letra. |

> Se uma linha tiver o motivo mas faltar ponteiro, contador ou layout, A e B **não ocorreram**.
> Se C tiver `BOUND` mas não os draws concluídos, C **não ocorreu**.

---

## 4. Atribuição independente do fail-fast

O dump não escolhe A–E. Para cada `dwm-crash-*.dmp`, registrar separadamente:

| Assinatura no dump | Implicação permitida |
|---|---|
| pilha contém `bc250umd.dll`, com símbolo/offset e caminho até o fail-fast | atribuição direta ao nosso módulo **somente no ponto mostrado pela pilha** |
| pilha não contém `bc250umd.dll`, mas contém DXGI/D3D/runtime com erro originado por retorno documentado da UMD | atribuir ao contrato/retorno medido; não converter correlação em chamada direta |
| pilha não alcança nosso módulo nem um retorno nosso identificável | fail-fast continua não atribuído à UMD; relatar o caminho real observado |
| dump ausente, truncado ou ilegível | atribuição **INCONCLUSIVA**; o resto da corrida ainda pode classificar A–E |

Também conferir `dwm-localdumps-state.json`: estado final restaurado, quantidade de dumps copiados e
`dwm_capture_restore_ok=true`. LocalDumps ainda armado ao final é falha de recuperação.

---

## 5. Controles e coleta obrigatória

- Comparar com a Corrida 40: 137 submissões GFX; zero `NAO_CONVERGIU`, TDR, BugCheck e
  `GFXHUB_FAULT`; seis texturas do DWM sincronizadas; dois descriptors; um draw concluído.
- Confirmar que a coleta `FINAL` alcança o prazo absoluto e inclui eventos posteriores à saída do
  harness. Se a pasta continuar menor que o log vivo, declarar a lacuna e recuperar a cauda.
- Recontar duplicidade por PID como pares adjacentes idênticos. Redução para zero valida o efeito da
  correção; não prova, sozinha, qual era a causa no runtime.
- Conferir rollback para BasicDisplay Código 0. Isso é recuperação, não sucesso do nosso desktop.
- Pixel continua **NÃO VALIDADO EM HARDWARE** até existir leitura de volta e comparação byte a byte.

---

## 6. Execução pelo proprietário

Antes do bind: `tools\inspect-boot-fuse.ps1` (`-Reset` se `TRIPPED`) e **boot frio obrigatório**.

Comando elevado para esta corrida, optando explicitamente pelo dump:

```text
tools\EXECUTAR_D3D11_NATIVO.cmd -CaptureDwmDump
```

Recuperação: `tools\BC250_RECOVERY.cmd`, elevado.

---

## 7. Pacote — 3.199.1.0, assinado, auditado, NÃO INSTALADO

`MCDM_LAB_BRINGUP` · `DISPLAY_RENDER_LAB` · WDDM 2.7 · schema 135 ·
`GfxWaveProbe=1` (**ARMADO**) · `HardwareValidated=false`

| Artefato | SHA-256 |
|---|---|
| SYS (pacote) | `749BA458F5F235B702C25204E17DF0DC85E98C9D6F0B5DD1F8E88926A7875041` |
| UMD (pacote) | `16146620436B47C7D142EF383C93801A9546A872E86B9E1E82D80583CFBD87BB` |
| INF | `2A345DD4948AB66AB96911E4CA8B55F7F326BD2BD0C9326575F353A8A7D7D7E7` |
| CAT | `1EFAFC735570F5F1A0D012E93AD1D239EAFAECDAB9F4286F972BE3A69650595B` |

Conferido por string na UMD assinada: `SRV_RESOURCE=%p`, `TextureWrites=%u`, `VkLayout=%s`,
`recurso e condicao nomeados`, `UNDEFINED` e `SHADER_READ_ONLY_OPTIMAL`.

Build KMD/contratos, build UMD/testes, auditoria de segurança, auditoria Node 1 e auditoria do pacote:
**PASS**. Nenhum bind ou instalação foi feito.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
