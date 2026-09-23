# Pós-Corrida 75: origem do VMID no dispatch, comparação upstream e critério da Corrida 76

> **22/09/2026.** Investigação offline, sem bind e sem corrida. Evidência da Corrida 75 em
> `docs/evidence/2026-09-22_22-34-53-DESKTOP-HQAG6T4-lab-binding/`; critério da 75 em
> [docs/342](342_CORRIDA74_ANALISE_E_CRITERIO_CORRIDA75_TROCA_DE_VMID.md).
> A Parte 5 é o critério da Corrida 76, escrito **antes** do ensaio (Leis 9 e 10).
> **Estado da DX.30 em 23/09: implementada e empacotada OFFLINE; NÃO VALIDADA EM HARDWARE.**

---

## 1. O que a Corrida 75 eliminou

**P0:** a DX.29 esteve ativa 77 vezes e não impediu nenhum dos três TDRs. Os três voltaram a
ter 240/240 ondas e um IB de dispatch logo depois de um IB de outro VMID. No acumulado das
Corridas 73–75, são **8 de 8**. Portanto, a hipótese estreita "o PFP começa o IB novo antes de
o IB anterior terminar" está **refutada**: o `WAIT_REG_MEM` no PFP sobre a fence anterior
realmente entrou no anel, e a falha sobreviveu.

Isso não refuta o defeito de VMID. Os faults de busca de instrução continuam trazendo o VMID
do job anterior na página do shader do job atual:

| TDR da 75 | IB travado | VMID do IB | VMID anterior | Fault relevante |
|---|---:|---:|---:|---|
| 1 | `0x240030000`, 280 DW | 2 | 1 | CID 9, VMID 1, `0x2003C0000` (shader do harness visto pela raiz do DWM) |
| 2 | `0x2405A8000`, 288 DW | 1 | 2 | latch de escrita anterior; não atribui o fault ao TDR |
| 3 | `0x2422F4000`, 280 DW | 3 | 2 | CID 9, VMID 2, `0x201600000`, WalkerError 6 (shader do DWM) |

O latch guarda o primeiro fault desde o último TDR. Logo, o segundo caso não permite chamar
o fault de escrita de causa. Os casos 1 e 3 são a assinatura forte; o mecanismo ainda é **H**.

## 2. O dispatch travado é do instrumento de readback

**P0 por evidência + P1 por fonte:** os IBs de 280/288 DW contêm um único
`DISPATCH_DIRECT`. As dimensões 160×90 e 320×135 correspondem a 1280×720 e 2560×1080 em grupos
8×8. Em `driver/umd/bc250_d3d_umd.c`, `Bc250UmdReadbackDrawTarget` roda depois de cada draw
grande, executa `vkCmdCopyImageToBuffer` do alvo inteiro para `LinearPresentBuf`, faz flush e
publica `[DRAW_READBACK]` seguido de `INSPECAO LINEAR_BUF GRADE`.

Assim, os oito TDRs observados atingiram a cópia imagem→buffer por compute usada pelo nosso
instrumento. Isso **não prova** que o instrumento seja a causa. Ele pode ser apenas o primeiro
dispatch que encontra a transição de VM incompleta. Desarmá-lo ocultaria o disparador atual e
retiraria a medição que provou os pixels; não corrigiria jogos que usem compute na fila gráfica.

**Decisão para a Corrida 76:** não desligar o readback. O desligamento fica como ensaio de
contorno, não como solução, se a DX.30 não produzir um desfecho atribuível.

## 3. O que o upstream faz e o nosso caminho não faz

### 3.1 De onde vem o VMID das ondas de compute na fila gráfica

**P1:** `PACKET3_DISPATCH_DIRECT` não tem campo de VMID. O `gfx_v10_0_ring_emit_ib_gfx`
coloca `vmid << 24` no control dword de `PACKET3_INDIRECT_BUFFER`. Portanto, o dispatch herda
o contexto de VM do IB/job; não há um segundo VMID no pacote de dispatch para a UMD programar.

