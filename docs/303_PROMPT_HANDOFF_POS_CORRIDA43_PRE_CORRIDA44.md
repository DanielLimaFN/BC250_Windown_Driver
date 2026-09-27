# PROMPT DE HANDOFF — BC-250 WDDM — pós-Corrida 43, antes da Corrida 44

> **Data do handoff:** 20/09/2026  
> **Workspace:** `C:\Users\Daniel\Desktop\NamixWDDM`  
> **Hardware:** AMD BC-250 `1002:13FE`, GC 10.1.3 / Cyan Skillfish 2, Windows 10 22H2 x64  
> **Próximo candidato:** lab `3.202.1.0`, `DISPLAY_RENDER_LAB`, WDDM 2.7, schema 135  
> **Estado do candidato:** compilado, assinado e auditado; **NÃO INSTALADO e NÃO VALIDADO EM HARDWARE**  
> **Próximo ensaio:** Corrida 44 — provar o restart do CP_RB0 com BASE corrigida  
> **Fonte de verdade:** `BASE_DE_AVANCO.md`; critério VIGENTE em `docs/302A_EMENDA_CRITERIO_CORRIDA44.md` (emenda escrita antes da corrida, que supera o §11 deste doc e as seções de veredito do `docs/302`)

## 0. Instrução à próxima IA

Continue o desenvolvimento deste repositório sem recomeçar o diagnóstico. Leia integralmente:

1. `AGENTS.md`;
2. o topo atualizado de `BASE_DE_AVANCO.md`;
3. `docs/299_HANDOFF_2026-09-20_CORRIDA42_UPLOAD_OK_TDR_0X116.md`;
4. `docs/300_CRITERIO_CORRIDA43_TDR_GFX_EXECUTAVEL.md`;
5. `docs/301_HANDOFF_2026-09-20_CORRIDA43_RESTART_GFX_FALHA_FECHADO_BASE_ERRADA.md`;
6. `docs/302_CRITERIO_CORRIDA44_BASE_CP_RB0_CORRIGIDA.md`;
7. `docs/302A_EMENDA_CRITERIO_CORRIDA44.md` — **critério vigente**, emendado antes da corrida;
8. este handoff.

Não reverta o worktree, não use `git reset --hard`/`checkout --`, não trate BasicDisplay como
sucesso do driver e não altere o critério depois de ver o resultado da próxima corrida.

## 1. Objetivo final e arquitetura vigente

O objetivo continua sendo jogos e aplicações Windows não modificadas na BC-250, com monitor
no DisplayPort da própria placa, D3D11/D3D12/Vulkan, sem WARP ou fallback de software.

Display v1 usa herança do UEFI GOP: VidPn de modo único, scanout em VRAM real, Present blt por
SDMA e VSync lido do frame counter do OTG. Escrita em DCN permanece proibida antes de P9.

O produto final exige desktop acelerado, frame/pixel correto e jogos. Código 0, Exit 0,
readback de registrador, demo modificada ou BasicDisplay não fecham a entrega.

## 2. Estado físico atual — não presumir máquina limpa

Última inspeção local deste handoff:

- BC-250: `CM_PROB_FAILED_POST_START`;
- serviço `bc250mcdm`: `Stopped`, `Manual`;
- fusível `C:\BC250_BOOT_FUSE.dat`: `TRIPPED`, `Boot Count=4`,
  `LastStatus=0xC0000001`, checksum válido;
- candidato 3.202.1.0 ainda não instalado;
- Corrida 44 ainda não foi executada;
- o `MEMORY.DMP` existente é o da Corrida 43 enquanto outro BugCheck não o sobrescrever.

Antes de qualquer bind é obrigatório recuperar BasicDisplay, limpar/resetar o fusível e fazer
boot frio. Não tente apenas habilitar o dispositivo por Device Manager.

## 3. Corrida 42 — por que o trabalho mudou para TDR

