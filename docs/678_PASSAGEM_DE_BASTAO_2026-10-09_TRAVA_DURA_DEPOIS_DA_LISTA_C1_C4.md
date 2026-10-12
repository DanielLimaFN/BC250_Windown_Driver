# 678 — Passagem de bastão (09/10/2026): a trava dura continua depois da P5 e da lista C1–C4

**Para a próxima IA. Leia este documento inteiro antes de mexer em qualquer coisa.**

- O anterior é o [doc 624](624_PASSAGEM_DE_BASTAO_2026-10-06_TRAVA_DURA_DEPOIS_DA_PARIDADE_P4.md) (até a Corrida 236).
- Este cobre as Corridas 249 a 255 e o trabalho offline entre elas, com ênfase na minha sessão (Corridas 252–255).
- O que não está aqui está no [`CLAUDE.md`](../CLAUDE.md) (linha de estado) e no [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md).

---

## 1. Onde estamos, em 6 linhas

1. **O problema único:** a máquina inteira congela (trava dura) entre ~8 s e ~10 min depois do bind, quase sempre logo depois de abrir o menu Iniciar ou a Pesquisa.
   - Não há BugCheck, TDR, fault nem timeout.
   - A GPU está ociosa e tudo o que foi submetido foi aposentado.
   - O log do KMD termina no meio de uma rajada normal de paginação (`PAGING_DDI` operação 12 = FLUSH_TLB).
   - No reboot aparece Kernel-Power 41, e a placa sobe em **Código 43 pelo fusível**.
2. **Sem o nosso driver** a plataforma não trava: W1, 45 min. **Parada no desktop**, com o nosso driver, também não: Corrida 237, 600 s. É preciso uso (menu, Pesquisa).
3. **No Linux**, o `amdgpu` com o mesmo firmware e 256 MB não trava sob paginação pesada (doc 552, L-B; doc 662, L1 com o DCN herdado do GOP).
4. **Tudo o que copiamos do `amdgpu` até agora funcionou e ficou, mas nenhuma mudança tirou a trava.** A lista de §3 é o mapa do que já foi eliminado.
5. **Último pacote:** lab **3.401.1.0**, SYS assinado `A022A815…`, UMD `B73F9BDA`, RADV `B4AE2E14`. Backup em `driver/wdk/package/output_lab_3.401.1.0_255`.
6. **Estado físico da placa agora:** Código 43 pelo fusível, depois da 255. Antes de qualquer bind: `tools\BC250_RECOVERY.cmd` elevado, `tools\inspect-boot-fuse.ps1 -Reset`, **desligar** (boot frio, não reiniciar).

## 2. O que eu fiz nesta sessão (09/10), em ordem

| Corrida | O que entrou | Doc | Resultado |
|---|---|---|---|
| — | **P5.0** (desenho): a L2/TLB do GFXHUB já estava igual; sobrava a forma da árvore. **Régua de FILLs** (`tools/regua-fills.py`, `docs/regua-fills.csv`) | 667 | offline |
| 252 | **P5**: tabela de páginas de 4 níveis × 9 bits, VA de 48 bits, `CONTEXTn_CNTL=0x007FFE07`, geometria em fonte única `driver/core/bc250_gpuva_geometry.h`. SOMBRA desligada | 668/669 | B: P5 em regime, trava aos ~124 s (831 FILLs). **Fica** |
| — | Mineração: a tradução velha da TLB e as "filas de compute quebradas" da comunidade não explicam a trava | 670 | offline |
| — | Lista de causas C1–C4 por paridade, sem perder performance | 671 | offline |
| 253 | **C1**: WPTR do anel GFX por doorbell 0x116 (byte 0x458), `CP_RB_WPTR_POLL_CNTL` no valor da placa, prova por 4 NOPs, rollback para MMIO. **C2**: sem código (o MEC já estava sem polling). **MON.1**: Tctl de volta e uso 3D/Copy no monitor | 672/673 | B: doorbell provada (`HIT=1`, 769/769 sem MMIO), trava aos ~40 s. **Fica** |
| 254 | **C3** (corrigida, ver §4.3): todo mapeamento da VRAM pela DRAM no KMD passa a WC (`bc250_vram_cpu_path.h`, `BC250_C3_VRAM_DO_WINDOWS_PELA_DRAM`) | 674/675 | B: 8 mapeamentos WC, zero UC, trava ~1,8 s depois do menu (~43 s). **Fica** |
| 255 | **C4**: zeramento por `CONST_FILL` em fatias de 4 MiB como `amdgpu_fill_buffer` (`BC250_C4_ZERAMENTO_COMO_O_AMDGPU`); a cópia de zeros da X2b fica dormente; o encoder aceita 0x400000 (`BC250_SDMA_CONST_FILL_PACKET_MAX_BYTES`) | 676/677 | B: 585 FILLs por CONST_FILL, trava ~17 s depois do menu (~73 s, 561 FILLs). **Fica** |