Existe o registrador `COMPUTE_VMID` (`gc_10_1_0_offset.h`, `mm=0x1BB4`, campo `DATA[3:0]`),
mas o `gfx_v10_0.c` e o RADV não o escrevem por job. O valor é estado observado do CP/shader,
não uma alternativa ao VMID do `INDIRECT_BUFFER`. Uma leitura dele no snapshot de TDR seria
medição útil; escrever nele sem procedência de sequência é proibido.

### 3.2 Ordem de `amdgpu_ib_schedule`

**P1, fontes upstream locais:** `amdgpu_ib_schedule` calcula `need_ctx_switch`, chama
`amdgpu_vm_flush` antes do IB e só então emite a sequência do IB. Para GFX10:

1. `gfx_v10_0_ring_emit_pipeline_sync`: `WAIT_REG_MEM` no PFP sobre a fence anterior;
2. `gmc_v10_0_emit_flush_gpu_tlb`, **dentro do mesmo anel**:
   - escreve `GCVM_CONTEXTn_PAGE_TABLE_BASE_ADDR_LO32/HI32`;
   - escreve `GCVM_INVALIDATE_ENGx_REQ`;
   - espera o bit `1 << vmid` em `GCVM_INVALIDATE_ENGx_ACK`;
3. `gfx_v10_0_ring_emit_vm_flush`: `PFP_SYNC_ME` depois da invalidação;
4. depois, na sequência do IB, `emit_mem_sync` (`ACQUIRE_MEM`) e
   `emit_cntxcntl` (`CONTEXT_CONTROL`) quando há troca de contexto;
5. `PACKET3_INDIRECT_BUFFER` com o VMID no control dword;
6. fences.

Nosso `Bc250ActiveGfxEngineSubmit` hoje põe no anel, na troca:

1. `WAIT_REG_MEM` sobre a fence anterior;
2. `PFP_SYNC_ME`;
3. `INDIRECT_BUFFER`;
4. `RELEASE_MEM`.

A base da page table e a invalidação acontecem antes, por MMIO da CPU, em
`Bc250GcvmInstallVmid`. Logo a DX.29 preservou a ordem **entre IBs**, mas não ordenou a mudança
de VM com o consumo do anel. Essa é exatamente a divergência que sobreviveu à Corrida 75.

`CONTEXT_CONTROL` e `ACQUIRE_MEM` também faltam, mas não entram na próxima corrida: misturá-los
com o vm-flush impediria atribuir o resultado. Permanecem candidatos se a DX.30 for refutada.

## 4. Variável proposta: DX.30, transição de VM ordenada no anel

### 4.1 Uma única variável comportamental

Na troca de VMID em `Bc250ActiveGfxEngineSubmit`, substituir o trecho DX.29 por uma
**transação de VM no próprio CP_RB0**, mantendo o readback da UMD e o ICD da Corrida 75:

1. esperar a fence anterior como hoje;
2. `WRITE_DATA` com confirmação para os dois registradores da raiz do **VMID alvo**, usando a
   raiz já validada e registrada no pool;
3. `WRITE_DATA` em `GCVM_INVALIDATE_ENG17_REQ` com o request já produzido pela fonte única
   `Bc250PagingBuildGfxhubTlbFlushPlan`;
4. `WAIT_REG_MEM` no `GCVM_INVALIDATE_ENG17_ACK`, máscara `1 << vmid`;
5. `PFP_SYNC_ME`;
6. `INDIRECT_BUFFER` do job.

Falha de construção, espaço do anel ou raiz ausente recusa antes de publicar WPTR. A
instalação MMIO existente permanece como preflight/readback; a autoridade de ordem para o job
passa a ser a sequência do anel. Não se escreve DCN.

**Medição somente leitura que pode acompanhar:** ler `COMPUTE_VMID` no snapshot de TDR e
registrar `TargetVmid`, `PreviousVmid` e `ObservedComputeVmid`. Isso não muda comportamento.

### 4.2 Condições antes de empacotar

- builder puro testa byte a byte a sequência e os offsets derivados de `inc/linux_ref/`;
- o teste negativo prova que a sequência não é emitida sem troca de VMID;
- o teste negativo prova recusa se a raiz do VMID alvo não estiver `READY` no pool;
- auditoria proíbe `INDIRECT_BUFFER` antes do ACK/PFP sync na janela da troca;
- todo portão novo é verificado por mutação;
- `build-contract`, SYS lab, UMD e auditorias offline devem passar; o autoteste físico
  pertence à Corrida 76, não ao pré-empacotamento;