Corrida 42: `2026-09-20_00-03-31-DESKTOP-HQAG6T4-lab-binding`, candidato 3.200.1.0.

Resultados essenciais:

- upload Vulkan com row pitch 2560x1080 executou corretamente;
- primeiro IB GFX parou em `RPTR=24/WPTR=32`;
- 6 submits GFX aceitos, zero aposentados;
- o restart antigo aceitava somente readbacks e declarava o CP_RB0 recuperado;
- pendências cresceram e terminou em `VIDEO_TDR_FAILURE 0x116`;
- dump atribuiu responsabilidade a `bc250mcdm.sys`, mas o PDB daquele candidato não existia.

Isso motivou no 3.201:

- prova pós-reset executável `NOP x4 + RELEASE_MEM`;
- fence sobre poison;
- exigência de `RPTR=WPTR=12`;
- abort/reconciliação de fences WDDM sem notificação falsa;
- limpeza do RPTR writeback;
- boot preamble fail-closed;
- linguagem honesta para MEC: readback, não prova executável.

## 4. Corrida 43 — resultado físico consolidado

Corrida 43: `2026-09-20_10-05-31-DESKTOP-HQAG6T4-lab-binding`, candidato 3.201.1.0.

### Houve avanço?

Sim, avanço diagnóstico e de honestidade; não houve avanço funcional. Veredito global:
**FALHA**.

### Sucessos medidos

- tripé de boot SDMA: `WPTR=RPTR=68`, fence `0x1001`, dados `4/4`;
- boot preamble GFX: `WPTR=RPTR=20`, fence `1`, `DrenoOk=1`;
- CP_RB0 online inicialmente em `RingMc=0xF40FF70000`;
- um `CommitVidPn`, três `SetVidPnSourceAddress`, VSync até pelo menos amostra 540;
- 78 submissões `IB EXECUTADO (INDIRECT)` aceitas no Node 0 e 70 vereditos assíncronos
  `TRIPE_PROVADO` registrados antes do crash;
- no TDR, SDMA reiniciou com `SYNC_NOP TRIPOD PASS`, `RPTR=WPTR=40`, fence `0x1053`;
- `[TDR_FENCE_RECONCILE] PASS`: 1 committed abortada, zero reservas, zero conclusão falsa;
- fusível impediu boot loop.

### Falhas medidas

- primeiro submit Node 1 GFX: 664 dwords, VMID 7, `DmaVA=0x244FE4000`;
- voltou a parar em `RPTR_Reg=24`, `RPTR_Wb=20`, `WPTR=32`;
- `TDR_GFX_PROBE Responsive=0`;
- quiesce: `GRBM_STATUS=0xA0403028`, GUI/CP busy, sem onda visível e sem fault GFXHUB;
- prova de restart: `RPTR=0`, `WPTR=12`, fence ainda `0x43DAFFFE`, 2000 polls;
- callback devolveu `STATUS_DEVICE_NOT_READY`;
- Windows gerou `VIDEO_TDR_FAILURE 0x116` e reboot.

### Veredito correto

O contrato 3.201 fez o que prometia: recusou o falso sucesso. Contudo, falha segura do callback
continua sendo falha da recuperação; o dxgkrnl produziu o BugCheck porque o adaptador não foi
recuperado.

## 5. Causa fechada da falha pós-reset

O caminho ONLINE correto escrevia:

```text
CP_RB0_BASE = RingGpuVa >> 8
```

O restart duplicava a codificação e escrevia a GPUVA bruta:

```c
BASE_LO = (uint32_t)(RingGpuVa & 0xFFFFFFFF);
BASE_HI = (uint32_t)((RingGpuVa >> 32) & 0xFFFF);
```

Para `RingGpuVa=0xF40FF70000`:

- correto: `BASE_HI:BASE_LO = 00000000:F40FF700`;
- escrito pelo 3.201: `000000F4:0FF70000`;
- base decodificada: 256 vezes maior.

