# 624 — Passagem de bastão (06/10/2026): a trava dura depois da paridade com o amdgpu até a P4.2

**Para quem:** a próxima IA que continuar o desenvolvimento.
**Escrito depois da Corrida 236** ([doc 623](623_CORRIDA236_P42_PTE_NO_SDMA1_EM_REGIME_E_TRAVA_DURA_AOS_45S.md)).
**Leia nesta ordem:**
1. este documento;
2. [`CLAUDE.md`](../CLAUDE.md) (Leis 1–10, regra de relato);
3. [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md) (topo);
4. [`PLANO_MESTRE.md`](../PLANO_MESTRE.md) §3.3;
5. docs 582, 593, 621, 622 e 623.

---

## 0. Estado da máquina e do repositório agora

| O quê | Estado |
|---|---|
| Placa | **Código 43** (`CM_PROB_FAILED_POST_START`), serviço `bc250mcdm` ligado, StartDevice `0xC0000182` pelo fusível anti-boot-loop depois da trava da 236 |
| Rollback do watchdog | **não rodou** (a trava mata o watchdog; diário parado em `OBSERVING`) |
| Recuperação | **pendente, é do Daniel:** `tools\BC250_RECOVERY.cmd` elevado → `powershell -ExecutionPolicy Bypass -File tools\inspect-boot-fuse.ps1 -Reset` → **boot frio** |
| CMOS (VRAM) | 256 MB (decisão do Daniel, doc 551; **não propor VRAM maior**) |
| Último candidato | lab **3.384.1.0** (INF fonte `DriverVer=10/06/2026,3.384.0.0` em `driver/wdk/package/bc250mcdm.inf`). **O próximo candidato tem de subir o DriverVer** (3.385.x), senão dois artefatos levam o mesmo número (erro já cometido no 3.92.1.0) |
| RADV implantada | `C:\BC250_LIB\vulkan_radeon.dll` = `C6BEC54B…` (P6, exige KMD 3.381+). Fonte: `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm` (fora do repo) |
| UMD | `8782A5E5…` (inalterada desde a P6) |
| Git | branch `p7-desktop-estabilidade`, **tudo não commitado** desde a 4c (dezenas de arquivos). Não commitar sem pedido do Daniel |
| Suíte offline | `driver\wdk\build-contract.bat` EXIT=0, `tools\audit-bc250mcdm-safety.py` PASS (verificado em 06/10 15:3x) |

**Arquivos alterados fora da minha sessão:** `driver/core/bc250_sdma1_bringup.c/.h` e `inc/linux_ref/PROVENANCE_P6_CP_EOP.txt` mudaram no disco depois da minha última leitura (06/10). A mudança na proveniência é só texto (bloco da P4.1). Nos dois `.c/.h` **não conferi o que mudou**: rode `git diff` e `build-contract.bat` antes de empacotar.

---

## 1. O problema central: a trava dura

**Assinatura (estável em todas as corridas com 256 MB, doc 582):**
- a máquina inteira congela: tela parada, sem BugCheck, sem TDR, sem dump;
- Ctrl+Break no KDNET não responde; Ctrl+Scroll Lock morto; ETW vazio;
- só o botão de reset volta;
- a GDDR6 **perde dados durante a trava** (as migalhas na VRAM decaem com o tempo travado): o controlador de memória parou de dar refresh. **Na BC-250 a GDDR6 é também a RAM da CPU;**
- no último tique de 100 ms do rastro: **GPU ociosa**, todo SUBMIT retirado, leituras de MMIO voltando;
- o fim do log de texto é a última drenagem do anel em RAM, **não o último evento** (memória `bc250-fim-do-log-nao-e-o-fim`);
- acontece em uso normal, quase sempre perto de rajadas de paginação do VidMm (FILL de zeramento de alocação nova de tela cheia, despejos, `FlushTlb`) e ao abrir o menu ou a Pesquisa;
- `RESET_REASON=0x40080400` depois de toda trava (bit 19: reset por `0xCF9`; bit 30: paridade). **Nunca comparamos com o valor de um reboot limpo:** é um dado barato que falta (ver §6);
- **o Linux (`amdgpu`) com 256 MB e o mesmo firmware não travou** sob 149 despejos completos da VRAM (doc 552, coleta L-B).

