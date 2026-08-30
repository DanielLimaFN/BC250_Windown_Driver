# Runbook F8b — primeiro shader observável nos CUs

## Objetivo e critério de aceite

Este ensaio existe para obter, no **mesmo ciclo físico**, a prova completa:

1. blob gfx1010 allowlisted relido byte a byte;
2. preflight WGP estável e somente leitura;
3. resultado muda de `0x43DAFFFE` para `0xBC250C01`;
4. `RELEASE_MEM` fica estritamente depois de `DISPATCH_DIRECT` e a fence muda
   de `0xA55A0FF0` para `0xBC250D15`;
5. `RPTR == WPTR == SubmittedDwords`;
6. zero divergências na comparação byte a byte;
7. registradores e todas as memórias pertencentes à transação voltam ao
   snapshot original, com readback;
8. `ShaderPhysicalReady=1` e `HqdActivePhysicalReady=1`.

Qualquer item ausente mantém o veredito **NÃO VALIDADO EM HARDWARE**.

## Contrato de segurança

Máquina de estados obrigatória:

```text
[PREFLIGHT] -> [SNAPSHOT] -> [APPLY] -> [READBACK] -> [PROVA NO SILÍCIO] -> [ROLLBACK/TEARDOWN]
```

- Executar apenas uma tentativa por boot limpo.
- Não repetir automaticamente após timeout, Código 43, tela preta ou falha de
  rollback.
- O ensaio não escreve `CC_GC_SHADER_ARRAY_CONFIG`,
  `SPI_PG_ENABLE_STATIC_WGP_MASK` ou `RLC_PG_ALWAYS_ON_WGP_MASK`.
- Os aliases históricos `0x34FC` e `0x9C1C` estão em quarentena e não fazem
  parte do caminho executável.
- O preflight lê duas vezes os endereços catalogados `0x089BC`, `0x0935C` e
  `0x3B14C`, exige estabilidade e pelo menos um WGP simultaneamente
  não-harvestado e always-on.
- `C:\BC250_RECOVERY.cmd` deve existir antes do build, pacote ou binding.
- O binding exige PowerShell elevado e arma watchdog de rollback antes de
  entregar o dispositivo ao miniport de laboratório.

## Preparação offline

Na raiz do repositório:

```powershell
cmd /d /c driver\wdk\build-contract.bat

powershell -NoProfile -ExecutionPolicy Bypass -File `
  tools\build-bc250mcdm-lab-bringup.ps1 `
  -Execute -IUnderstandThisEnablesDestructiveBringup

powershell -NoProfile -ExecutionPolicy Bypass -File `
  tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch
```

Antes do ensaio, confirmar:

- todos os testes offline terminaram em `PASS`;
- `driver\wdk\package\output_lab.manifest.json` contém
  `ShaderDispatchEnabled: true` e `HardwareValidated: false`;
- INF empacotado contém `SchemaVersion=98`, `SkipHwBringup=0` e
  `ShaderDispatch=1`;
- a receita física está selada no INF e no manifesto:
  `CpSoftReset=1`, `Vmid0PageTable=0`, `SdmaAutoCtxSw=0`;
- SYS e CAT têm assinatura Authenticode válida;
- não existe watchdog antigo armado nem tentativa anterior pendente.

## Ensaio físico único

Este é o ponto destrutivo. Manter acesso de recuperação fora da GPU em teste.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  tools\bind-bc250mcdm-lab.ps1 `
  -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch `
  -WatchdogTimeoutSeconds 180
```

Se o display continuar visível, usar **exatamente** o comando de confirmação
impresso pelo script de binding. Ele contém `EvidenceDir`, `InstanceId` e o
token aleatório do watchdog. A confirmação visual apenas desarma o watchdog;
ela não valida compute.

Em seguida, capturar e julgar a telemetria sem escrever no hardware:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  tools\test-shader-dispatch-acceptance.ps1
```

O script cria `docs\evidence\<timestamp>-<host>-shader-f8b\` e só emite
`SHADER F8b: VALIDADO EM HARDWARE` quando todos os critérios da primeira seção
forem verdadeiros. O JSON gerado é a evidência canônica do ensaio.

## Falha e recuperação

Em qualquer falha, não fazer segunda submissão no mesmo boot. Se o watchdog não
tiver concluído a recuperação, executar por console de recuperação ou por uma
saída independente:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  tools\rollback-bc250mcdm-lab.ps1 `
  -Execute -IAcceptDisplayInterruptionRisk
```

Preservar o diretório de evidência, o `watchdog-state.json`, a telemetria Probe,
o Código PnP e os hashes do pacote. Timeout, fence sem resultado, cursores
divergentes ou rollback incompleto são falhas fechadas — nunca sucesso parcial.

## Estado atual

Uma primeira tentativa física foi executada em 30/08/2026. O dispositivo
entrou em `bc250mcdm` com Código 0, porém o caminho parou em
`PfpDirectStatus=14 (PM4_NOT_CONSUMED)` antes de ativar HQD ou submeter o
shader. O watchdog restaurou `BasicDisplay` e gravou `state=ROLLED_BACK`.

A análise mostrou que o primeiro pacote não materializava a receita física já
provada em 40/40 ensaios (`CpSoftReset=1`, `Vmid0PageTable=0`,
`SdmaAutoCtxSw=0`). O candidato 3.26 agora sela essa receita no INF e no
manifesto, revalida hashes/assinaturas/recuperação imediatamente antes do
binding e passou novamente pela suíte offline completa. O registro detalhado
está em `docs/99_REGISTRO_PRIMEIRA_TENTATIVA_SHADER_F8B.md`.

Uma segunda tentativa física foi executada no mesmo dia, após novo boot limpo.
A receita corrigiu o gate PFP (`PfpDirectStatus=0`,
`PfpDirectPhysicalReady=1`), mas o caminho parou em
`HqdActiveStatus=2 (PREFLIGHT_FAILED)` antes do primeiro write de HQD. A causa
foi localizada no transporte: os três offsets WGP estavam no contrato, porém
ausentes da allowlist de leitura MMIO. O retorno fail-closed zerou as saídas e
impediu o dispatch. O watchdog restaurou novamente `BasicDisplay`, Código 0.

O candidato 3.27 adiciona exclusivamente os três offsets catalogados à
allowlist de leitura; os testes provam que todos continuam proibidos para
escrita. Build, testes, pacote, hashes e assinaturas passaram offline. O
registro detalhado está em
`docs/100_REGISTRO_SEGUNDA_TENTATIVA_SHADER_F8B.md`.

Ainda é necessário outro boot limpo e nova autorização para a próxima
tentativa física única. Veredito atual: **NÃO VALIDADO EM HARDWARE**.
