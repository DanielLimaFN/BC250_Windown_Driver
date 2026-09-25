# DX.57 — cauda real da UMD depois da recovery (24/09/2026)

## Achado da Corrida 101

Os arquivos `bc250_umd_log_*.txt` na pasta da Corrida 101 foram copiados pelo binder às **11:45:03–07**, enquanto a janela física ainda estava aberta. O binder executa a cópia antes de terminar; num travamento duro não chega à coleta final. Portanto, as últimas linhas dessas cópias (`dwm.exe` em `DXGI Present ENTER` e `explorer.exe` em `DestroyPresentContext ENTER`) **não localizam o travamento**. Os originais foram recuperados depois com PowerShell elevado: DWM e Explorer registraram até 11:49:27, ~0,66 s antes do último rastro DX.56. Ver [resultado atualizado](405_CORRIDA101_DX56_RESULTADO.md).

## Correção offline

- `tools/bind-bc250mcdm-lab.ps1` grava `umd-snapshot-manifest.json` com `kind=INTERMEDIATE_BIND_SNAPSHOT`, hora da captura, tamanho e SHA-256 de cada cópia. A cópia existente continua sendo feita.
- `tools/watch-bc250mcdm-safe-bind.ps1` arquiva `BC250_UMD_LOG_*.txt` em `umd-logs-pos-travamento/` **após** executar `BC250_RECOVERY.cmd`, com `umd-post-recovery-manifest.json`, SHA-256, boot da corrida e boot da recovery. A ACL da cópia é restaurada para a sessão de análise sem elevação; o resultado fica no manifesto. Falha de acesso/cópia não impede o rollback nem a verificação PnP.
- A auditoria de segurança exige que a chamada de coleta permaneça depois da recovery; proíbe perder o rótulo de snapshot, o hash da cauda ou a ACL legível. `tools/test-dx57-umd-tail-archive-mutations.py`: 4/4 mutações rejeitadas. `tools/test-dx57-umd-tail-archive.ps1`: cópia e manifesto PASS em fixture temporária. Parser PowerShell e auditoria integral PASS.

## Limites e próximo passo

Mudança apenas de coleta; **não corrige o travamento duro**, não altera KMD/UMD nem foi validada em hardware. O guardião só age quando o sistema consegue executá-lo; na Corrida 101 retomou após o reboot manual. A cauda da 101 foi copiada manualmente com SHA-256; a DX.57 automatizará isso nas corridas seguintes quando o binder armar a nova cópia do guardião. Nenhuma nova corrida foi executada aqui.
