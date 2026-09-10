# BASE DE AVANÇO — AMD BC-250 → WDDM com jogos no Windows

**Documento base do projeto.** Toda decisão de o-que-fazer-agora sai daqui.
**Passagem de bastão e auditorias de 09/09:** [docs/229 — Nó 1 PM4/MEC no Silício](docs/229_HANDOFF_2026-09-09_NODE1_COMPUTE_FECHADO_NO_SILICIO.md) · [docs/230 — Auditoria Offline do Estado WDDM](docs/230_AUDITORIA_ESTADO_WDDM_2026-09-09.md) · [docs/231 — Conferência da Corrida 21:20:13](docs/231_CONFERENCIA_CORRIDA_21-20_2026-09-09.md).
**Checklist operacional consolidado:**
[`docs/189_CHECKLIST_ATE_JOGOS_BC250.md`](docs/189_CHECKLIST_ATE_JOGOS_BC250.md).
**Data-base:** 2026-09-09 · **Última corrida física:** laboratório 3.122.1.0, 21:20:13 (recorte delimitado: 17 CommitVidPn, 2 SetVidPnSourceAddress, 102 INDIRECTs, 0 Blt, rollback bem-sucedido às 21:23:17 restaurando BasicDisplay Código 0). Candidato atual pós-auditoria offline: 3.122.1.0 com correções de segurança no rollback (gates -Execute/-IAcceptDisplayInterruptionRisk, remoção de /force inválido no Win10), expansão de objetos do KMD para 4096 com reciclagem de slots mortos (eliminando 0xC000009A), e fail-closed honesto na UMD (Type 130 E_NOTIMPL enquanto NATIVE_READY=0, honestidade DXGI). Barreira restante: backend gráfico real (rasterizador/PM4 Draw/compilador para DWM).

> **09/09 21:20:13 (P0) — Corrida 3.122.1.0 (Auditoria e Conferência do Recorte — docs/230 e 231):**
> Ensaio físico do pacote lab 3.122.1.0 auditado com recorte estrito (`start_offset=6776778`, 668.635 bytes a partir da linha 94574).
> A mineração delimitada registrou: 17 menções a `CommitVidPn` (9 entradas e 8 mensagens de transição para VidPn vazio); 2 `SetVidPnSourceAddress` configurando o scanout; 8 chamadas do OS de `[ControlInterrupt] VSYNC Enable=0/1` (não confundir com interrupções físicas de VBlank); 6 linhas de Present (todas referindo-se ao mesmo `Present #2`, com zero blits SDMA); `Present #2` foi rejeitado por contrato de fonte (`REJECT source contract: status=9 (ALLOC_NOT_RESIDENT), SlotSeg=0`); 102 execuções explícitas de `IB EXECUTADO (INDIRECT)` no recorte, das quais 47 fecharam `TRIPE_PROVADO`.
> **Contraprovas e achados críticos da corrida:**
> 1. Submissão #99 falhou na perna de dados: `IB_TRIPE_VEREDITO #99: DADOS_DIVERGEM` (cinco canários retiveram poison `DEADAFFE...` no destino `0x450ABC000`, necessitando rastreamento de alocação/GPUVA).
> 2. Exaustão de tabela de objetos no KMD: 56 linhas `[McdmCreateAllocation] Result=0xC000009A` e 8 recusas `CreateDevice/CreateProcess` pelo limite estático de 1024 slots sem reciclagem (corrigido offline: limite 4096 + reciclagem de slots mortos).
> 3. UMD: chamadas `DrawIndexed` rejeitadas com `E_NOTIMPL` por ausência de rasterizador/PM4 Draw.
> Às 21:23:17, o watchdog executou rollback gracioso sem TDR ou bugcheck nesta janela, restaurando `BasicDisplay` em Código 0. **Desktop acelerado e jogos: NÃO VALIDADO EM HARDWARE.**
> [Auditoria 230](docs/230_AUDITORIA_ESTADO_WDDM_2026-09-09.md) · [Conferência 231](docs/231_CONFERENCIA_CORRIDA_21-20_2026-09-09.md).
>
> **09/09 20:56:55 (P0) — Corrida 3.121.1.0 (BugCheck 0x119 no dxgmms2 e Cursor de Paginação):**
> O ensaio físico sofreu BugCheck 0x119 (`Arg1=2, Arg2=0xC0000185`) no `dxgmms2` durante operação de paginação do VidMm.
> **Causa raiz:** o dxgkrnl gerou um IB de paginação com salto inicial de cursor de 32 bytes além da base física da alocação. A verificação `IB_PA_MATCH` comparava estritamente `AllocBase` com `IbPhysicalAddress`, ignorando o offset interno do cursor, gerando um falso mismatch. A tentativa de fallback leu endereço desatualizado/lixo, corrompendo a paginação e derrubando o VidMm.
> **Correção offline:** implementado `Bc250PagingConsumeIbCpuPhysical` para consumir paginação via CPU física direta quando aplicável, com ajuste de offset de cursor na validação de `IB_PA_MATCH`.
>
> **09/09 19:56:09 (P0) — Corrida 3.119.1.0 (Nó 1 PM4 e Tripé UMD Fechados no Silício):**
> O harness `B7_UMD_NODE1_TRIPOD` submeteu IB sob VMID 9 pelo WDDM Winsys nativo da UMD.
> O Command Processor (MEC2/CPF) buscou o anel sem stalls de RCIU (`CpcStalled=0`), drenou completamente o anel (`RPTR==WPTR==12`), sinalizou a fence monitorada (`3156545281` / `0xBC250F01`) e gravou na VRAM os 5 canários de dados: `Values=BC250C01,BC250C02,BC250C03,BC250C04,BC250C05` (`hr=0x00000000`).
> **Ressalva de escopo auditada (doc 230):** o IB continha comandos PM4 `WRITE_DATA` e `RELEASE_MEM`; **não houve dispatch de shader** (`DISPATCH_DIRECT`) nem compilação de código de shader pela rota UMD/WDDM. Comandos PM4 e efeito em memória pelo Nó 1 estão **VALIDADOS EM HARDWARE**; shader compilado/dispatch na UMD permanece **NÃO VALIDADO EM HARDWARE**.
> No Nó 0 (SDMA), 33 submissões foram aceitas, e a corrida anterior das 19:48 eliminou o 0x119 no fill de 11 MB da primária (`TRIPE_PROVADO` com 5 canários em 128 polls).
> A tela permaneceu preta porque o DWM exige backend D3D10/11 na UMD (que ainda retorna `E_NOTIMPL`).
> 74 s após o fim do teste, o watchdog de confirmação visual expirou e executou rollback forçado via `pnputil /uninstall /force`, gerando BugCheck 0x119 no `dxgmms2` por remoção surpresa do driver com VSYNC ativo. O fusível anti-boot-loop funcionou perfeitamente no reboot (`State=2 TRIPPED`), impedindo boot loop.
> **Tripé do Nó 1 (PM4 WRITE_DATA/RELEASE_MEM) VALIDADO EM HARDWARE; Próximo bloqueador: Frente 2 (D3D10/11 UMD com rasterizador real).**
> [Handoff 229](docs/229_HANDOFF_2026-09-09_NODE1_COMPUTE_FECHADO_NO_SILICIO.md) · [Auditoria 230](docs/230_AUDITORIA_ESTADO_WDDM_2026-09-09.md).

> **09/09 18:25:22 (P0) — Corrida 3.117.1.0:** o silício registrou grande avanço microarquitetural:
> `WPTR` avançou de 0 para 12, doorbell kick funcionou (`QUEUE_IDLE: 1 -> 0`), e o Command Processor
> Fetcher (CPF) buscou o anel e decodificou o descritor PM4 `IT_INDIRECT_BUFFER` (`C0023F00, 00011000, 00000000, 09800011`),
> engajando o `HQD_IB_FETCHER` (`CP_CPF_BUSY_STAT = 0x68460000`, bit 18 `HQD_IB_FETCHER_BUSY = 1`) sob VMID 9.
> Contudo, o fetcher congelou aguardando leitura da malha interna (`CP_CPC_STALLED_STAT1 = 0x00040000`, `MEC2_WAIT_ON_RCIU_READ`).
> `RPTR` estagnou em 0, fence 18 e canários não foram produzidos, e após 2 s o TDR disparou. O DWM falhou por `E_NOTIMPL`
> na UMD (D3D10/11) e o watchdog restaurou BasicDisplay Código 0.
> **Engajamento do HQD_IB_FETCHER pelo CPF VALIDADO EM HARDWARE; tripé Node 1 NÃO VALIDADO.**
> [Handoff 227](docs/227_PASSAGEM_DE_BASTAO_2026-09-09_WDDM_GPU_3117.md).

> **09/09 17:47:40 (P0) — Corrida 3.116.1.0:** a reconciliação corrigiu no silício a
> geometria do anel: com `Submitted=Retired=17`, o par físico drenado `RPTR=0/WPTR=0`
> substituiu o cursor lógico antigo 204 e o descritor foi publicado em `WPTR=12`, não 216.
> O consumo, contudo, falhou: submit `17->18`, aposentado `17->17`, `RPTR=0`, fence zero e
> nenhum canário. Antes do doorbell, `CpcStalled=0x00040000` já havia reaparecido; depois,
> permaneceu igual, com `PQ_EMPTY=0` e `FaultStatus=0`. Assim, cursor incoerente não era a
> causa suficiente, e a ausência do stall na 3.115 era transitória. Watchdog preservou
> 76.526 linhas (`85E58D12...`), removeu `oem35.inf` e restaurou BasicDisplay Código 0.
> **Reconciliação VALIDADA EM HARDWARE; tripé Node 1 NÃO VALIDADO. Não repetir 3.116.1.0.**
> [Análise](docs/evidence/2026-09-09_17-47-40-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md).

> **09/09 16:55:02 (P0) — Corrida 3.115.1.0:** a política do firmware foi preservada no
> silício (`SPI=0xFFFF`, `RLC=0x3`, sem escritas `0x1F`) e o stall
> `MEC2_WAIT_ON_RCIU_READ` não reapareceu: `CpcStalled=0` antes/depois do doorbell e no TDR.
> Porém o Item 1 não fechou: submit aceito `17->18`, aposentado `17->17`, `WPTR 204->216`,
> `RPTR 204->204`, fence/canários ausentes. Na leitura física direta:
> `PRE RPTR=0/WPTR=0`, `POST RPTR=0/WPTR=216`; `PqControl=0xD030090D` já tem `PQ_EMPTY=0`.
> Logo, o 3.115 corrigiu o estado especulativo e retirou o stall, mas não produziu consumo.
> Novo discriminante: cursor lógico parte de 204 enquanto o par físico parte de zero.
> Watchdog arquivou 74.236 linhas (`C06205A2...`), removeu `oem35.inf` e restaurou
> BasicDisplay Código 0. **Não repetir 3.115.1.0; Node 1 NÃO VALIDADO EM HARDWARE.**
> [Análise](docs/evidence/2026-09-09_16-55-02-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md).

> **09/09 — candidato 3.116.1.0 antes da corrida:** criado contrato fail-closed para reconciliar o
> cursor lógico do anel com `RPTR/WPTR` físicos antes do planejamento. Só há rebase se
> `Submitted==Retired` e o hardware estiver drenado (`RPTR==WPTR`); nenhuma rotina escreve
> RPTR. No caso medido `Logical=204, Physical=0/0`, o próximo descritor começa em 0 e publica
> 12, preservando a sequência de fence 18. Físico ocupado, fence pendente, falha de leitura
> ou restauração recusam o submit. Seis casos de contrato e quatro mutações passaram;
> `build-contract.bat EXIT=0`, auditorias PASS. Pacote assinado: SYS `E145A7DD...`, INF
> `7CB3DC16...`, CAT `0279E1D1...`. O ensaio posterior validou o rebase para zero, mas não
> o consumo do descritor.
> [Handoff 226](docs/226_PASSAGEM_DE_BASTAO_2026-09-09_WDDM_GPU_3116.md).

> **09/09 15:47:07 (P0) — Corrida 3.114.1.0:** o harness obteve um submit Node 1 aceito
> (`P1SubmitAccepted 17->18`) e o `WPTR` avançou `204->216`, mas `P1RetiredTotal` ficou em
> 17 e o `RPTR` publicado ficou em 204. A fotografia direta da fila pendente mostrou
> `RPTR=0`, `WPTR=216`, fence observada 0 e `VMID=0x900`; os canários não foram executados.
> No último submit, `CP_CPC_STALLED_STAT1=0x00040000` já estava presente antes do doorbell.
> O seletor pedido 8 com readback 0, sozinho, **não prova banco errado**: a transação de HQD
> e o readback de `CP_HQD_VMID=0x900` alcançaram o banco selecionado. O watchdog removeu
> `oem35.inf`, restaurou BasicDisplay em Código 0 e criou
> `C:\BC250_REQUIRE_CLEAN_BOOT.json`. **Node 1 NÃO VALIDADO EM HARDWARE.**
>
> **09/09 — candidato fonte 3.115.0.0 OFFLINE:** removidas do submit Node 1 as escritas
> especulativas `RLC_PG_CNTL=0`, `SPI_PG_STATIC_WGP_MASK=0x1F` e
> `RLC_PG_ALWAYS_ON_WGP_MASK=0x1F`. O valor `0x1F` pertence ao patch opcional local de
> desbloqueio de 40 CUs, combinado com alteração de `CC_GC_SHADER_ARRAY_CONFIG`; não é
> política comprovada para o alvo de 24 CUs. Os três registradores continuam legíveis para
> telemetria, mas saíram da allowlist de escrita. Teste do transporte, auditoria estática e
> três mutações passam; `build-contract.bat` **EXIT=0**. Fonte versionada como 3.115.0.0,
> pacote lab `3.115.1.0` assinado e auditado (SYS
> `6E0F0794401C7ABBA9CA12A0A6F83FA8920CCC9F1A8C0E5106B8EBA45291C652`), ainda sem bind.
> O preflight bloqueia corretamente até haver boot posterior ao rollback das 15:50:10.
> **NÃO VALIDADO EM HARDWARE.**

> **09/09 11:36:53 (P0) — Corrida 3.109.1.0 (Telemetria TDR do MEC):** o pacote 3.109.1.0 foi executado no silício com hashes conferidos. O 1º submit Node 1 avançou `WPTR` de 204 para 216 e publicou o descritor `PACKET3_INDIRECT_BUFFER` (`0x3F`, 17 DW, `VA=0x11000`, VMID 9). O tripé Node 1 falhou fechado: submetido 18, aposentado 17, `LastRptrMmio` no probe permaneceu 204, sem fence nem canários (harness `exit=1`, `hr=0x80004005`). No TDR às 11:37:13, a nova telemetria capturou no silício: `FaultStatus=00000000`, `FaultAddr=00000000:00000000`, `CpfStalled=00000000`, `CpcStalled=00040000` (`MEC2_WAIT_ON_RCIU_READ`), `CpStalled1=00000C00`, `Mec2Hdr=C0000A00`, `Mec2Pntr=000017FB`, `RPTR=0`, `WPTR=216`. **Diagnóstico:** o microengine MEC2 congelou aguardando leitura da RCIU (`CP_CPC_STALLED_STAT1 = 0x00040000`). A mineração de controle (linha 52538) comprovou que `Mec2Pntr=0x17FB` e `Mec2Hdr=C0000A00` são o baseline de repouso do MEC2 e não pertencem exclusivamente a este pacote; o delta real foi a transição de `CpcStalled` de `0` para `0x40000`. A GCVM teve probabilidade reduzida (sem fault pontual nem espera CPF/UTCL1), mas não foi totalmente eliminada devido a `*_FAULT_ENABLE_DEFAULT` e ausência de máscara de validade na captura. Anomalia de RPTR detectada: `P1LastRptrMmio=204` no registro de probe vs `RPTR=0` na amostragem direta MMIO. O TDR recuperou SDMA0 (`SYNC_NOP TRIPOD PASS` em 2 polls) e o watchdog restaurou BasicDisplay às 11:39:58 com `FUSE CLEAN (0)`. **Node 1 NÃO VALIDADO EM HARDWARE.** [Evidência da corrida](docs/evidence/2026-09-09_11-36-53-DESKTOP-HQAG6T4-lab-binding).

> **09/09 08:26 (P0):** restauração dos 26 registradores da MMU concluída, exercitando a correção do ciclo de vida do MMIO. Cinco tripés SDMA completos e 13 transações TLB status=0. Node1 continua WPTR=216/RPTR=204; caminhada CPU resolve IB para 0x451FCC000, mas não prova busca/execução GPU. TDR às 08:26:23 e rollback às 08:29:09, BasicDisplay/Código 0 confirmado. A captura MEC_PENDING_ARCHIVE não foi produzida; não inferir falha do timer dessa ausência. **Node1 NÃO VALIDADO EM HARDWARE.** [Análise da corrida](docs/evidence/2026-09-09_08-26-06-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md).

> **09/09, candidato 3.106.1.0 OFFLINE:** coleta do primeiro polling antes de possível falha do retirement; fotografia independente no StopDevice; prefixo físico do IB observado pela CPU e arquivado. Build/testes, nove mutações e auditorias PASS; pacote assinado, não instalado. Isso corrige lacunas de diagnóstico, não prova a causa do hang nem implementa D3D10/11. **NÃO VALIDADO EM HARDWARE.** [Handoff 215](docs/215_HANDOFF_2026-09-09_NODE1_PRIMEIRO_POLL.md). Teto de um ensaio Node1 com critérios de abandono documentados.

**Histórico — candidato 3.105.1.0 antes da corrida:**

> **23:15 (P0):** cinco tripés completos SDMA; 11 transações TLB status=0, zero status=8. VMID9 instalado, mas Node1 continuou sem conclusão (WPTR=216/RPTR=204; fence e dados sem prova). TDR às 23:16:12; watchdog restaurou BasicDisplay/Código 0 às 23:18:53, confirmado por CIM.
> **Correções offline:** serialização do caminho de coerência MEC com a mesma TlbLock; MMIO compartilhado preservado até a restauração da MMU; MEC desligado antes de restaurar VMID9; captura de fila pendente arquivada no StopDevice. A causa exata da não conclusão do IB Node1 permanece aberta. TDR atual prova SDMA, não recuperação do MEC. **NÃO VALIDADO EM HARDWARE.** [Handoff 214](docs/214_HANDOFF_2026-09-08_MEC_TLB_MMIO.md).

**Histórico — corrida 15:43 e candidato 3.95.1.0:**
A corrida de laboratório de 08/09 15:43 validou a Faixa B7.1 no silício (P0): MapAllocation/MakeResident aceitaram `E_PENDING` (`hr=0x8000000A`), command buffer (64 KB, `VA=0x11000`) e destino (4 KB, `VA=0x21000`) tornados residentes, cerca monitorada criada e `pfnSubmitCommandCb` chamado com `S_OK`. O KMD recebeu o 1º submit Node 1 da história (`McdmSubmitCommandVirtual` Node=1, FenceId=1, DmaSize=68; gate abriu e `WPTR` avançou de 204 para 216 dwords no silício). O tripé falhou fechado: `RPTR` permaneceu em 204, cerca retornou `UINT64_MAX` e canários em VRAM zerados (harness `exit=1`). A mineração de log comprovou disputa física do registrador VMID 8 (`PT_BASE`): a thread de Present do Node 0 trocou o `PT_BASE` para a tabela do DWM (`0x450A8C000`) logo após o submit do Node 1, retirando a raiz do harness sob os pés do MEC. A hipótese de TDR/DEVICE_REMOVED foi refutada (zero ocorrências de ResetFromTimeout/RestartFromTimeout/QueryEngineStatus). Telemetria `VMID8_DISPUTE_PROBE` adicionada no Node 1. Handoff: `docs/206`. Node 1, desktop acelerado e jogos: **NÃO VALIDADOS EM HARDWARE.**


> **08/09, continuidade OFFLINE — VMID9 dedicado ao MEC:** candidato lab 3.95.1.0
> assinado e auditado, sem bind. Node0 conserva VMID8; Node1 instala a raiz em
> VMID9 antes da publicação, com snapshot/readback/ACK/restore e recusa de troca
> enquanto houver fence pendente. Um lote compute pendente por vez neste candidato.
> Build completo e auditorias PASS; gate novo e mutações verificadas. A troca de
> raiz na corrida 15:43 é observada, mas o page fault e o instante da busca MEC
> atribuídos a ela permanecem hipótese, não medição. Nenhum item físico foi fechado.
> **NÃO VALIDADO EM HARDWARE.** Handoff: [docs/208](docs/208_HANDOFF_2026-09-08_VMID9_ISOLADO_OFFLINE.md).

> **Toolchain canônica desde 07/09/2026:** Visual Studio 2026 Community 18.9.1,
> MSVC 19.51.36256.0 e SDK/WDK 10.0.28000.0 (pacotes 10.0.28000.2526). KMD, UMD,
> laboratório, RADV gate e suítes offline passaram; os manifests dos pacotes recusam
> mistura com a toolchain anterior. Isso não mede efeito no silício:
> **NÃO VALIDADO EM HARDWARE**. Registro completo em
> [`docs/173_TOOLCHAIN_VS2026_WDK28000.md`](docs/173_TOOLCHAIN_VS2026_WDK28000.md).

> **Correção e candidato de 07/09 15:05:** o ensaio isolado das 13:59 provou um IB
> SDMA sob VMID8 com `RPTR==WPTR`, fence física em 11 ms e quatro dwords conferidos;
> a PDE corrompida reteve o poison e produziu `MAP_ERR=1` na página `0x201000`.
> Isso prova o efeito causal do **conjunto** de programação global, não qual dos 26
> registradores isoladamente é a causa. Já a corrida WDDM das 14:32 falhou em snapshot
> da MMU global (`status=2`) e seu log delimitado contém zero paging op, zero submit e
> zero commit. O fail-open foi corrigido: `StartDevice` e `SetRootPageTable` agora
> recusam prosseguir; o rollback e o restore são verificados; a dummy page é reservada
> pelo allocator e excluída do segmento publicado ao VidMm. Suíte e auditoria de
> integridade passaram; efeito deste candidato no silício: **NÃO VALIDADO EM HARDWARE**.
> Handoff: [`docs/176_HANDOFF_2026-09-07_MMU_FAIL_CLOSED.md`](docs/176_HANDOFF_2026-09-07_MMU_FAIL_CLOSED.md).

> **Corrida física 07/09 15:16:** o novo fail-closed funcionou como desenhado, mas
> recusou o `StartDevice` antes de programar a MMU: `vramStatus=6`
> (`BC250_VRAM_NO_SPACE`). O allocator escolhido pertencia ao bootstrap ainda não staged
> e estava zerado. Resultado medido: Código 43, zero MMU global aplicada; o tripé de boot
> SDMA passou, e o watchdog removeu `oem34.inf` e restaurou BasicDisplay Código 0.
> Corrigido offline com allocator próprio, reserva explícita do scanout e dummy top-down;
> o teste com a geometria medida coloca a dummy em `0x0FFFF000`. Novo pacote ativo
> assinado/auditado: fonte `9A80C7AA...`, assinado `97C96152...`.
> Este pacote seguinte ainda é **NÃO VALIDADO EM HARDWARE**. Handoff:
> [`docs/177_HANDOFF_2026-09-07_15-16_GLOBAL_MMU_ALLOCATOR.md`](docs/177_HANDOFF_2026-09-07_15-16_GLOBAL_MMU_ALLOCATOR.md).

> **Corrida física 07/09 15:49:** a correção do allocator ultrapassou a reserva da dummy,
> mas o snapshot parou no primeiro registrador: `status=2 step=3 reg=0x0A610`. A causa é
> determinística no código: `Bc250MmioTopologyRead32` recusava `GCMC_VM_AGP_BASE` porque
> os 26 offsets da transação global não estavam na allowlist do transporte; a leitura não
> chegou ao MMIO. Resultado medido: Código 43. Embora Daniel tenha visto imagem, o script
> recusou a confirmação porque o dispositivo não estava em Código 0; isso não é prova de
> sucesso. O watchdog restaurou BasicDisplay Código 0. Corrigido offline com helper fechado
> compartilhado pelas allowlists de leitura/escrita, teste dos 26 offsets e vizinho negativo.
> Pacote atual: fonte `F0375F94...`, assinado `069A7330...`; **NÃO VALIDADO EM HARDWARE**.
> Handoff: [`docs/178_HANDOFF_2026-09-07_15-49_MMIO_ALLOWLIST.md`](docs/178_HANDOFF_2026-09-07_15-49_MMIO_ALLOWLIST.md).

