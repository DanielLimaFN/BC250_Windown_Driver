# 692 — Pacote lab 3.409.1.0 (T2) e critério da Corrida 263: linha de base com a carga de processo novo

**Data:** 10/10/2026. **Natureza:** preparação offline; nada rodou na placa. **Base:** [doc 691](691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md) (auditoria das corridas 253–262 e tarefas T1–T11).

---

## 1. O que é a Corrida 263

**É uma linha de base, não o teste de uma mudança.** A trava dura varia de 3 a 600 s com o mesmo binário, então nenhuma corrida isolada mede efeito. A 263 mede quantos **processos novos** a máquina aguenta até a trava, com uma carga fixa (a T3). As mudanças seguintes (T8, T11 etc.) serão julgadas contra essa taxa.

O pacote é o 3.408.1.0 com uma única diferença de driver, a **T2**: a folha de PTE volta ao MTYPE NC por padrão, como `gmc_v10_0_get_vm_pte`. Ela entra na base porque é paridade com o amdgpu e porque a 5B/5C saiu de uma causa falsa (doc 691 §2). O efeito da T2 **não** é separável nesta corrida; isso está declarado.

## 2. Identidade do candidato

| Item | Valor |
|---|---|
| DriverVer | `10/10/2026,3.409.1.0` (fonte 3.409.0.0) |
| SYS assinado | `81117FD15FFB8822540ED4F09BDCF35304F54E3B314077E206BF2AB14AE91D9F` |
| SYS fonte (lab) | `C26168297B3E541CD76C5E0F280E0BDF66A540EACBE2CCB142CADAC40622A8E1` |
| INF | `91A71B77A14D50B09636CF1E030D5598FA32DE94887E03C6FD560392FFE9B53D` |
| CAT | `2A98CE774F79A9403E2EB9FEE6A550FCABEC453488C84D500ED34F9ACD0EFD3E` |
| UMD | `B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66` (igual à 3.408) |
| RADV | `B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D` (igual) |
| RLC | `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB` (igual) |
| T3 (`tools/bc250-processo-novo.exe`) | `768C82F74EFB48B1AE765113E15879899A493B4843A98B8296F166C53C9D10DF` |
| Perfil | DISPLAY_RENDER_LAB, WDDM 2.7, P1 PERSISTENT SUBMISSION, GfxWaveProbe=1 (mesmas opções do 3.408) |

**Diferenças de código contra o 3.408.1.0:** só a T2 (`bc250_mcdm_callbacks.c`) e a prova de boot `[T2_PTE_MTYPE]` (`bc250_active_miniport.c`).

**Achado ao empacotar:** a linha de boot "5C … MTYPE_UC universal" que a série 5x dizia existir fica num trecho que a imagem de laboratório **não compila**. Ela não estava no SYS do 3.408 e nunca apareceu em corrida. Foi removida. A nova prova roda no caminho de laboratório e mede o binário: codifica páginas de teste pelo codificador real e registra o MTYPE que saiu. A string está no SYS assinado.

**Verificações offline:**
- `build-contract.bat`: EXIT 0.
- Build de laboratório: EXIT 0, bring-up confirmado no binário.
- Empacotador: EXIT 0, assinatura e catálogo sem erro.
- `audit-bc250mcdm-lab-package.ps1`: PASS.
- `audit-bc250mcdm-safety.py`: PASS, com o portão `audit_t2_pte_mtype_default_nc` estendido à prova de boot.

**Backups:** `driver/wdk/package/output_lab_3.408.1.0_262` e `output_lab_3.409.1.0_263`.

**Outras mudanças de ferramenta:**
- Preflight `tools/prepare-corrida263-t2-linha-de-base-processo-novo.ps1`. Confere os hashes acima, inclusive o da T3. Programas abertos são **só registrados** (decisão do Daniel, doc 691 T4). Continua recusando reinício a quente e depurador de kernel.
- Lançador `tools/EXECUTAR_CORRIDA263_T2_LINHA_DE_BASE_PROCESSO_NOVO_600S.cmd`. Watchdog de 600 s, igual às anteriores.
- `tools/RODAR_PROCESSO_NOVO.cmd`: 90 ciclos (~8 min, cabe na janela).
- `tools/coletar-pos-trava.ps1` (depois de trava) e `tools/bind-bc250mcdm-lab.ps1` (fim normal) passam a copiar `C:\BC250_PROCESSO_NOVO.log` e `C:\BC250_MONITOR.csv` para a pasta de evidência. O CSV do monitor não era arquivado desde a 256.

### 2.1 Correção do preflight (10/10, depois da primeira tentativa)

A primeira tentativa da 263 foi recusada com "último reset foi a quente (bit 19)", com a máquina vindo de "Desligar" (evento 1074 às 20:36:02, boot tipo 0x0 às 20:36:54).

- **Medido:** em **71 de 71** boots (06/10 a 10/10), a primeira leitura da CMOS veio com o bit 19 ligado. Todas as leituras seguintes do mesmo boot vieram sem ele (45 de 45), porque a leitura limpa o bit.
- **Consequência:** a checagem herdada da Corrida 238 recusava sempre a primeira tentativa e aprovava a segunda. O "0x40000400 depois de boot frio" do doc 625 era a segunda leitura.
- **Regra nova:** o boot frio é conferido pelo log do Windows. O último desligamento pedido antes do boot (evento 1074) tem de ser "Desligar", e o boot atual não pode ter um evento 41 (desligamento sujo, gravado depois do boot que segue uma trava, reset ou queda de energia). O RESET_REASON passa a ser só registrado.
- **Teste contra os 8 boots recentes:** os 4 depois de trava saem como reset e os 4 depois de "Desligar" saem como frio, inclusive o atual. O preflight não tem erro de sintaxe.