**Duração e FILLs recentes (régua):**

| Corrida | Duração | FILLs |
|---|---|---|
| 249 | ~462 s | 4.429 |
| 250 | ~213 s | 1.754 |
| 251b | ~53 s | 412 |
| 252 | ~124 s | 831 |
| 253 | ~40 s | 255 |
| 254 | ~43 s | 407 |
| 255 | ~73 s | 561 |

A dispersão sem mudança vai de 33 a 584 s. **Com n=1, nenhuma diferença de duração é atribuível a mudança nenhuma.**

## 3. Já fora das causas suficientes (medido no silício, não repetir)

**Display:**
- DCN e scanout: L1 no Linux (doc 662) e 251b, com zero escrita no scanout.
- Clock gating do SDMA0 (CG.1).

**Memória:**
- TMR do PSP (TMR.1).
- Forma da árvore de páginas (P5).
- Tradução velha da TLB (doc 670).
- PTE escrita pela CPU e invalidação pela CPU (4c inteira, 205–210).
- Leitura de tabela pela BAR (4c.3).
- VRAM pela BAR no KMD (P1b).
- Tipo de memória misto UC/WC (C3).
- Zeramento por cópia ou por CONST_FILL, em qualquer tamanho de fatia (X2b/203, C4/255).
- Falta de memória no segmento 1 (M1b.1).

**Engines e interrupções:**
- SDMA1 vivo e com PTE (P4).
- Interrupções e conclusão por IH (P3).
- Doorbell do SDMA0 (P2).
- Doorbell do GFX (C1).
- Polling de WPTR do MEC (C2, já igual).
- Anel e fences em GTT (D2/P7).

**Outros:**
- Instrumentação (P1, silêncio da BAR).
- Encadeamento da RADV com VMID 7. Era causa dos TDRs do menu, não da trava (241b).
- Golden settings (G.1, fica).
- RLC rodando ou na ROM: as duas formas travam. Com o RLC rodando as durações foram mais curtas, mas com n pequeno.

## 4. Erros que eu cometi (não repita)

### 4.1 Ferramentas e arquivos

1. **Fatiar arquivo C por `str.index()` apagou ~13.000 linhas de `bc250_active_miniport.c`.**
   - A âncora casou primeiro com o protótipo da função, não com a definição.
   - Recuperei de `~/.claude/file-history/...` mais as partes intactas.
   - **Regra:** exija `count == 1` na âncora, confira as linhas antes de gravar e nunca corte por intervalo sem conferir as duas pontas. Ver a memória `fatiar-por-index-pega-o-prototipo`.
2. **Heredoc do Bash corrompe conteúdo**, e aconteceu três vezes nesta sessão:
   - `"\n"` virou quebra de linha real dentro de uma string C (erro C2001 na compilação);
   - `\b` virou backspace num script Python;
   - apóstrofos (`So'`, `e'`) quebraram o heredoc.
   - **Regra:** todo script com barra invertida ou apóstrofo vai num arquivo escrito com **Write**, no scratchpad, e é executado de lá. Nunca `python - <<EOF` com C dentro.