**Tempos até a trava (s), com 256 MB:**
- 199: ~140, 202: ~95, 207: ~96, 208: ~290, 210: ~228, 212: ~259, 217: ~240, 218: ~123, 219: ~139, 220: ~137, 221: ~100, 223: ~338, 224: ~189;
- 228: ~454, 231: ~325, 232: ~77, 233: ~584, 234: ~136, 235: ~267, **236: ~45**;
- **passaram de 600 s:** 201 (591,8 s, rollback), 222, 226 e 229 (a 229 sem abrir o menu).

**Nenhuma etapa reproduziu um efeito claro sobre o tempo.** A variância é enorme, então uma corrida isolada não diz nada sobre melhora ou piora.

**Causas suficientes já eliminadas** (cada uma provada em regime na placa, e a trava veio igual):

| Etapa | O que é | Corrida/doc |
|---|---|---|
| Calor, fabric no FCH, SMU | instrumentação e leitura | 199/200 (555, 556) |
| CG.1 | MGCG do SDMA0 como o POST deixa | 201/202 (558, 560) |
| X2b | zerar por cópia em vez de `CONST_FILL` | 203 (562) |
| 4b | a CPU não lê o buffer linear pela BAR | 204 (565) |
| 4c.1–4c.4 | flush de TLB e PTE pelo SDMA, sem a CPU tocar a tabela | 205–210 (568–577) |
| M1b.1 | RADV no segmento 2 (como o GTT) | 211/212 (580, 581) |
| D2 + V0.1 + UC.1 | anel e fences do SDMA0 na RAM pela GART da VMID 0, MTYPE_UC | 215–218 (588–593) |
| P1 | silêncio da BAR (sem instrumentação lendo a VRAM) | 219 (595) |
| P7 | anel e fences do GFX na GTT | 220 (597) |
| P2 | WPTR do SDMA0 por doorbell (com a abertura do BIF) | 221–223 (599–601) |
| P1b | CPU na VRAM pela DRAM, sem HDP (como APU) | 226/228 (605, 608) |
| P3.0–P3.2 | MSI, GART do MMHUB, IH, conclusão do SDMA0 por interrupção | 229–233 (610–617) |
| P6 | MTYPE_UC nos BOs `GL2_BYPASS` da RADV | 234 (619) |
| EOP do CP | conclusão do GFX por interrupção | 234 (619) |
| P4.1 | SDMA1 vivo e ocioso | 235 (621) |
| P4.2 | PTE no SDMA1 com troca FENCE/POLL | 236 (623) |

---

## 2. O que funciona na placa hoje (P0, candidato 3.384.1.0)

**Ordem de subida no StartDevice:**
1. PSP carrega os firmwares;
2. RLC/CP/MEC/SDMA0;
3. SDMA1 pelo PSP tipo 10, logo depois do SDMA0;
4. GART da VMID 0 no GFXHUB (D2);
5. P7;
6. doorbells (abertura do BIF);
7. P1b;
8. bring-up do SDMA1 (P4.1);
9. GART da VMID 0 no MMHUB (**só depois do último uso do PSP**, porque o PSP é cliente do MMHUB e usa RAM física);
10. IH;
11. EOP do CP.

**Em regime:**
- desktop pelo DWM (UMD D3D11 → Vulkan/RADV), com menu e apps;
- paginação do VidMm pelo SDMA0, com PTE por `WRITE_LINEAR`, IB dividido no anel e `FlushTlb` no anel (4c.4);
- PTEs no SDMA1 (P4.2);
- conclusão por interrupção (TRAP do SDMA0 e EOP do CP pelo IH, MSI);
- TDR do SDMA0 recupera (provado em setembro; o do GFX VMID 10 também, na 201 e na 231).

