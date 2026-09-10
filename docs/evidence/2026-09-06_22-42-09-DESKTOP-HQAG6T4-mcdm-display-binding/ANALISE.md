# Análise da corrida 2026-09-06 22:42 — `P5_VIDPN_PIVOT_CONVERGENCE`

**Candidato:** pacote ativo fail-closed reconstruído em 06/09 22:29, com as correções da faixa A
(A1 caps de tipo de driver por fonte única, A5 `/Oy-`, A8 CRLF no `build-contract.bat`).
**SYS assinado:** `6CB992A2C8994D2921F2DFD4659896818B4B50D58AAAE71ADAC5D737F3779C2D`
**Veredito do teste nomeado:** os três marcos exigidos apareceram. **A Área de Trabalho
visível NÃO foi alcançada** — e a causa está medida.

## 1. O que passou

### A correção A1 chegou ao silício

```text
[DriverCaps] TypeCaps(NonVGA=1, SmoothRotation=0, DirectFlip=0, PerEngineTDR=1, FlipOnVSyncMmIo=1)
```

Primeira vez que os caps de tipo de driver são observáveis no log. `SupportNonVGA=1` é o
campo que o handler WDDM zerava.

### A negociação de VidPn convergiu

| Medida | Corrida 20:34 (perfil lab) | **Esta corrida (perfil ativo)** |
|---|---:|---:|
| `IsSupportedVidPn` | 97 | **18** |
| `EnumVidPnCofuncModality` | 65 | **26** |
| `GetStdAllocData` | 0 | **132** |
| `SharedPrimary` | 0 | **2** |
| `CommitVidPn` | 0 | **1** |
| `SetVidPnSourceAddress` | 0 | **1** |

```text
=== CommitVidPn Entered: AffectedSource=0, Started=1, hAlloc=FFFF918CBA569B90 ===
=== SetVidPnSourceAddress (SourceId=0, Segment=1, Addr=0xAB0000, hAlloc=FFFF918CBA569B90) ===
```

**Ressalva obrigatória de atribuição:** este é o **perfil ativo**, que **já havia convergido**
em 05/09 22:20 com o mesmo padrão de 18 consultas e primária em `0xAB0000`. Portanto esta
corrida **reproduz** a convergência e prova que as correções da faixa A **não a quebraram** —
ela **não** prova que a correção de pivô causou a convergência. Esse teste continua sendo o do
**perfil lab**, que é onde o padrão 97/0 aparece. **NÃO VALIDADO EM HARDWARE:** que o pivô
resolva o perfil lab.

### O blt de apresentação executou na engine

```text
[SdmaPresent] BLT_TRIPOD PASS #1: WPTR=380 RPTR=380 Fence=00001004 Poison=43DAFFFE Bytes=11059200
```

12 `Present`, **10 com `SDMA=YES`**, zero `CPU_FALLBACK`, zero `FAILED_CLOSED`, mais 12
tripés de `SYNC_NOP`. 11.059.200 bytes = um frame inteiro de 2560x1080x4.

## 2. Por que a tela ficou preta, mesmo assim

O blt copiou **um frame em branco**. O log é explícito, e as 63 amostras do arquivo são
unânimes:

```text
[Present #1] ScanoutSample @ 0x0: 00000000 00000000 00000000 00000000
[Present #1] BYTE_PROOF PASS: Source=0xAB0000 Dest=0x0 Bytes=11059200
             (PostScanout: NonZero=0/256, Hash=0x00000000)
```

`NonZero=0/256` em **63 de 63** amostras. Nenhum pixel diferente de zero, nem antes nem
depois do blt. A engine moveu 11 MB de preto.

### A primária ficou em branco porque o DWM nunca conseguiu criar device

`bc250umd.dll` foi **carregada pelo `dwm.exe`** e respondeu fail-closed (o mesmo já havia
sido observado na corrida de 05/09 22:20, analisada no doc 169):

```text
22:42:11.909 [pid=1200 dwm.exe] === bc250umd.dll CARREGADA ===
22:42:11.910 [pid=1200 dwm.exe] OpenAdapter10_2: D3D10/11 nativo indisponivel -> E_NOTIMPL
```

