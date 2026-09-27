# DX.31 — cópia de primária GOP observável; critério da próxima corrida

**23/09/2026. OFFLINE; zero bind, zero ensaio físico.** Origem: Corrida 76
([docs/345](345_CORRIDA76_VM_FLUSH_NO_ANEL_RESULTADO.md)), em que havia
superfícies de Flip não-nulas, tela física preta e zero `BYTE_PROOF PASS` do
`DxgkDdiPresent`. O código antigo das três DDIs de endereço descartava o
`BOOLEAN` de `Bc250SdmaPresentBlt` e devolvia `STATUS_SUCCESS` mesmo sem cópia.
Isso é defeito de contrato **provado em fonte**, não causa física provada.

## Alteração única e limite de confiança

- `SetVidPnSourceAddress`, MPO1 e MPO3 compartilham a cópia da primária do
  segmento 1 ao scanout GOP. `PRIMARY_COPY #N` correlaciona rota, offset e MC
  de origem/destino, tamanho/pitch, 256 amostras da origem e do scanout antes e
  depois, resultado da SDMA (RPTR + fence sobre poison) e 256 comparações
  pareadas após a cópia. Falha da engine ou endereço com overflow não
  retornam sucesso nem publicam a primária nova. Uma primária válida fora da
  janela de VRAM mapeada para CPU continua elegível para SDMA, com o rótulo
  `DATA_UNOBSERVED_CPU_MAP`, jamais como prova de pixel. Divergência de amostras é
  `INCONCLUSIVE`, pois o produtor pode ter reescrito a origem após a fence;
  não provoca device-lost artificial.
- `MATCH_BLANK_INCONCLUSIVE` é **inconclusivo**, não PASS; `SAMPLED_MATCH_NONZERO`
  é prova apenas dos pontos lidos, **não prova integral de bytes nem imagem no
  monitor**. `ALIAS` indica origem já no scanout, sem atribuir execução SDMA.
- Não fazemos comparação integral de ~11 MB nesta DDI. A [documentação da
  Microsoft para MPO3](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_setvidpnsourceaddresswithmultiplaneoverlay3)
  diz que ela costuma ser chamada em nível de interrupção, bloqueia o thread
  principal do scheduler e não deve fazer trabalho prolongado. A cópia SDMA
  síncrona **já existia**; sua latência/adequação ainda é dívida arquitetural.
  A DX.31 aumenta leituras e logs de forma limitada; efeito temporal no
  silício **NÃO VALIDADO EM HARDWARE**.
- Em MPO3, falha de cópia retorna `STATUS_INVALID_PARAMETER`, código documentado
  para flip não processado. O erro interno exato fica no log. Isto pode levar a
  device removal/DWM reiniciado; é risco explícito da recusa fail-closed.
- Não há escrita em DCN, nem mudança de UMD, SDMA, VMID/PM4 ou tunables nesta
  faixa. A DX.30 permanece no fonte; já falhou na Corrida 76, portanto esta
  próxima corrida diagnostica **display**, não revalida uma correção de TDR.

O runner D3D11 agora exige a contagem final de `Presents aceitos pelo runtime`
igual ao alvo e S1–S5 com `contador=65536` nas três variantes do autoteste.
`PRESENT_OK` periódico não é usado como número de quadros. Exit 0, pixels só
na UMD, BasicDisplay recuperado e uma imagem congelada não fecham desktop.

## Pergunta, prova e desfechos da Corrida 77 proposta

**Pergunta:** no primeiro Flip de origem não-nula, qual é o primeiro elo sem
prova entre a primária da UMD, a cópia SDMA e o scanout herdado? Guardar também
cronologia dos TDRs e status do autoteste; não atribuir o fault de VMID anterior
sem sentinela temporal e `ObservedComputeVmid`.

| Resultado | Assinatura necessária | Implicação investigativa |
|---|---|---|
| A — avanço visual | `PRIMARY_COPY BEGIN SrcNZ>0`, `END Proof=SAMPLED_MATCH_NONZERO...`, imagem móvel confirmada pelo Daniel **antes** do rollback, ao menos um Present aceito; zero TDR e controle 5/5 para chamar de sucesso do ensaio | A rota GOP entrega pixels no caso medido; ainda faltam 60 quadros e desktop sustentado para o produto. |
| B — cópia falha | Origem não-nula e `PRIMARY_COPY FAIL` com `Sdma=0`; DDI devolve erro | Auditar endereços, engine e fence desse mesmo #N. Não culpar DWM antes disso. |
| B2 — dados inconclusivos | `SAMPLE_MISMATCH_INCONCLUSIVE` com SDMA concluída | Investigar race de produtor, coerência da leitura CPU, formato/tiling e bytes; não chamar de falha física da cópia. |
| C — origem preta | Primeiro Flip relevante tem `SrcNZ=0/256`, scanout depois também zero; nenhum Flip anterior não-nulo mostra outra causa | Rastrear UMD, identidade/residência e sincronização do recurso antes da DDI. Amostra zero não prova superfície inteira preta. |
| D — bytes chegam, monitor preto | `SAMPLED_MATCH_NONZERO` no scanout, mas tela preta confirmada **no mesmo intervalo**, antes do rollback | Ler, sem escrever, endereço/formato/pitch do HUBP GOP e confrontar o scanout efetivo; amostras em `0x0` não bastam. |
| E — rota não exercitada | Nenhum `PRIMARY_COPY` para os Flip não-nulos, ou só `ALIAS`/`PlaneCount=0` | Investigar roteamento de Present/Flip e callback efetivo, não a SDMA. |
| F — controle regride | Autoteste abaixo de 5/5, TDR antes do primeiro Flip observável, BugCheck, ou harness 0/60 | Corrida falhou para função. Preservar a telemetria que houver; não inferir ausência de imagem a partir de log incompleto. |
| G — fora do mapa | Qualquer mistura sem assinatura completa, log truncado sem tail recuperável, divergência entre prova e observação | Descrever o observado e redesenhar; não forçar A–F. |

