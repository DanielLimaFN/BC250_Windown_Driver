# Fase 3 — Diagnóstico em silício real (BC-250, 20/08/2026)

Registro do que foi medido na placa, do que foi corrigido e do que continua bloqueado.
Build de referência: `1.0.0820.1518` (`BuildId 8201518`), device em `CM_PROB_NONE`.

---

## 1. O que foi implementado e está comprovado

Telemetria lida de `HKLM\SYSTEM\CurrentControlSet\Services\bc250v2\Parameters`
após instalação e restart do dispositivo:

| Chave | Valor | Leitura |
|---|---|---|
| `ScreenWidth` / `ScreenHeight` | 2560 / 1080 | scanout do GOP herdado |
| `MonitorSet_ModeCount` | 13 | `RecommendMonitorModes` publicou 13 modos |
| `SourceSet_ModeCount` | 13 | conjunto de origem publicado com 13 modos |
| `TargetSet_ModeCount` | 1 | pruning correto: com origem pinada em 2560x1080, só o nativo é cofuncional |
| `Cofunc_CallCount` | 28 | o dxgkrnl negociou 28 vezes |
| `Cofunc_SrcUnpinnedCount` | 14 | 14 dessas publicaram a lista completa de origem |
| `Commit_Status` | 0 | `CommitVidPn` aceitou |
| `Mode_ActiveW/H`, `Mode_OffsetX/Y` | 2560/1080, 0/0 | geometria ativa coerente |
| `Cofunc_SourceSetStatus` / `TargetSetStatus` | ausentes | nenhuma falha de publicação |

Os 13 modos são exatamente o esperado: 15 entradas da tabela menos 2560x1440 e
1920x1200, que excedem a altura nativa e são filtradas em runtime.

Nenhum crash, TDR ou regressão: `Present` continua funcionando e o desktop segue
estável em 2560x1080.

---

## 2. O bloqueio

Do lado do SO, o Windows não expõe lista de modos utilizável:

```
EnumDisplaySettingsEx(dev, i, flags=0)   ->  0 modos
EnumDisplaySettingsEx(dev, i, EDS_RAWMODE) ->  1 modo: 800x600 32bpp 60Hz
EnumDisplaySettingsEx(dev, ENUM_CURRENT_SETTINGS) -> 2560x1080 32bpp 60Hz
```

O único modo raw (800x600) **não é o modo em execução** e tem cara de fallback
genérico do Windows, não de entrada nossa.

```
ChangeDisplaySettingsEx(CDS_TEST):
  2560x1080 -> DISP_CHANGE_BADMODE      <-- modo ATIVO
  1920x1080 -> DISP_CHANGE_BADMODE
  1600x900  -> DISP_CHANGE_BADMODE
  1280x720  -> DISP_CHANGE_BADMODE
  1024x768  -> DISP_CHANGE_BADMODE
   800x600  -> DISP_CHANGE_BADMODE
```

**O dado decisivo:** `CDS_TEST` falha até no modo atualmente ativo (2560x1080).
Um teste de mode-set no modo ativo não pode falhar por causa da tabela de modos
da Fase 3. Logo o bloqueio é **anterior e independente** da Fase 3 — nunca foi só
"faltam modos na lista".

---

## 3. Hipóteses testadas e descartadas

| Hipótese | Como foi testada | Resultado |
|---|---|---|
| Faltavam modos nas tabelas VidPN | tabela de 15 modos + publicação de origem/destino/monitor | implementado; lista do GDI continua vazia |
| Falta advertir scaling no path | `ScalingSupport.Centered/Stretched = 1`, `DriverScaling = 1` | sem efeito |
| Falta pruning de cofuncionalidade | leitura dos pins antes de publicar; filtro `SourceFitsTarget`/`TargetFitsSource` | corrigido (`TargetSet=1` quando origem pinada); sem efeito no sintoma |
| Timings com blanking zero (`TotalSize == ActiveSize`) | modelo CVT-RB: +160 px H, +30 linhas V | corrigido; sem efeito no sintoma |
| Versão de DDI inválida | `0x300E` = `DXGKDDI_INTERFACE_VERSION_WIN8` | válido para KMDOD; descartado |
| Monitor filho ausente | `EnumDisplayDevices` no adaptador | existe: `Default_Monitor`, flags 0x3 (ativo+anexado); descartado |
| EDID sobrepondo nossos modos | `QueryDeviceDescriptor` retorna `STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA` | sem EDID, nossos modos são a fonte; descartado |
| `RecommendFunctionalVidPn` errado | retorna `STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN` | igual ao sample da MS; descartado |
| Harness de teste cego | `WinSta0\Default`, SessionId 1, `UserInteractive=True` | ambiente OK; descartado |