**Layout da GART da VMID 0** (entradas de 4 KB; tabela na VRAM em `BC250_D2_GART_TABLE_VRAM_OFFSET 0x0FF02000`):

| Entradas | Dono |
|---|---|
| 0–31 | MEC (fences em VA `0x2200`…) |
| 32–33 | D2: anel e Work do SDMA0 |
| 64–80 | P7: anel e fences do GFX |
| 128–192 | IH (anel 256 KB em `0x80000`, writeback do WPTR em `0xC0000`) |
| 224–225 | SDMA1: anel `0xE0000` e Work `0xE1000` (WPTR 0x00, RPTR 0x08, FENCE 0x40, Go 0x80, Done 0x88) |

**Chaves de compilação** (`driver/wdk/bc250_active_miniport.c`, linhas ~60–160): `BC250_D2_SDMA_RING_IN_SYSTEM_MEMORY`, `BC250_P7_GFX_RING_IN_SYSTEM_MEMORY`, `BC250_P2_SDMA0_WPTR_BY_DOORBELL`, `BC250_P3_IH_ENABLED`, `BC250_P3B_MMHUB_GART_ENABLED`, `BC250_P6_MTYPE_UC_COHERENT`, `BC250_CP_EOP_IRQ`, `BC250_P4_SDMA1_ENABLED`, `BC250_P42_SDMA1_PTE_ROUTE`. Também `BC250_P1B_VRAM_CPU_PELA_DRAM` em `bc250_vram_cpu_path.h`. Todas estão em 1. Cada uma volta ao comportamento anterior com 0, e as auditorias exigem a forma.

---

## 3. Como o trabalho é feito (o ciclo)

**Papéis:** o **Daniel** executa tudo o que toca o hardware: bind, reboot, recuperação, CMOS. A IA lê, compila, testa offline, empacota, escreve o critério e analisa. Corrida é cara (Lei 9). Mesmo assim, o Daniel decidiu em 05/10: **paridade com o amdgpu, sem economizar corridas, uma diferença por corrida**.

**Ciclo de uma etapa:**
1. **Ler o Linux** em `inc/linux_ref/` (há `.c` do amdgpu: `sdma_v5_0.c`, `gfx_v10_0.c`, `gmc_v10_0.c`, `gfxhub_v2_0.c`, `mmhub_v2_0.c`, `navi10_ih.c`, `nv.c`…, mais os `*_offset.h`/`*_sh_mask.h`). Registrador novo só com proveniência: catálogo `docs/03` + arquivo `inc/linux_ref/PROVENANCE_*.txt` (Lei 7). Escrita em DCN é proibida fora da P9.
2. **Implementar** com a Lei 5 (preflight → snapshot → apply → readback → prova → rollback), atrás de uma chave `#define` e com log que **fala em toda saída** (as saídas mudas já custaram várias corridas).
3. **Testes offline** em `driver/tests/*_test.c`, ligados em `driver/wdk/build-contract.bat` (CRLF obrigatório; em LF o `cmd.exe` come caracteres). Rodar: `cmd /c driver\wdk\build-contract.bat` → EXIT=0.
4. **Portão** em `tools/audit-bc250mcdm-safety.py` (`audit_<etapa>`), e **verificar por mutação**: um script que estraga o fonte, roda a auditoria, exige `AUDIT: FAILED` e restaura o fonte (conferindo por hash). Mais de uma vez o próprio portão passou numa mutação; aí o portão estava errado.
5. **Subir o DriverVer** no INF fonte.
6. **Compilar o SYS de laboratório:** `powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup`.
7. **Empacotar:** `powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -ArmGfxWaveProbe -IAcceptSelectorWritesDuringTdr`. **O empacotador não compila, só copia**: confira no `.sys` assinado as strings novas de log.
8. **Auditar:** `tools\audit-bc250mcdm-lab-package.ps1` (PASS) e a auditoria de segurança (PASS). Se o empacotador falhar no meio (IOException), repita: o pacote fica inconsistente.
9. **Scripts da corrida:** copiar o par `tools/prepare-corrida236-p42-pte-no-sdma1.ps1` e `tools/EXECUTAR_CORRIDA236_P42_PTE_NO_SDMA1_600S.cmd`, trocando os três hashes (SYS, INF, CAT), o DriverVer e os textos. **Salve o `.cmd` com CRLF:** a 236 foi recusada uma vez por isso. O preflight a seco deve passar nos hashes e parar só na leitura da CMOS.
10. **Doc de critério** (Lei 10): pré-condição, desfechos A–E com assinatura e implicação separadas, "desfecho parcial não é desfecho". Atualizar `BASE_DE_AVANCO.md` (topo), `PLANO_MESTRE.md` §3.3 e o parágrafo de estado do `CLAUDE.md`.
11. **O Daniel roda:** boot frio → `.cmd` elevado (lê a CMOS sozinho) → uso normal (menu, Pesquisa, Gerenciador de Tarefas). Se travar, reset; a coleta automática roda no boot seguinte.
12. **Análise** (§4) e relato nos 5 pontos (avanço / sucessos / falhas / veredito / recuperação e pendências) + a tabela ✅🟡❌ do desktop mínimo.

