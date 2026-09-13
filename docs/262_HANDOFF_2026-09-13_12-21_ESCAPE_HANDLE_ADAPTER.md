# Handoff 13/09/2026 — corrida 12:21 e contrato correto do escape de composição

## Veredito

A corrida física de 13/09 12:21, candidato 3.168.1.0, **refutou** a atribuição
do bloqueio a `D3DDDI_ESCAPEFLAGS.HardwareAccess`: com a flag em zero, a UMD
repetiu a mesma assinatura da corrida 12:09, que usava a flag em um — oito
`BltViaKmd ENTER`, zero `BltViaKmd LEAVE` e nenhuma chegada do opcode
`0xBC250002` ao KMD.

O defeito comum às duas corridas estava na chamada da UMD. O tipo
`PFND3DDDI_ESCAPECB` recebe `hAdapter` como primeiro argumento, mas
`Bc250UmdBltViaKmd` passava `d->Winsys.RuntimeDevice`. O `hDevice` pertence ao
campo `D3DDDICB_ESCAPE.hDevice`, que já estava preenchido separadamente.

A correção foi implementada, compilada, testada e empacotada. **Ainda não foi
instalada nem validada em hardware.**

## Evidência física da corrida 12:21

- dispositivo `bc250mcdm` em Código 0 durante a janela;
- 8 entradas `BltViaKmd ENTER` e 0 saídas `LEAVE`;
- 0 ocorrências do opcode `0xBC250002` no KMD;
- 0 entradas/êxitos de `COMPOSITE_BLT` no KMD;
- 3.681 submissões Node 0 aceitas e 3.681 aposentadas;
- 0 `IB_TRIPOD TIMEOUT`, 0 TDR e 0 falha de notificação;
- 1.248 tripés completos e 2.433 provas de duas pernas;
- rollback online restaurou `BasicDisplay` em Código 0.

Evidência arquivada:
`docs/evidence/2026-09-13_12-21-46-DESKTOP-HQAG6T4-lab-binding/`.

Esses dados isolam o bloqueio antes do handler de escape do KMD. Eles não
provam que a correção atual atravesse o dxgkrnl.

## Correção implementada

Em `driver/umd/bc250_umd_blt.inl`:

- o primeiro argumento de `pfnEscapeCb` agora é
  `d->pAdapter->hRTAdapter.handle`;
- `args.hDevice` continua sendo `d->Winsys.RuntimeDevice`;
- `args.hContext` continua nulo;
- `args.Flags.HardwareAccess=1`, porque o handler dirige o SDMA;
- adapter, handle runtime do adapter, device, callbacks e callback de escape
  ausentes recusam antes da chamada;
- o trace publica `hAdapter`, `hDevice`, `hContext` e flags separadamente.

O teste D3D UMD passou a usar handles deliberadamente diferentes (`0xA00A`
para o adapter e `0xD00D` para o device), confere cada campo no callback e
testa a recusa sem adapter e sem handle runtime.

A auditoria de segurança exige o handle correto e `HardwareAccess=1`, proíbe
o padrão antigo e exige as asserções dos dois handles. Uma mutação simultânea
para `HardwareAccess=0` e primeiro argumento `hDevice` foi recusada com as duas
causas esperadas; depois da restauração, a auditoria voltou a passar.

## Validação offline

- build e testes UMD: `EXIT=0`;
- teste de contrato D3D UMD: todos passaram;
- build-contract completo: `EXIT=0`;
- auditoria de segurança: `PASS`;
- auditoria de segurança por mutação negativa: `FAIL` esperado nas duas
  violações;
- rebuild após restauração: `EXIT=0`;
- auditoria do pacote lab: `PASS`.

## Candidato produzido — não instalado

- `DriverVer`: `09/13/2026,3.169.1.0`;
- perfil: `DISPLAY_RENDER_LAB`, WDDM 2.7;
- pacote: `driver/wdk/package/output_lab/`;
- manifesto: `driver/wdk/package/output_lab.manifest.json`;
- SYS: `80374141A9A1C0828929255B96D0912A7476F3A78BC477246C9ADFB6F3C05F3E`;
- UMD: `A5DE7CD24489807A7D78FE9A58C758BC047DEDDEC8076C26BA4D29F79ED157DC`;
- INF: `82FFEFD4D49A7422144F6396088EC2C0C205AFCB39CCECFFD2FD6340B159615B`;
- CAT: `EA2EE0B3E35164ADE56A6E5E770785DF9FCAEB21607F078E3B49640FEFAEC371`.

O SYS assinado é o mesmo da corrida anterior; a mudança está na UMD e no
contrato do pacote. `HardwareValidated=false` permanece no manifesto.

## Teste que fecha esta faixa

Na próxima corrida física, o primeiro discriminante é uma cadeia única:

1. `BltViaKmd ENTER` mostra `hAdapter != hDevice` e `Flags=0x1`;
2. o KMD registra `[McdmEscape] ENTER` com `Dword0=0xBC250002`;
3. aparece `COMPOSITE_BLT ENTER` e depois o veredito do blt SDMA;
4. a UMD registra `BltViaKmd LEAVE` com o `HRESULT` e `ResultCode`;
5. nenhuma submissão de contexto é criada pela composição e não há TDR.

Se a cadeia parar, o último marco alcançado nomeia a fronteira. Pixel visível
e prova de bytes continuam sendo exigidos para atribuir a imagem à GPU; apenas
atravessar o escape não fecha o desktop.

**VEREDITO DA CORREÇÃO E DO PACOTE 3.169.1.0: NÃO VALIDADO EM HARDWARE.**