> **Corrida física 07/09 16:19 — avanço e reprovação:** `bc250mcdm` ficou em Código 0;
> `Global GFXHUB MMU initialized: Regs=26`, ACK físico de TLB, journal até Generation=9,
> um `CommitVidPn`, um `SetVidPnSourceAddress` e 12 Present. A superfície primária em
> `0xAB0000` permaneceu zero e os blits fecharam no máximo duas pernas; zero sobre zero não
> prova pixels. Quatro processos DWM falharam; dois dumps de ~202 MB têm
> `RaiseFailFastException(e0464645, 887A0004=DXGI_ERROR_UNSUPPORTED)`, enquanto a UMD
> registra `OpenAdapter10/10_2 -> E_NOTIMPL`. Logo a MMU/allowlist atravessou o gate, mas o
> desktop é bloqueado pelo backend D3D10/11 ausente. O watchdog voltou a BasicDisplay
> Código 0. Não rebinding do mesmo pacote. Handoff completo:
> [`docs/179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md`](docs/179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md).

> **Faixa A13, 07/09, OFFLINE e sem corrida — higiene de prova.** A mineração do log da
> 16:19 mostrou que **14 `IB EXECUTADO` = 2 `PLANO A DIRECT FILL` + 12 `PLANO A SYNC NOP`,
> com zero `INDIRECT`**: a frase de execução de IB saía nos três ramos e afirmava que a GPU
> leu um DMA buffer que ela nunca leu. Corrigido — a frase agora é exclusiva do `INDIRECT`.
> **Achado novo:** `TRIPÉ NO SILÍCIO COMPROVADO … WPTR=68 RPTR=52` não era telemetria errada
> e sim **gate errado** — a perna 1 (`RPTR==WPTR`) não entrava na decisão do bringup de SDMA,
> e um timeout de 2000 polls passava. O veredito passou a sair de `Bc250ClassifySdmaTripod`,
> com a fonte do RPTR nomeada no log e anel nunca armado saindo `INDETERMINADO`. Terceiro
> item: o tunable `SdmaIndirectOnly` (default 0, fail-closed) desliga a precedência dos
> `PLANO A` para que o `INDIRECT` seja exercitável. `build-contract.bat` `EXIT=0`, três
> auditorias PASS e os dois portões novos verificados **por mutação**. **NÃO VALIDADO EM
> HARDWARE.** Handoff:
> [`docs/180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md`](docs/180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md).

> **Corrida física 07/09 17:13 — o IB WDDM real executou sob VMID8.** Com
> `SdmaIndirectOnly=1` lido (`present=1`), as 14 submissões do Node 0 saíram como **10
> `INDIRECT` + 4 `RECUSADO`**: dez `IB EXECUTADO (INDIRECT)` de DMA buffers de 64 bytes
> (`VA=0x22F000..0x22F1C0`), cada um com `RPTR==WPTR` e fence `0x1002..0x1014` sobre poison,
> ~111 polls, zero timeout e `FAULT_STATUS=0`. A dívida central — *"o `INDIRECT` é o único
> que não funciona"* — está **refutada como estava formulada**. O risco declarado não
> ocorreu: as 4 recusas devolveram `STATUS_IO_DEVICE_ERROR` sem bugcheck, Código 0 até o
> rollback. **Ressalva que manda no veredito:** `IB_TRIPOD PASS` era rótulo de **duas**
> pernas — nenhum byte do destino era conferido —, então **o B1 não fecha com isso**.
> Display: 5 `CommitVidPn`, 20 `Present` (10 `SDMA=YES`) e frames vazios
> (`BlankStreak=3`); 7 falhas do DWM. Faixa **A14** (offline) deu perna 3 ao `INDIRECT` por
> canários, aparou a confirmação do binder e fez `DestroyProcess` liberar as faixas de GPUVA
> do processo — o `status=10 OWNER_MISMATCH` dos `Present #16..#20` era faixa viva de um
> `dwm.exe` **morto**. Pacote `3.83.0.0` assinado e **não instalado**. Handoff:
> [`docs/181_HANDOFF_2026-09-07_17-13_INDIRECT_EXECUTOU.md`](docs/181_HANDOFF_2026-09-07_17-13_INDIRECT_EXECUTOU.md).

> **Corrida física 07/09 17:43 — B1 FECHADO NO SILÍCIO, e um `0x119` por aritmética nossa.**
> Duas submissões saíram com `IB_TRIPE_VEREDITO: TRIPE_PROVADO`: as **três** pernas sobre o
> DMA buffer real do `dxgkrnl` — `RPTR==WPTR` nas duas leituras, fence da GPU sobre poison, e
> **cinco canários não-nulos plantados antes do disparo, todos substituídos**
> (`Divergentes=0`). Zero sobre poison é conclusivo: **o IB de paginação executa sob VMID8 e
> produz o efeito declarado**. Depois, `FenceId=24` deu `IB_TRIPOD TIMEOUT` com
> `TargetWPTR=516`, `RPTR=4612` e a fence **batendo** — `4612 = 4096 + 516` num anel de 4.096
> bytes. A comparação de RPTR era **crua**; o timeout devolveu `STATUS_IO_DEVICE_ERROR` e o
> VidMm derrubou a máquina (`0x119`, Arg1=2, Arg2=`0xC0000185`). A aritmética já existia no
> arquivo e este caminho não a usava — e a auditoria **exigia** a comparação crua no blt.
> Faixa **A15** corrigiu os três sítios, o discriminante `anel_drenado` e o gate. O fusível
> anti-boot-loop recusou o `StartDevice` no reboot: **não há loop**, o Código 43 atual é o
> fusível. Pacote `3.84.0.0` assinado e não instalado. Handoff:
> [`docs/182_HANDOFF_2026-09-07_17-43_B1_FECHADO_E_0x119.md`](docs/182_HANDOFF_2026-09-07_17-43_B1_FECHADO_E_0x119.md).

> **Corrida física 07/09 18:16 e faixas B3/B4 (offline).** A corrida fez **32 submissões
> `INDIRECT` com zero timeout e zero bugcheck**, Código 0 até o rollback, e **reproduziu o
> B1** (2 × `TRIPE_PROVADO`, 5 canários, `Divergentes=0`). Zero `REJECT source contract` e
> zero `SpaceResolveAddress failed` com 4 `[DestroyProcess] GPUVA` — a classe
> `OWNER_MISMATCH` da A14.3 desapareceu com a mesma rotatividade de `dwm.exe` que a
> produzia. Ressalvas: o `RPTR` casou com o alvo **cru** nas submissões pós-wrap, então a
> assinatura de falha da 17:43 não reapareceu (a A15 não foi exercitada no modo que importa);
> e a imagem que voltou foi o **rollback**, não o nosso caminho de pixel (30 `Present`,
> `BlankStreak=4`). **B3:** os `PLANO A` saíram da precedência — o default do Node 0 é
> **somente `INDIRECT`**, com `SdmaPlanoAFallback` (default 0) apenas para bissecção e a
> string `SdmaIndirectOnly` proibida por gate. **B4:** a recusa do Node 1 era **muda** e o
> gate **não podia abrir** no pacote de produção (`GpuvaLabPromotion` lido só no `#else` de
> bring-up); agora a recusa nomeia o `BlockMask` e o tunable é lido em produção, ainda com
> default 0. Pacote `3.85.0.0` assinado e não instalado. Handoff:
> [`docs/183_HANDOFF_2026-09-07_B3_B4_SUBMISSAO.md`](docs/183_HANDOFF_2026-09-07_B3_B4_SUBMISSAO.md).

> **B6 / item 25 em 07/09 (offline) — o par de TDR deixou de devolver sucesso sem silício.**
> `ResetFromTimeout` e `RestartFromTimeout` escreviam uma linha de log e devolviam
> `STATUS_SUCCESS` **sem tocar em registrador** — Lei 4 violada no par de DDIs em que o
> `dxgkrnl` confia para decidir se o adaptador voltou; e o `mcdm_callbacks_test` **exigia**
> esse sucesso, congelando a mentira. Agora há quiesce físico (snapshot de
> `STATUS`/`ERR_LOG`, `RB_ENABLE` desligado, F32 travado, **sem** desmapear o BAR) e
> reinício **com prova de fence sobre poison**; falha devolve `STATUS_DEVICE_NOT_READY`.
> **O item 25 continua ABERTO** — falta a corrida com hang induzido. Dívida declarada: não
> existe hang induzido em `tools/`, e a detecção própria (item 24) não está ligada. Pacote
> `3.86.0.0` assinado e não instalado. Handoff:
> [`docs/184_HANDOFF_2026-09-07_B6_TDR_COM_BACKEND.md`](docs/184_HANDOFF_2026-09-07_B6_TDR_COM_BACKEND.md).

> **itens 24 e 25 em 07/09 (offline) — as duas dívidas do TDR fechadas em código.**
> **item 24:** `QueryEngineStatus` respondia `Responsive=1` **sem olhar registrador** — e o
> `dxgkrnl` chama essa DDI *durante* o TDR, então o driver sabotava a própria recuperação;
> o teste também exigia isso. Agora a resposta sai de duas passagens de `RPTR`/`WPTR`, com
> os ponteiros **reduzidos a uma volta do anel** (sem isso, os valores crus de 17:43 diriam
> "travada" para engine ociosa). **item 25:** hang induzido de verdade — `POLL_REGMEM`
> esperando um valor que nunca aparece, submissão aceita e **completion deliberadamente não
> notificado**, para o timeout de 2 s disparar o TDR; fail-closed em três camadas e disparo
> único por bind. O item 25 **continua aberto** e, por exigir *display sobrevivendo*, só
> fecha junto com o B7. Pacote `3.87.0.0` assinado e não instalado. Handoff:
> [`docs/185_HANDOFF_2026-09-07_ITEM24_25_TDR_MENSURAVEL.md`](docs/185_HANDOFF_2026-09-07_ITEM24_25_TDR_MENSURAVEL.md).

> **Correção da ordem do ensaio TDR, 07/09 (offline):** tentar armar pelo registro antes da
> instalação foi corretamente recusado: a chave de serviço só nasce pelo `AddReg` do INF, e
> o KMD lê o valor no `StartDevice`. O INF fonte permanece fail-closed em `TdrInduceHang=0`;
> o empacotador agora materializa `1` apenas no INF do candidato, exige versão nova e aceite
> explícito, e o binder exige o mesmo aceite. Pacote TDR `3.88.0.0` assinado/auditado,
> `BuildKind=MCDM_TDR_INDUCED_HANG`, **NÃO INSTALADO**. Fonte `5B6F07ED...`, SYS assinado
> `C4498C9B...`, CAT `5F0E41F6...`, INF `45CDC569...`. Testes: package audit **PASS**,
> safety audit com `audit_induced_hang_package_order` **PASS**, gates negativos sem aceite
> e com versão reutilizada **RECUSADOS antes de mutar o pacote**. Item 25 continua
> **ABERTO** e **NÃO VALIDADO EM HARDWARE**. Handoff:
> [`docs/186_HANDOFF_2026-09-07_TDR_ORDEM_E_PACOTE_ARMADO.md`](docs/186_HANDOFF_2026-09-07_TDR_ORDEM_E_PACOTE_ARMADO.md).

> **Corrida TDR 07/09 20:41 (P0) e correção offline:** o hang induzido disparou o
> `dxgkrnl`; `ResetFromTimeout` mediu/quiesceu `RPTR=68, WPTR=92, ERR_LOG=0x0F`, mas o
> restart antigo não abortou o `POLL_REGMEM`: o `SYNC_NOP` saiu com `WPTR=40`, permaneceu
> em `RPTR=68` e a fence conservou o poison. A DDI falhou fechada com
> `STATUS_DEVICE_NOT_READY`, seguido de **BugCheck 0x116** com esse status em Arg3.
> `QueryEngineStatus` não apareceu e o item 24 não foi exercitado nesta rota. Offline, o
> restart passou a pulsar `GRBM_SOFT_RESET.SDMA0`, reconstruir integralmente o anel, exigir
> readback e só depois provar com fence física. Pacote TDR `3.89.0.0` assinado/auditado e
> **NÃO INSTALADO**. Build e auditoria PASS; dois gates verificados por mutação. Item 25
> continua **ABERTO** e a correção é **NÃO VALIDADA EM HARDWARE**. Handoff:
> [`docs/187_HANDOFF_2026-09-07_TDR_0x116_E_RESET_INTEGRAL.md`](docs/187_HANDOFF_2026-09-07_TDR_0x116_E_RESET_INTEGRAL.md).

> **Corrida TDR 07/09 21:49 (P0): reset e readmissão funcionaram.** O Windows gerou live
> dump `VIDEO_TDR_TIMEOUT_DETECTED (0x117)` atribuído a `bc250mcdm.sys`. A cadeia física
> fechou `TDR_HANG_INDUZIDO` → `TDR_QUIESCE` → `SYNC_NOP TRIPOD PASS` em 2 polls →
> `TDR_RESTART PROVADO`. Depois disso houve **40 `INDIRECT` concluídos**, quatro com tripé
> completo, 36 `Present`, zero recusa e zero timeout; o adaptador permaneceu Código 0 até o
> watchdog fazer rollback e reboot planejado. A tela continuou preta porque o DWM ainda
> fail-fasta sem backend D3D11. Portanto detecção, reset, restart e readmissão estão
> **VALIDADOS EM HARDWARE**; o item 25 permanece aberto somente na perna “display visível
> sobrevive”, dependente do B7. Não repetir o TDR antes do B7. Handoff:
> [`docs/188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md`](docs/188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md).

> **Corrida 08/09 11:48 (P0, candidato lab 3.90.1.0):** Código 43 no StartDevice (`0xC00000BB`)
> antes do bring-up físico, gate do Node 1 marcou `UMD_NODE1_SKIPPED`. Causa: transporte MMIO
> permaneceu mapeado após a programação da MMU global, fazendo a guarda do segundo preflight
> readonly recusar com `STATUS_INVALID_PARAMETER`. Faixa A16 corrigiu: transporte esvaziado antes
> do segundo preflight, probe `SecondPreflightStatus` adicionado e fallback de submissão persistente
> protegido contra execução inexistente. Handoff:
> [`docs/199_HANDOFF_2026-09-08_A16_ORDEM_DO_SEGUNDO_PREFLIGHT.md`](docs/199_HANDOFF_2026-09-08_A16_ORDEM_DO_SEGUNDO_PREFLIGHT.md).
>
> **Corrida 08/09 12:36 (P0, candidato lab 3.92.1.0):** A16 validada em hardware (`stage=8, gate=1`),
> StartDevice retornou sucesso, mas Código 43 apareceu em `RuntimeCapsStatus=0xC0000182`.
> A dummy page da MMU (`0x45FFFF000`) foi tratada como prefixo a pular em vez de truncamento de topo,
> colapsando o segmento sealed. Corrigido offline separando prefixo de reserva alta. Handoff:
> [`docs/200_HANDOFF_2026-09-08_12-36_A16_VALIDADA_E_SEGMENTO_SEALED.md`](docs/200_HANDOFF_2026-09-08_12-36_A16_VALIDADA_E_SEGMENTO_SEALED.md).
>
> **Corrida 08/09 13:00 (P0, candidato lab 3.92.1.0 SYS 7B34393F):** Código 0 e segmento sealed
> validado no silício (`RuntimeCapsBaseLow32=0xA8C000`, `RuntimeCapsLocalLow32=0xF531000`,
> `QUERYSEGMENT4` e `SetRootPageTable` com readback OK). O VidPn convergiu 39 vezes, mas entrou
> em laço (46 reinícios) porque um processo nascia e morria entre a convergência e o commit.
> Handoff: [`docs/201_HANDOFF_2026-09-08_13-00_SEGMENTO_VALIDADO_E_O_LACO_DO_VIDPN.md`](docs/201_HANDOFF_2026-09-08_13-00_SEGMENTO_VALIDADO_E_O_LACO_DO_VIDPN.md).
>
> **Corrida 08/09 14:06 (P0, candidato lab 3.93.1.0 SYS EF91F17F):** Instrumentação provou que
> o processo que morria era cliente GDI (`Gdi=1, System=0`), e nenhuma DDI nossa recusava. O achado
> crucial foi `DmaBuf=0x0, SegSet=0x0` sendo publicado em `McdmCreateContext` porque `ContextAbi` só
> era inicializado no ramo de produção! Corrigido com fonte única `Bc250ActiveInitializeContextAbi`.
> Handoff: [`docs/202_HANDOFF_2026-09-08_14-06_QUEM_MORRE_E_O_CONTEXTO_ZERADO.md`](docs/202_HANDOFF_2026-09-08_14-06_QUEM_MORRE_E_O_CONTEXTO_ZERADO.md).
>
> **Corrida 08/09 14:26 (P0, candidato lab 3.94.1.0 SYS 8CA949DC):** `CommitVidPn` aconteceu!
> O laço de VidPn acabou: `DmaBuf=0x10000, SegSet=0x2`, ciclo de processos cessou (2 Create / 0 Destroy),
> `CommitVidPn` (1), `SetVidPnSourceAddress (0xAAC000)`, `SetVidPnSourceVisibility(1)`, 60 `GetStdAllocData`,
> 20 `CreateAllocation`, 30 `OpenAllocation`, e 367 interrupções VSYNC. DWM rodou e fail-fastou com
> `0xe0464645` (8 vezes) pela recusa de D3D10/11 na UMD. As paredes de lab e de produção convergiram
> em uma única parede conhecida. Handoff:
> [`docs/204_HANDOFF_2026-09-08_14-26_COMMITVIDPN_E_A_PAREDE_UNICA.md`](docs/204_HANDOFF_2026-09-08_14-26_COMMITVIDPN_E_A_PAREDE_UNICA.md).

> ⚠️ **Correção de registro (04/09).** A linha anterior dizia "WDDM Full Graphics, DWM ativo a
> 60 Hz, SDMA Blts ativos". A corrida de 09:04 não sustenta nenhuma das três afirmações:
> o enlace roda a **56,13 Hz** (timings do OTG: H_TOTAL 2784, V_TOTAL 1111), a cadência do
> `Present` nunca foi medida, e o **SDMA0 não armou** nessa inicialização
> (`RPTR=0` com `WPTR=68`, fence com poison intacto, 4/4 bytes divergentes) — todos os 12
> `Present` caíram em cópia por CPU. Ver a nota no item 63.
**Roadmap detalhado:** [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md)
**Regras normativas:** [`rules.md`](rules.md) · **Recuperação:** [`RECOVERY_BC250V4.md`](RECOVERY_BC250V4.md)

---

## Onde queremos chegar — e o que NÃO é o objetivo

**O objetivo deste driver é WDDM acelerado por GPU e jogos rodando com Vulkan.** Nada mais.

Rodar **jogos** na BC-250 (`1002:13FE`, Cyan Skillfish 2, GC 10.1.3), usando as 24 CUs e os
16 GB de GDDR6, com o monitor ligado ao DisplayPort da própria placa, com o **WDDM executado
pela GPU** — composição, apresentação e render acelerados no silício — via Vulkan (e D3D
traduzido por cima dele), **sem WARP e sem fallback de software**.

### Lei do escopo (06/09) — decisão do dono do projeto, não sugestão

1. **LLM é secundário.** `llama.cpp`, tokens/s, DirectML e afins são **subproduto**, nunca
   marco. Nenhum item de LLM entra em prioridade, em PASS de etapa ou em veredito.
2. **Compute isolado não é entregável.** Compute é **pré-requisito** de jogos (a submissão
   Vulkan passa por ele) e vale exatamente por isso — não como destino nem como consolo.
3. **É proibido propor compute ou LLM como "caminho mais rápido" quando o item de vídeo
   estiver bloqueado.** Item travado se resolve, se declara travado, ou se declara
   `NÃO VALIDADO EM HARDWARE`. Trocar a meta por uma mais fácil é fuga de escopo, e este
   documento a rejeita.
4. **Demorar é aceito.** O custo em tempo não é argumento para trocar o objetivo. Um caminho
   longo em direção a jogos vale mais do que um caminho curto para outro lugar.
5. O critério final de sucesso continua sendo o da P8: **jogo real rodando na tela ligada à
   placa, sem WARP, com TDR recuperando de hang sem perder o display.**

> ⚠️ **Correção de registro (06/09):** a plataforma desta máquina é **Windows 10 Pro 22H2,
> build 19045, UBR 2965** — não Windows 11. Confirmado pelo dump do DWM
> (`Windows 10 Version 19045 MP (12 procs) Free x64`) e pelo registro da máquina. O WDDM
> máximo aqui é 2.9, compatível com o contrato 2.0/2.7 que usamos, mas os documentos que
> diziam "Windows 11 x64" estavam errados e induziam decisão de DDI.

## Decisão de arquitetura vigente

**Display v1 é por herança do UEFI GOP — não implementamos DCN no caminho crítico.**

A BC-250 **tem** DCN 2.0.1 e ele funciona no Linux. Mas o GOP já programou OTG/DIG/PHY no
boot, e nada desliga isso quando o `dxgkrnl` assume. Os itens 57–62 (todos ✅) provam que a
imagem sobrevive à tomada de ownership com **zero linhas de DCN**. Logo o display v1 é:
VidPn de um único modo herdado, scanout em VRAM real, `DxgkDdiPresent` no modelo **blt**
por SDMA, e VSync **lido** do frame counter do OTG.

Escrita em registrador de DCN está **proibida no caminho crítico**. Leitura é permitida.
Mode-set, hotplug, flip de hardware, VRR e HDR são **P9 — opcional**.

### Backend de shader: rota B — DXBC → SPIR-V → RADV/ACO (decidida em 06/09)

O `dwm.exe` exige device D3D11 e `pfnCreateVertexShader` recebe **bytecode DXBC**; jogos
exigem Vulkan. Havia duas rotas para o backend de shader e de pipeline gráfico:

| Rota | O que exige | Decisão |
|---|---|---|
| A — DXBC → ISA nativo | compilador próprio de DXBC para RDNA **e** pipeline gráfico próprio | **rejeitada** |
| **B — DXBC → SPIR-V → RADV/ACO** | tradutor DXBC→SPIR-V no UMD + winsys da branch `wddm2` adaptado ao nosso *private data* | **ADOTADA** |

**Por que B:** um único backend atende as duas frentes do objetivo — o D3D11 que o DWM
exige e o Vulkan que os jogos exigem — em vez de construir dois. Reaproveita o compilador
ACO e o pipeline gráfico GFX10 do RADV, que já são testados, e tem precedente estabelecido
de tradução em modo usuário (DXVK e VKD3D-Proton convertem DXBC/DXIL para SPIR-V).

**Consequências normativas:**

1. O item 146 (winsys RADV para Windows) deixa de ser só "P7 de jogos" e passa a ser
   **infraestrutura compartilhada** com o caminho de vídeo.
2. **Não se escreve compilador de DXBC para ISA.** Qualquer proposta nessa direção contraria
   esta decisão e precisa reabri-la por escrito.
3. O *private driver data* é **nosso** (`BC250_ALLOCATION_PRIVATE_V1`). O winsys da branch
   `wddm2` é adaptado para emitir o nosso formato — **não** se imita a ABI da AMD.
4. A decisão é de arquitetura, **não** um resultado: nada dela está validado em hardware.
   Dimensionamento em [`docs/173_DIMENSIONAMENTO_D3D11_PARA_DWM.md`](docs/173_DIMENSIONAMENTO_D3D11_PARA_DWM.md).

## 🛑 BLOQUEADOR ATIVO (07/09) — leia antes de qualquer bind

**Estado medido às 15:05:** o valor
`HKLM\SOFTWARE\BC250\Umd\D3D11CapsProbe` está **ausente**. O bloqueador anterior foi
desarmado. Ainda assim, todo binder deve manter o preflight que exige o probe ausente/zero;
se reaparecer em `1`, a UMD publica a tabela incompleta, o DWM falha em
`3DPIPELINESUPPORT` e a tela preta é intencional.

Comando de recuperação caso o preflight o encontre armado (exige elevação):

```
powershell -ExecutionPolicy Bypass -File tools\arm-d3d11-caps-probe.ps1 -Disarm
```

**Handoff completo desta sessão:**
[`docs/174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md`](docs/174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md)
— o que foi feito, o que está provado, o que é hipótese, e a corrida recomendada com o que ler
no log.

---

## Restrições de ambiente (mandam no plano)

