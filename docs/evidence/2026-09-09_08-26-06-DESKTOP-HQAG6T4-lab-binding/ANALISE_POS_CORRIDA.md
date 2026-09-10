# Corrida 09/09 08:26 — laboratório 3.105.1.0

## Medição

Identidade do SYS confirmada pelo binder: `19F5D13030D7B7BEF08D2D8A935F8F0A62D783AF1A11531C901A455E809DFBC2`. O recorte `display-log-post-run.txt` parte do offset 3130810 registrado no baseline, evitando misturar corridas antigas do log cumulativo. Hash e contagens em `post-run-inspection.json`.

- **Restauração MMU exercitada com sucesso (P0):** StopDevice registra `Global GFXHUB MMU restored to snapshot: Regs=26`. A falha anterior por MMIO state=0 não reapareceu. A correção de manter o mapeamento até a restauração foi exercitada nesta corrida; isso não prova todos os caminhos de erro de teardown.
- **Node0 (P0):** cinco tripés completos com cinco canários e Divergentes=0; StopDevice registra Accepted=11, NotExecuted=0. Nenhuma transação TLB status=8 no recorte.
- **Node1 (P0):** VMID9_BIND Root=0x45FFB0000 Status=0 Ready=1; publicação de 68 bytes em VA=0x11000. Harness: Accepted 17->18, Retired 17->17, WPTR 204->216, RPTR 204->204, fence/dados não comprovados. **NÃO VALIDADO EM HARDWARE.**
- **Caminhada CPU (P0):** PDE=0x45FFAF001; PTE=0x451FCC071, válida, resolve VA=0x11000 para 0x451FCC000. Isso prova a leitura de uma tradução válida pela CPU, não o conteúdo do IB nessa página, nem a busca/execução do MEC.
- **Recuperação (P0):** sequência TDR SDMA com NOP/fence provados; evento Display/4101 às 08:26:23. Watchdog ROLLED_BACK às 08:29:09, exit_code=0. CIM independente confirma BasicDisplay/Código 0. Consulta de eventos não encontrou novo BugCheck ou boot não limpo no intervalo.

## Lacuna do diagnóstico introduzido no candidato

Nenhuma linha MEC_PENDING_ARCHIVE ou MEC_IB_DESCRIPTOR_ARCHIVE no recorte, inclusive no StopDevice. O arquivo contém PT_WALK NODE1_IB, portanto o caminho novo foi exercitado parcialmente. A captura pendente é condicional à execução do polling com engine online, VMID9, fence pendente, observação diferente da esperada e idade >=100 ms. A ausência não identifica qual condição impediu a captura. Os contadores de DPC/poll no arquivo `probe-registry-at-bind` são anteriores ao ensaio Node1 e não demonstram que o timer não rodou durante o ensaio.

Correção de enquadramento: a captura prometida no handoff 214 **não foi obtida**. Não transformar sua ausência em diagnóstico de timer ou GPU. Antes de outra corrida, é necessário tornar a fotografia de encerramento independente desse polling e registrar os motivos de não captura. A causa exata do hang Node1 permanece aberta. Não repetir 3.105.1.0 por tentativa.