Isso explica diretamente a prova pós-reset parada em `RPTR=0`: o CP não conseguiu buscar nem
o primeiro dword do pacote privado. A Corrida 42 não tinha prova executável e mascarava o erro
com readback estrutural.

Essa causa explica a falha do **restart**. Ela não explica o stall inicial do IB real em
`RPTR=24/WPTR=32`; essa dívida permanece separada.

## 6. Dump da Corrida 43 — atribuição final

Relatório CDB:

- `memory-dmp-private-analysis.txt`;
- SHA-256 `7B629120F6CC1220524FF17C2DBC394C4E5584EF498513A9775FC933A5805C19`;
- BugCheck `0x116`;
- Arg2 `fffff8028a0cfb80` = `bc250mcdm+0x1FB80`;
- Arg3 `0xC00000A3` = `STATUS_DEVICE_NOT_READY`;
- pilha: `TdrBugcheckOnTimeout -> ADAPTER_RENDER::Reset -> DXGADAPTER::Reset ->
  TdrResetFromTimeout`;
- sem bucket de AV, IRQL, verifier ou corrupção.

O PDB inicialmente arquivado estava corrompido: hash real `CCDA116B...E959F5`, contra
`8376AA54...4542` declarado. O `symchk` o recusou. O SYS instalado exato foi recuperado com
hash `A76A3DA6...E514`.

Foi reconstruído offline um par SYS/PDB do estado 3.201. O hash global difere por metadados
CodeView, mas as seções `.text` e `.pdata` são byte a byte idênticas ao SYS instalado:

- `.text`: `8E6C71C96D8D620086B65E31C0F0277078A5FB5B6185E719C04E6D623DDEB1EF`;
- `.pdata`: `4D8BB8EB8C344FD5DD76295568A9E9B360F5A9396D6FBCEB5E425303D4226B24`.

Com esse layout idêntico, o `dbh` resolve RVA `0x1FB80` exatamente como
`Bc250McdmRestartFromTimeout`. O Arg2 é a owner tag da DDI; não é um access violation dentro
dela. Relatório completo:
`docs/evidence/2026-09-20_10-05-31-DESKTOP-HQAG6T4-lab-binding/memory-dmp-symbol-attribution.txt`.

## 7. Coleta quebrada na Corrida 43 e correção

O reboot interrompeu o binder. Três arquivos ficaram pré-alocados só com zeros:

- `display-log-this-bind.txt`;
- `prebind-policy.json`;
- `safe-bind-watchdog-config.json`.

A cauda foi recuperada do log cumulativo:

- arquivo: `bc250_display_log-tail-recovered.txt`;
- 173.347 bytes;
- SHA-256 `36B67CA1552BCECDAE304549BA1C68D993F61259D9DEC931536B68BBF5FB0536`.

O binder anterior copiava apenas o PDB e registrava o hash da origem, sem reabrir o destino.
Agora `tools/bind-bc250mcdm-lab.ps1`:

- copia `bc250mcdm.sys` e `bc250mcdm.pdb` antes do bind;
- recalcula SHA-256 dos dois arquivos de destino;
- compara com o manifesto;
- falha fechado antes de tocar no dispositivo se qualquer hash divergir.

A auditoria estática exige essas quatro propriedades.

## 8. Correção offline pós-Corrida 43

### Fonte única para BASE

Foram criados:

- `Bc250GfxRingEncodeBaseLo(uint64_t)`;
- `Bc250GfxRingEncodeBaseHi(uint64_t)`.

Eles vivem em `driver/core/bc250_gfx_ring_contract.*` e são usados pelo caminho ONLINE e pelo
restart. O teste usa `0x000123456789AB00` para conferir simultaneamente LO e HI. A auditoria
proíbe a expressão antiga com GPUVA bruta no restart.

### Telemetria decisiva da Corrida 44

O restart agora lê BASE de volta e emite:

```text
[TDR_GFX_RESTART_BASE] RingGpuVa=... Esperado=HI:LO Readback=HI:LO Pass=0|1
```

Readback divergente falha fechado antes da prova executável. Assim, uma nova falha separará
base não retida de outra dependência pós-reset.

### Contrato de restart preservado

Continuam obrigatórios:

- abortar fences pendentes sem `DMA_COMPLETED` falso;
- liberar pins GPUVA abortados;
- zerar RPTR writeback e WPTR shadow;
- executar `NOP x4 + RELEASE_MEM`;
- exigir `RPTR=WPTR=12` e poison substituído;
- manter admissão fechada se qualquer passo falhar.

## 9. Identidade congelada do candidato 3.202.1.0

Manifesto: `driver/wdk/package/output_lab.manifest.json`.
Pacote instalável: `driver/wdk/package/output_lab/`.

- DriverVer: `09/20/2026,3.202.1.0`;
- perfil: `DISPLAY_RENDER_LAB`;
- contrato: WDDM 2.7;
- schema: 135;
- SYS fonte: `1007DACB3B70C59DA3A14B7C420FD57C925769F1B4EF1C7EDCB9BF35F1040894`;
- SYS assinado: `33E8FF29868D643D409D6256BD8120E061883B9B4BE8D3A74652FBB4D3ABCCFF`;
- PDB: `EF6EFD68063F2344498ACD9753D6C2E7B6118E70446C5FAC2D94BD2DDF82575F`;
- UMD assinada: `934DBD84E04F10024F6B7CAB28FBE014CAA41D35E103600F160BBF395AB5236A`;
- INF: `B2FE0DE42C7E365FFF32DEC11ABA540AEEBB7C5B4CB0E21F7DA41AF13C66C998`;
- CAT: `70FB1638E35B2E6FC1EB53937D7C509229FFC41D2B43B0AB383644CEC4BC7A8F`.

Validação offline concluída:

- `driver/wdk/build-contract.bat`: PASS;
- `tools/audit-bc250mcdm-safety.py`: PASS;
- `tools/audit-bc250mcdm-lab-package.ps1`: PASS;
- package signability: zero erros, zero warnings;
- `git diff --check`: sem erros; apenas avisos LF/CRLF.

Nada disso prova o efeito no silício.

## 10. Próxima ação física — sequência exata

### 10.1 Recuperar em terminal elevado

```powershell
Set-Location C:\Users\Daniel\Desktop\NamixWDDM
& 'C:\BC250_RECOVERY.cmd'
powershell -ExecutionPolicy Bypass -File .\tools\inspect-boot-fuse.ps1 -Reset
```

Exigir que a recuperação confirme BasicDisplay Código 0. Se houver erro, parar e analisar
`C:\BC250_RECOVERY.log`; não instalar o candidato.

### 10.2 Boot frio

```powershell
shutdown /s /t 0
```

Depois de a máquina desligar completamente, ligar novamente.

### 10.3 Corrida 44

```powershell
Set-Location C:\Users\Daniel\Desktop\NamixWDDM
.\tools\EXECUTAR_D3D11_NATIVO.cmd -CaptureDwmDump
```

Esse ensaio pode produzir tela preta, TDR, BugCheck e reboot. Não executar outra experiência
em paralelo.

## 11. Critério congelado da Corrida 44