**Ferramentas de texto:** heredoc do Bash corrompe barras invertidas; para C, PowerShell e Python use Write/Edit. Num here-string do PowerShell, `$nl` gruda no texto seguinte (`$nlint`): use `${nl}`.

---

## 4. Como analisar uma corrida (receita)

**Evidência** em `docs/evidence/<data>_<hora>-DESKTOP-HQAG6T4-lab-binding/`:
- `display-log-this-bind-RECUPERADO.txt`: log do KMD resgatado do log cumulativo `C:\BC250_DISPLAY_LOG.txt`. A cauda inclui o boot de recuperação, com o StartDevice `0xC0000182` do fusível;
- `paging-trace-this-bind-RECUPERADO.bin` e `paging-trace-decodificado.json`: rastro de 100 ms. **Corte no primeiro recuo do `writer_tick`**, porque os últimos registros são do boot seguinte;
- `umd-logs-pos-travamento/`: logs por processo da UMD e da RADV. O AppContainer (menu, Pesquisa) grava com prefixo do pacote;
- `reset-reason-pos-trava.txt`, `ramtest-pos-trava.txt` e `vram-cmos/`.

**Greps úteis:**
- `StartDevice LEAVE`;
- `[P3B_MMHUB] resultado`, `[P3_IH] ATIVO`, `[CP_EOP] ARMADO`, `[P4_SDMA1]`;
- `[P3_IH] regime` e `[P4_SDMA1] regime` (a cada 10 s);
- falhas: `IB_TRIPOD TIMEOUT`, `TDR_QUIESCE`, `GFXHUB_FAULT`, `Blt Timeout`, `DEVICE_REMOVED`, `INSIDE_IB`, `ActiveOpenAlloc] RECUSADO`;
- volume: `IB EXECUTADO`, `[Present #`, `PRIMARY_COPY #`, `[PT_GPU_SPLIT]`.

**Tempo:** QPC a 10 MHz. Use a diferença entre a primeira e a última `qpc=` do log, e cruze com o horário da última linha da UMD do `dwm.exe`.

**Armadilhas de leitura:**
- **o log de display é amostrado** (FIRST/PERIOD por classe): ausência de linha não prova ausência de evento, a não ser que a classe seja isenta. As isentas estão em `bc250_display_ddi.c`;
- linhas `BC250_HANG`/`PAGING_DDI` vêm em pares ENTER/LEAVE;
- desktop restaurado pelo BasicDisplay é recuperação, não sucesso;
- não contar operação como prova: "zero contra zero" é inconclusivo.