As duas correções (pruning e blanking) são corretas em si e ficam no código,
independentemente de não terem movido o sintoma.

---

## 4. Próximo passo decisivo

Publicar **um único modo não-nativo** (ex.: só 1920x1080 como origem + destino +
monitor, sem o nativo) e observar o resultado:

- **Desktop sobe em 1920x1080** → mode-set funciona; o defeito está na construção
  da lista de modos e a Fase 3 é recuperável no caminho atual.
- **Desktop fica em 2560x1080 ou falha** → o dxgkrnl está prendendo o adaptador ao
  modo POST/GOP. Esse é o comportamento conhecido de drivers display-only sobre
  framebuffer de firmware (o Adaptador de Vídeo Básico da Microsoft também oferece
  apenas a resolução de boot). Nesse caso a Fase 3 exige programar o CRTC do DCN —
  bring-up real do display engine, muito maior do que o roadmap assume, e
  provavelmente deveria vir depois da Fase 4.

O experimento pode deixar a máquina sem vídeo. Recuperação: o pacote anterior
(`oem5.inf`) continua na driver store, e resta o modo de segurança.

---

## 5. Observação secundária

O log de eventos registra, em toda tentativa de start, `Kernel-PnP` id 411 com
`Status do Problema: 0xC00000E5` (`STATUS_INTERNAL_ERROR`) — inclusive para o
`display.inf`/`BasicDisplay` da Microsoft no mesmo dispositivo. O device termina em
`CM_PROB_NONE`, então parece ser a primeira tentativa de start antes do filho de
monitor existir. Não é o bloqueio, mas merece investigação própria.

---

## 6. Resultado positivo importante: o CCD tem os nossos dados

Testei inicialmente apenas a API **legada** (`EnumDisplaySettings` / `ChangeDisplaySettingsEx`).
Isso foi um furo: as Configurações de Vídeo do Windows usam **CCD**
(`QueryDisplayConfig` / `SetDisplayConfig`), um caminho diferente.

`QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)` no build `1.0.0820.1529`:

```
PATH[0]  srcId=0 tgtId=0  outputTech=OTHER  rotation=IDENTITY
         scaling=IDENTITY  refresh=60/1  targetAvailable=1
MODE[0]  TARGET  active=2560x1080  total=2720x1110
                 pixelRate=181152000  hSync=66600/1  vSync=60/1
MODE[1]  SOURCE  2560x1080  fmt=32BPP  pos=(0,0)
```

`total=2720x1110` é exatamente o modelo de blanking introduzido na seção 3
(2560+160 na horizontal, 1080+30 na vertical), e
`2720 × 1110 × 60 = 181.152.000` fecha com o `pixelRate` reportado.

**Conclusão:** a cadeia driver → dxgkrnl → CCD funciona e transporta os dados que o
driver publica. O defeito está restrito à construção da lista de modos do GDI e ao
shim legado do `ChangeDisplaySettings`, não à publicação VidPN do driver.

Corolário: o `Scaling=4` observado na seção 5 era configuração em cache de builds
antigos; o path ativo usa `IDENTITY`. O suporte a `AspectRatioCenteredMax` foi
adicionado de todo modo (é correto e é o comportamento desejável para resolução
não-nativa em ultrawide), mas não era a causa.

## 7. Correções aplicadas até aqui (todas mantidas)

1. Pruning de cofuncionalidade: ler os pins de origem/destino antes de publicar e
   filtrar cada conjunto pelo que é utilizável junto com o outro.
2. Blanking real nos timings sintéticos (CVT-RB), em vez de `TotalSize == ActiveSize`.
3. `ScalingSupport.AspectRatioCenteredMax` advertido, aceito no `CommitVidPn` e no
   `UpdateActiveVidPnPresentPath`, e implementado no Present preservando proporção
   (1280x720 num scanout 2560x1080 vira 1920x1080 centralizado, com barras laterais).
4. Contadores acumulativos de telemetria (`Cofunc_CallCount`,
   `Cofunc_SrcUnpinnedCount`) para enxergar a sequência de negociação.

Nenhuma delas moveu o sintoma da API legada, mas todas são corretas em si.