- novo DriverVer: 3.236.1.0 já foi instalado; não reutilizá-lo.

### 4.3 Implementação offline e candidato (23/09/2026)

- `Bc250Pm4EmitGfxVmSwitch` emite 31 dwords: espera da fence anterior no PFP;
  `WRITE_DATA`/PFP com confirmação para PT_BASE LO/HI e REQ; `WAIT_REG_MEM`/ME no ACK;
  `PFP_SYNC_ME`. O endereço PM4 de registrador é índice DWORD (offset em bytes dividido por 4).
  A raiz é emitida com `BC250_PTE_VALID`, como na instalação MMIO. Não se usa semáforo
  GFXHUB nem se escreve DCN.
- `Bc250ActiveGfxEngineSubmit` reserva 46 dwords na troca, exige raiz instalada e plano
  GFXHUB engine 17 antes de tocar no anel, emite a transição antes do IB, e só publica o
  WPTR no caminho de sucesso. A primeira troca copia sob lock e registra os 31 dwords da
  transição **mais os 4 dwords do IB**; isso permite conferir ORDEM-OK na evidência.
- Teste de builder confere os 31 dwords contra literais independentes e recusa raiz zero.
  Auditoria DX.30 verifica ordem, raiz, espaço, dump e precedência; mutações virtuais de
  offset REQ e reserva do anel foram detectadas.
- `build-contract.bat`: EXIT 0; build SYS lab: EXIT 0; build UMD: EXIT 0; safety audit,
  package audit, package readiness e auditoria Node 1 da UMD: PASS. O RADV fork não mudou
  nesta faixa; a tentativa de auditoria RADV sem `--mesa-root` foi apenas erro de invocação,
  não foi contada como PASS.
- Pacote assinado `3.237.1.0`, perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, submissão persistente:
  SYS SHA-256 `64348A9E085176FA9365A16D4C46C15E54D3302163229DF2FDB0AEE25B96EDEC`,
  UMD `239B046C9FDA264BB1579F46F48DDCFDCB4A8F6DA273F879928D629C25F9A75D`,
  CAT `5BBB45EE9B1CBEA85D0026687AF41B5D544F700D7E3BAE56B19231AD2B50467D`.
  Manifesto em `driver/wdk/package/output_lab.manifest.json`. **Não instalado; zero corrida.**

## 5. Critério da Corrida 76

### 5.1 Perguntas

1. A transação de VM é consumida no anel antes do IB alvo?
2. Os TDRs de 240 ondas logo após troca de VMID desaparecem?
3. O desktop, o harness e o autoteste mantêm os pixels já provados?
4. Se houver TDR, `COMPUTE_VMID` observado coincide com o VMID alvo ou com o anterior?

### 5.2 Sinais

- **VM-FLUSH-ATIVO:** uma linha por troca com VMID anterior/alvo, raiz, REQ, máscara de ACK e
  fence seguinte; contagem próxima às trocas reais.
- **ORDEM-OK:** dump do trecho do anel decodifica `WAIT fence -> PT_BASE LO/HI -> INV_REQ ->
  WAIT ACK -> PFP_SYNC_ME -> INDIRECT_BUFFER`, com o VMID alvo e sua raiz.
- **SEM-TDR-PÓS-TROCA:** zero TDR de 240 ondas em IB logo após VMID diferente.
- **CONTROLES:** autoteste 5/5 nas três variantes; zero `PREAMBLE_CNTL`; CSIB programado;
  zero IB cumulativo; zero BugCheck; `Submetidas == Aposentadas` ao fim.
- **HARNESS:** número de quadros completos entre 0 e 60 e qualquer `DEVICE_REMOVED`.
- **DESKTOP:** mesmo `dwm.exe` vivo, pelo menos 10 grades não-nulas e mouse móvel, se a
  janela durar o bastante.
- **VMID-SNAPSHOT:** em TDR, `TargetVmid`, `PreviousVmid` e `ObservedComputeVmid`.

### 5.3 Desfechos (assinatura e implicação separadas)

#### A — A transição de VM no anel elimina a classe