Somando os 7 logs de UMD desta corrida: **1024 `OpenAdapter10_2`**, **512 `OpenAdapter10`**,
**2 `OpenAdapter12`**, e **770 `E_NOTIMPL`**. O DWM tentou em laço e então lançou exceção:

```text
Nome do aplicativo com falha: dwm.exe
Nome do módulo com falha: KERNELBASE.dll
Deslocamento da falha: 0x000000000012d862
```

Dois dumps do DWM foram capturados (`dwm-crash-01-dwm.exe.8548.dmp`,
`dwm-crash-02-dwm.exe.3920.dmp`, ~178 MB cada).

### Cadeia causal completa, toda medida

```text
bind OK -> Codigo 0 -> VidPn convergiu (18 consultas) -> primaria comitada em 0xAB0000
   -> 12 Present, 10 por SDMA, tripe de engine OK (RPTR==WPTR, fence sobre poison)
   -> MAS a primaria estava em branco (NonZero=0/256 em 63 amostras)
   -> porque bc250umd.dll devolveu E_NOTIMPL 770 vezes ao dwm.exe
   -> dwm.exe lancou em KERNELBASE e caiu (2 dumps)
   -> sem DWM nao ha Present -> tela preta
   -> adaptador degradou para Codigo 43 depois da coleta
   -> ~50 s depois a imagem voltou (BasicDisplay/GOP)
   -> watchdog reiniciou a maquina e devolveu BasicDisplay Codigo 0
```

`device-after.json`, coletado **antes** da degradação, registra `ConfigManagerErrorCode: 0`
e o serviço em `RUNNING`. O Código 43 foi observado depois, na inspeção pós-falha.

## 3. Dois defeitos de honestidade encontrados na própria instrumentação

**`BYTE_PROOF PASS` certifica preto contra preto.** A comparação passa quando origem e
destino são ambos zero, ou seja, ela declara sucesso exatamente no caso em que a tela está
preta. Isso é rótulo de falso sucesso e precisa **falhar fechado** quando
`NonZero == 0` na amostra pós-blt.

**O tripé do blt tem duas pernas, não três.** `Bc250SdmaPresentBlt` verifica
`RPTR == WPTR` e a fence física sobre poison (`bc250_sdma_present.c:567-588`). A terceira
perna da Lei 2 — conferência byte a byte contra gabarito da CPU — **não é feita** para o
frame de 11 MB; o `Bytes=11059200` da linha é o tamanho, não um resultado de comparação. O
`BYTE_PROOF` separado é que amostra o destino, e é ele que está zerado.

## 4. Consequência para o plano

A barreira do desktop visível **deixou de ser a P5 e passou a ser a UMD**. O caminho de
display — VidPn, primária, `SetVidPnSourceAddress`, blt por SDMA no scanout — executou. O que
falta é **alguém desenhar** na primária, e isso exige backend D3D real na `bc250umd.dll`,
que hoje é fail-closed por decisão.

⚠️ **Crédito de precedência:** isto **não é achado novo desta corrida.**
[`docs/169_DWM_D3D11_ROOT_CAUSE_AND_NATIVE_WINSYS.md`](../../169_DWM_D3D11_ROOT_CAUSE_AND_NATIVE_WINSYS.md)
já estabeleceu a causa-raiz em 06/09 a partir do **dump real** da corrida de 05/09 22:20:
fail-fast `0xe0464645` com HRESULT **`0x887A0004` (`DXGI_ERROR_UNSUPPORTED`)** em
`dwmcore!CD3DDevice::CreateD3D11Device`. Esta corrida **reproduz** aquela causa com o
candidato novo e acrescenta a contagem (`770 × E_NOTIMPL`, 7 cargas da DLL, 2 dumps) e a
prova de que as correções da faixa A não quebraram a convergência.

O que isso significa para a tabela "Encerrado" da `BASE_DE_AVANCO.md`: a linha que diz
"teoria «o `E_NOTIMPL` da UMD apaga a tela» — REFUTADA POR MEDIÇÃO (03/09), a DLL nunca foi
carregada" **está superada**. A refutação valia porque a DLL não era carregada; ela passou a
ser carregada, e o dump nomeia a chamada exata que falha.

