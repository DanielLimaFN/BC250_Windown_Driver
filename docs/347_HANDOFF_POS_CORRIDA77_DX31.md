# Handoff pós-Corrida 77 — DX.31 e estado para outra IA

**23/09/2026, America/Sao_Paulo.** Este documento encerra esta etapa a pedido
do Daniel: **não continuar o desenvolvimento nem executar outro bind nesta
sessão**. A nova IA deve começar lendo `AGENTS.md`, `BASE_DE_AVANCO.md`,
`DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md` e este handoff. Nenhuma
correção de driver foi feita depois da Corrida 77; após o ensaio foram feitas
somente leitura/análise, recuperação da cauda de log e documentação.

## 1. Resumo honesto

| Pergunta | Resposta com limite de prova |
|---|---|
| Houve avanço sobre a Corrida 76? | **Sim, visual e diagnóstico; não funcional sustentado.** Na 76 Daniel viu tela preta e houve zero `BYTE_PROOF PASS` do Present. Na 77 ele viu **desktop congelado**, e o KMD registrou quatro `BYTE_PROOF PASS` em Present #1, #5, #10 e #25 (o último só na cauda recuperada). Isso não prova movimento, composição correta nem que a DX.31 causou sozinha a diferença. |
| O que a DX.31 mediu? | 30 operações `PRIMARY_COPY` distintas (#1–#30): 22 `SAMPLED_MATCH_NONZERO_NOT_FULL_BYTE_PROOF`, 8 `MATCH_BLANK_INCONCLUSIVE`, zero `FAIL`. Cada `END` não-nulo informa SDMA com fence/RPTR e 256 amostras pareadas sem divergência. **Todas as amostras de destino antes da cópia não-nula já coincidiam com a origem** nos registros consultados: portanto não atribuir a mudança de bytes àquela cópia individual. Amostra não é leitura integral nem prova de visibilidade física. |
| Controle D3D11/Vulkan | As três variantes do autoteste RADV (`nodcc`, `nodcc,nonggc`, `nodcc,nongg`) terminaram S1–S5 5/5 e contador S5=65.536. O harness D3D11 selecionou BC-250, criou dispositivo e pipeline, mas **não produziu contagem final verificável de Presents aceitos**. O runner devolveu **exit 3/falha**, corretamente; não converter ausência de medição em `0/60` demonstrado. |
| TDR/recuperação | Um TDR de GFX durante o teste (evento Display 4101 às 07:42:21; snapshot KMD com 240/240 ondas e restart físico). Um segundo evento 4101 às 07:43:50 ocorreu **depois** de o watchdog iniciar rollback às 07:43:46, durante recuperação/reboot; a cauda KMD mostra outro reset, agora 0/0 ondas. Não somar ambos como duas reproduções da mesma falha de render. Nenhum BugCheck foi identificado nos eventos inspecionados; reboot foi solicitado pelo watchdog (`User32` 1074 às 07:43:48). |
| Veredito do critério pré-escrito | **F — falha funcional com avanço diagnóstico/visual parcial.** O critério A exigia imagem **móvel**, ao menos um Present aceito, zero TDR e controle 5/5. Houve desktop congelado, primeiro TDR e nenhum resumo de Presents. A categoria D pressupunha monitor preto no mesmo intervalo; não se aplica ao relato da 77. |
| Estado final | Watchdog: `RECOVERY_OK_ONLINE` na tentativa 2 às 07:45:01; BasicDisplay Código 0 em cinco amostras. Leitura CIM posterior confirmou BasicDisplay Código 0. Essa imagem é recuperação, não sucesso do DWM experimental. **Boot frio e preflight são obrigatórios antes de qualquer futuro bind; nenhum está autorizado por este documento.** |

## 2. Candidato, execução e arquivos de prova

- Corrida 77: pasta `docs/evidence/2026-09-23_07-41-45-DESKTOP-HQAG6T4-lab-binding/`.
  Pacote lab `DISPLAY_RENDER_LAB`, DriverVer `09/23/2026,3.238.1.0`, WDDM 2.7,
  RenderNode1, submissão persistente, `GfxWaveProbe=0`. O Daniel executou
  `tools/EXECUTAR_D3D11_NATIVO.cmd` elevado; o agente **não** executou bind.
- Hashes do pacote: SYS assinado
  `4AFA7AAFA4BB35AA43DBFD7C9E09411472F88906BA7DD9081EE2656D678D81F4`;
  UMD `239B046C9FDA264BB1579F46F48DDCFDCB4A8F6DA273F879928D629C25F9A75D`;
  INF `E982660E6B57341656E6C35C057DC80CAD9B73EDB8B00DDB93DA1050C09DBA5F`;
  CAT `4ECC196B24F9D8AA16E1A42A787FDC5E7C758AF3E013F3CFA1699025D3254CA4`.
  ICD externo `C:\BC250_LIB\vulkan_radeon.dll`:
  `5988B38E86FEBB514C4C5F1D5FB5CE5A4B6FDD2B57668F2A1B6BACA3B1A0461F`.
- Antes do bind: kernel iniciou às 07:38:16; auditoria read-only de prebinding
  **PASS** (sem `PendingFileRenameOperations`, BasicDisplay Código 0, sem serviço
  experimental); auditoria do pacote **PASS**; boot fuse sem arquivo.
- Log KMD `display-log-this-bind-FINAL.txt`, SHA-256
  `E73725D1707BE2AAE0723C7DF1565A2F8B14F0B9409FE08EADF1A3C8455C05C6`,
  3.685.653 bytes. O JSON do coletor diz `complete_at_capture=true` **no instante
  da captura** (07:43:45), mas a atividade KMD continuou depois.
- Para preservar o segundo TDR e `PRIMARY_COPY #30`, rodei somente o recuperador
  existente `tools/recover-bc250mcdm-lost-tail.ps1 -MaxFolders 1`, que leu o log
  cumulativo sem tocar no driver. Arquivou
  `bc250_display_log-tail-recovered.txt`, 3.598.786 bytes, SHA-256
  `7D50F1A1AC81EB658FF4B923ABBD7C7013C8C972C712A015A30B320121851B31`.
  **A cauda sobrepõe o FINAL desde `PRIMARY_COPY #2`; nunca somar suas contagens.**
  Não há bind posterior conhecido que delimite a cauda, mas ela termina no
  teardown da mesma instância experimental; conferir o manifesto
  `display-log-tail-recovered.json` antes de atribuir linhas futuras.
- UMD `bc250_umd_log-FINAL.txt`, SHA-256
  `8B342BCAD7AAC806524F723D0930D7F4A723E652FD2C38F1072192AB32F0DF1B`.
  Console de conformidade SHA-256
  `A6A176EBEEF4A629DD36D8B1D7D4E1CA68427E99F63E14C82434DC0426E3E39A`.
  Há também stdout/stderr do harness e das três variantes de autoteste,
  `gfx-ib-dump/`, proveniência, PnP e journal do watchdog.

## 3. Por que esta faixa foi feita e exatamente o que mudou

O ponto de partida foi a Corrida 76 (relatório `docs/345`): Flip com conteúdo
não-nulo na UMD, monitor preto, zero `BYTE_PROOF PASS`, três TDRs de GFX,
autoteste `nodcc` 4/5 e harness sem frame aceito. A auditoria do nosso fonte
achou um erro de contrato nas três DDIs de troca de primária: descartavam o
retorno booleano de `Bc250SdmaPresentBlt` e podiam retornar sucesso mesmo sem
cópia. Isso era **defeito provado em fonte**, não causa física provada da tela
preta. O objetivo da DX.31 foi localizar a primeira ruptura entre superfície,
SDMA e scanout GOP, sem introduzir escrita em DCN nem outra variável de VMID.

1. `driver/wdk/bc250_display_ddi.c`: helper comum
   `Bc250DisplayCopyPrimaryToGop` nas rotas `SetVidPnSourceAddress`, MPO1 e
   MPO3. Registra `PRIMARY_COPY #N` com offsets/MC, pitch e bytes, 256 amostras
   antes/depois, estado da SDMA e comparação pareada. Falha real da SDMA ou
   overflow recusa a troca; MPO3 retorna `STATUS_INVALID_PARAMETER` documentado
   para Flip não processado. Origem fora do BAR mapeado ainda pode ir por SDMA,
   mas vira `DATA_UNOBSERVED_CPU_MAP`. Zero/zero é inconclusivo; diferença de
   amostras também, pois o produtor pode reescrever a origem. A leitura é
   limitada: MPO3 pode rodar em nível de interrupção e bloquear scheduler;
   comparar ~11 MB integralmente ali pioraria risco de TDR. A cópia SDMA
   síncrona preexistente continua uma dívida arquitetural.
2. `driver/tests/display_vidpn_test.c`: teste offline para ausência de SDMA,
   exigindo que SetAddress/MPO3 não publiquem a primária nova.
3. `tools/audit-bc250mcdm-safety.py` e
   `tools/test-dx31-primary-copy-mutations.py`: auditoria da rota, retorno
   fail-closed e telemetria; três mutações deliberadas detectadas.
4. `tools/test-d3d11-native-conformance.ps1`: gate do runner exige resumo final
   `Presents aceitos pelo runtime` igual ao alvo e as três variantes S1–S5 PASS
   com contador 65.536. `Exit 0`, `PRESENT_OK` periódico e pixels só na UMD
   deixam de ser falsos sucessos. Isso explica o **exit 3 correto** da 77.
5. `driver/wdk/package/bc250mcdm.inf` e pacote lab assinado 3.238.1.0:
   materializaram essa faixa para a corrida. Build de contratos, auditorias
   safety/pacote/UMD Node1 e mutações passaram offline. `docs/346` fixou
   **antes do ensaio** critérios A–G e teto de uma corrida. O pacote anterior
   3.237.1.0 foi preservado em zip na evidência da 76.

**A DX.30 também permanece neste checkout e no SYS da 77, mas foi criada
antes:** transição de VMID no anel CP_RB0 com PT_BASE, invalidação/ACK e
`PFP_SYNC_ME`, com dump PM4. Motivo e limites em `docs/344`; a Corrida 76
exercitou a sequência e ainda teve TDRs (`docs/345`). Não atribuir à DX.31
uma correção dessa classe: ela mudou principalmente observabilidade e retorno
das DDIs de primária, não VM/PM4/UMD/ICD.

## 4. Análise técnica da Corrida 77

### Pixel e apresentação

- `PRIMARY_COPY #1–#3`: origem 0/256, destino 0/256, prova
  `MATCH_BLANK_INCONCLUSIVE`; não tratá-los como falha nem como imagem.
- Primeiro sinal não-nulo: `PRIMARY_COPY #4` rota MPO3, origem
  `SrcOff=0x153C000`, destino GOP `DstOff=0x0`, 11.059.200 bytes,
  pitch 10.240, `SrcNZ=256/256`, `DstAfterNZ=256/256`, 0/256 divergências,
  `Sdma=YES`, fence/RPTR confirmadas. KMD `Present #1 BYTE_PROOF PASS` próximo
  desse trecho; `Present #5`, `#10` e `#25` acrescentam prova do próprio Blt.
  O `PostScanout` desses quatro foi, respectivamente, **256/256, 1/256,
  256/256 e 256/256** amostras não-nulas; o #5 é prova muito mais fraca de
  conteúdo. Mesmo o `BYTE_PROOF` não confirma HDMI/DisplayPort visual nem
  desktop móvel.
- Há 22 `PRIMARY_COPY` não-nulos (#4–#6 e #12–#30) e oito em branco
  (#1–#3, #7–#11). O destino já tinha as amostras não-nulas antes de cada
  uma das cópias não-nulas inspecionadas. É compatível com scanout já populado;
  **não demonstra que cada Flip mudou o endereço efetivamente escaneado ou
  atualizou pixels**. O hash de algumas amostras muda entre operações, mas
  isso não data nem atribui uma atualização ao monitor.
- Daniel confirmou **desktop congelado antes do rollback**. Esse relato é
  compatível com bytes no GOP e ausência de progresso visível, e é melhor que
  a tela preta da 76. Não há horário de foto/quadro nem medição de mouse/movimento;
  não afirmar taxa de frames, composição dinâmica ou causalidade exclusiva.
- O harness D3D11 iniciou e criou device BC-250 FL `0xA100`; stdout acaba
  após configurar pipeline. O `d3d11_native_conformance.log` termina com
  `WaitForSynchronizationObjectFromCpu failed` (`0xC00002B6`) e
  `CreateAllocation2 failed 0xC00002B6` depois de operações de GPUVA.
  Não há linha final de Presents aceitos; `exit 3` é do runner/gate, não
  evidência de 0 Presents medidos. `C00002B6` deve ser investigado no contexto
  do TDR, não declarado causa-raiz sem cronologia adicional.

### TDR e fault: separar medição de hipótese

- Primeiro TDR: `TDR_GFX_PROBE Responsive=0`, RPTR 3155→3155 com WPTR 3164;
  `TDR_GFX_WAVES` 240/240. Snapshot de fault `Vmid=3`, CID 9,
  `WalkerError=6`, `MappingError=1`, endereço candidato `0x201600000`.
  **O latch estava preso, a época da tabela mudou e 14/16 carimbos de raiz
  divergiam**; não ligar esse fault causalmente ao IB travado nem à DX.31.
  Reset/restart físico de SDMA0 e CP_RB0 registrou NOP/fence sobre poison;
  MEC teve apenas readback coerente.
- Segundo evento Display 4101 às 07:43:50, já na recuperação:
  cauda KMD com fault `Vmid=1`, CID 0, `Rw=1`, VA `0x2401A2000`,
  `Maps=0 Unmaps=1` na janela; snapshot GFX RPTR=WPTR=6792 e ondas 0/0.
  Mesmo assim houve reset/restart. É assinatura diferente da primeira.
  O log pode ser posterior ao começo do rollback e não prova regressão do
  caminho normal de render. A semântica do fault segue inconclusiva pelos
  avisos de latch/janela do próprio KMD.
- `VMID_SWITCH_VM_FLUSH` chegou a #85 na cauda, mas isso só prova emissão da
  sequência e aposentadoria observada de outras fences; não valida que a
  transição eliminou os hangs. O primeiro TDR de 240 ondas demonstra que não.

### Watchdog e ambiente final

- O deadline absoluto de 120 s expirou às 07:43:46 sem confirmação explícita.
  Recovery online 1 devolveu exit 5, então watchdog solicitou reboot às
  07:43:47; `User32` 1074 registra o shutdown planejado às 07:43:48.
  No novo kernel (boot 07:44:24), tentativa 2 fez recovery exit 0, e as
  amostras PnP 07:44:57–07:45:01 registraram BasicDisplay Código 0.
- Nenhum BugCheck foi observado na janela de eventos inspecionada. Não é prova
  de ausência de todos os erros de sistema. Marcador de boot limpo/boot fuse
  precisa ser reavaliado se outra pessoa, **com autorização do Daniel**,
  propuser novo bind.

## 5. Pendências e instruções de leitura para a próxima IA

1. **Não implementar automaticamente** com base neste handoff. Daniel pediu
   somente documentação e transferência. Apresentar primeiro uma análise
   reproduzível e um critério prévio para qualquer nova variável/ensaio.
2. O produto continua sem desktop acelerado móvel/estável, 60/60 D3D11,
   jogos, D3D12/Vulkan de produto e DCN P9. **NÃO VALIDADOS EM HARDWARE** pela 77.
   BasicDisplay recuperado não fecha nenhum desses itens.
3. Investigar por leitura a correspondência temporal entre `Present #1/#5/#10/#25`,
   amostras de `PRIMARY_COPY`, registro GOP/HUBP **somente leitura**, relato
   visual e TDR; evitar confundir endereço lógico de Flip com endereço físico
   escaneado. Um destino já igual antes da cópia limita a inferência.
4. Investigar, separadamente, a primeira falha de GFX com 240 ondas e a falha
   do harness em WaitForSynchronizationObjectFromCpu. Usar os IB dumps,
   carimbos de raiz/época e logs UMD/KMD. Não atribuir o fault VMID3/CID9
   sem sentinela temporal ou tabela do instante; `ObservedComputeVmid` não
   foi provado aqui. O segundo TDR ocorreu na recuperação e tem 0 ondas.
5. `git status` está intencionalmente sujo e **não houve commit**. Mudanças
   DX.30 e DX.31 coexistem com histórico anterior; preservar edições do Daniel.
   Arquivos centrais desta faixa: `driver/wdk/bc250_display_ddi.c`,
   `driver/tests/display_vidpn_test.c`, `tools/test-d3d11-native-conformance.ps1`,
   `tools/audit-bc250mcdm-safety.py`, `tools/test-dx31-primary-copy-mutations.py`,
   `driver/wdk/package/bc250mcdm.inf`, `docs/346`, este documento e
   `BASE_DE_AVANCO.md`. O `output_lab` 3.238.1.0 foi **instalado só para a 77**
   e retirado pelo watchdog; é artefato assinado, não driver ativo.
6. Para recontar: use `display-log-this-bind-FINAL.txt` até #29 e a cauda
   recuperada **apenas para o segmento posterior**. Há sobreposição extensa.
   `display-log-final-archive.json` atesta a captura naquele momento, não o
   fim de toda atividade antes do reboot.

**Veredito final:** Corrida 77 = **falha funcional (F)**, com avanço visual
parcial e forte ganho diagnóstico do caminho de pixel. O objetivo final e a
causa-raiz dos TDRs continuam abertos. Esta entrega é um handoff, não uma nova
proposta de bind.

---

## 6. Revisão de 23/09 — o que este handoff deixou passar (adendo)

Conferido contra `bc250_display_log-tail-recovered.txt` da 77. Os números acima estão corretos;
faltam três leituras que os próprios logs respondem.

1. **O "destino já igual antes da cópia" tem explicação: a cópia do flip é redundante (P0).** Toda
   `PRIMARY_COPY` não-nula do MPO3 copia a **mesma origem** que o `DxgkDdiPresent`
   imediatamente anterior acabou de copiar por SDMA: `Present #1 BYTE_PROOF PASS` (0x153C000) vem
   logo antes de `PRIMARY_COPY #4` (0x153C000); `Present #10` (0x266C000) antes de `#12`
   (0x266C000). Por isso as amostras do destino batiam com a origem em 22 de 22. Não é mistério de
   scanout; é uma segunda cópia síncrona de ~11 MB numa DDI que a Microsoft documenta como chamada
   em nível de interrupção. Tratado na DX.32 (docs/348).
2. **O scanout mudou de conteúdo por um tempo e depois parou (P0 amostral).** O hash das 256
   amostras do scanout antes de cada cópia muda de flip para flip de `#12` a `#21`
   (00AE9017 → 00AF9C28 → 00979A6D → 009A9760 → 00909D6B → 009F8871 → 0092857C) e fica constante
   de `#22` a `#29` (0092857C). O desktop se atualizou cerca de dez vezes e congelou. A frase "não
   demonstra que cada Flip atualizou pixels" é conservadora demais para esse trecho; continua valendo
   que amostra não é leitura integral nem prova de imagem no monitor.
3. **A classe de TDR chegou a 9 de 9.** O TDR de 240 ondas da 77 foi de novo num IB de dispatch de
   288 DW logo após troca de VMID (3 → 1), com a DX.30 ativa: segunda corrida em que o flush de VM
   no anel não impede. O segundo TDR (0 ondas, IB de 560 DW) aconteceu já na recuperação e é outra
   assinatura.
4. **`ObservedComputeVmid` nunca foi implementado** (prometido no docs/344 §4.1). Implementado na
   DX.32 como leitura somente leitura de `COMPUTE_VMID` no snapshot de TDR.
5. **O anel e a CPU disputavam o engine de invalidação 17** (REQ 0xA2D0), sem serialização. O
   upstream dá um engine a cada anel. Na DX.32 o anel passa ao engine 0.

A atribuição da foto à Corrida 74 (docs/345 §6, repetida na BASE) foi corrigida: a foto é da 75.
