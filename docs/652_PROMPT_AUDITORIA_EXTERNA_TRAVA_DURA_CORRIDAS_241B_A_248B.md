# 652 — Prompt de auditoria externa: a trava dura da BC-250 (Corridas 241b–248b)

> Escrito em 08/10/2026 a pedido do Daniel, para ser colado em outras IAs. Quem escreve é a IA que conduziu as últimas corridas. **Leia isso como depoimento de parte interessada:** o objetivo é que você discorde dela.

---

## PROMPT (copiar daqui para baixo)

Você é auditor externo de um driver de GPU para Windows escrito do zero. A IA que conduziu o trabalho até agora está há muito tempo na mesma linha de raciocínio, e o dono do projeto (Daniel) acha que ela ficou presa: gastou ~11 corridas físicas em uma semana trocando uma variável por vez sem mudar o resultado. Queremos **ideias novas**, críticas ao método e leitura independente do código e dos logs. Não repita a abordagem anterior só porque está documentada. Se achar que a premissa está errada, diga.

### 1. O sistema

- **Placa:** AMD BC-250 (PCI `1002:13FE`), APU Cyan Skillfish 2: GC 10.1.3 (RDNA1), 24 CUs, 16 GB GDDR6 **compartilhados entre CPU e GPU** (não há RAM separada). CPU Zen 2.
- **SO:** Windows 10 Pro 22H2 (19045). Não existe driver AMD para essa placa no Windows.
- **Nosso driver:** KMD WDDM (`bc250mcdm.sys`, miniport em `driver/wdk/`, núcleo em `driver/core/`) + UMD D3D11→Vulkan (`driver/umd/`) + um fork da RADV (Mesa) com winsys WDDM (`../bc250-win/bc250-win/mesa-radv-wddm/`). Display herdado do GOP do UEFI (não programamos o DCN).
- **VRAM:** 256 MB reservados pela CMOS (decisão do Daniel, sem VRAM grande como muleta); o resto vem de memória de sistema pelo segmento 2 do WDDM (como o GTT do Linux).
- **Referência:** no Linux, o `amdgpu` com o **mesmo firmware** e os mesmos 256 MB não trava, nem sob despejo pesado da VRAM (doc 552).

### 2. O problema

**Trava dura da máquina inteira** depois de segundos a minutos de uso leve do desktop (roteiro fixo: abrir e fechar o menu Iniciar a cada 30 s).

Assinatura, igual em ~30 corridas:
- tela congela, teclado e mouse mortos, só reset físico resolve;
- **sem BugCheck, sem dump, sem TDR, sem fault de GPU, sem timeout** registrado;
- o log do KMD (gravado por thread própria em `C:\BC250_DISPLAY_LOG.txt`, que sobrevive ao reset) termina no meio de atividade normal (paginação op 12, IBs do SDMA0 executados, Present), com a GPU ociosa no último registro;
- **com o WinDbg (KDNET) ligado, Ctrl+Break não para a máquina na trava** (as CPUs não respondem ao depurador), enquanto no controle ele para;
- `RESET_REASON` no boot seguinte igual ao de qualquer reset a quente (sem sync flood, sem erro de fabric no FCH);
- teste de RAM de 8 GB sem erro; temperatura ~80 °C, sem relação;
- **sem o nosso driver** (BasicDisplay) a máquina roda 45 min sem nada (W1); **com o nosso driver parado no desktop, sem abrir nada**, 600 s sem trava (Corrida 237). A trava aparece com uso (menu, Pesquisa).

### 3. As últimas 11 corridas (todas com o mesmo roteiro, só o menu Iniciar)

Tempo = execução até a trava, descontadas as pausas do depurador quando houve.

| Corrida | Mudança testada | Tempo até travar | Observação |
|---|---|---|---|
| 241b | encadeamento GFX da RADV como o upstream | ~330 s | sem TDR do menu (antes havia) |
| 242 | RPTR do IH por doorbell (P3.3) | ~10–15 s | antes do 1º quadro |
| 242b | repetição da 242 | ~20 s | |
| 243 | P3.3 + faixa de doorbell do MEC como o amdgpu | ~49 s | |
| 244 | P3.3 off; **RLC rodando o firmware** (antes ficava na ROM) + reset do GFX como o amdgpu | ~40 s | |
| 245 | 244 sem o RLC rodando (RLC na ROM) | ~289 s | |
| 246 | 245 + 34 golden settings do Cyan Skillfish | ~33 s | |
| 246b | repetição da 246, com KD | ~228 s reais | |
| 247 | 246 + RLC rodando, com KD | ~108 s reais | |
| 248 | 247 + `RLC_PG_CNTL` bit 23 (handshake RLC↔SMU desligado) + log mínimo na UMD | ~8 s | |
| 248b | 248 sem o bit 23 (só o log mínimo a mais que a 247) | ~22 s | |

Mais antigas (docs 555–636): travas entre ~45 s e ~584 s com dezenas de mudanças (PTE pelo SDMA, anel e fences na RAM via GART, MTYPE UC, doorbells, conclusão por interrupção, SDMA1 ativo, MMHUB GART, IH ligado, HDP desligado, CPU sem ler VRAM pela BAR...). **Nenhuma** mudança eliminou a trava; algumas corridas raras chegaram a 600 s (201, 222, 226, 237 parada).

**Uma leitura que a IA anterior não fez a tempo:** separando por "RLC rodando o firmware" (244, 247, 248, 248b: 40/108/8/22 s) contra "RLC na ROM" (245, 246, 246b: 289/33/228 s), o RLC rodando parece encurtar a vida da máquina, mas a dispersão sem mudança vai de 33 s a 584 s. **Avalie você se há sinal estatístico aqui ou se tudo é ruído.**