## 4b. O dump do DWM, aberto (06/09 23:0x) — decide a arquitetura

`cdb` sobre `dwm-crash-01-dwm.exe.8548.dmp`. Bucket:
`APPLICATION_FAULT_FAIL_FAST_EXCEPTION_887a0004_dwmcore.dll!MilFailFastForHR`.

```text
KERNELBASE!RaiseFailFastException+0x152          <- e0464645, Parameter[0] = 0x887A0004
dwmcore!MilFailFastForHR+0x69
dwmcore!ModuleFailFastForHRESULT+0x9
dwmcore!MilInstrumentationHandleFailure_MaybeFailFast+0x69
dwmcore!MilInstrumentationCheckHR_MaybeFailFast+0x5f
dwmcore!CD3DDevice::CreateD3D11Device+0xcd3d4    <- HRESULT 0x887A0004 DXGI_ERROR_UNSUPPORTED
dwmcore!CD3DDevice::Create+0x3b
dwmcore!CDeviceManager::CreateNewDevice+0x48
dwmcore!CDeviceManager::GetDevice+0x78
dwmcore!CLegacyRenderTarget::EnsureSwapChain+0x195
dwmcore!CLegacyRenderTarget::CheckOcclusionState+0x31
dwmcore!CComposition::PreRender+0x5f0
dwmcore!CPartitionVerticalBlankScheduler::ProcessFrame+0x55f
dwmcore!CConnection::RunCompositionThread+0x21d
```

Confirma o doc 169 com a pilha completa, e acrescenta **a conclusão que decide o caminho**:

1. O DWM está no caminho **`CLegacyRenderTarget`** e chama `EnsureSwapChain`, que exige um
   device **D3D11**.
2. Ao receber `DXGI_ERROR_UNSUPPORTED`, o DWM **não cai para software** — ele passa por
   `MilInstrumentationCheckHR_MaybeFailFast` e **fail-fasta** (`e0464645`). O processo morre.
3. Portanto **`DriverSupportsCddDwmInterop` não é rota de fuga.** Nós já declaramos esse cap
   em `PresentationCaps` (`0x1006C903`, bit 8 ligado) e o DWM foi para o D3D11 do mesmo jeito.

**Consequência normativa:** para haver Área de Trabalho visível neste driver, um backend
**D3D10/11 real na `bc250umd.dll` é obrigatório**. Não existe caminho por caps, por CDD nem
por interop que evite isso. A hipótese "fazer o DWM aceitar CDD" está **refutada por dump**.

## 4c. Correção de registro: o alvo é Windows 10 22H2, não Windows 11

O dump reporta `Windows 10 Version 19045 MP (12 procs) Free x64`, e a máquina confirma:
`Windows 10 Pro`, `DisplayVersion 22H2`, `CurrentBuild 19045`, `UBR 2965`. A documentação do
projeto diz "Windows 11 x64" em vários lugares — está **errado**. O WDDM máximo desta
plataforma é 2.9, o que é compatível com o contrato 2.0/2.7 que usamos, mas a divergência
precisa sair dos documentos para não induzir decisão de DDI.

## 5. Estado da máquina

Recuperação automática funcionou: o watchdog expirou, fez rollback e reiniciou. Depois do
reboot — `Adaptador de Vídeo Básico da Microsoft`, `ConfigManagerErrorCode: 0`, fusível
`CLEAN (0)`, serviço `bc250mcdm` ausente do SCM e `oem34.inf` fora do DriverStore.

## 6. Artefatos

`bc250_display_log.txt` (149.109 bytes) · 7 logs de `bc250umd.dll` · 2 dumps do DWM ·
`application-events-after.txt` com o Event ID 1000 · `device-after.json` ·
`setupapi-after-tail.txt`.

**Compute Vulkan, submissão Node 1, Área de Trabalho com pixels visíveis e jogos:
NÃO VALIDADOS EM HARDWARE.**