---

## 8. EDID sintetico (implementado no fonte, NAO compilado)

### Por que

O filho de video nao expunha descritor algum: `DxgkDdiQueryDeviceDescriptor`
retornava `STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA` incondicionalmente. O
resultado, observado no registro:

```
HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\Default_Monitor\...\Device Parameters
    BAD_EDID = [0 bytes]
DeviceDesc = Generic Non-PnP Monitor
HardwareID = MONITOR\Default_Monitor
```

E `Get-CimInstance -Namespace root\wmi WmiMonitorListedSupportedSourceModes`
responde "Sem suporte" — nao ha mode set de monitor a consultar.

Sem EDID o `monitor.sys` usa um mode set default minimo, e e esse conjunto que
estrangula a lista final, **independentemente de quantos modos o driver publique
no VidPN**. Isso explica o que nenhuma das quatro correcoes anteriores explicava:
por que ate o modo nativo em uso responde `BADMODE`.

### O que foi implementado

- `BC250_EDID_BLOCK_SIZE` e os campos `Edid[128]` / `EdidValid` na extensao.
- `Bc250BuildEdid()`, chamado em `StartDevice` depois de `ScreenWidth/Height`
  serem conhecidos, para o descritor detalhado descrever o scanout real.
- `Bc250DdiQueryDeviceDescriptor()` serve o bloco em fatias, respeitando
  `DescriptorOffset` / `DescriptorLength`, e sinaliza fim ao passar de 128.
- Blanking ajustado para `vTotal = h + 45` (era `+30`).

### Conteudo do EDID 1.3

| Origem | Modos |
|---|---|
| Timing detalhado (preferido) | 2560x1080 @60 — o scanout nativo |
| Standard timings (8) | 1920x1080, 1680x1050, 1600x900, 1440x900, 1152x864, 1280x1024, 1280x800, 1280x720 |
| Timings estabelecidos | 1024x768, 800x600, 640x480 |
| Limites de faixa (0xFD) | 50–61 Hz vertical, 20–90 kHz horizontal, clock max 190 MHz, GTF default |
| Nome (0xFC) | "BC-250 GOP" |

Total: 12 modos — exatamente os 12 que `g_Bc250ModeTable` produz apos o filtro de
runtime num scanout 2560x1080. O mode set do VidPN e o EDID descrevem o mesmo
conjunto.

### Validacao aritmetica (feita sem compilar)

Os bytes foram reproduzidos e feito o parse de volta:

```
checksum                -> soma total mod 256 = 0            OK
descritor detalhado     -> 2560x1080, 183.600.000 Hz
                           refresh reconstruido = 60.0000 Hz  OK
                           hSync = 67,5 kHz (faixa 20-90)     OK
                           clock = 183,6 MHz (max 190)        OK
porches                 -> H 48/32/80 = 160 | V 3/5/37 = 45   OK
8 standard timings      -> todos reconstroem exato            OK
```

`vTotal = h + 45` nao e arbitrario: poe o clock nativo em
2720 x 1125 x 60 = 183.600.000 Hz, multiplo exato de 10 kHz e portanto
representavel no descritor detalhado. E confere com `Bc250FillSignalInfo`, que
anuncia `HSyncFreq = 60 x 1125 = 67500` e `PixelRate = 183.600.000` — o timing do
EDID e o do VidPN sao o mesmo.

### Dois erros corrigidos durante a validacao

1. `1366x768` e `1360x768` foram **removidos** da tabela de modos. Um standard
   timing codifica apenas a largura e a razao de aspecto: 1360 em 16:9 reconstroi
   765, nao 768; e 1366 nao e multiplo de 8, nem sequer e codificavel. Em seu
   lugar entrou `1152x864`, que em 4:3 reconstroi exato.
2. Um `pixelClock` aparentemente errado no parse era bug do script de validacao,
   nao do driver: em PowerShell `-shl` sobre `[byte]` trunca em 8 bits
   (`[byte]0x47 -shl 8` da 0). Em C o `e[i] << 8` promove para `int`.

### Estado

Implementado apenas no **fonte**. Nao compilado e nao instalado, por decisao
tomada apos constatar edicao concorrente do `wddm_driver.c` / `wddm_hw.c` /
`wddm.h` com bring-up de **SDMA0 (Fase 4)** em andamento. Compilar agora
empacotaria bring-up de SDMA0 pela metade junto com a Fase 3, e um problema em
silicio real ficaria impossivel de atribuir a uma das duas.

