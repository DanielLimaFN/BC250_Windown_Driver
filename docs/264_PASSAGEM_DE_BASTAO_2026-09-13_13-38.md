# PASSAGEM DE BASTÃO — BC-250 WDDM — 13/09/2026 13:38

## Leia primeiro

Você está assumindo o desenvolvimento experimental do driver Windows para a
AMD BC-250 (`PCI VEN_1002 DEV_13FE`, GC 10.1.3 / Cyan Skillfish 2), no
repositório `C:\Users\Daniel\Desktop\NamixWDDM`.

Leia integralmente, nesta ordem:

1. `AGENTS.md` — regras obrigatórias, segurança e honestidade de prova;
2. `BASE_DE_AVANCO.md` — fonte única de verdade do projeto;
3. este documento;
4. `docs/263_HANDOFF_2026-09-13_WATCHDOG_ABSOLUTO_E_BLT_RETANGULAR.md`;
5. evidência física da última corrida em
   `docs/evidence/2026-09-13_13-38-51-DESKTOP-HQAG6T4-lab-binding/`.

Não recomece o projeto e não descarte o estado atual. O worktree contém muitas
mudanças válidas e evidências ainda não commitadas. Preserve tudo que não for
estritamente relacionado à próxima correção.

## Objetivo final

Jogos no Windows 10 22H2 executando na BC-250, usando as 24 CUs e os 16 GB de
GDDR6, com monitor ligado ao DisplayPort da própria placa, D3D12 e Vulkan, sem
WARP, sem rasterização por CPU e sem fallback de software.

O caminho de display vigente herda o UEFI GOP: VidPn de modo único, scanout em
VRAM real, Present blt por SDMA e VSync lido do frame counter do OTG. Não
implementar DCN no caminho crítico e não escrever registradores de DCN antes da
prioridade P9.

## Estado físico consolidado antes da última corrida

- miniport WDDM chega a Código 0;
- SDMA, GCVM, CP/PM4, MEC/HQD e fences físicas já foram provados no silício;
- RADV Vulkan compute foi validado no Node 1;
- swapchain Vulkan animada a 60 FPS foi validada, 100% GPU;
- Marco 3 sustentado: 600 blts, zero TDR, zero fallback e zero cópia da CPU;
- UMD D3D10/11 parcial existe, mas ainda não há backend nativo completo de
  rasterização/DrawIndexed;
- o objetivo imediato é obter o desktop DWM visível com composição realmente
  chegando à primária apresentada.

## Última corrida física: candidato 3.170.1.0, 13:38:51

O usuário observou tela preta por algum tempo. Isso não foi uma corrida perdida.
Ela validou duas mudanças importantes no silício.

### 1. COPY_LINEAR_SUB_WIND foi validado

Resultado medido no log integral:

- 80 `BltViaKmd ENTER` e 79 `LEAVE` bem-sucedidos; o último ficou em voo quando
  o rollback começou;
- 79 `COMPOSITE_BLT ENTER` e 79 `COMPOSITE_BLT OK RECT 1:1`;
- 79 `COMPOSITE_DATA_PROOF SAMPLED_PASS`;
- todas as provas: `Match=5/5`, com pelo menos uma amostra não zero;
- zero `COMPOSITE_DATA_PROOF FAIL`;
- zero `COMPOSITE_BLT FALHOU`;
- zero `BLT_RECT TIMEOUT` e zero `IB_TRIPOD TIMEOUT`;
- 79 Draws substituídos por cópia 1:1 via SDMA;
- 5.095 IBs indiretos executados;
- 1.468 vereditos completos de tripé;
- zero ResetFromTimeout, RestartFromTimeout e BugCheck.

Foram executadas geometrias com pitches diferentes, entre elas:

- 590x800, pitch 2560 -> 10240;
- 1246x603, pitch 5120 -> 10240;
- 1920x1080, pitch 7680 -> 10240;
- 16x16, pitch 256 -> 10240;
- 2560x1080 contígua, pitch 10240 -> 10240.

Portanto, a hipótese de codificação “contagem menos um” do pacote
`COPY_LINEAR_SUB_WIND` está promovida por medição: o SDMA copiou os cinco
pixels correspondentes de cada operação e retirou a fence. Isso prova cópia
retangular de dados; não prova rasterização 3D.

### 2. O novo watchdog foi validado

O prazo absoluto expirou às 13:41:52. O journal registrou
`ROLLBACK_STARTED` antes da recuperação, com a mensagem de que não houve
pré-consulta WMI. A recuperação terminou online, sem reboot, e confirmou
BasicDisplay Código 0 em cinco amostras consecutivas. Estado final:
`RECOVERY_OK_ONLINE`, `cleanup_ok=true`, `recovery_exit_code=0`.

O registro intermediário `BLOCKED` veio da segunda tarefa concorrente vendo
que a primeira já havia adquirido o direito de recuperar; não é falha do
rollback. O mutex impediu uma segunda recuperação.

O dispositivo ficou em BasicDisplay Código 0 e o pacote experimental foi
removido. A corrida validou em hardware o deadline absoluto e o rollback
online.

## Por que a tela ainda ficou preta

A composição agora gera dados reais, mas esses dados não chegam à superfície
física que o Present lê.

Fatos medidos:

- as 79 cópias terminaram com dados não zero;
- todas publicaram `DstIsScanout=0`;
- em várias operações, o destino foi `DstMc=0xF400D8C000`;
- `SetVidPnSourceAddress` publicou `Segment=1, Addr=0xD8C000`;
- a geometria do segmento é `SegBase=0xA8C000`;
- para endereços de segmento, o endereço cru de VRAM correspondente é
  `0xD8C000 + 0xA8C000 = 0x1818000`;