| Restrição | Consequência no plano |
|---|---|
| **O pacote `output_lab` está ESTAGNADO** (SYS assinado às 14:16 de 07/09, hash-fonte `5D7333...`, anterior ao candidato atual cujo hash-fonte é `9A80C7...`) | Reconstruí-lo exige `build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup` — flag que existe porque a imagem executa PFP/CP/GCVM/HQD no `StartDevice` **detendo o display**. Decisão do dono, não passo de build |
| `build-bc250mcdm-lab-bringup.ps1` **sem** os flags retorna **`EXIT=0`** | Ele só valida precondições e **não compila**. Ler o exit code como sucesso deixa o SYS antigo no lugar |
| **Hash do pacote ≠ hash do build, por construção** | O empacotador **assina** o SYS. Prova de frescor é procurar, como ASCII dentro do binário, uma string de log do candidato — ex.: `IB_QUEUE_DISCRIMINANTE` |
| **Não existe segunda máquina** | **KDNET está fora.** A P3 foi reescrita para diagnóstico de uma máquina só: dump, telemetria interna, ETW autologger e bugcheck deliberado |
| A corrida física é executada pelo **Daniel** | Eu compilo, analiso e preparo. Toda corrida precisa de teto e critério de abandono definidos antes |
| Estado de engine não cai com desbind | **Boot frio obrigatório** entre binds |
| Uma BC-250 e um monitor | Matriz de hardware fica para depois (item 178) |
| O empacotador **não compila** o SYS | Pular o build de laboratório assina o binário do candidato anterior |

---

## Fonte normativa local (WDK docs) — usar antes de gastar corrida

O repositório tem uma cópia da documentação oficial em
`referencias/windows-driver-docs/windows-driver-docs-pr/display/`. Ela é `P1` (derivada do
upstream) e responde de graça perguntas que hoje custam corrida física. Os documentos que
já produziram resultado, em 06/09:

| Documento | Para que serve aqui |
|---|---|
| `enumerating-cofunctional-vidpn-source-and-target-modes.md` | **Confirma o contrato de pivô** que estava sendo violado, e lista mais dois deveres do `EnumVidPnCofuncModality` (multisampling em source pinado; mode set do target derivado do monitor) |
| `wddm-driver-and-feature-caps.md` | Tabela de caps **obrigatórios por tipo de driver**. Expôs três booleanos **M** de Full Graphics em FALSE no nosso caminho ativo |
| `device-paging-queues.md` | Semântica da **device paging fence** assíncrona — explica a espera infinita do `vkCreateDevice` |
| `gpummu-model.md` · `gpu-virtual-memory-in-wddm-2-0.md` | Contrato do modelo GpuMmu (root page table redimensionável, granularidade de 4 KB, quem atribui GPUVA) e prova que **não** houve page fault na nossa corrida |
| `iommu-model.md` | Diz que IoMmu é **só memória de sistema** e que o VidMm mapeia a primária na **aperture** — incompatível com a exigência de scanout em VRAM real da BC-250 |
| `timeout-detection-and-recovery.md` · `tdr-registry-keys.md` | Timeout default de 2 s, `0x117`/`0x141`/`0x142` e as chaves de registro. Base do gate de saída da P6 (item 25) |
| `debugging-tips-for-wddm-drivers.md` | Alavancas de diagnóstico de **uma máquina só**: `GraphicsDrivers\Scheduler\EnablePreemption=0`, GPUView, log interno do `Watchdog.sys`, e a exigência de desligar FPO em WDDM 1.2+ |
| `direct-flip-of-video-memory.md` | `SupportDirectFlip` é **Mandatory** para Full Graphics — responde o risco (a) da P2 |

> ⚠️ **`mcdm-implementation-guidelines.md` NÃO se aplica a este driver.** O nome
> `bc250mcdm` é histórico. A especificação MCDM **proíbe** expressamente
> `DxgkDdiCommitVidPn`, `DxgkDdiEnumVidPnCofuncModality` e `DxgkDdiControlInterrupt` — as
> três DDIs de que o nosso caminho de display depende — e exige
> `Class=ComputeAccelerator` no INF. Nossos INFs declaram `Class=Display`
> (`{4d36e968-e325-11ce-bfc1-08002be10318}`), que é o correto para Full Graphics. Seguir a
> guia de MCDM aqui é erro de contrato; a referência válida é a de Full Graphics WDDM.
> A lista de DDIs mínimas do MCDM continua útil **apenas** como conferência do lado de
> render (paging buffer, GPUVA, root page table, `SubmitCommandVirtual`, reset/TDR).

## Lei de honestidade deste documento

1. Um item entra na **Tabela 1** apenas com **prova física arquivada** em `docs/evidence/`.
2. Compilar, assinar, retornar `STATUS_SUCCESS` ou passar teste offline **não** promove
   nada. Isso fica na Tabela 2 com estado `🧪`.
3. Toda linha da Tabela 2 carrega a **prova física exigida** para fechá-la. Sem essa
   prova, o item não fecha — não importa quanto código exista.
4. O tripé é obrigatório onde se aplica: **`RPTR==WPTR`** + **fence física em VRAM com
   verificação de poison** + **conferência byte a byte** contra gabarito da CPU.
5. Onde a prova não existir, o veredito é literalmente **`NÃO VALIDADO EM HARDWARE`**.

**Legenda de estado:** ✅ validado no silício · 🧪 provado só offline · 🟡 parcial ·
⛔ bloqueado · ⬜ não iniciado · 🧹 dívida a remover · ❌ reprovado/encerrado
**`#`** = numeração histórica do checklist, preservada para rastreabilidade.

---

## 📋 Diretrizes Universais de Auditoria, Mineração de Prova e Higiene Operacional (09/09/2026)

> Normas mandatórias derivadas da auditoria offline e conferência das corridas de 09/09 ([docs/230](docs/230_AUDITORIA_ESTADO_WDDM_2026-09-09.md) e [docs/231](docs/231_CONFERENCIA_CORRIDA_21-20_2026-09-09.md)), obrigatórias para qualquer agente ou humano operando neste repositório.

1. **Fatiamento Obrigatório de Log Acumulado (`display-log-baseline.json` e `start_offset`):**
   - O arquivo `C:\BC250_DISPLAY_LOG.txt` é estritamente acumulativo (append-only) e cresce a cada bind (ultrapassando 7 MB em 09/09).
   - É terminantemente **PROIBIDO** minerar ou contar ocorrências no log integral para avaliar o resultado de uma corrida específica.
   - Toda análise deve ler `display-log-baseline.json`, extrair `start_offset` e fatiar o arquivo a partir desse marco em bytes (ou utilizar ferramentas automatizadas que respeitem o baseline).
   - Distinguir rigorosamente: linhas de log emitidas ≠ chamadas de DDI recebidas ≠ submissões aceitas ≠ operações fisicamente concluídas no silício. O último `FenceId` não afere contagem de submissões consecutivas comprovadas.

2. **Semântica Estrita de Telemetria de Interrupções:**
   - Ocorrências de `[ControlInterrupt] VSYNC Enable=0/1` são chamadas do sistema operacional solicitando habilitar ou desabilitar o VSync. Elas **NÃO** representam pulsos físicos de VBlank recebidos ou tratados pelo controlador OTG.
   - Contagens de interrupções físicas devem ser derivadas exclusivamente de medições de VBlank registradas pelo hardware ou pelo DPC real de VSync.

3. **Escopo Real de Prova do Nó 1 (Compute MEC):**
   - O teste de harness `B7_UMD_NODE1_TRIPOD` provou submissão PM4 com `WRITE_DATA` e `RELEASE_MEM` sob VMID 9 e atualização de memória física.
   - Isso **NÃO** demonstra dispatch de shader gráfico ou de compute (`DISPATCH_DIRECT`), nem prova existência de compilador DXBC/SPIR-V funcional pela rota UMD/WDDM.
   - É expressamente proibido registrar a expressão "executou o shader" sem prova de dispatch real no silício.
   - A cerca monitorada de usuário (`0xBC250F01`) é distinta da cerca interna de retirement do scheduler (`18`); conclusão de fence do usuário não prova retirement pelo scheduler.

4. **Higiene e Segurança de Scripts de Rollback e Ferramentas:**
   - Scripts de rollback (`tools/rollback-bc250mcdm-lab.ps1`) devem exigir explicitamente `-Execute` e `-IAcceptDisplayInterruptionRisk` para efetuar alterações de estado de hardware ou PnP. Chamadas sem parâmetros devem operar exclusivamente em modo de visualização (dry-run).
   - É proibido utilizar switches inexistentes no sistema operacional alvo (ex.: `pnputil /disable-device /force` só existe no Windows 11 22H2+; a máquina alvo é Windows 10 Pro 22H2 build 19045).
   - O código de saída (`$LASTEXITCODE` / `$?`) do `pnputil` e demais comandos deve ser verificado após cada invocação.

5. **Honestidade e Princípio Fail-Closed na UMD Gráfica:**
   - É proibido retornar falso sucesso (`S_OK`) para DDIs DXGI ou D3D10/11 cujas operações não sejam efetivamente executadas pela GPU (ex.: `Present` sem callback, `Blt` sem hardware blt).
   - O Type 130 (`3DPIPELINESUPPORT`) na UMD deve retornar `E_NOTIMPL` fail-closed enquanto `BC250_D3D10_11_NATIVE_READY == 0`. Anunciar níveis D3D10/11 sem backend rasterizador e sem shader dispatch viola a Lei 4 e causa falhas com fail-fast no DWM.

6. **Reciclagem e Gestão Limpa de Recursos do KMD:**
   - Tabelas de objetos internos (Contexts, Allocations, Processes) devem implementar limites adequados (4096) e reciclagem de slots mortos (`State == DEAD`, `ChildCount == 0`, `InFlightSubmissions == 0`) para impedir exaustão determinística (`STATUS_INSUFFICIENT_RESOURCES` / `0xC000009A`) durante alta rotatividade de processos (ex.: DWM respawn).

---

# TABELA 1 — JÁ FEITO · validado no silício

**69 itens** com prova física arquivada. Estes são os ativos reais do projeto.

| # | Bloco | Item | Prova física que sustenta o ✅ |
|---:|---|---|---|
| 1 | Base | Identificação PCI | `1002:13FE` Cyan Skillfish 2 lido do barramento |
| 2 | Base | Topologia Zen 2 + GC 10.1.3 | 24 CUs observadas em runtime |
| 3 | Base | BARs e recursos PCI | Lidos e mapeados no bring-up físico |
| 6 | Base | Driver de laboratório `bc250dbg` | Diagnóstico e telemetria isolados, em uso |
| 7 | Base | Recovery manual | `C:\BC250_RECOVERY.cmd` executado com sucesso |
| 8 | Base | Watchdog fire-and-collect | 3.80 fechou o ciclo sozinho: bind → retrato → rollback → reboot → `BasicDisplay / Código 0` |
| 9 | Base | Binding sem watchdog | Aceite explícito + recovery manual exercitados |
| 10 | Base | Hash instalado vs pacote | SYS comparado a cada bind, divergência reprova |
| 11 | Base | Assinatura de laboratório | Pacotes assinados e auditados a cada candidato |
| 12 | Base | Gate de boot limpo / dirty-engine | Fail-closed disparou e bloqueou corrida real |
| 12b | Base | Guarda de engine suja persistente | Validado no silício em 03/09: leitura direta de `C:\BC250_BOOT_FUSE.dat` sobrevive a rollback e reboot (`evidence/2026-09-03_09-25-43-...`) |
| 13 | Firmware | PSP / GPCOM | Carga autenticada, `RespStatus == 0` |
| 14 | Firmware | RLC | Saiu da ROM; firmware aceito pela engine |
| 15 | Firmware | PFP | Carga direta e execução provadas |
| 16 | Firmware | ME | Tríade CP comprovada |
| 17 | Firmware | CE | Tríade CP comprovada |
| 18 | Firmware | MEC | Compute persistente fisicamente funcional |
| 19 | Firmware | SDMA | Motor inicializado e trabalho real executado |
| 20 | Silício | Golden registers GFX | Sequência upstream aplicada com máscaras SOC15 e readback |
| 21 | Silício | Clocks GFX | Suficientes e estáveis para P1/P2 |
| 28 | Memória | VRAM GDDR6 acessível | Buffers físicos usados nas provas P1/P2 |
| 29 | Memória | PTE 0x10 / tradução GCVM | Tradução exercitada e conferida no silício |
| 42 | Memória | Scanout / Primary em VRAM real | Scanout decodificado em VRAM física real (`0xC0000000` / `0xF400000000ull`) herdado do UEFI GOP; 128 MB mapeados em kernel (`0xFFFFC178FAC00000`) em 03/09 (`evidence/2026-09-03_23-32-07-...`) |
| 43 | Filas | MQD / HQD | MEC2 / Pipe0 / Queue0 ativada e lida de volta |
| 44 | Filas | Ring persistente | Engine online, estado transacional |
| 45 | Filas | Indirect Buffer | Readback byte a byte antes do kick |
| 46 | Filas | Shader real na GPU | Resultado saiu do padrão de poison |
| 47 | Filas | **Prova P1** | 17/17, duas fences GPU, `RPTR == WPTR` |
| 48 | Filas | **Prova P2** | 16/16 aceitas e aposentadas, zero mismatches, reproduzido n=2 |
| 49 | Filas | Fence física direta | Escrita **pela GPU** em VRAM, poison verificado |
| 50 | Filas | Retirement direto | Telemetria confirma aposentadoria física |
| 57 | KMDOD | Registro display-only | `DxgkInitializeDisplayOnlyDriver` em Código 0 |
| 58 | KMDOD | Framebuffer do GOP | Endereço, pitch e modo herdados e lidos |
| 59 | KMDOD | VidPN KMDOD | `IsSupportedVidPn` / `CommitVidPn` aprovados |
| 60 | KMDOD | `PresentDisplayOnly` | Entrada, status e cópia funcionais |
| 61 | KMDOD | VSync KMDOD | Contadores e notificação observados (Tipo 5 / Target 1) |
| 61b | KMDOD | **VSync físico via OTG Frame Counter** | Validado no silício em 03/09 (`evidence/2026-09-03_10-01-10-...`): leitura direta de `mmOTG0_OTG_STATUS_FRAME_COUNT` em BAR5 + `0x14030` (`0xFE800000`), inicial 69380; DPC sincroniza VSync com o pulso físico do display controller; 73 interrupções tratadas sem BSOD |
| 62 | KMDOD | **Imagem após binding (Display v1)** | Confirmada visualmente com watchdog desarmado interativamente (`DISPLAY_OK`) em 03/09 (`evidence/2026-09-03_09-39-12-...`); modo 2560x1080@60Hz pinado, `PresentDisplayOnly` copiando pixels ao scanout GOP em Código 0 |
| 63 | Display/SDMA | **Display acelerado via SDMA0** | Validado no silício em 03/09 (`evidence/2026-09-03_22-55-39-...` e `23-32-07-...`): PSP carrega SDMA0 (`status=0, PhysReady=1`), F32 des-haltado (`status=0`), Tripé no StartDevice (`WPTR=68, RPTR=52, Fence=0x1001, 4/4 OK`). 32 submissões no Node 0 SDMA com notificação física `DMA_COMPLETED` para Fences 1..32 (`Submitted=32, Completed=32, Fallbacks=0`). **Nao reprodutivel boot-a-boot:** na corrida de 04/09 09:04 (`evidence/2026-09-04_09-04-24-...`) o mesmo arm falhou (`RPTR_Reg=0` com `WPTR_Reg=68`, poison intacto, 4/4 divergentes) e `HardwareEnabled` ficou FALSE; `RingWorkStatus=2` (ALLOCATION_FAILED) mostra que o caminho provado do `bc250_sdma_ring_work.c` nem chegou a rodar nesse boot. Cinco divergencias de offset/mascara entre o modulo de apresentacao e o modulo provado foram corrigidas em 04/09 e travadas por `C_ASSERT` -- correcao NAO VALIDADA EM HARDWARE. |
| 66 | DCN | Inventário read-only | Dois snapshots com intervalo de 1,122 s + zero escrita provada (`evidence/2026-09-02_...`) |
| 67 | DCN | OTG / frame counter vivo | `OTG_STATUS_FRAME_COUNT` avançou no silício (+63 frames a ~56,13 Hz) com zero escrita |
| 68 | DCN | HUBP / framebuffer herdado | Viewport 2560x1080, pitch 2560, formato ARGB e base `0x450000000` decodificados da placa |
| 81 | Display | **Takeover do GOP sem escrever no DCN** | Validado no silício em 03/09 (`evidence/2026-09-03_18-01-51-...`, `22-55-39-...`, `23-32-07-...`): Código 0 sustentado >500 s com imagem viva contínua no KMDOD e scanout passivo sob WDDM, sem tocar em registradores de escrita de DCN |
| 90 | WDDM | `DxgkInitialize` | Miniport WDDM 2.0/2.7 registrado e mantido ativo em Código 0 |
| 91 | WDDM | `AddDevice` | Device context e extension criados com sucesso sob `RenderNode=1` |
| 92 | WDDM | `StartDevice` (1 source / 1 child) | Retornou `STATUS_SUCCESS` com 1 source e 1 child; BAR0/BAR2/BAR5 mapeados, ISR/DPC conectados |
| 93 | WDDM | A/B contrato 2.0 vs 2.7 | Ambos consumidos; **mesma** falha no estágio 3 |
| 94 | WDDM | Trace de `QueryInterface` | 7 dos 8 GUIDs identificados no WDK; todos opcionais — **não** são a causa |
| 97 | WDDM | **CÓDIGO 43 — gargalo único superado** | Validado no silício em 02/09 e 03/09: Código 0 sustentado por 121 s e >500 s; `DRIVERCAPS` e `QUERYSEGMENT4` respondidos e aceitos |
| 100 | WDDM | Perfil `RenderOnlyLab` | Implementado e executado no silício (3.79/3.80); perfil aposentado |
| 102 | WDDM | **Display+render WDDM em Código 0** | Validado no silício em 02/09 e 03/09: `ConfigManagerErrorCode: 0` e `Status: Iniciado` confirmados por CIM e PnPUtil sob `RenderNode=1` |
| 103 | WDDM | Caps `QueryAdapterInfo` | `DRIVERCAPS` (`Type=1`) e `QUERYSEGMENT4` (`Type=11`) respondidos com 2 segmentos (VRAM 256MB + GART 512MB) |
| 133 | WDDM | UMD declarado no INF | Seção SoftwareDeviceSettings com `InstalledDisplayDrivers` e `UserModeDriverName` aceita pelo `dxgkrnl` |
| 123 | Ferramentas | Decoder de GUIDs | Resolvido pela fonte WDK; resta só `6BA41A33` |
| 167 | Testes | Suíte unitária core | 37 testes de kernel + 21 invariantes UMD, 0 falhas |
| 168 | Testes | Teste da init struct efetiva | WDDM 2.0/2.7 e tabela de ponteiros auditados |
| 169 | Testes | Testes de VidPN | Cobertura offline no build |
| 171 | Testes | Validadores P1/P2 | Read-only, evidência JSON arquivada |
| 172 | Testes | Safety audit | Gate no build, reprova em violação |
| 173 | Testes | Package audit | INF / CAT / SYS / manifesto conferidos |
| 174 | Segurança | **Fusível anti-boot-loop persistente** | Provado em silício em 03/09: desarma e bloqueia StartDevice em `State=2` após BSOD/falha, protegendo contra boot loops; autoriza em `State=0` (`evidence/2026-09-03_09-25-43-...` e `09-39-12-...`) |
| 175 | Ferramentas | **Watchdog com auto-reboot** | Provado em silício em 03/09: timeout expira, executa rollback e agenda reboot limpo automático, devolvendo o BasicDisplay em Código 0 sem intervenção manual |
| 176 | Diagnóstico | Análise de `MEMORY.DMP` | `cdb.exe` em 02/09: Bugcheck 0x113 (0x1C) corrigido em `SetRotationSupport` / `EnumVidPnCofuncModality` |
| — | WDDM | **Topologia VidPn comitada sob `RenderNode=1`** | Validado no silício em 03/09 (`evidence/2026-09-03_23-32-07-...`): DMM chamou `CommitVidPn` com `pathCount=1`, modo 2560x1080@60Hz pinado, `hAlloc` real atribuído (`FFFFDE8CC4728B28`), superando definitivamente a topologia vazia |
| — | SDMA | **Sanitização da GDDR6 por CONST_FILL** | Validado no silício em 03/09 (`evidence/2026-09-03_23-13-13-...` e `23-32-07-...`): `Bc250SdmaPresentFill` (opcode 11) limpou 100% da VRAM do scanout físico, eliminando ruído térmico e artefatos cinzas da tela |
| — | WDDM | **VSync do OTG ligado ao `dxgkrnl`** | Validado no silício em 03/09 (`evidence/2026-09-03_23-32-07-...`): `DxgkDdiControlInterrupt` (Tipo 5) conectado ao frame counter de hardware do OTG (`BAR5 + 0x14030`) sob WDDM |
| — | Paging | **`UpdatePageTable` via Paging Buffer** | Validado no silício em 03/09 (`evidence/2026-09-03_22-55-39-...`): `McdmBuildPagingBuffer` (Op 11 UPDATE_PAGE_TABLE) executado com sucesso no silício para Lvl 0 e Lvl 1 (`status=0x00000000`) |
| — | Scheduler | **Scheduler submetendo no Node 0 SDMA** | Validado no silício em 03/09 (`evidence/2026-09-03_22-55-39-...`): `SubmitCommandVirtual` chamado pelo scheduler, 32 fences físicas aposentadas com notificação `DMA_COMPLETED` |
| — | WDDM | **Cadeia de alocações padrão destravada** | Validado no silício em 03/09: `Bc250McdmGetStandardAllocationDriverData` implementado para SharedPrimary, Shadow, Staging e GDI, destravando criação de alocações pelo VidMm e DMM. ⚠️ **Correção de registro (06/09):** a menção original a `IoMmuSupported=1` não corresponde ao código atual — `bc250_mcdm_callbacks.c` reporta `pGetNodeMetadata->IoMmuSupported = FALSE` e declara apenas `GpuMmuSupported`. Ver a linha de auditoria de caps na P5 |

**Resumo da Tabela 1:** compute direto no silício, display KMDOD/GOP com ownership
físico, Código 0 sob WDDM Full Graphics (`RenderNode=1`), topologia VidPn comitada com `pathCount=1`,
DWM ativo a 60 Hz, sanitização de scanout via SDMA CONST_FILL e motor SDMA0 executando transferências
físicas no silício estão **VALIDADOS EM HARDWARE** com evidência arquivada em `docs/evidence/`.

---

# TABELA 2 — O QUE FALTA · em ordem de prioridade

Uma lista, segmentada por prioridade `P0 → P9`, mais `T` (transversal) e `❌` (encerrado).
Cada segmento abre com **o que aquela etapa exige** antes das linhas de item.
A última coluna de cada linha é **o teste que fecha o item** — sem ele, o item não sai daqui.

---

## 🛡️ P0 — Segurança e repetibilidade do ensaio

> **Objetivo:** tornar **barato e reversível** o custo de uma corrida que falha, para que as
> 2 corridas da P4 possam ser gastas sem medo.
> **Por que vem antes de tudo:** hoje um FAIL da P4 pode custar tela preta **e** boot em
> loop, exigindo recuperação manual. O problema não é a tela preta em si — é o **custo** dela.

### A perda inicial da imagem no bind é esperada; a permanência em preto é um defeito medido

A BC-250 tem **uma única função PCI com saída de vídeo**. Quando o `bc250mcdm` liga, o
BasicDisplay é deslocado. Entre "BasicDisplay para de apresentar" e "nosso driver
apresenta", a tela está preta **por construção** — o doc 162 já registrou isso com essas
palavras. Não existe instalar sem apagar.

O que é corrigível é a **duração**:

| Hoje | Alvo (PASS da P5) |
|---|---|
| Preta **permanente** → TDR → reboot → às vezes loop | Preta por 1–2 s, depois imagem de volta |

E a tela voltar exige que o driver **apresente** — isto é literalmente a P5, que depende da
P4. **A dependência é o inverso da intuição:** a tela preta está *depois* do gargalo, não
antes. Não há como consertá-la primeiro.

### Instalar permanentemente para ir testando: sim, em dois dos três veículos