> ⚠️ **SUPERADO EM 20/09/2026, ANTES DA CORRIDA 44,** por
> [`docs/302A_EMENDA_CRITERIO_CORRIDA44.md`](302A_EMENDA_CRITERIO_CORRIDA44.md), que é o
> **critério vigente**. Esta seção fica como registro.
>
> Três defeitos motivaram a emenda: (a) esta lista tinha 7 itens e a do `docs/302`, também
> declarada congelada, tinha 6 — dois critérios discordando em disco; (b) faltava o desfecho
> `E — algo fora do mapa` exigido pela Lei 10, regra 2; (c) os itens 6 e 7 dependem do stall
> inicial em `RPTR=24/WPTR=32`, que o §5 deste mesmo documento declara dívida **separada** —
> com eles no sucesso obrigatório, H44 podia ser confirmada por inteiro e a corrida ainda ser
> graduada FALHA, atribuindo à correção de BASE uma falha que não é dela.
>
> No 302A os itens 1–5 formam o **sucesso da faixa**, e os antigos 6 e 7 viram **observação
> declarada como dependente de dívida aberta** — relatada sempre, mas sem rebaixar o veredito.

Hipótese H44: a falha `RPTR=0` da prova pós-reset foi causada pela BASE 256 vezes errada.

### Sucesso obrigatório

Todos precisam ocorrer:

1. `[TDR_GFX_RESTART_BASE] ... Pass=1`, com esperado igual ao readback;
2. `[TDR_FENCE_RECONCILE] PASS` e `NotificadasComoConcluidas=0`;
3. `[TDR_GFX_RESTART_PROOF] PASS`, `RPTR=WPTR=12`, poison substituído;
4. `[TDR_GFX_RESTART] CP_RB0 rearmado e comprovado por execucao fisica`;
5. callback de restart retorna sucesso;
6. uma submissão GFX posterior é aceita **e aposentada** com fence real;
7. nenhum segundo TDR equivalente e nenhum BugCheck.

### Falha informativa

- `BASE Pass=0`: codificação escrita não foi retida ou o domínio de leitura está errado;
- `BASE Pass=1`, `RPTR=0`: H44 fecha apenas a BASE, mas existe outra inicialização faltante;
- `RPTR` parcial: BASE funcionou, outro estado/pacote bloqueia o CP;
- prova passa, submit seguinte trava: restart privado funciona, restauração de contexto real
  continua incompleta.

Não ampliar o soft-reset por tentativa. Preservar dump, cauda KMD e readbacks antes de editar.

### Falha de contrato

- declarar sucesso sem `TDR_GFX_RESTART_PROOF PASS`;
- concluir fence abortada;
- reabrir admissão depois de prova falha;
- perder a cauda do KMD;
- chamar BasicDisplay ou reboot de recuperação do nosso adaptador.

## 12. Como analisar a próxima corrida

Se houver reboot antes do arquivamento final:

```powershell
Set-Location C:\Users\Daniel\Desktop\NamixWDDM
.\tools\recover-bc250mcdm-lost-tail.ps1 -MaxFolders 1
```

Na pasta de evidência mais nova, procurar:

```powershell
rg -n "TDR_GFX_RESTART_BASE|TDR_FENCE_RECONCILE|TDR_GFX_RESTART_PROOF|TDR_GFX_RESTART|McdmRestartFromTimeout|GFX_TRIPE_VEREDITO|TDR_GFX_PROBE|BugCheck" .\docs\evidence\<PASTA>\*
```

Também coletar:

- Eventos System 41, 1001, 6008 e 4101;
- estado PnP da BC-250;
- estado do serviço;
- fusível;
- tamanho/timestamp do `C:\Windows\MEMORY.DMP`;
- dumps do DWM, se existirem;
- hashes do SYS/PDB arquivados e resultado do `symchk`.

Após a análise física, o relato obrigatório ao Daniel contém: houve avanço, sucessos, falhas,
veredito, recuperação e pendências. Código 0/Exit 0 isolados não fecham o teste.

## 13. Dívidas abertas — não declarar resolvidas

- causa do stall inicial do IB GFX em `RPTR=24/WPTR=32`;
- submissão GFX real posterior ao restart;
- execução pós-reset do MEC — hoje só há readback;
- desktop acelerado e visível pelo nosso driver;
- frame completo e pixel correto;
- apresentação sustentada sem TDR;
- caso físico acolchoado 590x800;
- `pfnLockCb=0x80070057` em caminhos ainda observados;
- publicação atômica do journal GPUVA antes de PTEs físicas;
- D3D11 nativo completo, D3D12 e jogos;
- DCN básico em P9.

