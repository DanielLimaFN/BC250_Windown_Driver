# 722 — Pacote lab 3.425.1.0 (D19 + diagnóstico de recovery) e critério da Corrida 275

**Data:** 13/10/2026, offline (sem corrida). Continuação dos docs [720](720_CORRIDA274_D05_CONFIRMADO_RECUPERACAO_COM_REBOOT_E_TELA_PRETA_TRANSITORIA.md) (pendências da 274) e [721](721_TABELA_COMPLETA_DE_FEATURES_DO_DCN.md) (mapa completo de features).

**Pedido do Daniel:** bundlar o máximo de coisas úteis numa corrida só (corrida custa caro) e fazer a mudança de resolução/refresh funcionar.

## 1. Por que D19 (refresh) e não D18 (resolução) nesta corrida

Investigação offline antes de propor qualquer coisa: li `inc/linux_ref/dcn/dcn201_dccg.c` (o bloco de clock do DCN para **este ASIC exato**, Cyan Skillfish 2). Achado:

```c
static void dccg201_update_dpp_dto(struct dccg *dccg, int dpp_inst, int req_dppclk)
{
    /* vbios handles it */
}
```

Neste chip, **o relógio de pixel não é programado pelo driver de vídeo — é a VBIOS (firmware) que decide**, por uma tabela ATOM que não temos catalogada (Lei 7: sem proveniência, não escrever). Isso é o que D18 (troca de resolução de verdade) precisaria tocar, e por isso ele **continua bloqueado** — tentar "no escuro" arrisca tela preta precisando de boot a frio, exatamente o risco que o `CLAUDE.md` já avisa para mexida de PHY/clock.

**D19 é diferente**: troca só a taxa de atualização (Hz) **dentro do mesmo relógio de pixel**, mexendo apenas em `OTG0_V_TOTAL` — um registrador puramente de temporização (quantas linhas tem um quadro), sem precisar de relógio novo, sem PHY, sem link. Essa é a parte seguramente alcançável da "mudança de resolução" nesta rodada, e a base de qualquer D18 futuro.

## 2. O que foi construído

- **D19** (`driver/core/bc250_dcn_ext_contract.h/.c`, `driver/wdk/bc250_display_ddi.c`): função pura `Bc250DcnComputeVTotalForRefresh` (só diminui o refresh — aumentar poderia encolher o front porch até invadir o sync) + `Bc250Dcn2VTotalProve`, autocontida: lê o V_TOTAL do GOP, calcula um V_TOTAL para -10 Hz, escreve, confere por readback (falha fechada se não bater), **mede de verdade pela varredura** (não só o readback do nosso registrador) via `Bc250Dcn2MeasureTiming`, loga o resultado, e **devolve ao V_TOTAL do GOP antes do desktop aparecer**, medindo a volta também.
- **Diagnóstico de recovery** (`tools/BC250_RECOVERY.cmd`): quando a 1ª tentativa de apagar `bc250mcdm.sys` falhar com "ainda existe depois da exclusão forçada" (o que aconteceu na 274), agora registra o estado do serviço (`Get-CimInstance Win32_SystemDriver`) no log — só leitura, não muda nenhum comportamento, só dá mais dados se a pendência da 274 (StopDevice sem log de teardown) se repetir.
- **D06 e D12** seguem exatamente como na 274/doc 718: D06 lido (chave já ligada por padrão no `DcnExt=125` herdado), D12 **continua fora** do pacote.

## 3. Candidato: lab 3.425.1.0

| Chave | Valor | Efeito |
|---|---|---|
| `DcnFlip` | 1 | Flip pelo HUBP0 |
| `DcnExt` | **381 (0x17D)** | `0x3D` (273) + `0x40` D06 + **`0x100` D19 armada** |
| `DcnExtAuxHwModeArmed` | **false** | D12 continua fora |
| `DcnExtVTotalRefreshTestArmed` | **true** | D19 armada nesta corrida |
| `DebugTrace` | 1 | Modo debug |

Build: `build-contract.bat` completo (todas as ~70 suítes, incluindo `DCN FLIP CONTRACT` e `DCN EXT CONTRACT` com os novos casos de D19) **OK**; `build-bc250mcdm-lab-bringup.ps1` **OK**; `package-bc250mcdm-lab.ps1 ... -ArmVTotalRefreshTest -IAcceptVTotalRegisterWrite` **OK**; `audit-bc250mcdm-lab-package.ps1`, `audit-bc250mcdm-safety.py` (com o portão novo `audit_dcn19_vtotal_refresh_proof`), `audit-mcdm-ddi-matrix.py`, `audit-ddi-noop-callbacks.py`: todos **PASS**.

