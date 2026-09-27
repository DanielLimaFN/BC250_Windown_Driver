# Critério da Corrida 58 — PTE do alvo `0x200040000`

> Escrito antes do ensaio. Candidato lab **3.219.1.0**, SYS assinado
> `5E53951F2A1F89D91C3B3815C9615F6870DC519B053D292617832F72C46FBC8C`.
> Esta corrida mede a identidade e a permissão do alvo sem depender do latch de fault.
> Pacote auditado e **NÃO INSTALADO**.

## Pergunta

Quando o RADV mapeia a arena de 256 KiB em `0x200040000`, o DXGK entrega a página como
executável e o KMD grava o bit `EXECUTABLE` na PTE?

## Controle obrigatório

- `[DX_TARGET_200040] MAP` deve aparecer pelo menos uma vez e trazer `GpuVa=0x200040000`,
  `MapPages=64`, token/nome, `AllocBytes`, `NoExecute`, `EncodedPte` e `EncodeStatus`.
- O perfil e os hashes devem coincidir com este documento.
- Relatar TDR/restart, BugCheck, harness D3D11, pixel e rollback sem converter controles em
  sucesso da medição.

## Desfechos

### A — defeito de permissão encontrado

`NoExecute=1`, ou `NoExecute=0` mas `EncodedPte` sem o bit 4. A primeira variante atribui a
origem ao contrato DXGK/RADV; a segunda atribui ao codificador KMD. Não corrigir ambos às
cegas: o campo decide o dono.

### B — alvo executável

`NoExecute=0`, `EncodeStatus=0` e `EncodedPte & 0x10 != 0`. Isso refuta falta de permissão de
execução como explicação suficiente. O próximo passo passa a ser conteúdo/publicação da arena
e o ponto de parada, mantendo `GfxWaveProbe=0` porque a Corrida 45 mediu `selector_stuck`.

### C — alvo não apareceu

Nenhuma linha dedicada, mas o D3D11/RADV não chegou a mapear `0x200040000`. Corrida
inconclusiva; não é falha do novo log.

### D — identidade incompleta

A PTE fala, porém `AllocKnown=0`, token zero ou classe desconhecida. As permissões ainda são
interpretáveis, mas DX.11 continua aberto e o ciclo de vida do objeto precisa ser corrigido.

### E — inesperado

Linha incoerente com a atualização, erro de codificação, novo endereço/PC, BugCheck, TDR sem
restart ou qualquer combinação não coberta acima. Descrever antes de julgar.

## Veredito permitido

Sucesso diagnóstico se A ou B ocorrer com linha completa e coerente. Nenhum desfecho fecha
DX.5, D0.7, pixel ou desktop.

## Comando emitido pelo dry-run

```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableD3D11LabPromotion -EnableD3D11ShaderTranslator -EnableNode1JournalGatePerProcess -RunD3D11Native -WatchdogTimeoutSeconds 120 -EnableDwmCrashCapture -IAcceptAutomaticRecoveryReboot
```

## Resultado medido — 21/09/2026 17:55:20

Evidência: `docs/evidence/2026-09-21_17-55-20-DESKTOP-HQAG6T4-lab-binding/`.
Identidade conferida: lab 3.219.1.0 e SYS
`5E53951F2A1F89D91C3B3815C9615F6870DC519B053D292617832F72C46FBC8C`.

**Veredito da pergunta pré-declarada: sucesso diagnóstico, desfechos B + D.** Das 102 linhas
do alvo, 27 eram mapeamentos válidos. As 25 ocorrências com a geometria da arena
(`MapBase=0x200040000`, `MapPages=64`, `AllocBytes=0x40000`) trouxeram invariavelmente
`ReadOnly=1`, `NoExecute=0`, `RawFlags=0x29`, `EncodeStatus=0` e PTE terminada em `0x31`:
o bit `EXECUTABLE` 0x10 está presente. A classe permaneceu `CLASSE_DESCONHECIDA`, portanto a
identidade semântica continua incompleta. As outras duas ocorrências eram sobreposições
nomeadas `RESOURCE` e `PRIMARY`, grandes e graváveis, corretamente `NoExecute=1`; não são a
arena de 64 páginas e não contradizem B.

O ensaio também encontrou um bloqueador mais forte fora da pergunta original. Três TDRs
decodificaram uma **escrita** (`Cid=8`, `Rw=1`) na GPUVA `0x2469A6000`, dentro de uma faixa
viva de 0x5C0000 bytes. Na terceira ocorrência, em VMID 2, a janela de publicação ficou
inalterada entre a submissão e o fault, e a caminhada pela raiz `VIVA_READY` resolveu uma
PTE válida com flags `0x71` para `0x458F72000`. Isso refuta, para essa ocorrência, tanto
"folha ausente" quanto "folha CPU-visível sem R/W/X". Não prova que a GPU observou a mesma
publicação; visibilidade/invalidação e semântica dos níveis da tabela permanecem abertas.

Controles: 6 TDRs e 6 `TDR_RESTART PROVADO`; 3 sem latch e 3 com o fault acima; 1.900 IBs
SDMA; 26 instalações do pool, zero exaustão; máximo GFX `83/81/2`; 107 vereditos com fence
escrita pela GPU, mas todos com a perna 3 aberta. O harness falhou no primeiro Present
(`0x887A0005`, remoção `0x887A0007`, 0/600), houve dois `BYTE_PROOF INCONCLUSIVO` e nenhum
pixel foi provado. Rollback online sem reboot, `RECOVERY_OK_ONLINE`, BasicDisplay Código 0
em cinco amostras. Nenhum BugCheck ou dump novo foi observado no material coletado.