3. **`sed -i` no Git Bash regrava em LF** e quebra `.bat`/`.cmd` (o `cmd.exe` come caracteres). Edite em modo binário preservando CRLF.
   - Os arquivos do repositório misturam CRLF e LF: `mcdm_callbacks_test.c` é LF, `bc250_mcdm_callbacks.c` é CRLF.
   - Detecte o fim de linha antes de comparar âncoras; o meu script de teste falhou com `IndexError` por assumir CRLF.
4. **`build-contract.bat` não roda pelo Bash**: dá "não é reconhecido". Rode pelo PowerShell: `cmd /c ".\build-contract.bat"` dentro de `driver\wdk`.
5. **O empacotador (`tools\package-bc250mcdm-lab.ps1`) às vezes falha com IOException no `Copy-Item` da UMD** e deixa o pacote inconsistente. A auditoria do pacote reprova com "assinatura ou signer inválido" e "DriverVer diverge".
   - **Repita o empacotador.** Nunca aceite pacote sem `BC250 MCDM LAB PACKAGE AUDIT: PASS` logo depois.
6. **O empacotador não compila.** A ordem é:
   1. subir `DriverVer` em `driver/wdk/package/bc250mcdm.inf` (fonte x.y.0.0 → candidato x.y.1.0; nunca reutilize um número já instalado);
   2. `tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup`;
   3. `tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -ArmGfxWaveProbe -IAcceptSelectorWritesDuringTdr`;
   4. `tools\audit-bc250mcdm-lab-package.ps1`;
   5. conferir por string no SYS assinado (`grep -c -a -F "<linha nova>"`) que o código novo está lá;
   6. copiar para `output_lab_<versão>_<corrida>`.

### 4.2 Testes e portões

7. **Os testes offline congelam o comportamento antigo.**
   - Cada mudança de paridade quebrou testes que exigiam o valor velho: END_HI, VMID9 `0x80555407`, adaptador 48 bits, limite do journal, fatias de 256 KiB, cópia de zeros da X2b, `0x3FFFFC` do encoder.
   - Também quebrou portões da auditoria (X2a, X2b, MEC.1, P1, I.PLAT).
   - **Atualize o teste e o portão para o novo invariante, nunca os afrouxe.** E prove o portão novo por mutação: escrevi scripts `mut_*.py` no scratchpad, que aplicam uma mutação, rodam `tools/audit-bc250mcdm-safety.py`, exigem `AUDIT: FAILED` com a linha do portão e restauram com conferência de SHA.
8. **Função `static` que fica sem uso vira aviso → erro** (`/W4 /WX`). Quando desligar um caminho por chave, cerque a função com o mesmo `#if` (como fiz com `Bc250McdmBuildFillByCopy`).

### 4.3 Diagnóstico

9. **Premissa errada da C3.** Escrevi no doc 671 que "o Windows acessa a VRAM pela BAR/HDP". Era falso.
   - Desde a P1b, `Bc250ActiveQueryRuntimeCaps` publica `CpuPhysicalStart` já traduzido para a DRAM (`bc250_active_miniport.c`, perto da linha 4031).
   - Eu tinha lido só metade do caminho; implementei uma versão redundante e tive de desfazê-la (doc 674 §1).
   - **Regra:** antes de propor uma diferença com o Linux, siga o endereço até onde ele é publicado ao VidMm. Ver a memória `bc250-historico-antes-de-reverter`.
10. **O `QUERYSEGMENT4` existe em dois sítios** de `bc250_mcdm_callbacks.c` (um no ramo `#else`). Âncora única ou o edit cai no ramo errado.
11. **Nomes de tipo:** é `BC250_MMIO_TOPOLOGY_CONTEXT`, não `..._TRANSPORT`; `LevelIndex` precisa de cast para `WORD`.
12. **Expectativa honesta antes da corrida.** Na C4 eu sabia que antes de 29/09 o zeramento já era CONST_FILL de ~4 MiB e travava. Escrevi isso no critério ("expectativa honesta: B") e acertei.
    - Faça o mesmo: procure no histórico se a "diferença nova" já foi o estado antigo.

