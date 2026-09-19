# Handoff 198 — bind protegido integrado ao tripé Node 1

**Data:** 08/09/2026  
**Trabalho:** integração e validação offline  
**Instalações/binds/submissões:** 0  
**Veredito:** **NÃO VALIDADO EM HARDWARE**

## Entrega

`tools/bind-bc250mcdm-lab.ps1` ganhou `-RunUmdNode1Tripod`. O ensaio não depende mais de
uma segunda janela depois que o display apagar: o próprio binder, ainda sob watchdog,
espera os marcos de display deste bind e chama exatamente uma vez o runner
`B7_UMD_NODE1_TRIPOD`.

O caminho exige:

- `DISPLAY_RENDER_LAB`, Render Node e submissão persistente;
- `GpuvaLabPromotion` com aceite explícito;
- watchdog automático ativo e janela mínima de 90 s;
- pacote SYS/UMD/INF/CAT com hashes idênticos ao manifesto;
- UMD fonte idêntica ao `SourceUmdSha256`;
- `Node1TripodDefaultArmed=false` e `D3D10_11NativeReady=false`;
- escolha exclusiva: RADV e tripé UMD não podem rodar no mesmo bind.

O runner passou a provar que ainda restam `TimeoutSeconds + 10` no watchdog, confere que o
watchdog pertence à mesma instância PCI e limita o harness a 30 s. Em timeout, encerra
somente o processo filho; em todos os desfechos remove o token do ambiente e desarma a
autorização. O console vai para `umd-node1-tripod-console.txt` e o gate de display para
`compute-display-prerequisite-gate.json`.

## Estado medido antes da corrida

| Preflight | Estado |
|---|---|
| BC-250 | BasicDisplay, Código 0 |
| boot fuse | `CLEAN`, checksum válido, Boots=3 |
| D3D11CapsProbe | desarmado |
| Node1Tripod | desarmado, sem token |
| recuperação | presente; SHA-256 `789547DD42A0375A0BE88044E2BD7DE0E77F8F5454A0BAE05E36DD91F7BD1DB8` |
| marcador pós-rollback | `CLEAN_BOOT_REQUIRED`, criado em 08/09/2026 09:53:51 |

O dry-run do binder foi recusado porque `rollback_boot_ticks` ainda coincide com o boot
atual (`LastBootUpTime` 08/09/2026 08:47:23). Isso é o comportamento correto. O marcador
não foi removido e nenhum APPLY ocorreu.

## Verificações

| Gate | Resultado |
|---|---:|
| parse dos dois scripts PowerShell | PASS |
| `py_compile` da auditoria | PASS |
| `tools/audit-bc250mcdm-safety.py` | PASS, com gates novos do bind/runner |
| runner Node 1 sem `-Execute` | DRY-RUN PASS |
| binder completo sem `-Execute` | RECUSADO pelo boot frio pendente |

## Próxima ação física

1. Fazer **boot frio** mantendo BasicDisplay.
2. Abrir PowerShell elevado no repositório.
3. Executar primeiro o mesmo comando sem `-Execute`; ele deve chegar a `Modo Dry-Run`.
4. Somente então repetir com `-Execute -IAcceptBlackScreenRisk`.

Flags congeladas para este candidato:

```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 `
  -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission `
  -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion `
  -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180
```

Na execução física, acrescentar apenas:

```text
-Execute -IAcceptBlackScreenRisk
```

Teto: uma corrida. Abandono: primeiro preflight inválido ou primeira falha. Até ela:
**NÃO VALIDADO EM HARDWARE**.