**Dump** (quando houver BugCheck):
```
& "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -z C:\Windows\MEMORY.DMP -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols" -c "!analyze -v; lm m bc250*; q"
```
O anel de log perdido pode ser extraído do dump com `tools\extrair-anel-do-dump.ps1`. Na trava dura **não há dump**.

`C:\Windows\Temp` pode não estar acessível na sessão da IA: peça ao Daniel uma cópia elevada.

---

## 5. Pendências pequenas (código), achadas na 236

1. **`[P4_SDMA1] resultado: ATIVO … nenhuma submissao vai para ele nesta etapa`** é texto da P4.1. Com `BC250_P42_SDMA1_PTE_ROUTE=1` a frase é falsa (Lei 1). Corrigir a mensagem em `Bc250ActiveP4Sdma1Start` (`bc250_active_miniport.c`) e, se algum portão casar a string, o portão junto.
2. **~21% das faixas de PTE são `descartadas` no envio** (2.985 de 14.014). Elas rodam corretamente dentro do IB do SDMA0. Acrescentar em `Bc250PagingSplitTake` (`bc250_mcdm_callbacks.c`) um contador por motivo: faixa parcial (cruza o fim do envio) ou faixa órfã (buffer nunca enviado). Falta saber se é o VidMm dividindo o paging buffer.
3. **Teste de RAM pós-trava saiu com 0 bytes** na 236 (na 219 e na 235 funcionou). Conferir `tools\rodar-teste-de-ram.ps1` e a tarefa registrada por `tools\registrar-coleta-automatica.ps1`.
4. **Teardowns nunca medidos em regime** desde a 233 (SDMA1, EOP, IH, GART do MMHUB): todas as corridas terminaram em trava. Só uma corrida que chegue aos 600 s os exercita.
5. **Divergência entre o rastro e a QPC:** o rastro da 236 marca `writer_tick` 332 (~33 s) contra 41,5 s de QPC no log. Pode ser a definição do tique; vale conferir antes de usar o rastro para tempo absoluto.

---

## 6. Para onde ir (proposta; a decisão é do Daniel)

**A pergunta que importa:** o que o Linux faz, ou deixa de fazer, que impede a GDDR6/fabric de parar? Os registradores do inventário do doc 593 §4 estão quase todos alinhados.

**Restam da paridade de hardware:**
- **P3.3: RPTR do IH por doorbell.** Barata, mas improvável: o IH nunca deu overflow e a conclusão por interrupção já está em regime.
- **P5: VM de 4 níveis, 48 bits** (nós: 2 níveis, 34 bits). É a maior diferença estrutural que resta, e cara: muda o encoder de PTE, `PAGE_TABLE_DEPTH`, o journal de GPUVA, o `SetRootPageTable` e a RADV (VA bits).

**Hipóteses ainda não testadas (H), da mais barata para a mais cara:**
1. **`RESET_REASON` de controle:** ler o `S5_RESET_STATUS` depois de um reboot limpo, sem nosso driver, e depois de um reset pelo botão sem trava. Se o bit 30 (paridade) aparecer só depois das travas, a trava tem assinatura de erro de memória ou fabric, e isso muda a busca. **Custo: zero corridas de driver.**
2. **Linha de base sem uso:** a 229 passou 600 s **sem abrir o menu**, e a 226 e a 222 também passaram. Repetir 600 s parado no desktop (n=2) com o candidato atual separaria "carga de trabalho" de "tempo ligado". Se a trava só vier com uso, a bissecção passa a ser por carga (qual app, qual tipo de alocação).
3. **Política de paginação (não registradores):** o VidMm pede FILL e despejos de tela cheia (11 MB) de uma vez, enquanto o TTM do Linux trabalha em pedaços menores e com outra ordem. As travas caem perto dessas rajadas (docs 560, 577, 582; na 236, 344 `FlushTlb` e 10 FILL no último segundo). Um ensaio seria limitar o tamanho por IB ou espaçar as rajadas. Ainda não existe inventário com a régua do doc 593.
4. **Tráfego da CPU na mesma GDDR6:** os logs da UMD gravam muito em disco (dezenas de MB por corrida na 235). Na BC-250, CPU e GPU disputam a mesma memória. A P1 tirou a instrumentação pela BAR, mas não a pressão de memória da CPU em geral. Um ensaio barato: a UMD com log mínimo.
5. **P5**, se as anteriores não discriminarem.

