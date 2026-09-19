# docs/251 — C3.14: candidato de capacidade GPUVA pronto para corrida

**Data:** 2026-09-12  
**Estado:** **EXECUTADO E INSUFICIENTE** na corrida 18:21. O snapshot 1024 não bastou porque o journal anterior continuou limitado a 256; ver [docs/252](252_HANDOFF_2026-09-12_18-21_JOURNAL_NO_SPACE_E_WATCHDOG.md).  
**Candidato:** laboratório `3.157.1.0`, perfil `DISPLAY_RENDER_LAB`, WDDM 2.7.

## Decisão

A leitura integral da evidência corrigiu dois fatos do handoff 250:

- a amostra é `Mapped=73 Reusable=183`, não o inverso; houve pico de `Mapped=252`;
- o `Scratch` começa vazio e representa somente o estado final. Logo, replace in-place não faz um estado final com mais de 256 faixas caber no teto antigo.

A C3.14 eleva `BC250_GPUVA_SPACE_MAX_RANGES` de 256 para **1024**. A reação do `ActiveFlushTlb` ao erro não mudou, para preservar uma variável por candidato.

## Prova offline

- replay de contrato: 252 faixas existentes + commit de 256 mudanças = **508 faixas publicadas**;
- teste confere publicação atômica, slot ativo, ocupação e cobertura;
- build completo: `EXIT=0`;
- auditoria de segurança: `PASS`;
- mutação 1024 → 256: auditoria recusou; restauração para 1024: `PASS`;
- auditoria do pacote e prebinding LAB: `PASS`.

## Identidade do pacote

| Campo | Valor |
|---|---|
| DriverVer | `09/12/2026,3.157.1.0` |
| BuildKind | `MCDM_LAB_BRINGUP` |
| SYS fonte | `805B81855CF15B994E4BBD606DD5D77AAEF285BDCC85113E4535279772FBD9C2` |
| SYS empacotado | `0244CE8C876DBF4D58612F2C1F7897683804AF9EF32863F4CDC722C545024B58` |
| UMD | `DD4DFED4DDC0A8E18E133FB2C82F1D2AE788F268799383BE6D91A9C36A5C13B1` |
| INF | `40586BE38815C0F2599969A8FAACA1DE37779D8A3CD6637F2324AB057FCE4CF6` |
| CAT | `89A2FA3B049BA71D568D5F68EE8BDE4091847028C918DCFAA88E8823109D9CB2` |

## Estado do host antes da corrida

- fusível: `CLEAN`, Boots=0, LastStatus=0;
- dispositivo: `BasicDisplay`, Código 0;
- serviços e arquivos experimentais: ausentes;
- pacotes OEM correspondentes: ausentes;
- reboot pendente: nenhum indicado pelo preflight.

## Corrida física solicitada

Após boot frio, em PowerShell elevado, a partir da raiz do repositório:

```powershell
powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-prebinding.ps1 -LabPackage
```

Somente se terminar em `BC250 MCDM PRE-BINDING AUDIT: PASS`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableD3D11LabPromotion -EnableNode1JournalGatePerProcess -RunRadvPresentTripod -WatchdogTimeoutSeconds 180
```

## Critério de decisão

**C3.14 passa** se a corrida não emitir `PublishStatus=3`/`BC250_GPUVA_NO_SPACE`, não entrar em `ExecutionState=7`, completar o RADV present tripod e preservar Present por GPU com `HostCpuCopies=0`.

Se `NO_SPACE` persistir, registrar `Cap`, `Mapped`, `Reusable` e o tamanho do batch; não aumentar o teto novamente sem explicar a geometria observada. Se houver travamento duro, usar **Ctrl direito + Scroll Lock duas vezes** para gerar dump, sem reset pelo botão.
