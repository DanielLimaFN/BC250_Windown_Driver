# DX.50 — bissecção: dirty rects desligados; critério da Corrida 96

> 23/09/2026. Faixa **offline**, sem bind.
> **Base:** [Corrida 95](391_CORRIDA95_DX49B_RESULTADO_MAIS_RAPIDO_E_TRAVAMENTO.md),
> com ~3× mais Presents, defeito visual e travamento duro. A Corrida 94 também
> travou.
> **Teto:** uma corrida, depois de desligamento completo.

## 1. Pergunta

**Pergunta:** o travamento duro e o defeito visual das 94/95 vêm das cópias SDMA
por retângulo (dirty rects), que só passaram a existir com a DX.49?

**Por que esse é o primeiro corte:**
- os dirty rects são o comportamento de hardware mais novo (zero cópias por
  retângulo antes da 94, 384 na 95);
- mexem direto no scanout, onde apareceu o defeito;
- o scanout também recebe a escrita direta da UMD (`radv_bc250_present_scanout`).

## 2. A mudança

**Variável comportamental (uma):** `bc250_display_ddi.c`,
`static const BOOLEAN s_Bc250Dx50DirtyRectsEnabled = FALSE;`.
- O Present só entra no caminho de retângulos se o interruptor estiver ligado.
  Desligado, todo Present volta à cópia de tela cheia da Corrida 93.
- A validação continua compilada. A recusa sai no log como
  `DIRTY_RECTS_RECUSADOS … motivo=DX50_BISSECCAO_DESLIGADO` (amostrada: 16
  primeiras e depois 1 em 128).

**Continuam ligados:**
- lote de draws;
- staging persistente;
- trace durável (DX.49/49b).

**UMD:** inalterada, `725279BA…`.

**Correção de coleta, sem efeito na GPU** (`tools/bind-bc250mcdm-lab.ps1`):
- **Antes:** os `BC250_UMD_LOG_*` residuais em `C:\Windows\Temp` eram
  **apagados** antes de cada bind. Depois de um travamento duro, o log do
  dwm.exe da corrida travada só existe ali, e o bind seguinte o destruía: foi o
  que aconteceu com o log da 94 no bind da 95.
- **Agora:** são **movidos** para `umd-logs-residuais-do-bind-anterior/`, com
  `SHA256SUMS.txt`, na pasta do bind novo.
- **Efeito na 96:** o bind da 96 vai arquivar o log do dwm.exe da **Corrida 95**
  (pid 1200), se ele ainda estiver lá.

## 3. Verificação offline

- **Build:** build lab EXIT=0 (inclui `build-contract` e a auditoria de
  segurança).
- **Portão novo `audit_dx50_dirty_rect_bisection`:**
  `tools/test-dx50-bisection-mutations.py` **5/5 reprovam**:
  - dirty rects religados;
  - Present ignorando o interruptor;
  - recusa sem o motivo da bissecção;
  - binder voltando a apagar;
  - arquivo sem hash.
- **DX.49:** 19/19 continua.
- **Pacote:** auditoria PASS; `DX50_BISSECCAO_DESLIGADO` presente no SYS.

## 4. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.256.1.0` SYS `041E71DC128BC6D4C66F074958CA24C7C59C7F59767C956C38CB7C2E4D720F5C`, INF `4F304588…FDBC`, CAT `0976811B…CD1C` |
| UMD | `725279BA…` (a mesma da 95) |
| RADV | DX.42 `FCAA11FE…` |
| Pré-voo | `tools/prepare-corrida96-dx50.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA96_DX50.cmd`, elevado, janela de 300 s, depois de **desligamento completo** (marcador do rollback de 23:37:51) |

## 5. Critério, escrito antes

**Controle:**
- `StartDevice LEAVE status=0x00000000`;
- `DIRTY_RECTS_RECUSADOS … motivo=DX50_BISSECCAO_DESLIGADO` presente;
- zero `DIRTY_RECTS n=`;
- `[DX49_BATCH]` no log do dwm.exe (o lote continua ligado).

**Presents/min** são medidos no log do dwm.exe (`DXGI Present LEAVE hr=0`).

| | Assinatura | Implicação |
|---|---|---|
| **A** | Janela inteira **sem travamento**, 60/60, **sem defeito visual** relatado, e presents/min **≥ 70** | Os dirty rects eram a causa do travamento e do defeito. A próxima faixa corrige o caminho por retângulo, provavelmente a escrita dupla do scanout (KMD e UMD), antes de religá-lo. |
| **B** | Sem travamento e sem defeito, mas presents/min **< 70** | Os dirty rects causavam o problema e também carregavam parte da vazão. A mesma correção vale, e a vazão fica para depois. |
| **C** | **Travamento** de novo, com os dirty rects desligados | A causa não são os dirty rects: fica o lote, ou a vazão maior em si. Próximo corte: lote desligado (`BC250_DX49_BATCH_MAX_DRAWS` = 1) mantendo o staging. |
| **D** | Sem travamento, mas **com defeito visual** | Travamento e defeito têm causas diferentes. O defeito vem do lote ou de outro ponto, e a foto e o log do DWM apontam qual. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Secundárias:**
- conteúdo de `umd-logs-residuais-do-bind-anterior/`, que deve trazer o log do
  dwm.exe da 95;
- ciclo de vida DX.48;
- DX.43;
- guardião;
- relato visual, com foto se houver defeito.

**Depois da corrida:** as 5 seções e a tabela-checklist atualizada.