- o Present resolveu seu slot `SegOff=0xD90000` para
  `VramOff=0x181C000`, aplicando corretamente a base do segmento;
- as amostras de `PRIMARY`, `PRESENT_SLOT` e scanout vistas pelo Present eram
  0/256 não zero;
- mesmo assim, a composição provou pixels em `0xD8C000`, sem a soma
  `0xA8C000`.

### Hipótese principal H1 — ainda não promover a causa raiz

Existe uma divergência de domínio de endereço entre o resolvedor GPUVA usado
pelo escape de composição e o caminho de SegmentId/SegmentOffset usado pelo
Present. A composição parece tratar `SegmentOffset` como offset cru de VRAM e
escrever `0xA8C000` bytes antes do backing físico que o Present considera
correto.

Isso explica simultaneamente:

- composição com prova `5/5` e dados não zero;
- destino nunca identificado como scanout;
- primária e slot apresentados totalmente zerados;
- tela preta apesar de SDMA, fence e dados funcionarem.

Ainda é H porque falta correlacionar numa mesma operação: processo, handle da
alocação, GPUVA, `SegmentId`, `SegmentOffset`, MC resolvido, VramOffset cru e
handle publicado em `SetVidPnSourceAddress`. Não simplesmente some
`SegBase` dentro do escape sem provar de onde vem cada endereço; corrigir duas
vezes produziria corrupção silenciosa.

### Bloqueador independente H2

A UMD ainda recusou 34 `DrawIndexed` com a mensagem de que faltam
compilador/shader-state/draw PM4. A substituição atual cobre `Draw`, não
`DrawIndexed`. Mesmo depois de corrigir o domínio da primária, o desktop pode
continuar incompleto ou o DWM pode reiniciar. O backend D3D11 nativo continua
aberto; não declarar que cópias 1:1 são rasterização.

## Próxima ação recomendada

Não faça outra corrida idêntica. Ela apenas repetirá o mesmo endereço errado.

Primeiro, offline:

1. rastrear `Bc250ActiveEscapeResolveGpuVaToMc`, a publicação do journal GPUVA,
   `Bc250SegmentOffsetToMc`, `SetVidPnSourceAddress` e o resolvedor do Present;
2. adicionar uma linha de telemetria de identidade por composição contendo
   `Pid/ProcessHandle`, GPUVA, allocation handle/token, SegmentId,
   SegmentOffset, SegmentBase, McBeforeBase, McAfterBase e spans;
3. congelar em teste os números medidos:
   `SegBase=0xA8C000`, `PrimarySegOff=0xD8C000`,
   `PrimaryRawVram=0x1818000`, `PresentSegOff=0xD90000`,
   `PresentRawVram=0x181C000`;
4. identificar qual camada deve possuir a conversão. Deve haver uma única
   fonte de verdade e uma única soma da base;
5. manter falha fechada para SegmentId diferente de 1, overflow, faixa fora da
   VRAM ou identidade de processo/alocação divergente;
6. auditar por mutação para impedir regressão ao `SegmentOffset` cru;
7. só então gerar uma versão nova, build completo, UMD, pacote lab assinado e
   auditorias.

Critério da próxima corrida:

- composição deve provar dados não zero no backing físico da primária;
- `DstIsScanout` ou uma prova equivalente deve ligar a composição à alocação
  efetivamente apresentada;
- o Present deve observar `NonZero > 0` na primária/slot correto;
- zero timeout/TDR/BugCheck;
- watchdog deve continuar fechando em `RECOVERY_OK_ONLINE` se não houver
  confirmação;
- desktop visível antes de confirmar é o único critério visual.

Depois de fechar essa diferença de endereço, atacar separadamente os 34
`DrawIndexed` recusados com backend real de shader/IA/raster/RT e PM4. Não
substituir DrawIndexed por cópia sem uma prova geométrica válida.

## Arquivos mais relevantes

- `BASE_DE_AVANCO.md`
- `driver/core/bc250_segment_address_contract.[ch]`
- `driver/core/bc250_gpuva_space_contract.[ch]`
- `driver/core/bc250_gpuva_journal_contract.[ch]`
- `driver/wdk/bc250_active_miniport.c`
- `driver/wdk/bc250_display_ddi.c`
- `driver/wdk/bc250_sdma_present.c`
- `driver/umd/bc250_umd_blt.inl`
- `tools/audit-bc250mcdm-safety.py`
- `tools/watch-bc250mcdm-safe-bind.ps1`
- `docs/263_HANDOFF_2026-09-13_WATCHDOG_ABSOLUTO_E_BLT_RETANGULAR.md`

## Estado do candidato e segurança

O candidato 3.170.1.0 foi instalado e depois removido automaticamente. Seus
hashes estão no docs/263 e no manifesto, mas ele não deve ser reinstalado para
repetir o ensaio. A próxima correção precisa subir o `DriverVer` fonte antes de
empacotar.

O `C:\BC250_RECOVERY.cmd` usado na corrida tinha SHA-256 idêntico ao manifesto.
O watchdog absoluto está validado nesta corrida, mas todo novo bind continua
destrutivo: boot frio, fusível CLEAN, PowerShell elevado, coleta integral e
confirmação somente com display visível.

## Resumo em uma frase

O projeto já executa compute, Vulkan e cópias DWM retangulares com dados reais
na GPU; o bloqueio imediato é fazer o endereço da superfície composta coincidir
com o backing físico que o WDDM apresenta, e depois completar DrawIndexed/D3D11
nativo até chegar ao objetivo de jogos sem fallback.