## 14. Trabalho anterior de UMD que permanece no worktree

Não atribua todo o diff atual apenas à Corrida 43. O worktree contém as faixas Corridas
41/42 e suas correções, incluindo:

- preservação de `D3D11LabPromotion` no runner;
- upload 2D linha a linha e `bufferRowLength=RowPitch/bpp`;
- telemetria de descriptor/draw;
- publicação GPUVA com fotografia de batch/índice;
- PDB no manifesto/pacote;
- recuperação de cauda delimitada;
- captura de DWM e coleta final robusta;
- reconciliação de fences TDR;
- correção atual de BASE GFX.

Antes de editar esses arquivos, leia os docs 297–302 e o diff. Não remova mudanças por parecerem
“não relacionadas”. Elas pertencem à cadeia ainda não commitada do proprietário.

## 15. Estado do Git

O worktree está deliberadamente sujo. Há dezenas de arquivos modificados e docs 295–302 não
rastreados, além deste doc 303. Não há autorização para limpar, descartar ou consolidar via
reset. Preserve alterações sobrepostas e use `apply_patch` para edições pontuais.

Arquivos centrais desta faixa:

- `driver/wdk/bc250_active_miniport.c`;
- `driver/core/bc250_gfx_ring_contract.[ch]`;
- `driver/core/bc250_gfx_ring_transaction.c`;
- `driver/wdk/bc250_submission_engine.[ch]`;
- `driver/tests/gfx_ring_contract_test.c`;
- `driver/tests/active_miniport_test.c`;
- `tools/audit-bc250mcdm-safety.py`;
- `tools/bind-bc250mcdm-lab.ps1`;
- `tools/recover-bc250mcdm-lost-tail.ps1`;
- `BASE_DE_AVANCO.md`.

## 16. Evidência que deve ser preservada

- Corrida 42: `docs/evidence/2026-09-20_00-03-31-DESKTOP-HQAG6T4-lab-binding/`;
- Corrida 43: `docs/evidence/2026-09-20_10-05-31-DESKTOP-HQAG6T4-lab-binding/`;
- dump Corrida 43: `memory-dmp-private-analysis.txt`;
- atribuição final: `memory-dmp-symbol-attribution.txt`;
- cauda recuperada: `bc250_display_log-tail-recovered.txt`;
- par reconstruído: `reconstructed-symbol-pair/`.

Não sobrescrever nem “corrigir” os artefatos corrompidos originais; eles são prova da lacuna de
coleta. Novos artefatos devem ter nome próprio e hash registrado.

## 17. Regra de decisão depois da Corrida 44

- Se a prova de restart passar e a submissão seguinte aposentar, fechar a falha específica de
  BASE/restart e voltar ao stall inicial em candidato separado.
- Se BASE passar e prova falhar em RPTR=0, investigar a sequência pós-soft-reset comparando a
  inicialização ONLINE, sem adicionar escrita não catalogada.
- Se a prova avançar parcialmente, usar header dump/CP busy/IB restante para localizar o novo
  ponto; não voltar a culpar GPUVA sem `GFXHUB_FAULT`.
- Se houver BugCheck, preservar o dump antes de qualquer nova corrida e usar o SYS+PDB agora
  selados no diretório da evidência.
- Em todos os casos, atualizar `BASE_DE_AVANCO.md` somente com conclusões sustentadas por P0.

## 18. Resumo de uma linha

A Corrida 43 provou que o restart antigo apontava o CP_RB0 para uma base 256 vezes errada; o
candidato 3.202.1.0 corrige e mede essa base, está pronto mas não instalado, e a próxima tarefa
é recuperar a máquina e executar exatamente uma Corrida 44 sob o critério já congelado.