---

## 9. EDID compilado e instalado: funciona, mas nao era o bloqueio

Builds `1.0.0820.1544` e `1.0.0820.1548`, device em `CM_PROB_NONE`.

### O EDID e aceito pelo Windows (comprovado)

| Evidencia | Antes | Depois |
|---|---|---|
| Identidade do monitor | `Generic Non-PnP Monitor` / `MONITOR\Default_Monitor` | `Generic PnP Monitor` / `MONITOR\BCX13FE` |
| `Device Parameters` | `BAD_EDID = [0 bytes]` | `EDID = [128 bytes] 00 FF FF FF FF FF FF 00 08 78 FE 13 ...` |
| `WmiMonitorListedSupportedSourceModes` | "Sem suporte" | **12 modos**, preferido no indice 3 = 2560x1080 |
| `WmiMonitorBasicDisplayParams` | — | 67 x 28 cm, gamma 120 |

`BCX13FE` e o nosso EDID: fabricante "BCX" + product code 0x13FE. Telemetria do
driver: `Edid_Valid=1`, `Edid_ServedBytes=128` (o dxgkrnl leu o bloco inteiro),
`Edid_Clock10kHz=18360`, `Edid_Checksum=25` (0x19) — todos exatamente como a
validacao aritmetica previu.

Os 12 modos que o monitor reporta sao exatamente os 12 desenhados. **A ausencia de
EDID era um defeito real e esta corrigida.**

### Mas o sintoma nao mudou

```
modos com filtro de monitor : 0
modos raw (EDS_RAWMODE)     : 1  -> 800x600 60Hz
CDS_TEST (todas)            : DISP_CHANGE_BADMODE
Configuracoes do Windows    : so 2560x1080
```

## 10. Destino unico nativo: tambem nao mudou

Hipotese: um modo de destino so e utilizavel se o seu `VideoSignalInfo` casar com
um modo do monitor. Os modos que o `monitor.sys` deriva dos standard timings do
EDID usam blanking GTF/CVT — 1280x720 vira hTotal 1664 / vTotal 746 / ~74,5 MHz —
que nunca casa com o nosso blanking sintetico +160/+45 (hTotal 1440 / vTotal 765 /
66,096 MHz). Somente o nativo casa, porque para ele publicamos um descritor
DETALHADO no EDID com exatamente os nossos numeros.

Correcao aplicada em `1548`: `Bc250PublishTargetModeSet` passou a publicar
**apenas o timing nativo**, revertendo a decisao anterior de emparelhar um destino
por resolucao. Isso tambem e a descricao honesta do hardware — o CRTC nunca muda
de timing — e as outras resolucoes passam a existir so como modos de ORIGEM,
acomodadas por Centered / Stretched / AspectRatioCenteredMax no Present.

Resultado: **nenhuma mudanca no sintoma.**

## 11. O fato invariante, e o que ele aponta

Em **seis builds** a lista raw do GDI teve sempre exatamente uma entrada:
`800x600 @ 60Hz`. Ela nao responde a nada que o driver publique. Nao e um modo
derivado do driver — e o valor de fallback do win32k para um adaptador cuja lista
de modos nunca foi construida.

E o nome do adaptador GDI subiu a cada instalacao:

```
DISPLAY15 -> DISPLAY19 -> DISPLAY23 -> DISPLAY27 -> DISPLAY36 -> DISPLAY40
```

Estado da maquina no momento desta anotacao:

```
LastBootUpTime      08/20/2026 14:48:53   (uptime ~1 hora)
pacotes instalados  1503, 1515, 1518, 1529, 1535, 1537, 1544, 1548   (8, sem reboot)
monitores phantom   8
reboots             0
```

**Nunca reiniciamos a maquina.** Todo o teste foi feito com
`pnputil /restart-device`, que reinicia o dispositivo mas nao faz o win32k
reconstruir a lista de modos do GDI — ela e montada na inicializacao da sessao
grafica. Isso e consistente com a invariancia do 800x600 ao longo de todos os
builds, e com o fato de que o desktop continua em 2560x1080 (modo herdado do POST
no boot) enquanto a lista de modos permanece vazia.

**Proximo passo: reiniciar a maquina e repetir a medicao.** E o passo mais barato
e o de maior probabilidade de importar, e deveria ter vindo muito antes das seis
iteracoes de codigo.