| Veículo | Pode ficar instalado? | Tela | Serve para |
|---|:---:|---|---|
| **`bc250dbg`** (laboratório KMDF) | ✅ **Sim, permanente** | Intacta — não toca ownership | **Toda a P1.** É a bancada de teste contínuo |
| **`bc250mcdm` perfil KMDOD** (`RenderNode=0`) | ✅ **Sim** — está em Código 0 com imagem viva (itens 57–62) | Funciona | **Toda a P2** e a exceção da P9 |
| **`bc250mcdm` perfil WDDM** (`RenderNode=1`) | ❌ **Não, até a P5 passar** | Apaga | Só fire-and-collect: bind → janela → retrato → rollback → reboot |

Ou seja: **você já pode testar continuamente com driver instalado** — só não no perfil que
está sob investigação. Na corrida de 06/09 20:34, a permanência em preto foi delimitada:
`IsSupportedVidPn` foi consultado 97 vezes, mas houve **0 `CommitVidPn`, 0
`SetVidPnSourceAddress` e 0 `Present`**. Logo, nessa corrida não existiu primária comitada
para alimentar o scanout. Isso se repete nas seis corridas do perfil lab e não deve ser
confundido com falha do blit SDMA. Deixar o perfil WDDM instalado antes da P5 é garantir uma máquina
sem tela.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | Nenhum. Tudo offline, implementável hoje |
| **Problema real a resolver** | O doc 161 (Fase 0.5) já registra o defeito: a guarda de engine suja lê a chave `Parameters\Probe`, **que desaparece junto com o serviço no rollback**, e por isso passa em silêncio com estado vazio |
| **Entregável 1 — fusível anti-loop** | Contador de tentativa em armazenamento que **sobrevive ao rollback e ao reboot** (fora da chave do serviço). No `StartDevice`: se a tentativa anterior não registrou desligamento limpo, **recusar o bind** e devolver o BasicDisplay. Converte "boot em loop + recuperação manual" em "um boot ruim e volta sozinho" |
| **Entregável 2 — guarda de engine suja corrigida** | Mesma fonte de verdade persistente; a guarda passa a reprovar de fato, não em silêncio |
| **Entregável 3 — canal de observação sem tela** | Habilitar acesso remoto (RDP ou SSH) para inspecionar e rolar de volta **com a tela preta mas o SO vivo**. Não substitui o dump se houver bugcheck, mas cobre o caso "preta e sistema respondendo", que é o mais provável |
| **Entregável 3b — limpeza offline pelo WinRE** | `tools/BC250_WINRE_CLEAN.cmd`: um `.cmd` no pendrive que roda no Prompt de Comando da recuperação. Corrige as três armadilhas do método manual — o Windows não é `C:` em WinRE, o `oemNN.inf` muda de número, e `bc*.sys` casa com o `bcmfn2.sys` da Microsoft |
| **Entregável 4 — orçamento formalizado** | Item 177: teto de corridas, critério de abandono e recovery escritos **por etapa**, antes de armar |
| **Já a favor** | `StartType=3` (demand start) e `ErrorControl=0` (`SERVICE_ERROR_IGNORE`) já estão no INF — uma **falha de carga** não derruba o boot. O loop vem do driver **carregar** e travar depois, e é isso que o fusível pega |
| **Quem executa** | Eu implemento e testo offline; o Daniel valida com 1 bind de KMDOD (perfil seguro) |
| **Custo** | Zero corridas de risco. A validação do fusível roda no perfil KMDOD, que mantém a imagem |
| **PASS da etapa** | Fusível provado: forçar uma falha, e no boot seguinte o driver **recusar sozinho** o bind, com o BasicDisplay de volta e sem intervenção manual |
| **FAIL / abandono** | Se o fusível não puder ser provado sem arriscar a tela, ele é adiado — mas então o orçamento da P4 cai de 2 para 1 corrida |
| **Proibido** | Gastar corrida da P4 antes de o fusível estar provado ou explicitamente dispensado |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | **Fusível anti-boot-loop persistente** | ✅ | Validado no silício em 03/09: bloqueou `StartDevice` em `State=2` após crash protegendo o boot; liberou após reset (`State=0`) |
| 12b | Guarda de engine suja corrigida | ✅ | Validado no silício em 03/09: leitura direta de `C:\BC250_BOOT_FUSE.dat` sobrevive a rollback e reboot |
| — | **Watchdog com auto-reboot** | ✅ | Validado no silício em 03/09: timeout expira e reinicia automaticamente devolvendo BasicDisplay em Código 0 sem Code 43 |
| — | **Limpeza offline pelo WinRE** (`tools/BC250_WINRE_CLEAN.cmd`) | 🧪 | Um arquivo `.cmd` no pendrive; detecta o volume e o `oemNN.inf` sozinho. Lógica validada em `/list` (achou os 7 resíduos, recusou `bcmfn2.sys`). **Fecha quando for usado numa sessão real de WinRE e devolver o BasicDisplay** |
| — | Canal de observação sem tela (RDP/SSH) | ⬜ | Sessão remota funcionando com a tela preta e o SO vivo, permitindo retrato e rollback interativos |
| **177** | **Orçamento de corridas formalizado** | ⬜ | Teto, critério de abandono e recovery escritos por etapa, **antes** de armar cada uma |

---

## 🟢 P1 — Inventário DCN somente leitura

> **Objetivo:** saber exatamente o que o UEFI GOP deixou programado no display, para poder
> herdar aquele modo na P5 sem tocar em nada.
> **Por que é a primeira:** é a única etapa que **não gasta corrida destrutiva** e é
> pré-requisito real da P2 e da P5.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | Nenhum. Pode começar hoje. BasicDisplay continua dono da tela |
| **Veículo** | Driver de laboratório `bc250dbg` — **nunca** o miniport WDDM |
| **Ferramenta** | `IOCTL_BC250DBG_REG_READ` ([`inc/bc250dbg_abi.h:49`](inc/bc250dbg_abi.h)) — já existe |
| **Fonte dos endereços** | `cyan_skillfish_ip_offset.h` (kernel) + headers `dcn_2_0_1`. **Não** o `REGISTER-MAP-BC250.md` de referência, cuja base plana está errada |
| **Quem executa** | Eu escrevo o catálogo e o probe offline; o Daniel roda o probe (1 execução, sem bind) |
| **Entregável** | Catálogo `(reg, BASE_IDX)` com proveniência + JSON de dois snapshots em `docs/evidence/` |
| **Custo** | **Zero corridas destrutivas.** Zero risco de tela preta |
| **PASS da etapa** | Framebuffer herdado decodificado, frame counter vivo, e auditoria provando **zero escrita** |
| **FAIL / abandono** | Se as bases não conferirem por readback, os offsets vão para quarentena (`Q`) e a P5 passa a depender de descoberta, não de catálogo |
| **Proibido** | Qualquer escrita. Uma única escrita nesta etapa invalida a corrida inteira |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | `DMU_BASE = {0x12, 0xC0, 0x34C0, 0x9000}` | ✅ | Readback na placa batendo com `cyan_skillfish_ip_offset.h` (`probe-dcn-readonly.ps1` em 02/09) |
| 4 | Identidade do bloco de display (DMU) | 🟡 | `DCE_HWIP → DMU_BASE` confirmado por leitura, não por inferência |
| 5 | Catálogo de registradores DCN | ✅ | Entradas catalogadas em `docs/03` e confirmadas no silício por readback (`dcn_inventory.json`) |
| 64 | Existência do DCN 2.0.1 | 🟡 | Versão confirmada por leitura na própria placa |
| 65 | Base real do DCN | 🟡 | Bases com checksum e bounds; ambíguas vão para quarentena |
| 66 | Inventário read-only | ✅ | Dois snapshots com intervalo de 1,122 s + zero escrita provada (`dcn-inventory` em 02/09) |
| 67 | OTG / frame counter vivo | ✅ | `OTG_STATUS_FRAME_COUNT` avançou de 192770 para 192833 (+63 frames a ~56,13 Hz) no silício |
| 68 | HUBP / framebuffer herdado | ✅ | Viewport 2560x1080, pitch 2560, formato ARGB e base `0x450000000` decodificados da placa |
| — | Framebuffer está na **VRAM real**? | ⬜ | Endereço classificado como VRAM carve vs stolen. Exigência do chip: *SG display* desligado no cyan skillfish por patch upstream |

---

## 🟢 P2 — VSync físico no KMDOD + verificação offline da P5

> **Objetivo:** trocar o timer de VSync do KMDOD pela leitura do frame counter real do OTG,
> e responder no WDK — de graça — as quatro perguntas que podem derrubar a P5.
> **Por que agora:** é o único progresso possível **enquanto o WDDM está bloqueado**, e o
> código sai 100% reaproveitado na P5.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P1 fechada (fase e período do VBlank medidos) |
| **Veículo** | **KMDOD** (`DxgkInitializeDisplayOnlyDriver`) — ele **já está em Código 0** e **não** está bloqueado pelo Código 43, que é do caminho `DxgkInitialize` |
| **Ferramenta** | Leitura de `OTG_STATUS_FRAME_COUNT` no caminho de VSync do KMDOD + WDK/headers para a verificação offline |
| **Quem executa** | Eu implemento e a verificação de WDK; o Daniel roda 1 bind de KMDOD |
| **Entregável** | KMDOD com VSync derivado do hardware + documento respondendo os 4 riscos da P5 |
| **Custo** | **Zero escrita em hardware.** 1 corrida de KMDOD, que é o perfil seguro e já provado |
| **PASS da etapa** | KMDOD em Código 0 com VSync do frame counter físico, DWM sem stutter mensurável, e os 4 riscos da P5 respondidos por documentação |
| **FAIL / abandono** | Se o `dxgkrnl` recusar VSync derivado de polling, cair para timer e **registrar a dívida** — não insistir |
| **Fecha uma dívida** | Item 61 hoje é ✅ com timer. Isto o torna honesto perante a Lei 3 (Zero Fakes) |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | Fase e período do VBlank medidos | ✅ | Polling read-only com período estável (~56,13 Hz medidos no silício com zero escrita) |
| 61b | **Trocar o timer de VSync pela leitura do OTG** | ✅ | KMDOD em Código 0 com VSync derivado do frame counter físico lido em BAR5+`0x14030` (`evidence/2026-09-03_10-01-10-...`) |
| — | Verificar no WDK os 4 riscos da P5 | 🟡 | **RESPONDIDO EM PARTE PELA FONTE NORMATIVA (06/09, `P1`)** — leitura de `referencias/windows-driver-docs/.../display/`: **(a)** flip de hardware não é opcional no papel: `direct-flip-of-video-memory.md` marca *Driver implementation — Full graphics: **Mandatory***, e `wddm-driver-and-feature-caps.md` lista `SupportDirectFlip` e `FlipCaps.FlipOnVSyncMmIo` como **M** para Full Graphics. **(b)** VidPn de modo único é legítimo: `enumerating-cofunctional-vidpn-source-and-target-modes.md` manda ajustar **somente** os mode sets **sem** modo pinado — um source com modo pinado deve ficar intacto. **(c)** VSync por polling **não** é tratado por nenhum destes documentos — segue **em aberto**. **(d)** o layout de `DXGK_PRESENTATIONCAPS` foi conferido contra `d3dkmddi.h` do WDK 10.0.26100 e o valor reportado `0x1006C903` decodifica exatamente nos campos que o código escreve. **Fecha quando (c) tiver resposta escrita.** |

---

## 🟡 P3 — Diagnóstico com **uma máquina só**

> **Objetivo:** saber o `NTSTATUS` e o ponto exato em que o `dxgkrnl` desiste do adaptador.
> **Mudança de plano:** **não temos segunda máquina, então KDNET está fora.** A estratégia
> passa a ser em quatro camadas, todas de uma máquina só, em ordem crescente de custo.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | Nenhum para a camada 1 (o dump já existe no disco) |
| **Camada 1 — dump pós-morte** | `C:\Windows\MEMORY.DMP`, 1,2 GB, de 02/09 18:00, **nunca aberto**. `cdb.exe` já instalado. Exige **PowerShell elevado**. Custo: minutos |
| **Camada 2 — telemetria interna ampliada** | Já provou valor: foi o anel de 16 DDIs + bitmap de QAI (schema 134) que produziu o encerramento do Gate 1.5. Ampliar para persistir estado e status **antes** do `StopDevice`. Custo: build + 1 corrida |
| **Camada 3 — ETW autologger do `dxgkrnl`** | `Microsoft-Windows-DxgKrnl-Diagnostic` numa sessão **AutoLogger** (persistente, começa no boot), que é o que permite capturar uma falha 5 s após o bind e ler depois do reboot. Verificar offline se o canal existe e o que expõe. Custo: 1 corrida |
| **Camada 4 — bugcheck deliberado** | O próprio driver chama `KeBugCheckEx` no ponto de interesse **carregando o `NTSTATUS` nos parâmetros**. Vira um "breakpoint" que o dump captura. Custo: 1 corrida + reboot |
| **Sobre `windbg -kl`** | O doc 124 diz que não serve. **Correção parcial:** local KD não permite breakpoint, `g`, step nem call stack — mas **permite ler memória e rodar extensões** (`!dxgkrnl.*`). Útil para inspeção, não para fluxo. Marcar como `H` até confirmar que as extensões respondem |
| **Quem executa** | O Daniel roda o `cdb` elevado; eu escrevo a instrumentação e leio a saída |
| **PASS da etapa** | Bugcheck identificado com stack, **ou** o `NTSTATUS` exato da desistência capturado por qualquer das quatro camadas |
| **FAIL aceitável** | O dump ser de outra causa. Registrar e seguir — a P4 **não** depende disto |
| **Regra da camada 4** | Só atrás de tunable, default desligado, com o harness de rollback armado. Um bugcheck deliberado é uma escolha, não um acidente |
| **Se um dia aparecer** | Qualquer PC ou notebook na mesma rede serve de host KDNET. Se surgir um, a camada 1–4 continua válida e o KDNET vira bônus |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| 176 | Analisar `C:\Windows\MEMORY.DMP` | ✅ | `cdb.exe` rodado em 02/09: Bugcheck 0x113 (0x1C) em `SetRotationSupport+0x137` por valor residual `0x29B` em `RotationSupport` de `bc250mcdm` na DDI `EnumVidPnCofuncModality` |
| 121b | Telemetria persistida antes do `StopDevice` | ⬜ | Status e argumentos de cada DDI sobrevivendo ao teardown e legíveis pós-reboot |
| 174 | ETW / WPP / IFR com AutoLogger | ⬜ | Trace de alta resolução capturado atravessando o reboot, sem persistir em ISR/DPC |
| — | Bugcheck deliberado com `NTSTATUS` nos parâmetros | ⬜ | Minidump trazendo o status exato com que o `dxgkrnl` abandonou o adaptador |
| 126 | Símbolos internos do `dxgkrnl` | 🟡 | Validados com `x`; fallback documentado |
| 175 | ~~Runbook KDNET~~ | 🧹 | **Fora do plano por falta de segunda máquina.** Rebaixar o doc 21 e marcar as camadas 1–4 como o caminho oficial |
| 124 | Runbook WinDbg local | 🧹 | Corrigir o texto: `-kl` **lê** memória e roda extensões; não faz breakpoint/stack/`g` |

---

## 🟢 P4 — Código 43 superado · WDDM Display+Render ativo em Código 0

> **Objetivo:** o adaptador WDDM permanecer em Código 0 em vez de ser desmontado no retorno
> do `StartDevice`.
> **Estado:** **FECHADA NO SILÍCIO (02/09 e 03/09).** Código 0 sustentado por mais de 500 s
> sob `RenderNode=1`, com DMM, VidPn, alocações e DWM operacionais (`DRIVERCAPS` e `QUERYSEGMENT4` respondidos).
> Os itens centrais desta etapa foram promovidos à **Tabela 1** (itens 90, 91, 92, 97, 102, 103 e 133).

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | **P0 fechada** (fusível anti-loop provado) **e** P3 camada 1 (o dump) não apontar defeito nosso. Sem o fusível, um FAIL pode custar boot em loop e recuperação manual |
| **O que já está pronto** | Candidato **3.81 assinado e auditado**: seção `.HW` no INF com `InstalledDisplayDrivers` / `UserModeDriverName` / `Wow`, e `bc250umd.dll` real com `OpenAdapter10/10_2/11/12` e `pfnCreateDevice = E_NOTIMPL` fail-closed |
| **Hipótese sob teste** | O adaptador é recusado por não declarar user-mode driver. É a **única hipótese viva** — sources/children já foi refutado (12 corridas), e as recusas de `QueryInterface` também |
| **Ferramenta** | Harness fire-and-collect: bind → janela fixa → retrato → rollback → reboot, sem intervenção |
| **Quem executa** | O Daniel arma; eu leio o anel de DDI e o bitmap de QAI |
| **Custo / orçamento** | **2 corridas.** Boot frio entre elas |
| **PASS da etapa** | `bc250mcdm / Código 0` **sustentado** por 5 amostras de PnP **e** o bit de `DRIVERCAPS` aceso em `V5QaiTypeSeenLo` |
| **PASS parcial que ainda vale** | Avançar no anel de DDI e falhar em ponto **novo** já é informação — muda a hipótese |
| **FAIL / abandono** | 4 corridas sem avanço no anel → parar de gastar corrida e escalar para a P3 camada 4 (bugcheck deliberado) |
| **Risco conhecido** | 3.81 é display+render (1 source / 1 child) **sem DCN**. Se o `dxgkrnl` aceitar, ele tira o GOP e a tela apaga. O rollback + reboot do harness é o que torna isso aceitável |
| **Proibido** | Rearmar sem boot frio; rearmar sem ler a evidência da corrida anterior |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| **97** | **CÓDIGO 43 — gargalo único** | ✅ | **FECHADO NO SILÍCIO (02/09 e 03/09):** Promovido à Tabela 1; Código 0 sustentado por 121 s e >500 s; `DRIVERCAPS` perguntado e respondido (`bc250_display_log.txt`) |
| 133 | UMD declarado no INF | ✅ | **FECHADO NO SILÍCIO:** Promovido à Tabela 1; `dxgkrnl` aceitou o pacote e avançou por toda a inicialização WDDM |
| 134 | D3D UMD `bc250umd.dll` | 🧪 | DLL declarada e carregável na placa |
| 102 | Display+render em Código 0 | ✅ | **FECHADO NO SILÍCIO (02/09 e 03/09):** Promovido à Tabela 1; `ConfigManagerErrorCode: 0` e `Status: Iniciado` confirmados por CIM e PnPUtil sob `RenderNode=1` |
| 90 | `DxgkInitialize` | ✅ | **FECHADO NO SILÍCIO:** Promovido à Tabela 1; adaptador permaneceu ativo em Código 0 |
| 91 | `AddDevice` | ✅ | **FECHADO NO SILÍCIO:** Promovido à Tabela 1; executado com sucesso no ciclo de bind |
| 92 | `StartDevice` (1 source / 1 child) | ✅ | **FECHADO NO SILÍCIO:** Promovido à Tabela 1; retornou `STATUS_SUCCESS` com 1 source e 1 child; sem recusa |
| 103 | `QueryAdapterInfo` caps | ✅ | **FECHADO NO SILÍCIO:** Promovido à Tabela 1; `DRIVERCAPS` (`Type=1`) e `QUERYSEGMENT4` (`Type=11`) consultados e respondidos |
| 98 | `StopDevice` | 🧪 | Revalidar com trabalho WDDM pendente na fila |
| 99 | `RemoveDevice` | 🧪 | Revalidar recursos e ownership de display |
| 95 | `QueryChildRelations` | 🧪 | Validado no perfil display sem tela preta |
| 96 | `QueryDeviceDescriptor` | 🧪 | EDID coerente com o monitor real |
| 119 | `DxgkDdiPresent` retorna `NOT_SUPPORTED` | 🧹 | Superado pela implementação do pipeline Blt por SDMA |
| 120 | `SetDisplayPrivateDriverFormat` sucesso vazio | 🧹 | Removido da tabela de callbacks até existir contrato real |
| 122 | `QueryVidPnHWCapability` zera caps | 🟡 | Auditoria dizendo se zero é semanticamente correto |
| 127 | Matriz DDI | 🟡 | Saída renomeada como *structural-only* + testes semânticos reais |
| 130 | INF fonte vs pacote | 🟡 | Manifesto como fonte única, com catraca no build |

---

## 🟠 P5 — Present por blit no modo herdado · primeiro display sob WDDM