**Abandono/teto:** uma corrida física, somente após boot frio, pacote lab novo
assinado e auditado, watchdog/rollback prontos e preflight do binder aprovado.
Daniel executa `tools\EXECUTAR_D3D11_NATIVO.cmd` elevado, sem `-Force` para
contornar o marcador de boot. Se o boot fuse ou outro preflight recusar, parar;
não reinstalar por tentativa. Arquivar log KMD integral + tail, UMD, IB dumps,
stdout/stderr/autoteste, estado PnP, horário da foto/observação e hashes do
pacote. A foto deve ter horário relativo ao rollback; um quadro congelado não
fecha A.

## Pacote offline e condição antes do bind

Em 23/09, depois da confirmação do Daniel, o build de laboratório foi feito
com a flag explícita de bring-up ativo; **isso só compilou e assinou, não
instalou nem ligou hardware**. Pacote `DISPLAY_RENDER_LAB`, WDDM 2.7,
submissão persistente, `GfxWaveProbe=0`, DriverVer **3.238.1.0**:

| Artefato | SHA-256 |
|---|---|
| SYS assinado | `4AFA7AAFA4BB35AA43DBFD7C9E09411472F88906BA7DD9081EE2656D678D81F4` |
| UMD assinada | `239B046C9FDA264BB1579F46F48DDCFDCB4A8F6DA273F879928D629C25F9A75D` |
| INF | `E982660E6B57341656E6C35C057DC80CAD9B73EDB8B00DDB93DA1050C09DBA5F` |
| CAT | `4ECC196B24F9D8AA16E1A42A787FDC5E7C758AF3E013F3CFA1699025D3254CA4` |

ICD externo `C:\BC250_LIB\vulkan_radeon.dll` ainda tem SHA-256
`5988B38E86FEBB514C4C5F1D5FB5CE5A4B6FDD2B57668F2A1B6BACA3B1A0461F`,
igual à Corrida 76. `build-contract.bat`, auditoria de segurança, auditoria
Node 1 da UMD, auditoria do pacote lab e mutações DX.31: PASS offline.
`output_lab.manifest.json` registra os hashes integrais.

**Prebinding read-only em 23/09: BLOQUEADO** por
`PendingFileRenameOperations`. A BC-250 está em BasicDisplay Código 0,
nenhum serviço/arquivo experimental está ativo e o recovery instalado tem o
mesmo hash da fonte; o boot fuse está sem arquivo (primeiro boot assume CLEAN).
Isso **não autoriza ignorar o bloqueio**: Daniel precisa fazer boot frio e
repetir o preflight antes do bind. O pacote anterior 3.237.1.0 foi arquivado
posteriormente na pasta de evidência da 76 (ver docs/345 §6) antes de
substituir `output_lab`.

**Preflight repetido às 07:22 de 23/09, após relato de boot frio:** ainda
`BLOCKED` pelo mesmo valor `PendingFileRenameOperations` (15 pares, todos de
arquivos de fax/impressão; nenhum da BC-250). `Win32_OperatingSystem` ainda
reporta `LastBootUpTime=23/09 00:19:20 -03:00`, e `HiberbootEnabled=1`.
Isso é compatível com desligamento híbrido/Fast Startup, mas a causa exata do
novo relato não está provada. Para cumprir também o boot frio de hardware,
usar `shutdown /s /t 0` (desligamento completo, sem `/hybrid`), esperar a
máquina apagar e ligar novamente; depois repetir o preflight. Não apagar a
chave nem usar `-Force`.

**Preflight repetido às 07:41 de 23/09, após novo boot:**
`LastBootUpTime=23/09 07:38:16 -03:00`; auditoria read-only de prebinding
**PASS**, sem `PendingFileRenameOperations`, BasicDisplay Código 0, sem serviço
ou arquivo BC-250 experimental ativo, pacote lab 3.238.1.0 auditado **PASS**.
Fusível de boot sem arquivo (primeiro boot assume CLEAN), marcador de boot
limpo ausente; ICD de origem confere com SHA-256 declarado. Ainda **zero bind**
da Corrida 77 neste ponto; a confirmação de desligamento físico cabe ao Daniel.

**NÃO VALIDADO EM HARDWARE:** execução da DX.31, pixels no monitor, ausência de
TDR, recuperação após falha da nova DDI, 60 quadros e desktop acelerado.

**Pós-ensaio:** a Corrida 77 foi executada depois deste critério; resultado,
limites de prova e handoff em [docs/347](347_HANDOFF_POS_CORRIDA77_DX31.md).
O trecho acima registra o estado **pré-corrida**, não o estado final.