## 3. Roteiro (Daniel)

1. Em cmd elevado: `tools\BC250_RECOVERY.cmd`. Em PowerShell: `tools\inspect-boot-fuse.ps1 -Reset`. Depois **desligue** a máquina (boot frio).
2. Ligue e rode como administrador `tools\EXECUTAR_CORRIDA263_T2_LINHA_DE_BASE_PROCESSO_NOVO_600S.cmd`.
3. Quando o desktop aparecer: anote a hora, abra o menu (Win), espere 10 s e feche (Esc).
4. Em outra janela, como administrador: `tools\RODAR_PROCESSO_NOVO.cmd`.
5. Pode usar a máquina normalmente; programas abertos são permitidos. Anote a hora de qualquer tela preta, congelamento ou imagem corrompida.
6. Se travar: anote a hora, aperte o botão de reset em 10 a 15 s (não corte a energia) e deixe a coleta automática terminar no boot seguinte.

## 4. Critério (escrito antes da corrida)

### Pré-condição (sem ela a corrida é inválida)

1. Log do KMD com `[T2_PTE_MTYPE] folha padrao MTYPE=0 snoop=0 | coerente MTYPE=0 snoop=1 | pedido P6 MTYPE=3` e os três status `0x00000000`.
2. As linhas de regime da base 3.408 presentes: `[4C_HDP_FLUSH_BYPASS] ATIVO`, `[C3_VRAM_WC]`, `[C1_GFX_DOORBELL] ATIVA`.
3. Desktop desenhado pelo nosso driver (Presents no log).
4. Se a T3 rodou: as linhas `FILHO … ADAPTADOR 1002:13FE` no `C:\BC250_PROCESSO_NOVO.log` da sessão desta corrida. Código 2 (adaptador errado) invalida a parte da T3.

### Medidas que a análise tem de reportar sempre

- Tempo do bind até a trava ou até o rollback.
- Ciclos da T3 completos (`CICLO k filho … saiu codigo=0`), e o último evento da T3 antes da trava.
- Processos que carregaram a UMD (logs da UMD), com hora de início, inclusive os programas abertos pelo Daniel.
- Intervalo entre o último início de processo novo (T3 ou não) e a trava.
- FLUSH_TLB, FILL, TRANSFER e IBs executados, contados no log recuperado; uso de VRAM e memória compartilhada pelo `BC250_MONITOR.csv` desta sessão.
- TDR, fault do GFXHUB e BugCheck (contagem).

### Desfechos

**A — 600 s sem trava, com a T3 completando ≥ 80 ciclos.**
- *Assinatura:* rollback do watchdog com `RECOVERY_OK_ONLINE`; log da T3 com `SESSAO FIM … motivo=COMPLETO`, ou ≥ 80 ciclos com código 0 antes do rollback; zero TDR, fault e BugCheck.
- *Implicação:* a taxa com esta carga é baixa (< 1 trava por ~80 processos). Não conta como melhora (a 256 também chegou a 600 s). Repetir (263b) com a mesma carga; se repetir A, a próxima linha de base sobe a carga (`--clear`, mais texturas ou intervalo menor).

**B — trava dura com a T3 rodando, dentro de um processo filho.**
- *Assinatura:* a última linha da T3 é de um FILHO entre `INICIO` e `FIM` (sem o `FIM` do ciclo k); GPU ociosa no último tique do rastro; zero TDR e fault.
- *Implicação:* reforça a hipótese do processo novo (doc 691 §5). A taxa é 1 trava em k processos. Próximo passo: mineração T5 do ciclo k (etapa do filho em que parou) e repetição (263b) para ter n=2.

**C — trava dura com a T3 rodando, entre ciclos.**
- *Assinatura:* a última linha da T3 é do PAI (`CICLO k … saiu` ou intervalo), sem filho vivo; nenhum outro processo novo da UMD nos 3 s anteriores.
- *Implicação:* enfraquece a hipótese do processo novo. A trava não depende do início de processo; voltar à mineração de regime (T6, MMIO em regime).

**D — trava antes de a T3 começar** (subida do desktop ou menu, como a 256b e a 262).
- *Assinatura:* log da T3 sem sessão nova desta corrida; trava até ~3 s depois de processos do shell iniciarem.
- *Implicação:* conta como ponto de dado de "processo novo" (processos do shell), mas não dá taxa da T3. Repetir.

**E — fora do mapa.** Exemplos: TDR, BugCheck, fault do GFXHUB, imagem corrompida ou artefato que não aparecia no 3.408 (possível efeito da T2 no cache), filho preso sem a máquina travar (`motivo=FILHO_PRESO`), pré-condição falha.
- *Implicação:* descrever o observado antes de julgar. Imagem corrompida ou fault novo sugerem efeito da T2 (NC no GL2) e pedem comparação com o 3.408.

**Regra (Lei 10):** desfecho parcial não é desfecho. B exige as três coisas: trava, último evento dentro de um filho e GPU ociosa. Se faltar uma, é E.

### Teto e abandono

- Até **3 corridas** com este pacote e esta carga (263, 263b, 263c) para fechar a linha de base.
- Se as 3 terminarem em D, a carga T3 não é alcançável e a linha de base passa a ser o tempo até a trava na subida do desktop.
- Pré-condição falhando por defeito meu: corrigir e repetir, sem contar no teto.

**NÃO VALIDADO EM HARDWARE.**