> **Objetivo:** o desktop do Windows ser desenhado **pelo nosso driver**, no modo que o UEFI
> deixou, com o frame produzido na GPU e copiado por SDMA para o scanout.
> **Marco:** é aqui que a BC-250 passa a desenhar a tela.
> **Estado (06/09 22:42, corrida `P5_VIDPN_PIVOT_CONVERGENCE`):** o caminho de display
> **executou de ponta a ponta** com o candidato ativo que carrega as correções da faixa A.
> Convergência em **18** `IsSupportedVidPn`, **132** `GetStdAllocData`, `CommitVidPn`,
> `SetVidPnSourceAddress (Segment=1, Addr=0xAB0000)`, **12 `Present` com 10 `SDMA=YES`** e
> `BLT_TRIPOD PASS` de um frame inteiro (`WPTR=RPTR=380`, fence `0x1004` sobre poison
> `0x43DAFFFE`, 11.059.200 bytes). **E a tela continuou preta**, porque a primária estava em
> branco: `NonZero=0/256` em **63 de 63** amostras. A engine copiou 11 MB de preto.
> **A barreira do desktop visível deixou de ser a P5 e passou a ser a UMD** — ver a linha
> nova na P7. O perfil **lab** (97/0) continua sem convergir e sem diagnóstico.
> Evidência: `docs/evidence/2026-09-06_22-42-09-.../ANALISE.md`.
> Diagnostico da tela preta ainda aberto. Na corrida de 09:04 o `Present` trouxe
> `hDeviceSpecificAllocation=NULL` e `SegmentId=12`, enquanto o tracker global resolveu
> `0xAC0000` para `0x8A0000` e o blt caiu em copia por CPU. Isso demonstra um caminho de
> origem invalido, mas nao prova qual buffer continha a imagem — **NÃO VALIDADO EM HARDWARE**.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P4 (Código 0) **e** P1 (framebuffer decodificado) **e** P2 (os 4 riscos respondidos) — **TODOS FECHADOS** |
| **Ferramenta** | SDMA — **já validado no silício** (itens 19, 48, 49, 63). O blit é uma cópia SDMA, não cópia por CPU |
| **Orçamento de banda** | 2560 × 1080 × 4 B ≈ 11,1 MB/frame → ~664 MB/s a 60 fps. VRAM→VRAM em GDDR6: irrelevante |
| **Exigência do chip** | Scanout **em VRAM real**. *SG display* está desligado no cyan skillfish por patch upstream — GTT e memória de sistema não servem |
| **Quem executa** | Eu implemento; o Daniel arma cada corrida |
| **Custo / orçamento** | **5 corridas.** Cada FAIL custa tela preta, recovery e boot frio |
| **PASS da etapa** | Código 0 sustentado, **imagem desenhada pelo nosso driver**, 10 min sem TDR nem corrupção, e o poison do scanout substituído **pela GPU** |
| **FAIL / abandono** | 3 FAILs com a **mesma assinatura** → voltar à P1 e ampliar o catálogo antes de gastar a quarta |
| **Aceito por projeto** | Sem trocar resolução, sem hotplug, sem VRR. Isso é v1 por decisão, não por limitação escondida |
| **Proibido** | Escrever em qualquer registrador de DCN. O takeover é **passivo** |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| **81** | **Takeover do GOP sem escrever no DCN** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; Código 0 sustentado por mais de 500 s com imagem viva contínua, sem tocar em registradores de escrita de DCN (`bc250_display_log.txt`). |
| — | **VidPn de modo único herdado sob `RenderNode=1`** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; `CommitVidPn` chamado com `pathCount=1`, modo 2560x1080@60Hz pinado, `hAlloc` real atribuído (`FFFFDE8CC4728B28`) em `evidence/2026-09-03_23-32-07-...`, superando definitivamente a topologia vazia. |
| 42 | Scanout / primary allocation em **VRAM real** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; Scanout decodificado em VRAM física real (`0xC0000000` / `0xF400000000ull`) herdado do UEFI GOP. Mapeamento de 128 MB em kernel (`0xFFFFC178FAC00000`). |
| — | **`DxgkDdiPresent` modelo blt por SDMA** | 🟡 | **AVANÇO CRÍTICO NO SILÍCIO (04/09):** VidPn comitado, dirty rects chegaram a `DxgkDdiPresent` e corridas anteriores registraram `SDMA=YES`. A corrida 09:04, porém, apresentou contrato de origem inconsistente (`hAlloc=NULL`, `SegmentId=12`), tradução pelo tracker global para `0x8A0000` e `SDMA=CPU_FALLBACK`. O tracker paralelo foi removido do candidato: o journal/snapshot canônico agora aceita `UpdatePageTable` antes de `SetRootPageTable`, isola por processo e resolve backing por PTE; entradas inconsistentes falham fechado e não escrevem no scanout. Testes offline nomeados: `gpuva_journal_contract_test`, `gpuva_space_contract_test` e `display_vidpn_test`. **Fecha quando:** corrida no silício provar origem válida, `RPTR==WPTR`, fence GPU com poison, bytes do destino conferidos e Área de Trabalho visível por 10 min. **NÃO VALIDADO EM HARDWARE**. |
| — | **Cadeia display->blt executada, primária em branco** | 🟡 | **MEDIDO NO SILÍCIO (06/09 22:42):** `CommitVidPn` + `SetVidPnSourceAddress(Segment=1, Addr=0xAB0000)` + 12 `Present` (10 `SDMA=YES`, zero `CPU_FALLBACK`) + `BLT_TRIPOD PASS` de 11.059.200 bytes com `RPTR==WPTR` e fence da GPU sobre poison. A origem, porém, estava **zerada em 63/63 amostras** (`NonZero=0/256, Hash=0x00000000`), logo o blt moveu preto. Causa da primária em branco: `dwm.exe` carregou `bc250umd.dll` e recebeu **770 × `E_NOTIMPL`** em `OpenAdapter10/10_2/12`, lançou em `KERNELBASE` e caiu (2 dumps de ~178 MB arquivados); sem DWM não há `Present`, e o adaptador degradou para Código 43 **depois** da coleta (`device-after.json` registra Código 0 com serviço `RUNNING`). **Fecha quando:** a primária chegar com pixels e a Área de Trabalho ficar visível por 10 min. **NÃO VALIDADO EM HARDWARE.** |
| — | **Convergência VidPn no perfil lab display+render** | ⛔ | **BLOQUEIO MEDIDO (06/09 20:34):** 97 `IsSupportedVidPn` (todos `Supported=1`) e 65 `EnumVidPnCofuncModality`, mas 0 `GetStandardAllocationDriverData` de primária, 0 `CommitVidPn`, 0 `SetVidPnSourceAddress` e 0 `Present`. Foi encontrado e corrigido um defeito objetivo: `EnumVidPnCofuncModality` alterava `ScalingSupport`/`RotationSupport` mesmo quando o campo era o pivô. A corrida falha teve 13 pivôs de cada tipo contra 2 na corrida boa. O teste offline `display_vidpn_test` cobre a preservação em `docs/evidence/2026-09-06_P5_VIDPN_PIVOT_CONVERGENCE_OFFLINE/`, mas se isso fecha a convergência é **NÃO VALIDADO EM HARDWARE**. O binder agora pula RADV sem primária+commit+source address do mesmo bind. |
| — | **Caps obrigatórios de Full Graphics não declarados** | 🟡 | **DIVERGÊNCIA ENCONTRADA POR LEITURA (06/09, `P1`+`H`):** `wddm-driver-and-feature-caps.md` marca como **M** para Full Graphics os campos `SupportNonVGA`, `SupportSmoothRotation` e `SupportDirectFlip` de `DXGK_DRIVERCAPS`. No caminho ativo sob `RenderNode=1` — o handler `McdmQAI` de `bc250_mcdm_callbacks.c` — os três ficam **FALSE**: o `RtlZeroMemory` inicial zera a estrutura e nenhum deles é escrito depois. O caminho KMDOD (`Bc250DxgkDdiQueryAdapterInfoDisplay`) escreve `SupportNonVGA=TRUE`, ou seja **os dois handlers do mesmo driver divergem**. `PresentationCaps=0x1006C903` foi decodificado contra `d3dkmddi.h` (WDK 10.0.26100) e confere com o código: `SupportKernelModeCommandBuffer=0` com `DriverSupportsCddDwmInterop=1` é a combinação documentada de "sem GDI acelerado, com interop CDD-DWM" — **essa** não é divergência. Que os três booleanos zerados expliquem a não convergência é **hipótese (`H`)**. **Fecha quando:** uma corrida com os três declarados produzir `GetStandardAllocationDriverData` + `CommitVidPn`, ou provar que não muda nada. **NÃO VALIDADO EM HARDWARE.** |
| — | **Duas obrigações do contrato de cofuncionalidade ainda não implementadas** | 🟡 | **LIDO NA FONTE NORMATIVA (06/09, `P1`):** `enumerating-cofunctional-vidpn-source-and-target-modes.md` lista, além do pivô já corrigido, dois deveres de `DxgkDdiEnumVidPnCofuncModality` que o driver não cumpre: **(1)** para cada source **com** modo pinado, reportar os métodos de multisampling via `pfnAssignMultisamplingMethodSet` — condicionado a existir codec com antialiasing, então pode ser legitimamente dispensável aqui; **(2)** para target com monitor conectado, listar **somente** modos que o monitor suporta, obtidos por `pfnAcquireMonitorSourceModeSet` — nunca chamado por nós. ⚠️ **Correção de registro (06/09):** a versão anterior desta linha dizia que o modo era anunciado a 60 Hz. Leitura do código refuta: o EDID sintetizado e o modo derivam de `BC250_NATIVE_PIXEL_RATE = 173.611.437 Hz` com os totais lidos do OTG, o que dá **56,13 Hz** — coerente com o silício. O dever (2) fica dependente do item 77 (EDID real por AUX), porque validar EDID sintetizado contra si mesmo é circular. **Fecha quando:** auditoria dizer se (1) é dispensável e (2) uma corrida provar se o mode set do target derivado do monitor muda a convergência. **NÃO VALIDADO EM HARDWARE.** |
| — | Caps de apresentação sem flip de hardware | ✅ | `PresentationCaps` ajustado (`NoScreenToScreenBlt=1, NoOverlapScreenBlt=1, CddDwmInterop=1, SwDeviceBitmaps=1`), aceito pelo DMM e DWM sem recusa. |
| — | **VSync do OTG ligado ao `dxgkrnl`** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; `DxgkDdiControlInterrupt` (Tipo 5) conectado ao frame counter de hardware do OTG (`BAR5 + 0x14030`). |
| — | **Sanitização da GDDR6 antes do 1º present** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; `Bc250SdmaPresentFill` opcode 11 CONST_FILL limpou 100% da VRAM do scanout físico (`evidence/2026-09-03_23-13-13-...`), eliminando ruído térmico e artefatos cinzas da tela. |
| **63** | **Display acelerado (SDMA0)** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; Firmware carregado via PSP (`status=0, PhysReady=1`), F32 des-haltado (`status=0`), tripé físico no StartDevice e 32 blits SDMA concluídos com sucesso. |

---

## 🔵 P6 — Render WDDM completo + TDR

> **Objetivo:** o compute já provado no silício passar a ser **dirigido pelo Windows**, com
> memória, objetos, submissão, fence, retirement e recuperação de hang.
> **Pode correr em paralelo** à P5, depois que a P4 fechar.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P4 (Código 0). A P5 **não** é pré-requisito desta |
| **Ativo de partida** | Tudo da Tabela 1: SDMA, CP/PM4, MEC/HQD, fence física, retirement. Falta o **contrato WDDM** em volta |
| **Ferramenta** | Harness D3DKMT (`OpenAdapterFromLuid → CreateDevice → CreateContextVirtual → CreateAllocation → Submit`) |
| **Tripé obrigatório** | Em cada item de submissão: `RPTR==WPTR`, fence física em VRAM com poison, conferência byte a byte |
| **Regra absoluta** | **Proibido completar fence pela CPU** em qualquer hipótese |
| **Custo** | A maior fase de kernel. Muitas corridas, mas a maioria sem risco de display se a P5 ainda não tiver assumido a tela |
| **PASS da etapa** | Submissão originada pelo `dxgkrnl` fisicamente aposentada e reconhecida pelo scheduler |
| **Gate de saída — não negociável** | **Item 25 (TDR).** Hang induzido → detectado → engine resetada → adaptador volta a aceitar submissão → display sobrevive. Sem isso, **não** se entra na P7: todo bug de shader viraria reboot |
| **FAIL / abandono** | Item por item, com bissecção. Não empilhar mudanças num mesmo candidato — foi isso que invalidou a corrida 3.75 |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| **—** | **Causa-raiz do VidPN vazio resolvida** | ✅ | **FECHADO NO SILÍCIO (03/09, corridas 21:18 a 23:32):** Implementação de `Bc250McdmGetStandardAllocationDriverData` para SharedPrimary/Shadow/Staging/GDI e a declaração do modelo de memória (`GpuMmuSupported`; **não** `IoMmuSupported`, ver correção de registro na Tabela 1) destravou a criação de alocações pelo VidMm, gerou `hAlloc` real e eliminou a topologia vazia (`pathCount=1`). Promovido à Tabela 1. |
| 104 | Nodes / engines WDDM | 🟡 | Node 0 SDMA e Node 1 Compute expostos e consumidos pelo scheduler. |
| 111 | Harness D3DKMT | ⬜ | Cadeia completa até `Submit` sem erro |
| 105 | `CreateDevice` | 🟡 | DWM e processos do sistema criando device context com sucesso sob `RenderNode=1`. |
| 106 | `CreateContext` / Virtual | 🟡 | Contextos de paging e present ativos e operacionais. |
| 107 | `HwQueue` | ⬜ | Create/Destroy/Submit por fila real, sem leak |
| 108 | `CreateAllocation` | 🟡 | Alocações SharedPrimary, GDI, Staging e Shadow criadas e aceitas pelo `dxgkrnl`. |
| 37 | Allocations WDDM | 🟡 | Create/Open/Close com ownership real exercitados na inicialização do DMM. |
| 38 | Segments WDDM | ✅ | **FECHADO NO SILÍCIO (03/09):** Segmento 1 (VRAM BAR0 256MB) e Segmento 2 (Aperture 512MB) consumidos pelo VidMm via `QUERYSEGMENT4`. |
| 39 | Residency | ⬜ | `MakeResident`/`Evict` com accounting conferido |
| 40 | Paging buffer (`UpdatePageTable`) | 🟡 | `BuildPagingBuffer` Op 11 retornou sucesso para Lvl 0/1 e o journal publicou updates, mas isso é construção CPU-side. **Correção 07/09:** a corrida das 14:32 tem zero Op 11 e zero submit no log delimitado; ocorrências no log acumulado não pertencem ao bind. Fecha somente com o DMA de paginação originado pelo VidMm e o tripé físico arquivado. **NÃO VALIDADO EM HARDWARE.** |
| 30 | VMID0 page table | 🟡 | Só promover depois da GPUVA completa |
| 31 | GPUVA core (34 bits traduzíveis) | 🟡 | **PARCIAL NO SILÍCIO:** em 06/09 a root `SegmentId=1, Offset=0` e a caminhada CPU/PT coincidiram. Em 07/09 13:59, o harness isolado executou IB SDMA sob VMID8 com tripé completo (fence em 11 ms e 4 dwords); PDE corrompida gerou `MAP_ERR=1` em `0x201000` e reteve poison. Isso valida o mecanismo isolado, não a submissão WDDM nem Node 1. |
| 32 | Journal GPUVA | 🟡 | **PARCIAL NO SILÍCIO:** após representar a root zero por chave interna não nula, flushes publicaram commits globais até `Generation=9`. Concorrência, rollback e teardown completos ainda **NÃO VALIDADOS EM HARDWARE**. |
| 33 | Snapshot GPUVA | 🟡 | **PARCIAL NO SILÍCIO:** `IB_GPUVA_SNAPSHOT Status=0` nas duas submissões de paginação da corrida 20:34. Snapshot para submissão Node 1 ainda **NÃO VALIDADO EM HARDWARE**. |
| 34 | Gate GPUVA | 🟡 | `GpuvaLabPromotion=1` exercitada com commits físicos de TLB (`AckObserved=1`); o gate permitiu snapshots Node 0, mas `vkCreateDevice` não chegou ao submit Node 1. |
| 35 | Map / unmap GPUVA | 🟡 | O conjunto global MMU + VMID8 foi provado no harness isolado em 07/09 13:59. No WDDM, `StartDevice`/`SetRootPageTable` agora falham fechado e rollback/restore têm readback; fecha quando map/unmap do VidMm produzir tripé físico e teardown exato no mesmo bind. **NÃO VALIDADO EM HARDWARE.** |
| 36 | Root page table por processo | 🟡 | Criada, associada, invalidada e destruída pelo `dxgkrnl` |
| 109 | Patch / Render / RenderKm | 🟡 | Semântica real; `NOT_SUPPORTED` não fecha |
| — | **Cadeia causal do hang do RADV** | 🟡 | O contrato normativo (`P1`) continua dizendo que updates de page table entram no motor de paginação e concluem por fence. O ensaio isolado das 13:59 refutou a forma forte da hipótese antiga — “IB sob VMID8 nunca executa” — porque ele executou com tripé. Permanecem hipóteses distintas: integração WDDM da MMU global, submissão/fence de paginação e caminho Node 1. A corrida das 14:32 não testou nenhuma delas: MMU `SNAPSHOT_FAILED`, zero Op 11/submit no log delimitado e RADV pulado. Fecha quando `vkCreateDevice` retornar após tripé de paginação WDDM no mesmo bind. **NÃO VALIDADO EM HARDWARE.** |
| 110 | `SubmitCommandVirtual` | 🟡 | **VALIDADO NO SILÍCIO (03/09):** Scheduler despachou 32 submissões no Node 0 SDMA com sucesso (`Submitted=32, Completed=32, Fallbacks=0`). |
| 112 | Fence WDDM | 🟡 | Fences físicas 1 a 32 notificadas ao scheduler via `DMA_COMPLETED`. |
| 113 | Interrupt / DPC WDDM | 🟡 | Completion físico de VSync e interrupções em runtime. |
| 23 | IH de compute | 🟡 | Interrupção real de completion observada |
| 114 | Retirement WDDM | 🟡 | Scheduler reconhecendo completion física para 32 submissões no Node 0 SDMA. |
| 51 | Multi-context | ⬜ | Isolamento entre processos/contextos verificado |
| 52 | Preemption | ⬜ | Preempt com fence coerente |
| 115 | Preemption WDDM | ⬜ | Preempt + fence/timeout pelo contrato WDDM |
| 53 | Cancel command | 🧪 | Cancelamento real com submissão pendente |
| 54 | Prioridades | ⬜ | Nodes/engines/prioridades honrados |
| 55 | Wrap do ring | ⬜ | Stress de wrap sem sobrescrever trabalho vivo |
| 56 | Concorrência | ⬜ | Multithread/process, locks e teardown sem corrupção |
| 24 | Reset de engine | 🟡 | Reset WDDM por node **preservando o display** |
| **25** | **TDR** | ⬜ | **Gate de saída da P6.** Hang induzido detectado, engine resetada, adaptador volta a aceitar submissão, display sobrevive |
| 116 | `ResetFromTimeout` / `RestartFromTimeout` | ⬜ | Quiesce completo e recuperação sem perder o display |
| 117 | `CollectDbgInfo` | 🟡 | Dump útil produzido durante TDR real |
| 118 | Power callbacks | 🟡 | F-states reais, ou não anunciar o recurso |

---

## 🟣 P7 — UMD e Vulkan · a maior fase restante