### 4.4 Erros herdados que continuam valendo (das sessões anteriores, ver `MEMORY.md`)

- **Ler registrador não é inócuo.** A foto de 69 registradores de clock gating travou a 251 dentro do StartDevice e deu tela verde no Linux. Só leia o que já foi lido sem dano.
- **KD não para a trava dura** (Ctrl+Break morre). Não gaste corrida com KD nem com dump para ela.
- **O fim do log não é o fim do evento**: é a última drenagem do anel de log. O tique do rastro (~115–127 ms, não 100 ms) e o QPC dão o tempo real.
- **O log de display é cumulativo e amostrado.** Ausência de linha amostrada não prova nada; resgate a cauda pelo offset do baseline.
- **`bc250compute` conflita com o KMD.** Tem de estar parado durante a corrida; o lançador recusa.
- **A RADV tem dois caminhos de DLL.** Confira os dois hashes (o preflight confere).

## 5. Procedimento de análise de corrida (o que eu usei em 252–255)

1. Pasta mais nova: `ls -td docs/evidence/*lab-binding | head -1`. Confira o `package_sys_sha256` em `binding-provenance.json` contra o candidato.
2. Resgate as caudas pelo offset dos baselines:
   - `tail -c +(start_offset+1) C:/BC250_DISPLAY_LOG.txt > display-log-this-bind-RECUPERADO.txt`;
   - o mesmo para `C:/BC250_PAGING_TRACE.bin` com `paging-trace-baseline.json`.
3. Corte a sessão antes do reboot, na 2ª linha `[DX56_PAGING_TRACE] START`, e grave em `b2.txt`.
4. Reboot e estado: `LastBootUpTime`, evento Kernel-Power 41 e `Get-PnpDevice ... DEV_13FE` (Código 43 = `CM_PROB_FAILED_POST_START`).
5. Pré-condição do critério: as linhas `[P1B_VRAM_DRAM] ATIVO`, `[C1_GFX_DOORBELL] ATIVA`, `[C3_VRAM_WC] ...=8` e `[C4_FILL] chave=1`.
6. Contagens: `IB EXECUTADO`, `Present #`, `COMPOSITE_BLT OK`, `SubmitVirtual Node1 GFX] ENTER`, `TDR_QUIESCE`, `GFXHUB_FAULT`, `TIMEOUT`, `INSIDE_IB`.
7. Último `dxg_fence` submetido contra o último `FenceId` aposentado, e último `ring_fence` do GFX contra `GfxRetire UltimaFence`.
8. Duração pelo QPC (`qpc=` do primeiro ao último, ÷ 10⁷), FILLs por `python tools/regua-fills.py`, e monitor por `awk '/# inicio AAAA-MM-DD HH:M/{f=1} f' C:/BC250_MONITOR.csv`.
9. Linha do tempo dos processos em `umd-logs-pos-travamento/`: primeira e última linha do menu (`StartMenuExperienceHost`), da Pesquisa e do `dwm.exe`.
   - Se a pasta não existir, a coleta automática do boot ainda está rodando; espere.
10. Relato em 5 pontos (regra do `CLAUDE.md`) e tabela ✅🟡❌ do desktop mínimo.
11. Arquive no doc da corrida e atualize o `BASE_DE_AVANCO.md` (entrada no topo), a linha de estado do `CLAUDE.md` e a tabela de estado do doc da lista.

## 6. O que ainda NÃO foi tentado (candidatos, todos H, nenhum preparado)

A lista C1–C4 se esgotou. Diferenças conhecidas com o `amdgpu` que não viraram etapa:

| Candidato | O que se sabe | Cuidado |
|---|---|---|
| **SMU** | O Linux conversa com o SMU (clocks, mensagens); nós não. O clock da GPU no monitor ficou para uma "SMU.1" | O mailbox do SMU não tem proveniência catalogada (Lei 7). Só com offsets citados de `inc/linux_ref` |
| **`fillsize` do CONST_FILL** | O `amdgpu` deixa 0; nós usamos 2 (DWORD, formato da Mesa) | Para zeros, o resultado deve ser igual (H). Diferença declarada no doc 676 |
| **EDID/VidPn** | Refresh real medido 60,5 Hz; o EDID sintetizado declara 56,13 (doc 666). Defeito nosso, não ligado à trava | É funcional, não da trava |
| **Correlação P3.3 e RLC rodando** | O RPTR do IH por doorbell (P3.3) e o RLC rodando encurtaram a vida da máquina (n=1–2). P3.3 está desligada; RLC.1 está ligada | Mecanismo desconhecido |
| **Novo inventário** | O doc 593 §4 tem o inventário de paridade original. Refazer comparando registrador por registrador o estado em regime com o Linux | A frente Linux foi encerrada pelo Daniel (doc 663 §6) |

**Uma diferença por corrida.** O critério é escrito antes, com A–E (Lei 10), e a repetição é obrigatória se der A.

## 7. Regras do Daniel que valem para você

- **Ele executa as corridas; você compila, analisa e prepara.** Não faça commit sem ele pedir.
- **Paridade com o `amdgpu` e nada que custe performance:** "o ideal é ser igual ao Linux".
- **VRAM mínima de 256 MB.** Não proponha VRAM maior como muleta (doc 551).
- **Não proponha WARP, fallback de CPU, nem trocar o objetivo** por compute ou LLM (Lei do escopo).
- **Depois de cada corrida:** relato em 5 pontos e a tabela ✅🟡❌. Quando ele diz "apenas analise", não prepare código.
- **Ao relatar, escreva NÃO VALIDADO EM HARDWARE** para tudo o que não foi medido.

## 8. Arquivos-chave tocados nesta sessão

| Arquivo | O quê |
|---|---|
| `driver/core/bc250_gpuva_geometry.h` | P5, fonte única da geometria |
| `driver/wdk/bc250_active_miniport.c` | C1 (doorbell do GFX, arme/desarme/TDR), C3 (4 sítios WC), linhas `[C1_GFX_DOORBELL]`/`[C3_VRAM_WC]` |
| `driver/wdk/bc250_sdma_present.c/.h` | helpers da doorbell (`Bc250SdmaPresentDoorbellWrite64`) |
| `driver/wdk/bc250_vram_cpu_path.h` | C3 (`Bc250VramCpuUseWc`, `Bc250VramCpuMapTranslated`) |
| `driver/wdk/bc250_mcdm_callbacks.c` | MON.1 (Tctl), C4 (chave, fatia, `[C4_FILL]`, X2b dormente) |
| `driver/core/bc250_sdma_packet_builder.h/.c` | C4 (teto do pacote 0x400000) |
| `driver/tests/mcdm_callbacks_test.c`, `sdma_packet_builder_test.c` | testes da C4 |
| `tools/audit-bc250mcdm-safety.py` | portões `audit_p5_geometria_unica_de_4_niveis`, `audit_c1_gfx_doorbell_like_amdgpu`, `audit_c3_vram_um_so_tipo_wc`, `audit_c4_zeramento_como_o_amdgpu` |
| `tools/monitor-corrida.py` | uso 3D/Copy por LUID, Tctl, GFX/s |
| `tools/EXECUTAR_CORRIDA25x_*.cmd`, `tools/prepare-corrida25x-*.ps1` | lançadores e preflights (hashes, manifesto, CMOS 256, boot frio, apps fechados) |

**Nada disso está commitado.** O `git status` tem ~200 entradas modificadas ou novas, na branch `p7-desktop-estabilidade`. Peça ao Daniel antes de commitar.

**NÃO VALIDADO EM HARDWARE:** a causa da trava dura.