**Mutação provada (Lei 10):** duas, para o portão novo de D19 — (a) mudei a prova para pedir um refresh MAIOR em vez de menor: a auditoria reprovou com "a prova tem de pedir um refresh MENOR"; (b) removi a devolução final ao V_TOTAL do GOP: a auditoria reprovou com "esperadas 3 escritas em OTG0_V_TOTAL". As duas revertidas, auditoria voltou a `PASS`.

Hashes do pacote (`driver\wdk\package\output_lab\`):
- `bc250mcdm.sys` = `2390C10B22A6B1935213657CFACAD5962F880EEB7F1FF639CBBD1FEBAF757DAA`
- `bc250mcdm.inf` = `F3A318484AA6D1F64B1206F5DA16AF2978223B9378C17CC78D680EF97AF4900B`
- `bc250mcdm.cat` = `6863280AF0300434F7E6C73ED7CDA8DC53AA6458123305C881CB5BC1C4A0D0C6`
- `bc250umd.dll` = `B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66` (igual à 273/274)
- RADV `vulkan_radeon.dll` = `B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D` (igual)

Lançador: `tools\EXECUTAR_CORRIDA275_D19_180S.cmd`. Preflight: `tools\prepare-corrida275-d19-recovery-diag.ps1` (recusa se `DcnExtAuxHwModeArmed` não for `false` ou `DcnExtVTotalRefreshTestArmed` não for `true`).

## 4. Critério da corrida, escrito antes (Lei 10)

**A — D19 confirmado.** `[DCN2_VTOTAL] APLICADO` seguido de `MEDIDO` com um Hz próximo do pedido (atual − 10 Hz, dentro de ~0,1 Hz de tolerância de medição) e depois `DEVOLVIDO` com o Hz voltando ao original — tudo isso **antes** da primeira linha de `[DX48_LIFECYCLE] StartDevice LEAVE`, ou seja, antes do desktop aparecer. → Trocar `V_TOTAL` muda o refresh de verdade, sem tocar clock/PHY/link; D19 fecha por medição, e é a base concreta para D27 (VRR) no futuro.

**B — D19 não mede o esperado.** `APLICADO` aparece mas o Hz `MEDIDO` não se move (fica igual ao original) ou se move para um valor muito diferente do pedido. → A hipótese de que blank/sync são independentes de `V_TOTAL` (o que sustenta a segurança da escrita) estaria errada ou incompleta; não repetir sem entender por quê.

**C — Readback ou devolução falhou.** `[DCN2_VTOTAL] READBACK FALHOU` ou `AVISO: devolução ... não confirmou`. → A escrita fechou por conta própria (falha fechada), sem artefato visual esperado; registrar e não insistir sem investigar o motivo do readback.

**D — Regressão.** Qualquer coisa que a 273/274 já tinham fechado piora: `conta=DIVERGE`, cursor ou DPMS parando de funcionar, BugCheck novo, ou a tela preta transitória do Alt+Tab virando permanente. → Nenhuma parte do resultado é aceita antes de entender qual.

**E — A pendência da 274 (StopDevice sem log).** Critério específico: `[DX48_LIFECYCLE] StopDevice ENTER` aparece desta vez (resolve/explica sozinho) **ou** não aparece de novo e o diagnóstico novo do `BC250_RECOVERY.cmd` mostra o estado do serviço no momento da falha (dá mais dado, mesmo sem resolver). Qualquer um dos dois é informação nova; a ausência total de dado (nem o log, nem o diagnóstico) seria o único desfecho realmente vazio.

## 5. Pendências

- **NÃO VALIDADO EM HARDWARE.** Nada foi instalado nem ligado na placa.
- **D18 continua bloqueado** até surgir uma via catalogada (Lei 7) para pedir um relógio de pixel novo à VBIOS neste chip — não é tarefa de uma corrida, é pesquisa própria primeiro.
- **D12** continua pronto no código, fora do pacote, esperando autorização.
- Antes do bind: desligar a máquina (não reiniciar) e ligar de novo.