> **Objetivo:** substituir o `E_NOTIMPL` honesto de hoje por backend real, até jogos
> traduzidos rodarem.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P6 **completa, incluindo o item 25 (TDR)**. Sem TDR, cada bug de shader é um reboot |
| **Ponto de partida** | `bc250umd.dll` fail-closed já existe e carrega. O ICD Vulkan é esqueleto |
| **Estratégia** | Winsys RADV adaptado aos objetos WDDM deste driver. **Não assumir que o RADV do Linux roda inalterado** |
| **Ordem obrigatória** | ABI → memória → queues → command buffers → submit → sync → compute → graphics → WSI → RADV → shader compiler → camadas de API |
| **Regra de verificação** | Em cada subfase, provar que o trabalho saiu na GPU: fence física + conferência de dados. `vulkaninfo` listar a placa **não** é prova |
| **Custo** | A maior fase do projeto em linhas de código |
| **PASS da etapa** | Suites de conformidade de DXVK e VKD3D passando **sem WARP e sem fallback**, confirmado por GPU counters |
| **FAIL / abandono** | Subfase por subfase. Uma subfase que não fecha não avança para a seguinte |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | **`P7_D3D11_CAPS_PROBE` — instrumentação de caps antes do device** | 🧪 | **EXPERIMENTO BARATO DEFINIDO (06/09).** Implementar `OpenAdapter10`/`OpenAdapter10_2` **apenas até os caps**: devolver tabela de adapter válida, responder `pfnGetSupportedVersions` e `pfnGetCaps` (`THREADING`, `SHADER`, `3DPIPELINESUPPORT`) e **falhar fechado em `pfnCreateDevice`**. Não é fallback nem cap falso — é instrumentação honesta que faz o runtime revelar, em **uma** corrida, qual interface ele fecha, quais caps valida e o que pede depois. Responde as três perguntas que a leitura não respondeu: se o `dwmcore` aceita feature level 10_1 (`H`), quais combinações de caps ele recusa, e qual header corresponde ao minor 17. **IMPLEMENTADO OFFLINE (06/09).** `driver/umd/bc250_umd_caps_probe.c` (lógica pura de decisão, TU isolado para não arrastar `d3d12umddi.h`), leitura do registro em `bc250_d3d_umd.c`, e instrumentação em `GetSupportedVersions`, `GetCaps` (com decodificação do `D3D10_2DDICAPS_TYPE` por nome) e `CreateDevice` (que loga a **interface acordada** e segue `E_NOTIMPL`). Armado por `tools\arm-d3d11-caps-probe.ps1 -Arm -IAcceptBlackScreenRisk`, chave `HKLM\SOFTWARE\BC250\Umd\D3D11CapsProbe`; **default desarmado** — sem a chave, o comportamento é idêntico ao de hoje. `BC250_D3D10_11_NATIVE_READY` permanece **0**: o probe concede instrumentação, nunca device. Travado por 2 casos novos em `bc250_d3d_umd_test` (tabela de decisão nos 4 estados + decodificação dos caps, incluindo que tipo desconhecido não pode inventar nome). `build-umd.bat` e `build-contract.bat` em `EXIT=0`. ⚠️ **CORRIDA v1 GASTA E FALHADA (06/09 23:45) — o defeito era meu.** A v1 aceitava **qualquer** `Interface`. O runtime pediu `0x000B0011`, respondemos `S_OK`, e ele **não chamou mais nenhuma DDI**: zero `GetSupportedVersions`, zero `GetCaps`, zero `CreateDevice`, e **66 ciclos** de carga/descarga da DLL dentro do `dwm.exe`. `OpenAdapter10` **nunca** foi chamado — por aceitarmos o degrau mais alto, o runtime **nunca desceu a escada** até o D3D10_1, que é justamente onde o probe queria observar os caps. Pior: devolver `S_OK` para um DDI não implementado é **declaração de capacidade falsa, proibida pela Lei 4**. **Aprendizado `P1` que nenhuma leitura daria:** a negociação de DDI é uma **escada descendente cujo degrau é escolhido pela recusa da UMD** — aceitar interrompe a descida, e não há re-negociação para baixo depois de um `S_OK`; e o runtime **não** usa `pfnGetSupportedVersions` como primeiro contato. **v2 (07/09, offline):** `Bc250UmdProbeAcceptsInterface` — allowlist de **um** item (`0x000A0002`), recusando o resto com `E_NOTIMPL` para a escada voltar a funcionar. Travada por `TestProbeInterfaceAllowlist`. ✅ **CORRIDA v2 (07/09 08:05) — a correção FUNCIONOU e está provada no silício.** Sequência medida: `0x000B0011` recusado (66×) → `0x000B000B` recusado (66×) → **`OpenAdapter10` com `0x000A0002` aceito com `S_OK` (22×)**. Na v1 o `OpenAdapter10` nunca era chamado. **Isso promove a `P1`** o que era `H`: a negociação de DDI é uma escada descendente e o degrau é escolhido pela **recusa** da UMD. ⚠️ **Mas o probe parou num ponto novo:** depois do `S_OK` a próxima linha é uma nova carga da DLL — zero `GetSupportedVersions`, zero `GetCaps`, zero `CreateDevice`. As perguntas 1 e 3 do doc 173 **seguem sem resposta**. Causa do impasse diagnóstico: **2 das 3 entradas de `D3D10DDI_ADAPTERFUNCS` eram mudas** (só `CreateDevice` registrava), tornando impossível distinguir “não chamou nada” de “chamou e não registramos”. **v3 (07/09, offline):** `pfnCalcPrivateDeviceSize` e `pfnCloseAdapter` passaram a registrar — as três entradas falam. Duas hipóteses vivas (`H`): (a) o caminho de 3 entradas é beco sem saída para o D3D11 porque não tem `pfnGetCaps`, e o alvo teria de ser `0x000A0002` **via `OpenAdapter10_2`** (5 entradas); (b) ele chamou `CalcPrivateDeviceSize` e desistiu ali. A v3 distingue as duas em uma corrida. Evidências: `docs/evidence/2026-09-06_23-45-38-.../ANALISE.md` e `docs/evidence/2026-09-07_08-05-47-.../ANALISE.md`. **Fecha quando:** o log trouxer a interface acordada e o caps set consultado. **NÃO VALIDADO EM HARDWARE.** |
| 132 | ABI KMD/UMD versionada | 🟡 | Handles, memória, queues e erros versionados e exercitados |
| 135 | ICD Vulkan | 🟡 | **PARCIAL (06/09 20:34):** ICD explícito carregado, instância/dispositivo físico/famílias enumerados; a chamada `vkCreateDevice` não retornou em 20 s. Carregar/enumerar não prova aceleração. |
| 136 | Enumeração pelo loader | ⬜ | `vulkaninfo` lista a BC-250 **sem fallback**, confirmado por GPU counters |
| 137 | Physical device properties | ⬜ | Caps derivadas do hardware; features não suportadas negadas |
| 138 | Memória Vulkan | ⬜ | Heaps/types coerentes com os segments da P6 |
| 139 | Queues | 🟡 | Dois contextos WDDM Node 1 foram criados durante `vkCreateDevice`, mas não houve submissão Node 1; criação completa da queue permanece **NÃO VALIDADA EM HARDWARE**. |
| 140 | Command buffers | ⬜ | PM4/IB gerado pelo ICD e conferido antes do kick |
| 141 | Submit | ⬜ | Submissão via `dxgkrnl` com fence física e tripé |
| 142 | Sync | ⬜ | Fence/semaphore/event físicos |
| 143 | Compute | ⬜ | Compute shader via ICD conferido byte a byte na VRAM |
| 144 | Graphics | ⬜ | Rasterização/images/renderpass produzindo pixels na GPU |
| 145 | WSI | ⬜ | Swapchain e present pelo caminho blt da P5 |
| 146 | RADV winsys para Windows | 🟡 | **ATIVO EXTERNO IDENTIFICADO (06/09, `P1`):** existe um port oficial de RADV para Win32 em andamento — Collabora, **financiado pela Valve**, branch `wddm2` de `gitlab.freedesktop.org/lfrb/mesa` (Louis-Francis Ratté-Boulianne). Já rodou **Counter-Strike 2** no Windows. Arquitetura: RADV como UMD Vulkan falando **D3DKMT**, com os *private driver data* do KMD da AMD **obtidos por engenharia reversa**. **Consequência para nós:** como **nós** somos o KMD, o caminho certo é **adaptar o winsys deles para emitir o nosso** `BC250_ALLOCATION_PRIVATE_V1` — não imitar a ABI da AMD. O que se aproveita de graça: build Win32, esqueleto de winsys D3DKMT, memória, filas, submissão e sync. **Não resolve o desktop visível** (ver P7 abaixo): RADV é Vulkan, e o `dwm.exe` exige **D3D10/11**. A WSI da branch deles é caminho de CPU. **ROTA B ADOTADA em 06/09:** este item deixou de ser só "P7 de jogos" e passou a ser **infraestrutura compartilhada** — o mesmo backend atende o D3D11 que o `dwm.exe` exige (via DXBC→SPIR-V) e o Vulkan dos jogos. **Fecha quando:** pipeline gráfico completo estável em long-run sobre o nosso KMD. |
| — | **ITEM ZERO — handshake `DXGKQAITYPE_UMDRIVERPRIVATE` (Type=0)** | 🟡 | **ACHADO POR MEDIÇÃO (07/09 08:25) — bloqueia todo o resto da P7.** As três corridas do `P7_D3D11_CAPS_PROBE` provaram que **nenhuma** entrada do adapter table da UMD é chamada (`CalcPrivateDeviceSize`, `CloseAdapter`, `CreateDevice`, `GetCaps`, `GetSupportedVersions` = **0**), e que o runtime descarta a DLL **sem nem fechar o adapter**. O log do KMD, na mesma corrida, mostra `[McdmQAI] Type=0, OutSize=2400` → **`Type=0 NOT SUPPORTED`, 80 vezes**: `DXGKQAITYPE_UMDRIVERPRIVATE` **não tem `case`** em `Bc250McdmQueryAdapterInfo` e cai no `default`. É o tipo **mais pedido** da corrida (160 chamadas, contra 1–2 dos demais) e o **único recusado em massa**. **Favorece a rota B:** o blob é **nosso nos dois lados**, sem ABI da AMD para imitar. `H`: que a recusa seja *a* causa do descarte — é a hipótese mais forte e coerente com toda a cadeia, mas `mcdm-implementation-guidelines.md` lista o tipo como **opcional** (para MCDM, que não é nosso perfil), e nada do que li o declara obrigatório para Full Graphics. **IMPLEMENTADO OFFLINE (07/09).** Contrato em `driver/core/bc250_umd_private_contract.c` — blob versionado (`Magic`+`Version`+`SizeBytes` primeiro, para UMD antiga contra KMD novo **falhar fechado** em vez de ler lixo), zero ponteiros na fronteira, e `GateFlags` declarando explicitamente **o que não existe** (`NO_3D_PIPELINE`, `NO_SHADER_COMPILER`, `NO_NODE1_SUBMIT`, `NO_IB_UNDER_VMID8`, `CAPS_PROBE_ONLY`) para a UMD não inferir capacidade do silêncio — foi assim que o probe v1 declarou suporte a um DDI inexistente. **KMD:** `case DXGKQAITYPE_UMDRIVERPRIVATE` em `Bc250McdmQueryAdapterInfo`, falhando fechado em buffer curto **sem escrever byte parcial**, tratando `pInputData` como não confiável (só o tamanho), e tirando os tamanhos de `runtimeCaps` — **a mesma fonte de verdade do `QUERYSEGMENT4`**, para não criar segunda fonte como aconteceu no A1. **UMD:** `Bc250UmdFetchPrivateBlob` chama `pfnQueryAdapterInfoCb` e **valida antes de publicar a tabela de adapter** — sem handshake, `E_NOTIMPL`. Travado por `umd_private_contract_test` (5 casos: round-trip, gates declarados, buffer curto sem escrita parcial, recusa de magic/versão/tamanho/incoerência, e recusa de fatos impossíveis no produtor). `build-contract.bat` e `build-umd.bat` em `EXIT=0`, auditoria em `PASS`. ✅ **VALIDADO NO SILÍCIO (07/09 09:40) — o handshake funciona de ponta a ponta:** **112** publicações no KMD (`UMDRIVERPRIVATE: 104 bytes publicados`), **zero** `NOT SUPPORTED`, e **32** validações na UMD (`blob OK -- v1, 104 bytes, NodeCount=2, GpuVaBits=48, GateFlags=0x1B`). O runtime pediu com `OutSize=2400` e **voltou a pedir com `OutSize=104`** — ele aprendeu o nosso tamanho, prova de que consumiu a resposta. ❌ **MAS A HIPÓTESE CAIU:** mesmo com o handshake funcionando, as entradas do adapter table seguem com **zero** chamadas (`INTERFACE ACORDADA`, `CalcPrivateDeviceSize`, `CloseAdapter`, `GetCaps`, `GetSupportedVersions` = 0). **A recusa do `Type=0` NÃO era a causa do descarte** — o critério estava escrito antes da corrida e foi honrado. **Suspeito restante, geométrico:** o runtime só oferece `0x000A0002` pelo `OpenAdapter10`, cuja tabela de **3 entradas não tem `pfnGetCaps`**; sem ele não há como perguntar feature level, e ele nunca pede `0x000A0002` pelo `OpenAdapter10_2` (5 entradas). **O handshake não foi desperdício:** é infraestrutura que a rota B exige de qualquer forma — o winsys do RADV precisa deste canal. Evidência: `docs/evidence/2026-09-07_09-40-57-.../ANALISE.md`. |
| — | **`P7_D3D11_CAPS_PROBE` v4 — allowlist por entry point + honestidade do caps** | 🧪 | **IMPLEMENTADO OFFLINE (07/09) após a refutação da hipótese do item zero.** `OpenAdapter10_2` passa a aceitar **`0x000B000B`** (`D3D11_0_7_DDI_INTERFACE_VERSION`), publicando a tabela de **5 entradas** — o **único caminho medido** para o `pfnGetCaps` existir; `OpenAdapter10` segue só com `0x000A0002`. O minor 17 continua fora (sem nome no WDK atual; declarar suporte ao que não se sabe nomear é chute). **Por que não é o erro da v1:** ali eu devolvia `S_OK` sem ter como qualificar a resposta — cap falso puro. Aqui aceito a **abertura** e declaro a verdade por `pfnGetSupportedVersions` (D3D10_0/D3D10_1), que é o mecanismo desenhado pela Microsoft para isso; **abrir não é prometer**, e `pfnCreateDevice` segue fail-closed. **Regra nova de honestidade:** zerar o `pfnGetCaps` é verdade para conjuntos de flags, mas **mentira** para `D3D11DDICAPS_3DPIPELINESUPPORT`, cujo payload é um `D3D11DDI_3DPIPELINELEVEL` onde **`0` == nível `10_0`** — zerar declararia um pipeline 3D inexistente, então esse tipo passa a **falhar**. Codificado em `Bc250UmdCapsZeroIsTruthful` e travado por teste. `build-umd.bat` e `build-contract.bat` em `EXIT=0`, auditoria `PASS`. ❌ **CORRIDA v4 (07/09 10:00) — a segunda hipótese também CAIU.** `OpenAdapter10_2` aceitou `0x000B000B` e publicou as **5 entradas** com `S_OK` (58 ciclos no `dwm.exe`), o handshake `UMDRIVERPRIVATE` seguiu perfeito (**142** publicações, zero recusas) — e o runtime chamou **zero** funções outra vez: `GetSupportedVersions`, `GetCaps`, `CreateDevice`, `CalcPrivateDeviceSize` e `CloseAdapter` todos em **0**. **Não era falta de `pfnGetCaps`.** **A pista decisiva:** o runtime **nunca chama `pfnCloseAdapter`** — um runtime que quer descartar um adapter chamaria, a não ser que esse ponteiro esteja **nulo do ponto de vista dele**. E o header descreve o campo como `pAdapterFuncs_2` **points to** uma `D3D10_2DDI_ADAPTERFUNCS`, com a doc dizendo que a tabela é *returned **in** the member* — ou seja, a convenção de `CREATEDEVICE.pDeviceFuncs`, marcada `in/out`: **o runtime aloca a struct e o driver preenche os membros**. Nosso código **sobrescrevia o ponteiro**, deixando o runtime a ler a struct dele, zerada. Isso explica tudo: nenhuma função chamada, `CloseAdapter` incluso, e descarte imediato. Evidência: `docs/evidence/2026-09-07_10-00-25-.../ANALISE.md`. |
| — | **`P7_D3D11_CAPS_PROBE` v5 — convenção de preenchimento da tabela de adapter** | ✅ | **IMPLEMENTADO OFFLINE (07/09) — `H` forte, e é defeito NOSSO se confirmar.** Cinco corridas eliminaram step-down, `UMDRIVERPRIVATE` e forma da tabela; sobrou a convenção. `OpenAdapter10`/`OpenAdapter10_2` passam a **preencher a struct que o runtime fornece** (`*pOpenData->pAdapterFuncs = tabela`) quando o ponteiro vem não-NULL, em vez de **sobrescrever o ponteiro** — e registram qual dos dois casos ocorreu, para a corrida decidir em vez de eu supor. `build-umd.bat` e `build-contract.bat` em `EXIT=0`, auditoria `PASS`. ✅ **CONFIRMADO NO SILÍCIO (07/09 10:11) — era defeito nosso, e o probe cumpriu a missão.** `pAdapterFuncs_2` vem **não-NULO** (`runtime forneceu pAdapterFuncs_2=0x... -> PREENCHENDO a struct dele`): o runtime aloca a struct e o driver preenche os membros. Prova de que pegou: **`pfnCloseAdapter` chamado 95 vezes**, contra **zero** nas quatro corridas anteriores. E pela primeira vez o runtime conversou: **`pfnGetSupportedVersions` 90 chamadas** (declaramos D3D10_0 + D3D10_1, e ele aceitou) e **`pfnGetCaps` 63 chamadas**. |
| — | **Backend D3D10/11 para o `dwm.exe`** | ⛔ | **BARREIRA ATIVA E ÚNICA DO DESKTOP VISÍVEL — CAMINHO DECIDIDO POR DUMP (06/09).** `cdb` sobre `dwm-crash-01-dwm.exe.8548.dmp`: bucket `APPLICATION_FAULT_FAIL_FAST_EXCEPTION_887a0004_dwmcore.dll!MilFailFastForHR`, com a pilha `CConnection::RunCompositionThread` → `CPartitionVerticalBlankScheduler::ProcessFrame` → `CComposition::PreRender` → `CLegacyRenderTarget::EnsureSwapChain` → `CDeviceManager::GetDevice` → `CD3DDevice::Create` → **`CD3DDevice::CreateD3D11Device`** → `MilInstrumentationCheckHR_MaybeFailFast` → `RaiseFailFastException(e0464645, 0x887A0004)`. **Três conclusões:** (1) o DWM exige device **D3D11** no caminho `CLegacyRenderTarget`; (2) ao receber `DXGI_ERROR_UNSUPPORTED` ele **fail-fasta, não cai para software**; (3) **`DriverSupportsCddDwmInterop` não é rota de fuga** — já está declarado (`PresentationCaps=0x1006C903`, bit 8) e o DWM foi para o D3D11 igual. Logo a hipótese "fazer o DWM aceitar CDD/interop" está **REFUTADA POR DUMP**, e um backend D3D10/11 real na `bc250umd.dll` é **obrigatório** para desktop visível. **RADV não cobre este item** (é Vulkan). **DIMENSIONADO EM 06/09** (`docs/173_DIMENSIONAMENTO_D3D11_PARA_DWM.md`, `P1`): o log da UMD mostra o runtime descendo a escada de DDI — `0x000B0011` (minor 17, sem nome no WDK 26100), depois `0x000B000B` = `D3D11_0_7_DDI_INTERFACE_VERSION`, e por fim `0x000A0002` = **`D3D10_1_DDI_INTERFACE_VERSION`**, que é o **piso**. Tamanho contado no header: tabela de adapter **3** funções (`OpenAdapter10`) ou **5** (`OpenAdapter10_2`, com `pfnGetSupportedVersions` e `pfnGetCaps`); tabela de device **105** entradas (`D3D10_1DDI_DEVICEFUNCS`) — 39 de recurso/view, 26 de estado fixo, 17 de shader, 8 de query, 10 de ciclo de vida, 5 de desenho. **Piso de feature level = 10_1:** os níveis 9_x do enum `D3D11DDI_3DPIPELINELEVEL` só existem a partir do DDI D3D11_1, logo "10level9" **não** é atalho aqui. O custo real não é a contagem: são o **compilador DXBC→ISA do GFX10**, o **pipeline 3D** (que o projeto não tem — só compute e SDMA) e os recursos sobre a nossa GPUVA. **Rota DECIDIDA em 06/09: B — DXBC→SPIR-V→RADV/ACO**, unindo esta obra ao item 146 em vez de construir dois backends (ver "Decisão de arquitetura vigente"). A decisão é de arquitetura, não resultado. 🎯 **ÚLTIMO PORTÃO NOMEADO E MEDIDO (07/09 10:11):** com a convenção corrigida, o runtime percorreu toda a negociação e pediu **exatamente um** cap antes do device — **`D3D11DDICAPS_3DPIPELINESUPPORT`, `DataSize=4`, `pInfo=NULL`** (63 chamadas). Nem `THREADING`, nem `SHADER`, nem `OPTIONS`. Recusamos, e a recusa é **correta**: `D3D11DDI_3DPIPELINELEVEL` não tem valor para "nenhum" — `0` já significa nível `10_0`. **Não existe truque de cap que passe deste ponto:** declarar um nível levaria o runtime a chamar `pfnCreateDevice` e as **105 entradas** inexistentes, trocando recusa honesta por crash com cap falso. **Logo, agora medido e não inferido: a única coisa entre este driver e um device D3D é um pipeline 3D real** (rota B). ✅ **REPRODUZIDO COM TOOLCHAIN DIFERENTE (07/09 11:08, VS2026 + WDK 28000, zero mudança de código):** mesma cadeia, mesmo `GetCaps Type=130` 63 vezes, mesmo ponto de parada — a conclusão não é artefato de compilador. **Fecha quando:** `CreateD3D11Device` devolver sucesso e a Área de Trabalho ficar visível por 10 min. **NÃO VALIDADO EM HARDWARE.** |
| 147 | Shader compiler | ⬜ | Shaders arbitrários sem corrupção, com cache |
| 149 | DXVK (D3D9/10/11) | ⬜ | Suite de conformidade sem WARP |
| 150 | VKD3D-Proton (D3D12) | ⬜ | Suite de conformidade sem WARP |
| 148 | Zink (OpenGL) | ⬜ | Depois de Vulkan graphics/WSI estáveis |
| 151 | OpenGL nativo | ⬜ | Não planejado — Zink é o caminho |
| 152 | D3D12 nativo | ⬜ | Opcional se VKD3D cobrir a meta |

---

## 🏁 P8 — Jogos e estabilidade · (LLM aqui é subproduto, não marco)

> **Objetivo:** o produto. Jogo real rodando, medido, estável.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P5 (imagem) **e** P7 (APIs) convergidas |
| **Critério de aceitação** | Frame real, fps medido, e **prova de ausência de fallback** por GPU counters e fences — não por "parece rodar" |
| **Ordem** | Triângulo → jogo DXVK simples → jogo VKD3D → long-run → jogo + desktop simultâneos |
| **LLM** | **SUBPRODUTO, NÃO MARCO** (Lei do escopo, item 1). Se cair de graça depois da P6, registra-se; **não** entra em PASS de etapa, não justifica corrida e não substitui nenhum item de vídeo ou de jogo |
| **PASS final do projeto** | Jogo rodando na BC-250, sem WARP, com TDR recuperando de hang sem perder o display |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| 158 | Triângulo Vulkan | ⬜ | Triângulo **na tela ligada à placa**, frame produzido na GPU |
| 159 | Jogo DXVK simples | ⬜ | Frame real medido, sem WARP nem llvmpipe |
| 160 | Jogo VKD3D | ⬜ | D3D12 traduzido, estável, com fps medido |
| 161 | Compilação de shaders | ⬜ | Cache e pipelines sem corrupção sob carga |
| 164 | Jogos + desktop simultâneos | ⬜ | DWM, jogo e compute compartilhando a GPU sem hang |
| 166 | Ausência de fallback de software | ⬜ | GPU counters e fences provando que nada foi desviado |
| 162 | Long-run compute | ⬜ | Horas sem hang, regressão ou fence perdida |
| 163 | Long-run display | ⬜ | Horas de present sem corrupção nem tela presa |
| 165 | Telemetria de desempenho | ⬜ | Busy, clocks, memória, temperatura e faults **lidos do silício** |
| 153 | Kernels LLM | 🧪 | *Subproduto.* Integrados via compute real — sem prioridade |
| 154 | llama.cpp Vulkan | ⬜ | *Subproduto.* Offload de layers com tokens/s medidos vs CPU |
| 156 | Alocação de modelos grandes | ⬜ | *Subproduto.* Residency/OOM/fragmentação nos 16 GB |
| 157 | Benchmark LLM | ⬜ | *Subproduto.* Tokens/s, consumo e temperatura com verificação de GPU |
| 155 | DirectML | ⬜ | Exige D3D12 UMD funcional; não é o primeiro caminho |

---

## ⚪ P9 — DCN com escrita · **OPCIONAL, não bloqueia jogos**

> **Objetivo:** trocar resolução pelo Windows, segundo monitor, flip sem tearing, VRR, HDR.
> **Nada aqui é necessário para jogar.**

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P5 estável. **Nunca** antes |
| **Veículo para escrita** | **KMDOD**, que tem ownership legítimo — ou o miniport WDDM depois da P5. **Nunca** o `bc250dbg`: escrever em HUBP/OTG sob o driver de laboratório é escrever num scanout vivo que o BasicDisplay está usando |
| **Ativo que reduz o custo** | O `dcn201` do Linux é aberto e comprovadamente funcional neste silício, e as bases do DMU já saem catalogadas da P1 |
| **Contra** | HPD é quebrado na placa **até no Linux**. Mexer em PHY/link custa tela preta e boot frio por tentativa |
| **Custo** | O item mais caro do projeto inteiro |
| **Regra** | Snapshot de **todo** registrador antes de tocar; rollback se qualquer readback divergir; orçamento de corridas definido antes |
| **A exceção antecipável** | Só o flip do `PRIMARY_SURFACE_ADDRESS` com master update lock, dentro do KMDOD. Converte a cópia por CPU num page flip real sem tocar timing, link ou PHY. **Possível e delimitado, não seguro** |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | Flip do `PRIMARY_SURFACE_ADDRESS` no KMDOD | ⬜ | **Exceção delimitada.** Snapshot + rollback + orçamento. Escrita em scanout vivo — derrubou a GPU no repo de referência |
| 82 | Master update lock | ⬜ | Atualização atômica do pipeline com readback |
| 83 | Flip A/B físico | ⬜ | Duas surfaces alternando; `SURFACE_INUSE` + frame counter provam qual está no ar |
| 84 | VBlank por interrupção | ⬜ | IH → ISR/DPC → `dxgkrnl`, substituindo o polling |
| 74 | HPD | ⬜ | Interrupção e estado físicos. **Quebrado na placa até no Linux** |
| 75 | AUX | ⬜ | Transações com timeout e retry reais |
| 76 | DPCD | ⬜ | Capabilities reais do monitor e do link |
| 77 | EDID real | 🟡 | Lido por AUX com checksum válido |
| 78 | Link training | ⬜ | CR/EQ, lane count, rate e status reais |
| 71 | DCCG / clocks de display | ⬜ | Clocks programados com readback, sem derrubar a imagem |
| 69 | DCHUB / DCHUBBUB | ⬜ | Watermarks e arbitragem seguras |
| 70 | DPP / MPC / OPP | ⬜ | Pipeline mínimo de uma plane |
| 72 | DIG / link encoder | ⬜ | Estado, enable e mapeamento |
| 73 | PHY | ⬜ | Power, lane mapping e sinal |
| 85 | Mode-set transacional | ⬜ | Um modo conhecido com rollback do link |
| 86 | Modos do EDID | ⬜ | Enumerar, validar e trocar mantendo imagem |
| 87 | Hotplug | ⬜ | Disconnect/reconnect sem crash nem tela presa |
| 79 | DMCU / DMUB | ⬜ | Presença e necessidade comprovadas. **Não inventar blob** |
| 80 | ATOM / board tables | ⬜ | Connector/encoder/clock extraídos da própria placa |
| 89 | HDR / VRR / MPO / HDCP | ⬜ | Tardio. Não anunciar antes de implementar |
| 88 | Áudio DisplayPort | ⬜ | Só depois do vídeo estável |

---

## ⚙️ T — Transversal · contínuo, atravessa todas as etapas

**Nesta faixa precisa:** disciplina de processo e qualidade. Não tem PASS único; cada item
é uma regra que vale em toda corrida. O item **177 (orçamento de corridas)** migrou para a
**P0** — ele é pré-requisito de gastar corrida, não item de fundo.

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | **Toolchain VS2026 + SDK/WDK 10.0.28000.0** | ✅ | **VALIDADA NO SILÍCIO (07/09 11:08)** por corrida de regressão **sem nenhuma mudança de código** — só compilador e headers. Reproduziu tudo: `PREENCHENDO a struct dele` 95, `GetSupportedVersions` 95 chamadas, `GetCaps Type=130` **63**, `CloseAdapter` 95, `CreateDevice` 0, `UMDRIVERPRIVATE` 186, `IsSupportedVidPn` 18, `Present` 175, e o **tripé da SDMA no `StartDevice` armando em 1 poll com `Dados=4/4 OK`** — o item mais sensível a codegen. Os três gates comprados a custo de corrida **sobreviveram**: `/Oy-` presente, zero `.bat` com LF solto, e contrato DDI **pinado em `0xC004`** (não bumpado além do que o Windows 10 22H2 suporta). Antes de armar, comparei por leitura os tamanhos das structs de fronteira entre os SDKs (`D3D10_2DDI_ADAPTERFUNCS` 5 e 5, `D3D10DDI_ADAPTERFUNCS` 3 e 3, `DXGK_DRIVERCAPS` 41 e 41) — **idênticas**, descartando o risco de corromper a heap do `dwm.exe` na cópia da struct da v5. Evidência: `docs/evidence/2026-09-07_11-08-15-.../ANALISE.md`. |
| — | **FPO ligado no build do SYS** | 🧹 | **`P1` (06/09):** `debugging-tips-for-wddm-drivers.md` exige desligar frame pointer omission em WDDM 1.2+. `driver/wdk/build-contract.bat` compila com `/O2` e **sem** `/Oy-`, degradando a call stack do dump — a única via de diagnóstico da P3. Fecha com `/Oy-` no build e gate na auditoria estática |
| — | **Log do KMD cumulativo e não arquivado** | 🧹 | **MEDIDO (06/09):** o log é cumulativo entre binds (6 binds num arquivo de 567 KB) e o da corrida 20:34 só existia em `C:\bc250_display_log.txt`, copiado à mão às 21:08. Fecha com marcador de início/fim de bind no log **e** arquivamento automático na pasta da evidência com SHA-256 |
| 121 | Auditoria de falsos sucessos | 🟡 | Teste AST/semântico geral no build |
| 125 | Breakpoint `+0x10` | 🧹 | Source breakpoint / return address controlado |
| 128 | Relatório 3.74 | 🧹 | "Causa definitiva" rebaixada a hipótese |
| 129 | Worktree sujo | 🟡 | Baseline/tag criada só com autorização |
| 131 | UMD/ICD esqueletos | 🟡 | Desabilitados até o KMD WDDM ser real |
| 170 | Testes GPUVA | 🧪 | Integração WDDM/hardware exercitada |
| 22 | SMU | 🟡 | Power management, thermal e clocks reais |
| 26 | Suspend / resume | ⬜ | S3/S4/reboot sem perder fila, display ou mappings |
| 27 | Thermal / power limits | ⬜ | Limites seguros sob carga longa, medidos |
| 178 | Matriz de hardware | ⬜ | Mais de um monitor/cabo/modo — só mais tarde |
| 179 | HLK | ⬜ | Depois de funcionalidade e estabilidade |
| 180 | Driver de produção | ⬜ | Gates perigosos removidos, firmware protegido, assinado |
| 181 | Instalador / desinstalador | 🟡 | Fluxo reversível e versionado |
| 182 | Documentação final | 🟡 | Consolidar quando os contratos estabilizarem |

---

## ❌ Encerrado · não reabrir

**Nesta faixa precisa:** nada. Está fechado por medição. Qualquer proposta de reabrir
precisa apresentar evidência nova, não argumento novo.

| # | Item | Por que está fechado |
|---:|---|---|
| 101 | Render-only em Código 0 | **REPROVADO** (`docs/162`). A recusa antecede qualquer cap de render; custou tela preta nas duas corridas |
| — | `MiscCaps.ComputeOnly` | Inalcançável: trafega só por `DRIVERCAPS`, que nunca é perguntado |
| — | Cross-adapter / MCDM como destino | APU de die único: não há segundo adaptador de display real. Teto de desempenho inaceitável |
| 41 | Shared / cross-adapter resources | Só serviria ao modo híbrido — **fora do destino** |
| 175 | KDNET | **Sem segunda máquina.** Substituído pelas quatro camadas da P3 |
| — | ~~Teoria "o `E_NOTIMPL` da UMD apaga a tela"~~ | 🔄 **SUPERADA — REABERTA COM EVIDÊNCIA NOVA (06/09).** A refutação de 03/09 valia porque `bc250umd.dll` **nunca era carregada**. Isso mudou: o doc 169 abriu o **dump real** do `dwm.exe` da corrida de 05/09 22:20 e nomeou a chamada — fail-fast `0xe0464645`, HRESULT **`0x887A0004` (`DXGI_ERROR_UNSUPPORTED`)** em `dwmcore!CD3DDevice::CreateD3D11Device`; e a corrida de 06/09 22:42 reproduziu com **770 × `E_NOTIMPL`** em `OpenAdapter10/10_2/12`, 7 cargas da DLL e 2 dumps de ~178 MB. **O item saiu de "encerrado" e virou a barreira ativa do desktop visível** — ver a P7. |