- **Assinatura:** VM-FLUSH-ATIVO, ORDEM-OK, CONTROLES e **zero TDR na corrida inteira**.
- **Implicação:** a ordenação da base/invalidação com o CP era causa necessária da classe.
  Se houver 60 quadros e desktop estável, avançar para composição real do DWM; se a janela
  terminar antes, estabilidade longa continua aberta sem diminuir o veredito desta classe.

#### B — A classe pós-troca some, mas sobra outro TDR

- **Assinatura:** VM-FLUSH-ATIVO, ORDEM-OK e SEM-TDR-PÓS-TROCA, com ao menos um TDR de
  assinatura diferente (outro pacote, zero ondas ou sem troca anterior).
- **Implicação:** a DX.30 corrigiu a classe alvo e revelou outra. Descrever a nova assinatura
  antes de escolher o próximo passo; não misturar as classes.

#### C — O fault ainda usa o VMID anterior

- **Assinatura:** VM-FLUSH-ATIVO e ORDEM-OK, mas reaparece TDR de 240 ondas após troca, com
  fault CID 9 sob o VMID anterior **ou** `ObservedComputeVmid == PreviousVmid`.
- **Implicação:** base + invalidação ordenadas não bastam. Próxima variável: `CONTEXT_CONTROL`
  na troca de contexto, porque o upstream manda carregar estado CS/GFX antes do IB.

#### D — O VMID observado está certo e as ondas ainda travam

- **Assinatura:** TDR de 240 ondas após troca, mas `ObservedComputeVmid == TargetVmid` e o
  fault, se houver, também pertence ao VMID alvo.
- **Implicação:** a hipótese de execução sob o VMID anterior cai para esse caso. Investigar
  `ACQUIRE_MEM`/visibilidade do IB e o próprio shader; não repetir flush de VM.

#### E — A própria transação trava ou é recusada

- **Assinatura:** PFP/ME parado no `WAIT_REG_MEM` do ACK, ACK abaixo da máscara, ou recusa
  fail-closed antes do WPTR por raiz/REQ/espaço inválido.
- **Implicação:** o ensaio não chegou ao IB. Revisar codificação, engine 17 e semântica de
  ACK contra `Bc250PagingBuildGfxhubTlbFlushPlan`; não concluir nada sobre o dispatch.

#### F — Regressão de controle

- **Assinatura:** autoteste abaixo de 5/5, CSIB perdido, `PREAMBLE_CNTL` reaparece, IB
  cumulativo, BugCheck ou submissões sem aposentadoria fora da assinatura E.
- **Implicação:** atribuir a regressão antes de interpretar TDRs. Reverter a DX.30 se o
  defeito estiver na sequência nova.

#### G — Fora do mapa

- **Assinatura:** qualquer observação que não caiba integralmente em A–F, inclusive falta dos
  logs que provam a ordem, imagem inesperada, fault sem sentinela temporal ou resultado visual
  sem prova de pixel.
- **Implicação:** descrever o observado antes de julgá-lo e escrever novo critério antes de
  outra corrida.

**Desfecho parcial não é desfecho** (Lei 10.3). Em particular, zero TDR sem VM-FLUSH-ATIVO e
ORDEM-OK não é A; e VM-FLUSH-ATIVO sem dump que prove a ordem não autoriza atribuição.

### 5.4 Teto e abandono

- **Teto:** uma corrida com a DX.30.
- Se cair em A ou B, não repetir: a classe foi respondida.
- Se cair em C ou D, não ajustar o flush por tentativa; seguir a implicação correspondente.
- Se cair em E, corrigir offline e só repetir com o mesmo critério se a assinatura provar que
  o IB alvo nunca partiu.
- Se cair em F ou G, preservar evidência e redesenhar antes de gastar novo boot.

Execução: boot frio obrigatório e `tools\EXECUTAR_D3D11_NATIVO.cmd`, pelo Daniel em
terminal elevado. **Não usar `-Force` quando o marcador de boot frio pode ser conferido**:
esse argumento ignora a checagem e apaga o marcador. Em 23/09 o `rollback_boot_ticks`
(`639257231065000000`) difere do boot atual (`639257282675000000`), e o preflight sem
`-Force` deve aceitar; se recusar, parar e investigar sem contornar o bloqueio.