### 4. O que pedimos

1. **Leia os logs e o código, não só os resumos.** Especialmente o fim dos logs do KMD das corridas acima (seção 6) e os caminhos de paginação, submissão SDMA/GFX, interrupções e present.
2. **Hipóteses novas para uma trava que mata as CPUs sem BugCheck**, numa APU onde CPU e GPU dividem a mesma memória. Pense em coisas que um driver Windows faz e o `amdgpu` não faz, ou vice-versa, inclusive fora da GPU: acesso a MMIO/BAR durante transições de energia, estados de C-state/P-state da CPU, ASPM/link PCIe, IOMMU, coerência de cache CPU↔GPU em memória compartilhada (snoop, MTYPE, write-combining), ordem de escrita de registradores do SMU/PSP, GFXOFF, clock gating, watchdogs do fabric (Data Fabric, MCA), acessos de MMIO a blocos desligados, deadlock de spinlock em DISPATCH_LEVEL com IPI, etc. Ordene por plausibilidade e diga **como testar cada uma sem corrida ou com uma corrida que discrimine**.
3. **Critique o método.** Bissecção por uma variável numa distribuição de 8–584 s com n=1 faz sentido? Que desenho de experimento daria resposta com poucas corridas (ex.: várias corridas curtas por configuração, ensaio sintético que trava em segundos, teste de estresse dirigido)?
4. **Observabilidade:** como capturar o que acontece no instante da trava, se o KD não para a máquina e não há dump? (Ex.: POST codes, NMI, MCA banks no próximo boot, ring buffer em memória que sobreviva ao reset a quente, leitura de registradores de erro do SMU/DF no boot seguinte, trace de PCIe, watchdog de hardware.)
5. **Diferenças concretas com o `amdgpu`** que ainda existam no nosso bring-up (o inventário está no doc 593 §4 e no doc 624). Aponte as que faltam ou que estão erradas.
6. **Erros nossos:** qualquer coisa no código que pareça bug real (corrida, uso de memória após free, ordem errada, acesso a registrador sem procedência), com arquivo e linha.

### 5. Regras do projeto que você deve respeitar nas propostas

- Sem WARP, sem fallback de software, sem fingir execução (fence pela CPU, sleep simulando GPU).
- Não escrever registradores de display (DCN).
- VRAM continua em 256 MB.
- Toda afirmação classificada: P0 (medido no silício), P1 (do código upstream), H (hipótese).
- Quem roda no hardware é o Daniel; cada corrida custa reboot e recuperação. Proponha o mínimo de corridas com o máximo de informação, e diga o critério de cada uma **antes**.

### 6. Onde está o material (repositório `NamixWDDM`)

- **Estado e histórico:** `CLAUDE.md` (topo), `BASE_DE_AVANCO.md`, `PLANO_MESTRE.md`.
- **Passagem de bastão completa até 06/10:** `docs/624_PASSAGEM_DE_BASTAO_2026-10-06_TRAVA_DURA_DEPOIS_DA_PARIDADE_P4.md` (leia primeiro).
- **Docs das corridas acima:** `docs/636` a `docs/651` (um por corrida, com critério escrito antes e veredito depois).
- **Mineração das travas:** `docs/560` §6, `docs/563`, `docs/578`, `docs/582` (o fim do log é a última drenagem, não o último evento), `docs/625` (RESET_REASON), `docs/630` (TDRs do GFX e erros do SQ).
- **Referência Linux:** `docs/552` (coleta com o `amdgpu` na mesma placa), `inc/linux_ref/` (offsets e máscaras do GC 10.1).
- **Evidência bruta:** `docs/evidence/<data>-DESKTOP-HQAG6T4-lab-binding/`, um por corrida; o log do KMD é `display-log-this-bind*.txt` (ou `-RECUPERADO.txt`), os logs da UMD são `bc250_umd_log_*.txt` e `umd-logs-pos-travamento/`. Últimas: `2026-10-07_22-*` a `2026-10-08_10-01-34-*`.
- **Código que mais importa:**
  - `driver/wdk/bc250_active_miniport.c` (bring-up, StartDevice, PSP, RLC, golden settings, GART, IH, TDR; arquivo grande, procure pelas chaves `BC250_*_LIKE_AMDGPU` perto da linha 120–180);
  - `driver/wdk/bc250_sdma_present.c` (submissão e present pelo SDMA0);
  - `driver/wdk/bc250_mcdm_callbacks.c` (DDIs WDDM);
  - `driver/wdk/bc250_mmio_topology_transport.c` (todo acesso a MMIO passa aqui, com allowlist);
  - `driver/core/bc250_gfx_ring_transaction.h` e vizinhos (offsets e contratos);
  - `driver/umd/bc250_wddm_winsys.c` e a RADV em `../bc250-win/bc250-win/mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/`.
- **Regras:** `CLAUDE.md` §2 (Leis 1–10), `rules.md`.

### 7. Formato da resposta

1. Resumo em 5 linhas: o que você acha que está acontecendo.
2. Hipóteses ordenadas, cada uma com: mecanismo, evidência a favor e contra **nos logs** (cite arquivo e linha), confiança (P1/H), e o teste que a confirma ou mata.
3. Críticas ao método e proposta de desenho experimental.
4. Bugs concretos encontrados no código (arquivo:linha).
5. Plano das próximas 3 corridas no máximo, com o critério escrito antes.

Seja direto. Se a resposta for "pare de mexer no GFX e olhe X", diga isso.