---

## Contagem

| | Itens |
|---|---:|
| **Tabela 1 — ✅ validado no silício** | **71** |
| Tabela 2 — 🧪 provado só offline | 11 |
| Tabela 2 — 🟡 parcial | 46 |
| Tabela 2 — ⛔ bloqueado | 3 |
| Tabela 2 — ⬜ não iniciado | 70 |
| Tabela 2 — 🧹 dívida a remover | 8 |
| Tabela 2 — ❌ encerrado | 6 |
| **Total** | **215** |

**Distribuição da Tabela 2:** P0 = 3 · P1 = 4 · P2 = 1 · P3 = 6 · P4 = 10 · P5 = 5 · P6 = 35 · P7 = 24 · P8 = 14 · P9 = 21 · T = 17 · encerrados = 6. **Total Tabela 2 = 145.** Recontagem automatizada em 06/09; as linhas ✅ das tabelas de fase são referências cruzadas à Tabela 1 e não entram nesta soma.

## Como seguir — o que ARRUMAR e o que DESENVOLVER

Duas listas distintas, porque exigem coisas diferentes. **Arrumar** é defeito já
identificado, de custo baixo e sem engenharia nova — quase tudo offline. **Desenvolver** é
capacidade que ainda não existe no driver, e é onde mora a barreira de compute e jogos.
Nada aqui está validado em hardware; a coluna final diz o que fecha cada linha.

### A) ARRUMAR — defeitos identificados, custo baixo

| # | O que está errado | Onde | Estado | Resultado medido |
|---:|---|---|:---:|---|
| A1 | Caps de tipo de driver vinham de dois produtores divergentes | `bc250_mcdm_callbacks.c` · `bc250_display_ddi.c` | ✅ | **FEITO (06/09, offline).** Fonte unica em `driver/core/bc250_driver_caps_contract.c`, consumida pelos dois handlers. `SupportNonVGA` corrigido para TRUE — e o teste `mcdm_callbacks_test` **afirmava `FALSE`**, isto e, congelava o defeito; foi refeito. `SupportSmoothRotation` e `SupportDirectFlip` **permanecem FALSE por decisao citada na norma** (ver A4). Novo teste `driver_caps_contract_test` passa; log novo `[DriverCaps] TypeCaps(...)` expoe os cinco campos na proxima corrida. **NAO VALIDADO EM HARDWARE.** |
| A2 | Mode set do target nao derivava do monitor (`pfnAcquireMonitorSourceModeSet`) | `bc250_display_ddi.c` | 🧹 | **RECLASSIFICADO por leitura de codigo.** Enquanto o EDID e' **sintetizado por nos**, derivar o mode set do monitor valida a nossa propria sintese contra ela mesma — e circular e nao adiciona informacao. O unico modo publicado e' o nativo herdado do GOP, que por construcao e' o que o monitor esta exibindo. Passa a **depender do item 77** (EDID real por AUX). Nao foi alterado codigo de negociacao antes da corrida 1. |
| A3 | EDID anunciaria 60 Hz com o enlace a 56,13 Hz | `bc250_display_ddi.c` | ✅ | **NAO ERA DEFEITO — verificado.** O descritor detalhado ja deriva de `BC250_NATIVE_PIXEL_RATE = 173.611.437 Hz` com `HBLANK=224`/`VBLANK=31` lidos do OTG: 173.611.437 / (2784 x 1111) = **56,13 Hz**, `clock10kHz = 17361`, hsync 62,36 kHz, e os range limits do EDID (V 50–61, H 20–90 kHz, pixel max 190 MHz) acomodam tudo. Os "60 Hz" existiam **na prosa deste documento**, nao no codigo. Prosa corrigida. |
| A4 | Multisampling em source pinado nao e' reportado | auditoria | ✅ | **DECIDIDO: nao se aplica.** `enumerating-cofunctional-vidpn-source-and-target-modes.md` condiciona o dever — *"If the display adapter has one or more video output codecs that are capable of antialiasing by multisampling"*. O display v1 e' blt de uma primaria ja composta, sem codec de antialiasing. Declarar metodo seria cap sem backend. Registrado como decisao, nao como pendencia. |
| A5 | FPO ligado no build do SYS (`/O2` sem `/Oy-`) | `driver/wdk/build-contract.bat` | ✅ | **FEITO (06/09, offline).** `/Oy-` no `CFLAGS` que gera o SYS, com gate novo `audit_frame_pointer_flag` em `tools/audit-bc250mcdm-safety.py`. O gate foi **verificado ao contrario**: removendo `/Oy-`, a auditoria reprova; devolvendo, volta a PASS. |
| A6 | Log integral do KMD nao era arquivado pelo binder de laboratorio | `tools/bind-bc250mcdm-lab.ps1` | ✅ | **FEITO (06/09, offline).** Causa exata: o binder **de laboratorio nunca chamou** `capture-bc250mcdm-binding.ps1` — so o de producao chama — e o log vive em `C:\bc250_display_log.txt`, cumulativo e sobrescrito pelo bind seguinte. Agora o binder arquiva o arquivo integral com SHA-256 e `bind_start_offset` em `display-log-archive.json`, independente de o ciclo chegar ao bloco de delta. Parser do PowerShell validado. |
| A7 | Barra de status e revisao divergentes nos quatro arquivos normativos | `CLAUDE.md` · `AGENTS.md` · `GEMINI.md` · `.cursorrules` | ✅ | **FEITO (06/09).** Data-base 2026-09-06 nos quatro e bloco unico com o resultado da faixa A. As barras de 06/09 20:34 ja estavam alinhadas; o que estava velho era a data e a ausencia do resultado da faixa A. |
| A8 | **ACHADO NO CAMINHO:** `build-contract.bat` estava em **LF**, e o `cmd.exe` retoma a leitura do `.bat` por deslocamento em bytes — depois de `call vcvars64.bat` ele passou a **comer o primeiro caractere de cada linha** | `build-contract.bat` + 11 `.bat`/`.cmd` do caminho de build | ✅ | **FEITO (06/09, offline) — e destravou cobertura perdida.** O sintoma era `LNK1181` com o nome de arquivo `'^.obj'` e linhas do proprio `.bat` executadas como comandos: **`readonly_miniport_test` nunca linkava** e a queda ficava escondida num log de centenas de linhas. Provado por A/B (a falha persistia sem a minha mudanca) e pela reproducao isolada, que montava o comando corretamente. Convertido para CRLF, `build-contract.bat` voltou a **`EXIT=0`** e `readonly_miniport_test.exe` passou a existir pela primeira vez. Gate novo `audit_batch_line_endings`, tambem verificado ao contrario. `docs/evidence/` nao foi tocado. |
| A9 | **`BYTE_PROOF PASS` certificava preto contra preto.** A comparação passava quando origem e destino eram ambos zero — declarava sucesso exatamente no caso em que a tela estava apagada. `NonZero=0/256` em 63/63 amostras e ainda assim `PASS` | `bc250_display_ddi.c` + novo `driver/core/bc250_blt_proof_contract.c` | ✅ | **FEITO (06/09, offline).** O veredito saiu do `if` solto e passou a vir de `Bc250ClassifyBltProof` em `driver/core`, com quatro estados: `CONTENT_OK`, `BLANK`, `MISMATCH`, `INDETERMINATE`. Cópia fiel de frame em branco agora sai como **`BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO)`** com `BlankStreak`, **nunca PASS**. ⚠️ **Desvio deliberado do que esta linha pedia antes:** eu havia escrito "falhar fechado quando `NonZero == 0`" — e isso está **errado**. O DWM pode legitimamente apresentar preto; desligar o SDMA aí transformaria sintoma em regressão de driver. O contrato fecha **só** em divergência de bytes (`Bc250BltProofRequiresFailClosed`). Travado por `blt_proof_contract_test` (5 casos) e por 5 gates novos na auditoria, incluindo um `forbid` do rótulo antigo — **verificado ao contrário**: reintroduzindo `BYTE_PROOF PASS: Source=`, a auditoria reprova. ✅ **PROVADO NO SILÍCIO (07/09 08:05):** a corrida emitiu **2× `BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO)`** com `BlankStreak`, e **zero** `BYTE_PROOF PASS` — com 50/50 amostras zeradas. Nenhuma linha de sucesso falso saiu da corrida. |
| A10 | **`BLT_TRIPOD` anunciava três pernas tendo duas.** `Bc250SdmaPresentBlt` verifica `RPTR == WPTR` e a fence da GPU sobre poison; a perna de dados vive fora, em `Bc250DisplayCompareSurfaceBytes`, e só roda em alguns `Present` | `bc250_sdma_present.c` · `bc250_sdma_present.h` | ✅ | **FEITO (06/09, offline).** ⚠️ **Achado que corrige esta linha:** a terceira perna **já existia** — `Bc250DisplayCompareSurfaceBytes` compara os **11.059.200 bytes inteiros**, origem contra destino, na CPU. O defeito não era ausência de prova, era **o log afirmar tripé sem consultá-la**. Agora o estado da perna de dados trafega em `BltDataProofState` e o log só imprime **`BLT_TRIPOD PASS #N (3 pernas)`** quando ela passou com conteúdo; nos outros casos imprime **`BLT_ENGINE_2LEGS`** dizendo se a perna de dados está `NAO CONFERIDA` ou `INCONCLUSIVA (frame em branco)`. Três gates novos na auditoria exigem as duas formas e o condicionamento. ✅ **PROVADO NO SILÍCIO (07/09 08:05):** `BLT_TRIPOD` = **0** e `BLT_ENGINE_2LEGS #1 (... PERNA DE DADOS NAO CONFERIDA)` = **1**. O log parou de afirmar três pernas tendo duas. |

> **Duas revisoes que a leitura da norma me obrigou a fazer, e que mudam o A1 original.**
> `wddm-driver-and-feature-caps.md` marca `SupportSmoothRotation` e `SupportDirectFlip` como
> **M** para Full Graphics, e a versao anterior desta tabela dizia para ligar os tres. Ligar
> seria **cap sem backend**: `optimized-screen-rotation-support.md` condiciona a exigencia a
> *"drivers that support rotated modes"* — nao suportamos modo rotacionado; e
> `direct-flip-of-video-memory.md` exige trocar a superficie de origem em
> `DxgkDdiSetVidPnSourceAddress`, o que pede reprogramar o endereco de scanout — escrita em DCN,
> proibida fora da P9. Ficam FALSE, com `SupportDirectFlip` registrado como **divida declarada**
> em `Bc250GetDriverCapsDeclaredDebtCount`.

**Portao offline apos a faixa A:** `driver\wdk\build-contract.bat` termina **`EXIT=0`**,
`driver_caps_contract_test` e `display_vidpn_test` passam, e
`python tools\audit-bc250mcdm-safety.py` volta **PASS** com os dois gates novos.
**Nenhum item da faixa A foi validado em hardware** — sao mudancas de contrato, de build e de
coleta de prova.

#### A12 — telemetria de fila da SDMA0 decodificada (07/09, offline)

Depois de fechar as perguntas 3, 4 e 5 por leitura, apliquei o mesmo método à **2** — e o
`docs/168_HANDOFF_2026-09-05_INDIRECT_SDMA.md` já tinha os dados, arquivados e **não
interpretados**. A seção 6, item 6 daquele documento encerrava assim: *"a semântica de bits de
`CONTEXT_STATUS` e `SKIP_CNTL` … não sei o que significa e não interpretei"*.

A semântica estava no repositório, em **`inc/linux_ref/gc_10_1_0_sh_mask.h`** — a tabela de
shifts e máscaras do GC 10.1.0 do upstream. Aplicando-a às leituras de silício de 05/09:

| Variante | `CONTEXT_STATUS` | Decodificado |
|---|---|---|
| `PHYS` (VMID 0, MC físico) | `0x0D` | `SELECTED` + **`IDLE`** + `EXPIRED` |
| `EXEC` (VMID 8, traduzido) | `0x09` | `SELECTED` + `EXPIRED`, **`IDLE = 0`** |

| Variante | `IB_CNTL` | Decodificado |
|---|---|---|
| `PHYS` | `0x00000101` | `IB_ENABLE=1`, `SWITCH_INSIDE_IB=1`, `CMD_VMID=0` |
| `EXEC` | `0x00080101` | `IB_ENABLE=1`, `SWITCH_INSIDE_IB=1`, `CMD_VMID=8` |

**Os dois `CONTEXT_STATUS` diferem em exatamente um bit — o `IDLE`. Os dois `IB_CNTL` diferem
em exatamente um campo — o `CMD_VMID`.** Medição `P0` mais decodificação `P1`: **não é
hipótese**, e estava disponível esse tempo todo.

🔶 **HIPÓTESE (`H`):** sob VMID 8 a engine consumiu o pacote do **anel**, latchou `IB_BASE` e
`CMD_VMID`, e **parou dentro do IB** — não terminou, por isso `IDLE` fica 0. Isso desloca a
pergunta 2 de *"não executou o pacote"* para *"começou o IB e travou"* — e **casa com a
hipótese da pergunta 3**: travar buscando/traduzindo é o que um fault redirecionado para a
dummy page, sem interrupção habilitada, produziria.

⚠️ **O que isto NÃO resolve.** A variante `PHYS` **foi a `IDLE`** — terminou — e a prova de
bytes dela **também falhou**. Sem tradução envolvida, aquela falha **não pode** ser de GCVM.
São **duas falhas diferentes**, e a decodificação não decide nenhuma. E não sei se
`EXPIRED = 1` é significativo ou o estado normal de uma fila drenada.

**Duas armadilhas de leitura que o catálogo agora registra:** `IB_RPTR`/`IB_OFFSET` têm o
campo `OFFSET` começando no **bit 2** — o cru está em bytes, o campo conta dwords, então
`IB_RPTR = 0x20` são **8 dwords**, não 32; e `EXCEPTION` é um campo de **3 bits**, não um flag.
O bit 1 de `CONTEXT_STATUS` **não tem nome no header do upstream**, e o decodificador o
reporta em `BITS_SEM_NOME` em vez de fingir que entendeu o valor todo.

**O que ficou pronto para a próxima corrida, a custo zero de corrida:** o
`SubmitCommandVirtual` do Node 0 lê os oito registradores do bloco `SDMA0_GFX_IB_*` **no
caminho de timeout** e emite `IB_QUEUE_RAW`, `IB_QUEUE_DECODED`, `IB_QUEUE_STATUS` e
`IB_QUEUE_DISCRIMINANTE`. Só leitura; **fora** do caminho de sucesso, para não pôr oito
acessos de MMIO no `Present` a 60 Hz. Novo contrato `bc250_sdma_queue_state_contract` não
escreve registrador nenhum — é aritmética pura, testável offline.

**Teste que fecha:** `driver/tests/sdma_queue_state_contract_test.c` — confere as máscaras
contra o header **e** reproduz os dois valores medidos em silício. Ele já se pagou: eu havia
escrito que `SWITCH_INSIDE_IB` era 0 na variante `PHYS`, por suposição; `0x00000101` tem o
bit 8, e o teste me corrigiu antes de a afirmação chegar a um documento.

⚠️ **NÃO VALIDADO EM HARDWARE** — a decodificação é de valores já medidos; a nova telemetria
nunca rodou.

#### A11 — geometria de GPUVA com fonte única (07/09, offline)

Procurar no nosso próprio `docs/` a resposta das perguntas abertas do doc 167 expôs **cinco
defeitos na mesma geometria**. Nenhum é o B1, mas **os três primeiros produziriam o sintoma
do B1** se acionados, e o primeiro era meu.

| # | Defeito | Correção | Trava |
|---|---|---|---|
| A11.1 | **Segunda fonte de verdade para `GpuVaBits`.** O blob `UMDRIVERPRIVATE` publicava `BC250_ADAPTER_GPUVA_BITS` = **48** enquanto o mesmo driver declarava `VaBits=34` ao WDDM — **medido** no log de 11:08: `[McdmQAI] GPUMMUCAPS: VaBits=34, Levels=2`. A UMD (e, na rota B, o winsys do RADV) dispõe endereços dentro do que o blob promete; prometer 48 sobre uma hierarquia que traduz 34 gera endereço sem PTE alcançável. **Defeito meu, criado ao escrever o blob** | `facts.GpuVaBits`/`PageTableLevels` passam a sair de `runtimeCaps` — a **mesma** struct que alimenta o `GPUMMUCAPS`; sem geometria o blob não sai (`DEVICE_NOT_READY`) | gate estático `audit_gpuva_geometry_single_source` (**verificado por teste de mutação**: trocar por `48u` faz a auditoria falhar) + asserção de repasse em `mcdm_callbacks_test`, usando o mock 48/4 que distingue repasse de hardcode |
| A11.2 | **`BC250_ADAPTER_GPUVA_BITS` literal.** Valia 48, e `MAX_GPUVA_ADDRESS` valia `0xFFFFFFFFFFFF`. Morto — só testes o liam — mas pronto para reintroduzir o defeito ao ser ligado | passa a **derivar**: `#define BC250_ADAPTER_GPUVA_BITS BC250_GPUVA_PT_VA_BITS` e `MAX = (1ull << bits) - 1` | mesmo gate; e a mensagem do build passou a dizer `34-bit GPUVA derivado` |
| A11.3 | **Fail-open no journal de GPUVA.** `Bc250GpuvaJournalStageLeafUpdate` recusava VA a partir de `2^48` — o teto **arquitetural** — em vez de `2^34`, o **traduzível**. Aceitava registrar página num VA que o VMID8 não alcança; sintoma indistinguível do B1. Não era acionável pelo alocador vigente (inicializado com `BC250_GPUVA_PT_VA_BITS`), mas era porta aberta | novo `BC250_GPUVA_TRANSLATABLE_LIMIT = 1ull << BC250_GPUVA_PT_VA_BITS`, com `MAX_LIMIT`/`DEFAULT_BITS` marcados como teto arquitetural que **não** serve de limite de tradução | 4 casos em `gpuva_journal_contract_test`: última página traduzível **entra**; a primeira fora dele, a faixa que **atravessa** o limite e o VA de 8 TB **não** |
| A11.4 | **Bloco de PROVENIÊNCIA falso** no topo de `bc250_gpuva_space_contract.h`: descrevia o desenho **anterior** (plano, `PAGE_TABLE_DEPTH = 0`, 22 bits de índice) logo acima das constantes de 2 níveis que o substituíram. Sob a **Lei 7** é armadilha — era a proveniência de registro, e estava falsa: quem a lesse programaria depth 0 e declararia 1 nível ao `dxgkrnl`, abaixo do mínimo do contrato | reescrito para a geometria vigente com a proveniência real (`docs/03`, `P1`); o texto antigo ficou marcado como **HISTÓRICO**, nada apagado | — |
| A11.5 | **Afirmação falsa e normativa em `docs/03`:** *"o driver lê `GCMC_VM_FB_OFFSET` do silício em cada construção; não usa constante"*. O leitor de topologia lê, mas o valor usado nas PTEs é a constante `BC250_GCVM_VRAM_MC_BASE_FALLBACK` — **deliberadamente** (a base do leitor "nunca fez um acesso funcionar"; a constante tem prova byte a byte). Hoje os dois concordam; o risco era o silêncio se deixassem de concordar | catálogo corrigido; `SetRootPageTable` passou a comparar e emitir `ROOT_MC: DIVERGENCIA` quando diferem, e a dizer explicitamente quando concordam. **Zero mudança de comportamento** | removida no mesmo passo uma inicialização morta que repetia `0x450000000ull` à mão — quarta grafia do mesmo número — e era sobrescrita sem condição |

**Estado:** `driver/wdk/build-contract.bat` **EXIT=0**, `driver/umd/build-umd.bat` **EXIT=0**,
e as três auditorias (`safety`, `ddi-matrix`, `ddi-noop`) voltam **PASS**. Dois testes que
**congelavam o defeito** foram descongelados com a norma citada: `TestRoundTrip` afirmava
`blob.GpuVaBits == 48u`, e `adapter_info_contract_test` afirmava `48u` / `0xFFFFFFFFFFFF` —
mesmo padrão do `SupportNonVGA == FALSE` corrigido no A1.

⚠️ **NÃO VALIDADO EM HARDWARE.** São mudanças de contrato, gate e teste offline. O que muda
para o silício é que o blob que a UMD vai ler deixou de mentir sobre o espaço endereçável —
e isso só se prova quando a UMD dispuser endereços de verdade.

**Estado dos pacotes depois deste passo — leia antes de armar qualquer corrida:**

| Pacote | Contém as mudanças de 07/09? | Como foi verificado |
|---|:---:|---|
| `package/output_active` | ✅ **SIM** | SYS assinado `069A7330...` executado às 16:19: MMU global e Código 0 validados, porém desktop **REPROVADO** por tela preta/fail-fast D3D; não rebinding sem mudança material |
| `package/output_lab` | ❌ **NÃO** — SYS assinado às 14:16, **anterior** ao fail-closed | manifesto registra fonte SYS `5D7333F4...`, diferente do candidato atual |

O lab **não** foi reconstruído de propósito: `tools/build-bc250mcdm-lab-bringup.ps1` exige
`-Execute -IUnderstandThisEnablesDestructiveBringup`, e esse flag existe justamente porque a
imagem de laboratório executa PFP direct load, CP, GCVM e HQD **durante o `StartDevice`,
enquanto detém o display**. Ligá-lo é decisão do dono do projeto, não passo de build.

⚠️ **Duas armadilhas medidas neste passo, ambas capazes de queimar uma corrida:**
1. `build-bc250mcdm-lab-bringup.ps1` **sem** os flags retorna **`EXIT=0`** — ele só valida
   precondições. Ler o código de saída como sucesso deixa o SYS antigo no lugar.
2. **Comparar hash do pacote com o hash do build não serve como prova de frescor** — o
   empacotador **assina** o SYS, então os dois diferem por construção. Cometi esse erro de
   medição aqui e o corrigi: a prova barata é procurar, como ASCII dentro do binário, uma
   string de log introduzida no candidato.

### B) DESENVOLVER — capacidade que ainda não existe

> **A dívida central, em uma frase:** o `SubmitCommandVirtual` do Node 0 tem três caminhos —
> `PLANO A DIRECT FILL`, `PLANO A SYNC NOP` e `INDIRECT`. Os dois primeiros **reimplementam
> a operação no anel** e a GPU nunca lê o DMA buffer; só o terceiro executa o buffer que o
> `dxgkrnl` entregou, e é o único que não funciona. Todo compute e todo jogo passam
> obrigatoriamente pelo terceiro (`bc250_active_miniport.c:6172`).