**Ensaios que não adiantam** (já esgotados): KD/Ctrl+Break, dump e ETW na trava (memória `bc250-kd-nao-para-a-trava-dura`); ler a última linha do log como causa; VRAM maior (proibido por decisão).

---

## 7. Próximos passos concretos

| # | Passo | Quem |
|---:|---|---|
| 1 | Recuperação (`BC250_RECOVERY.cmd`, `inspect-boot-fuse.ps1 -Reset`, boot frio) | Daniel |
| 2 | `git diff` dos arquivos alterados fora da sessão (`bc250_sdma1_bringup.c/.h`); suíte e auditoria | IA |
| 3 | Pendências 1–3 do §5 (mensagem, contador de descartadas, teste de RAM): uma faixa offline só, sem corrida | IA |
| 4 | Hipótese 1 do §6 (`RESET_REASON` de controle), com `tools\vram-cmos\bc250-vram.ps1 -Ler` depois de reboot limpo | Daniel, com roteiro da IA |
| 5 | Apresentar ao Daniel a escolha entre: linha de base sem uso (n=2), política de paginação, log mínimo da UMD, P3.3, P5 | IA propõe, Daniel decide |

---

## 8. Memórias e lições que custaram corridas

- **O gate congelava a mentira**, pelo menos cinco vezes: um teste ou portão exigia o comportamento errado. Ao corrigir, inverta o teste e verifique por mutação.
- **Saída muda de DDI** custou várias corridas. Toda saída de erro registra o motivo, inclusive a de payload inválido.
- **Coleta em snapshot perde o dado decisivo:** colete no fim da janela física (`-FINAL`) e pelo resgate da cauda.
- **Campo escrito num ramo só de `#if`:** aconteceu três vezes (A13, B4, ContextAbi). Inicialize por fonte única.
- **Empilhar duas mudanças num candidato** já custou corrida. Uma variável por candidato.
- **O MMHUB tem clientes com RAM física** (PSP): ligar a GART dele depois do último uso do PSP.
- **`DxgkCbMapMemory` só mapeia a BAR:** a DRAM usa `MmMapIoSpaceEx` (`Bc250VramCpuMapMemory`).
- **Doorbell exige `RCC_DOORBELL_APER_EN`** (abertura do BIF).
- **AGP refutada:** o Linux usa GART na BC-250.
- **A RADV trata o gfx1013 como GFX10.3** (winsys sem `ip_discovery_version`). No build da RADV, o ninja recompila só uma parte: depois de mudar um header, apague os `.obj` relevantes.
- **Os logs de AppContainer** (menu, Pesquisa) vão para subpastas próprias.
- **O firmware do SDMA1** veio de `bc250-win/AMD-BC-250-Windows-Driver/firmware`. Procure lá antes de declarar algo ausente.

Todas estão no índice de memória do projeto (`MEMORY.md` do Claude Code) e na BASE_DE_AVANCO.

**NÃO VALIDADO EM HARDWARE:**
- a causa da trava dura;
- os teardowns da P3, da P4 e do EOP em regime;
- a espera `RETRY 0xFFF` sob Done atrasado de verdade;
- tudo o que está em §5 e §6.