| # | O que falta construir | Por que é bloqueante | Estado hoje | Fecha com |
|---:|---|---|---|---|
| B1 | **Executar o IB de paginação sob VMID8.** ✅✅ **FECHADO NO SILÍCIO EM 07/09 17:43 — TRIPÉ COMPLETO:** duas submissões com `RPTR==WPTR` nas duas leituras, fence da GPU sobre poison e **5 canários substituídos** (`Divergentes=0`), sobre o DMA buffer real do `dxgkrnl` sob VMID8. Antes disso, em 17:13, o mesmo caminho executou com DUAS pernas: dez `INDIRECT` reais sob VMID8, `RPTR==WPTR` e fence da GPU sobre poison, sem timeout e sem fault. **NÃO FECHA pela Lei 2:** o gate não conferia byte nenhum do destino, e o rótulo `IB_TRIPOD PASS` era falso sucesso — corrigido na A14, que plantou canários para a perna 3. Fecha na próxima corrida se sair `IB_TRIPE_VEREDITO: TRIPE_PROVADO … Divergentes=0`. ⚠️ **O experimento da CSA foi DESCARTADO por leitura (07/09, `H`):** no upstream, `amdgpu_sdma_get_csa_mc_addr()` devolve **zero** quando a preempção no meio do comando está desligada — e o nosso `F_CNTL_MIDCMD` já saiu do `CNTL`. Logo `CsaAddress = 0` é o valor **correto**, não uma omissão, e implementar CSA seria divergir do upstream **gastando uma corrida à toa**. O formato do `INDIRECT` também confere com o `SDMA_PKT_INDIRECT` padrão e está agora **travado byte a byte** pelo bloco 6b de `sdma_packet_builder_test`. ✅ **DUAS DAS QUATRO PERGUNTAS DO DOC 167 FECHARAM EM 07/09, POR LEITURA DO NOSSO PRÓPRIO CATÁLOGO, SEM GASTAR CORRIDA.** A **4** (`PAGE_TABLE_DEPTH` do VMID8) já estava respondida em `docs/03` com `P1`: a convenção do GCVM é *níveis abaixo da raiz*, logo `DEPTH = Levels - 1 = 1` — e é isso que o driver programa (`BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE = 0x00555403`, `BLOCK_SIZE = 0`, `PT_END = 0x003FFFFF`, os quatro travados por `gcvm_context8_cntl_test`). A **5** (base da PTE) também: a PTE carrega **PA**, não MC — `PA = offset_de_VRAM + (GCMC_VM_FB_OFFSET << 24)` (`amdgpu_gmc_vram_mc2pa()`), e trocar pela base MC já foi tentado em 05/09 01:29, diagnosticado como erro de domínio e revertido. **Restam só a 2** (que estado faz o `RPTR` alcançar o `WPTR` sem executar) **e a 3** (hub de fault correto — o `FAULT_STATUS` do GFXHUB lê zero sempre). Logo: `DEPTH`, `BLOCK_SIZE`, `PT_END`, aritmética de PA da PTE e formato do `SDMA_PKT_INDIRECT` estão **todos** conferidos contra fonte e travados por teste — **o IB não falha em nenhum deles**. A 2 e a 3 não devem ser chutadas: são semânticas de registrador, cada combinação errada custa uma corrida e produz o mesmo sintoma | Sem isso, nenhum command buffer de UMD executa. É a raiz da barreira de compute e jogos | tradução provada (`IB_PA_MATCH ... CONFERE`), execução **não** | Alocar CSA no work buffer, submeter e obter o tripé **do próprio IB**: `RPTR==WPTR`, fence da GPU sobre poison e bytes conferidos |
| B2 | **Ler o hub de fault correto.** `GCVM_L2_PROTECTION_FAULT_STATUS` do GFXHUB lê `0x00000000` em todas as corridas; a pergunta 3 do doc 167 (SDMA no GC 10.1 roteia por GFXHUB ou MMHUB?) continua **aberta** | Sem fault legível, a parada do IB é silenciosa e o diagnóstico é cego | não iniciado | Provocar fault deliberado e ler o status latchado no hub certo |
| B3 | **Aposentar os caminhos `PLANO A`** depois que B1 fechar. ✅ **FEITO OFFLINE EM 07/09, depois do B1 fechar:** o default do Node 0 passou a ser **somente `INDIRECT`**, e os `PLANO A` ficaram atrás de `SdmaPlanoAFallback` (default 0, fail-closed, apenas bissecção). Falta a corrida que mede esse default. Passo anterior (A13): os `PLANO A` deixaram de se anunciar como `IB EXECUTADO` — a frase agora é exclusiva do ramo `INDIRECT` e eles declaram que a operação foi reimplementada no anel e que a GPU não leu o DMA buffer; e o tunable `SdmaIndirectOnly` (default 0, fail-closed) desliga a precedência deles para que o `INDIRECT` possa ser medido. Aposentar de fato continua condicionado ao B1. **NÃO VALIDADO EM HARDWARE** | Enquanto existirem, um `INDIRECT` que falha fica **escondido** por uma reimplementação que funciona — é fallback disfarçado de sucesso | ativos e em uso | `SubmitCommandVirtual` executando o DMA buffer real, com os `PLANO A` atrás de tunable default desligado |
| B4 | **Submissão no Node 1 (compute) originada pelo `dxgkrnl`.** ✅ **DOIS BLOQUEIOS REMOVIDOS OFFLINE EM 07/09:** a recusa era **muda** (`STATUS_NOT_SUPPORTED` sem log, por isso "zero submits" não distinguia recusa nossa de ausência de submissão) e o gate **não podia abrir** no pacote de produção — `GpuvaLabPromotion` era lido só no `#else` de bring-up. Agora a recusa publica o `BlockMask` decodificado e o tunable é lido em produção, com default 0. O caminho após o gate já existe completo (snapshot fixado, cobertura sem gaps, reserva de fence, anel MEC) | É o que o `vkCreateDevice`/`vkQueueSubmit` exige. Hoje o KMD recebe 2 contextos Node 1 e **zero** submits | 0 submits Node 1; gate legível | Submissão do scheduler aposentada fisicamente no Node 1, com tripé |
| B5 | **Residency (`MakeResident` / `Evict`)** — item 39 | O modelo WDDM 2.0 põe a residência sob responsabilidade do UMD; RADV vai exercer isso | não iniciado | Accounting conferido, sem leak, sob carga |
| B6 | **TDR — item 25.** Gate de saída da P6, não negociável. ✅ **BACKEND DAS DUAS DDIs FEITO OFFLINE EM 07/09:** `ResetFromTimeout` faz quiesce físico e `RestartFromTimeout` rearma **com prova de fence**; antes as duas devolviam `STATUS_SUCCESS` sem tocar no silício, e o teste exigia isso. Falta a corrida com hang induzido, e ela é o item | Sem TDR, cada bug de shader é reboot. `timeout-detection-and-recovery.md`: timeout default de 2 s, `0x117`/`0x141`/`0x142` | não iniciado | Hang induzido → detectado → engine resetada → adaptador volta a aceitar submissão → **display sobrevive** |
| B7 | **P7 completa: UMD/Vulkan/RADV.** Em 08/09, offline, a UMD ganhou transação Node 1 completa e acionador físico default-off: destino residente, cinco poisons pré-submit, command buffer por `Lock/Unlock`, `WRITE_DATA` + `RELEASE_MEM`, fence exata, readback byte a byte e teardown. Em 09/09 19:56:09, o harness `B7_UMD_NODE1_TRIPOD` executou no silício: `RPTR==WPTR==12`, fence monitorada `0xBC250F01` e cinco canários gravados via PM4 sob VMID 9. **Ressalva auditada (doc 230):** comprovou comandos PM4 e efeito em memória, mas **não houve dispatch de shader** (`DISPATCH_DIRECT`) nem compilação de código de shader pela rota UMD/WDDM. A UMD gráfica e o RADV seguem sem pipeline de render implementado. | É a maior fase do projeto em linhas de código, e só começa depois de B1–B6 | PM4 WRITE_DATA/RELEASE_MEM sob VMID9 **VALIDADO NO SILÍCIO**; pipeline de shader/Draw e Vulkan/RADV **NÃO VALIDADO EM HARDWARE** | Backend de tradução DXBC→SPIR-V→ACO funcional + winsys WDDM emitindo PM4 de render com sucesso |

### Ordem, e por que essa

```text
A1..A5 (offline, de graca)  ─┐
                             ├─> corrida 1: display converge?  ──> P5 fecha
A6..A7 (higiene de prova)   ─┘
                                                B1 -> B2 -> B3 -> B4 -> B5 -> B6 ─> P7 -> jogos
```

A faixa **A** não depende de nada e não gasta corrida — é o que se faz primeiro porque é
grátis e porque A5/A6 melhoram a qualidade da prova de todas as corridas seguintes. A
faixa **B** é sequencial de verdade: B1 é pré-requisito de tudo o que vem depois, e B6
(TDR) é o portão que a P7 exige.

⚠️ **Correção de enquadramento (06/09).** Uma versão anterior deste bloco dizia
"Compute/LLM sai em B4; jogos só depois de B7", o que sugeria B1–B4 como uma "trilha de
compute" alternativa ao vídeo. **Isso está errado e foi corrigido.** B1 (executar o IB sob
VMID8) e B4 (submissão Node 1) **não são desvio para compute** — são **pré-requisito da
submissão Vulkan**, logo de jogos. O que seria desvio é *parar* em LLM e chamar de entrega.
Pela Lei do escopo, B1–B6 são obra de jogos e é assim que devem ser lidos.

## As próximas ações — ordenadas pelo objetivo (WDDM por GPU + jogos)

Toda ação abaixo serve a vídeo acelerado ou a jogos. Nenhuma delas é compute como destino.

> ### ⛔ A CORRIDA ABAIXO JÁ FOI GASTA EM 07/09 16:19 — E NÃO PRODUZIU INFORMAÇÃO
>
> Contagem exata no log da 16:19 (`P0`): **14** `IB EXECUTADO` = **2** `PLANO A DIRECT FILL`
> + **12** `PLANO A SYNC NOP`, **zero** `INDIRECT` e **zero** linhas `IB_QUEUE_*`. O desfecho
> fraco previsto no fim deste bloco é o que aconteceu: os dois `PLANO A` têm precedência,
> absorveram **14/14** submissões, o ramo `INDIRECT` não foi exercitado e a **pergunta 2
> continua sem resposta**. Repetir esta corrida como está é gastar boot frio para reler o
> mesmo nada.
>
> **A corrida que substitui esta** está definida na faixa A13:
> [`docs/180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md`](docs/180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md)
> §5–§6 — o pacote **já está assinado e auditado**: `DriverVer 3.82.0.0`, SYS assinado
> `0CF9472F…`, CAT `7798EC55…`, com o INF declarando
> **`SdmaIndirectOnly=1`**, que desliga a precedência dos `PLANO A`. Receita exata em §6.2. Sem esse tunable a
> corrida repete a 16:19. Com ele, o risco declarado é que submissões que hoje "concluem"
> passem a `RECUSA` — caminho conhecido de `0x119`. Armar é decisão do dono do projeto.
>
> Registro original, preservado para leitura:
>
> **Uma corrida, pacote ativo atual, telemetria só-leitura, probe DESARMADO.**
>
> ```
> powershell -ExecutionPolicy Bypass -File tools\arm-d3d11-caps-probe.ps1 -Disarm
> powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-failclosed.ps1
> ```
>
> Boot frio antes. Pacote ativo `DriverVer 3.81.0.0`, com a telemetria nova conferida por
> string dentro do `.sys`.
>
> **Por que vale:** a telemetria da faixa A12 é **somente leitura, zero mudança de
> comportamento** — nenhum risco novo. E o log **não se perde**: `Bc250DisplayLog` abre,
> escreve, dá `ZwFlushBuffersFile` e fecha **a cada linha**
> (`driver/wdk/bc250_display_ddi.c:575-577`), então reboot sujo não apaga o que já saiu.
>
> **Por que NÃO bundlar o experimento de fault (`0x007FFE03`) na mesma corrida:** se armar as
> sete `*_ENABLE_INTERRUPT` provocar tempestade de fault, a máquina pode travar durante o ~1 s
> de polling — e é **exatamente aí** que as linhas `IB_QUEUE_*` são escritas. Sem TDR (item 25),
> perderíamos justo o dado que fomos buscar. **Banca a resposta da pergunta 2 primeiro, arma a 3
> depois.**
>
> **O que ler no log:**
>
> | Linha | O que decide |
> |---|---|
> | `IB_QUEUE_STATUS: … IDLE=?` | **`IDLE=0`** confirma a hipótese (parou dentro do IB); `IDLE=1` a **refuta** |
> | `IB_QUEUE_DECODED: … IB_RPTR=? dw IB_OFFSET=? dw` | se diferirem de 8/0, o `0x20` de 05/09 **não** era constante — mata a dúvida do doc 168 §5.3 |
> | `IB_QUEUE_STATUS: … EXCEPTION=?` | não-zero seria a resposta direta, e ninguém tinha olhado |
> | `IB_QUEUE_STATUS: … BITS_SEM_NOME=?` | o bit 1 sem nome no upstream, se aparecer |
> | `ROOT_MC: DIVERGENCIA` ou `concorda` | se a constante `0x450000000` envelheceu |
> | `UMDRIVERPRIVATE: GpuVaBits=34, PageTableLevels=2` | confirma o A11.1 no silício |
>
> ⚠️ **Desfecho possível que precisa ser DECLARADO, não maquiado:** o `INDIRECT` só é alcançado
> quando não há fill pendente, o buffer não tem 4 bytes, e a GPUVA do IB resolve sob VMID 8 — os
> dois `PLANO A` têm **precedência** (`driver/wdk/bc250_active_miniport.c:6198-6242`). **Se todas
> as submissões forem absorvidas por `PLANO A`, não sai nenhuma linha `IB_QUEUE_*`.** Isso ainda
> é informação — diz que o `INDIRECT` não está sendo exercitado no perfil ativo — mas é fraca.

1. ✅ **FEITO (06/09):** dump do DWM aberto com `cdb`. Pilha completa:
   `CLegacyRenderTarget::EnsureSwapChain` → `CD3DDevice::CreateD3D11Device` →
   `DXGI_ERROR_UNSUPPORTED` → **fail-fast**. Decisão de arquitetura resolvida: **backend
   D3D10/11 real é obrigatório**; CDD/interop está **refutado por dump**.
2. ✅ **FEITO (06/09):** A9 e A10 corrigidos e travados por teste e auditoria. A próxima
   corrida de display não pode mais "passar" com a tela apagada: frame em branco sai como
   `BYTE_PROOF INCONCLUSIVO` com `BlankStreak`, e a palavra "TRIPE" só aparece com as três
   pernas provadas. `build-contract.bat` em `EXIT=0`, auditoria de segurança em `PASS`.
3. ✅ **FEITO (06/09):** dimensionamento em `docs/173_DIMENSIONAMENTO_D3D11_PARA_DWM.md`.
   Piso = `D3D10_1_DDI_INTERFACE_VERSION`, **105** entradas de device, **5** de adapter,
   feature level de piso **10_1** (9_x indisponível nesta interface). O custo real são o
   compilador DXBC→ISA, o pipeline 3D e os recursos sobre GPUVA — não a contagem de entradas.
   **Nenhum prazo é dado:** com compilador de shader no escopo, estimativa em semanas seria
   palpite.
4. ✅ **ENCERRADO (07/09): `P7_D3D11_CAPS_PROBE`, 3 corridas gastas — localizou o item
   zero.** v1 (06/09 23:45) falhou por defeito meu: aceitava qualquer `Interface`, matou o
   step-down e era cap falso. v2 (07/09 08:05) restaurou o step-down — **`P1`: a negociação
   de DDI é escada descendente comandada pela recusa da UMD**. v3 (07/09 08:25) instrumentou
   as três entradas do adapter table e o **silêncio delas** apontou o kernel:
   `DXGKQAITYPE_UMDRIVERPRIVATE` recusado 80 vezes. **Autocrítica:** o `Type=0 NOT SUPPORTED`
   estava no log do KMD desde a primeira corrida; o caminho curto era ler os tipos de QAI
   antes de instrumentar modo usuário.
5. ✅ **`P7_D3D11_CAPS_PROBE` ENCERRADO (07/09 10:11) — 6 corridas, missão cumprida.**
   A v5 confirmou que o bloqueio era **defeito nosso**: `pAdapterFuncs_2` vem não-NULO e o
   driver deve **preencher a struct do runtime**, não sobrescrever o ponteiro. Com isso o
   runtime percorreu a negociação inteira e **pediu exatamente um cap**:
   `D3D11DDICAPS_3DPIPELINESUPPORT` (4 bytes). As três perguntas do doc 173 estão fechadas —
   o `dwmcore` **aceita** 10_1, valida **só** o nível de pipeline 3D, e o minor 17 deixou de
   importar. **O último portão é um pipeline 3D real, e não há atalho por caps.**
   ⚠️ **Autocrítica:** o defeito era uma convenção **legível no header desde a primeira
   corrida**. Quatro corridas testaram o comportamento do runtime quando a pergunta certa era
   "quem aloca esta struct?". O que salvou o método foi instrumentar para provar ausência —
   o silêncio das entradas, e depois a ausência específica de `pfnCloseAdapter`, é que
   apontou a convenção.
6. ✅ **DECIDIDO (06/09): rota B — DXBC→SPIR-V→RADV/ACO.** Um backend para as duas
   frentes. Registrada em "Decisão de arquitetura vigente"; rota A (compilador DXBC→ISA
   próprio) fica **rejeitada** e só se reabre por escrito.
7. **B1 — executar o IB de paginação sob VMID8.** Não é desvio para compute: é o que permite
   ao `dxgkrnl` entregar command buffers de verdade, logo é pré-requisito de **submissão
   Vulkan** e de jogos. ✅ **Em 07/09 o cerco fechou por leitura, sem gastar corrida:** das
   quatro perguntas de registrador que sustentavam este item, **três saíram** — a CSA
   (descartada: com `MIDCMD` desligado, `CsaAddress = 0` é o valor correto do upstream), a
   **4** (`PAGE_TABLE_DEPTH = 1`, já respondida em `docs/03` com `P1` e travada por
   `gcvm_context8_cntl_test`) e a **5** (a PTE carrega **PA**, não MC:
   `PA = offset_de_VRAM + (GCMC_VM_FB_OFFSET << 24)`). Somando o `SDMA_PKT_INDIRECT`, que já
   estava travado byte a byte: **nenhum dos parâmetros que eu poderia ter errado está errado.**
   ✅ **A pergunta 3 também fechou, no mesmo passo:** o hub **está certo** — `docs/03` cita
   `sdma_v5_0.c` atribuindo `ring->vm_hub = AMDGPU_GFXHUB(0)` —, e o caminho de invalidação
   confere nos **cinco** pontos que o catálogo exige (`REQ = 1 << 8` → `0x00F80100`,
   `CLEAR_PROTECTION_FAULT_STATUS_ADDR` em **0** para não apagar evidência, ACK no bit
   `1 << 8` de `ENG17_ACK`, leitura dummy do `REQ` obrigatória em GC < 10.3, e sem semáforo).
   **Logo `FAULT_STATUS = 0` é medição de verdade, não hub errado nem status apagado por nós.**

   🔶 **E aí apareceu a pergunta certa, com HIPÓTESE (`H`) e experimento que DISTINGUE duas
   causas.** Contando os bits do nosso valor alvo contra o POST — os dois já em `docs/03`: as
   sete classes de fault têm par `ENABLE_DEFAULT` / `ENABLE_INTERRUPT`; o POST tinha os
   **quatorze** ligados, e o nosso alvo liga os sete `DEFAULT` e **desliga os sete
   `INTERRUPT`**. Pela própria tabela do catálogo, `ENABLE_DEFAULT = 1` significa *"redireciona
   para dummy page default"*. Isso descreve hardware que, ao falhar a tradução, **manda o
   acesso para a dummy page e não avisa ninguém** — e explica as **três** medições do B1 de
   uma vez: `RPTR` alcança o `WPTR` (a engine consumiu o pacote), `FAULT_STATUS` zero (nenhuma
   interrupção habilitada) e destino sem dados (escritas na página default).
   ⚠️ **Não provado:** não se sabe — e não está nos nossos docs — se o `FAULT_STATUS` *latcha*
   com `DEFAULT=1` e `INTERRUPT=0`. Se latchar, a hipótese está **refutada** pela medição que
   já temos. E o valor alvo **não é erro nosso**: o catálogo o deriva do setup padrão do
   upstream, que também não habilita interrupção nesse passo.

   **Próximo passo — uma corrida que produz informação nos dois desfechos:** habilitar as sete
   `*_ENABLE_INTERRUPT` no `GCVM_CONTEXT8_CNTL` e submeter **o mesmo IB que hoje falha**.
   `0x00555403 | 0x002AAA00 = 0x007FFE03`, que difere do POST medido em **apenas `0x83`**
   (`ENABLE_CONTEXT` e `DEPTH`, deliberados, mais `RETRY_PERMISSION_OR_INVALID_PAGE_FAULT`, que
   o upstream desliga em APU) — é o estado de fábrica com a observabilidade preservada, não um
   valor inventado. O decodificador de IV já existe e está travado (`INTERRUPT HANDLER
   CONTRACT`, IV de 32 B do Navi/GFX10). Critério binário: **chega IV de fault** → hipótese
   confirmada, o IB executa, o defeito é de tradução e o `FAULT_ADDR` diz *qual* endereço;
   **não chega IV** → hipótese refutada, a engine não executa o pacote, e o centro volta a ser
   a pergunta 2. **Já declarado em código, e deliberadamente NÃO usado:**
   `BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE`, com a aritmética travada por 8 asserções
   em `gcvm_context8_cntl_test` — armar é decisão do dono do projeto, não efeito de compilar.
8. **Avaliar a branch `wddm2` do RADV** (Collabora/Valve,
   `gitlab.freedesktop.org/lfrb/mesa`) como base do item 146 — **offline**. É o atalho legítimo
   para a P7 **de jogos**: aproveita-se o port Win32 e o esqueleto de winsys D3DKMT;
   descarta-se a ABI de *private data* da AMD, porque o KMD é nosso.
   📌 **Este é o caminho do BLOQUEIO A** (pipeline 3D, a barreira do vídeo) **e não depende de
   corrida nenhuma** — é o que dá para avançar em paralelo enquanto as corridas do B1 são
   agendadas. Pré-requisito já satisfeito: o blob `UMDRIVERPRIVATE` deixou de mentir sobre o
   espaço endereçável (A11.1), que é o que o winsys do RADV usaria para dispor endereços.
9. **Corrida do perfil lab** para testar a correção de pivô no cenário 97/0. A corrida de
   22:42 usou o perfil **ativo**, que já convergia desde 05/09 — reproduziu a convergência e
   provou que a faixa A não a quebrou, mas **não** testou o pivô.
10. **TDR (item 25)** continua portão obrigatório de saída da P6. Sem ele, cada bug de shader
   de jogo é um reboot.

## Veredito

> **Handoff mais recente:**
> [`docs/174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md`](docs/174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md)
> — sessão inteiramente offline, **zero corridas gastas**: fechou as perguntas 1, 3, 4 e 5 do
> doc 167 por leitura do próprio repositório, deu dados à pergunta 2, corrigiu cinco defeitos na
> geometria de GPUVA (faixa A11) e decodificou a telemetria de fila da SDMA0 (faixa A12).
> **Nada disso foi validado em hardware.**

- **Compute direto no silício:** VALIDADO EM HARDWARE.
- **Display KMDOD / GOP com ownership físico:** VALIDADO EM HARDWARE.
- **WDDM Full Graphics (`RenderNode=1`) em Código 0:** VALIDADO EM HARDWARE.
- **Topologia VidPn comitada (`pathCount=1`) e alocações primárias:** VALIDADO EM HARDWARE.
- **DWM ativo a 60 Hz e motor SDMA0 executando Blts de apresentação:** VALIDADO EM HARDWARE.
- **Sanitização de scanout GDDR6 via CONST_FILL:** VALIDADO EM HARDWARE.
- **Cadeia display->primária->blt por SDMA sob WDDM:** **VALIDADA EM HARDWARE** em 06/09 22:42
  (`CommitVidPn`, `SetVidPnSourceAddress`, 10 `Present` com `SDMA=YES`, `BLT_TRIPOD PASS` de
  11.059.200 bytes). **Com a ressalva de que o frame estava em branco** (`NonZero=0/256`).
- **Causa do desktop preto:** **MEDIDA** — `dwm.exe` não obtém device D3D11 (`E_NOTIMPL` na
  UMD, `DXGI_ERROR_UNSUPPORTED` no dump). Não é falha de `Present` nem de SDMA.
- **Render-only WDDM:** REPROVADO e encerrado.
- **DCN é pré-requisito para jogos:** **NÃO** — itens 57–62 e 81 provam o contrário.
- **Root GPUVA em offset zero, snapshots dos dois IBs de paginação e dois fills físicos:**
  **VALIDADOS PARCIALMENTE EM HARDWARE**; execução do IB sob VMID8 não foi provada.
- **VidPn/primária no perfil lab atual:** **NÃO CONVERGE**; causa exata ainda
  **NÃO VALIDADA EM HARDWARE**.
- **RADV `vkCreateDevice`, submissão Node 1, Área de Trabalho visível, jogos e LLM:**
  **NÃO VALIDADOS EM HARDWARE.**

O gargalo anterior (**Código 43 / Item 97**) foi superado. Há agora dois bloqueios
independentes: o perfil lab não chega a criar/comitar a primária, e o RADV não retorna de
`vkCreateDevice`. Os candidatos ativo e lab foram reconstruídos, assinados e auditados
offline; o próximo passo físico continua explicitamente **NÃO VALIDADO EM HARDWARE**.
