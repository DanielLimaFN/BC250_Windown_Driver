# 693 — Corrida 263: linha de base com processo novo — trava dura dentro do 22º processo da T3 (desfecho B)

**Data/hora:** 10/10/2026, bind às 20:42:04, trava por volta de 20:44:43. Boot frio às 20:36:54 (evento 1074 "Desligar o sistema", tipo de boot 0x0).
**Candidato:** lab 3.409.1.0. SYS assinado `81117FD15FFB8822…`, conferido na pasta de evidência.
**Evidência:** `docs/evidence/2026-10-10_20-42-04-DESKTOP-HQAG6T4-lab-binding/`. Coleta automática concluída às 20:47:23.
**Critério:** [doc 692](692_PACOTE_3409_T2_E_CRITERIO_CORRIDA263_LINHA_DE_BASE_PROCESSO_NOVO.md) §4.

---

## 1. Pré-condição (completa)

| Item | Medido |
|---|---|
| `[T2_PTE_MTYPE]` | `folha padrao MTYPE=0 snoop=0 \| coerente MTYPE=0 snoop=1 \| pedido P6 MTYPE=3`, três status `0x00000000`: exatamente o esperado. A T2 está no silício. |
| Base 3.408 | `[4C_HDP_FLUSH_BYPASS] ATIVO`, `[C3_VRAM_WC] … =8`, `[C1_GFX_DOORBELL] ATIVA (ONLINE)` |
| Desktop pelo nosso driver | 1.765 Presents; o DWM apresentava (`DXGI Present LEAVE cb: hr=0x00000000` às 20:44:43.028) |
| T3 na BC-250 | 22 filhos com `ADAPTADOR 1002:13FE`; nenhum código 2 |

## 2. O que aconteceu

- **T3:** sessão `20261010-204226-10664`, começou às 20:42:26. **21 ciclos completos, todos com código 0**, de 1,0 a 2,1 s por filho.
- **Trava dentro do ciclo 22.** O filho (pid 5040) começou às 20:44:42.532, criou o device, leu a memória (`local uso=0 orcamento=198 MB`) e **não chegou** a registrar as texturas.
- **Lado da UMD do pid 5040** (log gravado direto no arquivo, sem amostragem):
  - 3ª alocação do processo: `Allocate` de 8 MB (`hAlloc=0x40000800`), `MapAllocation` e `MakeResident` com fence de paginação 7008.
  - `WaitPaging` **voltou** às 20:44:42.845 (fence 7008 alcançada).
  - Última linha, às 20:44:42.846: `Bc250UmdVkCreateResource: TEXTURE2D 1024x2048`, ou seja, a criação da imagem Vulkan da RADV para essa textura.
- **A máquina ainda vivia ~180 ms depois:** a última linha do DWM é um Present bem-sucedido às 20:44:43.028.
- **Último tique do rastro (1339, ~154 s depois do StartDevice):** todas as submissões aposentadas (SDMA0 até a fence 8114, GFX até 2050), SDMA1 ocioso, Tctl 83 °C. GPU ociosa, como nas travas anteriores.
- **Totais:** zero TDR, zero `GFXHUB_FAULT`, zero BugCheck. 8.157 IBs executados, 22.086 FLUSH_TLB, 877 FILL, 354 TRANSFER (contagens do log recuperado).
- **Programas abertos:** o preflight registrou claude, msedge, msedgewebview2 e M365Copilot. Os únicos processos da UMD vivos perto da trava eram o dwm.exe, o Explorer.EXE e a T3.
- **Depois da trava:** teste de RAM com 7.868 MB e 0 erros. `RESET_REASON` 0x40080400, só registro (o bit 19 aparece em todo boot; ver §5).

## 3. Veredito pelo critério

**Desfecho B**, com os três sinais exigidos:
1. trava dura com a T3 rodando;
2. último evento da T3 dentro de um filho (ciclo 22 sem `FIM`);
3. GPU ociosa no último tique, com zero TDR e zero fault.

**Taxa medida (n=1):** 1 trava em 22 processos novos da T3, cerca de 2 min 20 s depois de a T3 começar.

## 4. O que a corrida acrescenta

1. **Terceira trava no mesmo passo da vida de um processo novo.**

   | Corrida | Processo | Alocação | Fence de paginação |
   |---|---|---|---|
   | 258 | estresse, pid 4384 | `hAlloc=0x40000800` | 7008 |
   | 259 | estresse, pid 10996 | `hAlloc=0x40000800` | 7008 |
   | 263 | T3, pid 5040 | `hAlloc=0x40000800` | 7008 |
   | 260 | estresse, pid 1940 | 7ª alocação | 7014 |
   | 261 | estresse, pid 2520 | 7ª alocação | 7014 |

   A numeração da fence é por processo, então é a mesma etapa: as primeiras alocações residentes de um processo que acabou de abrir. Agora com uma carga pequena (32 MB, dentro do orçamento) e não com a de 500 MB.

2. **Pressão de VRAM em cada processo novo.** Os filhos registraram `local uso` de 40 a 64 MB para 32 MB de texturas: há memória em dobro, a alocação WDDM mais a imagem da RADV (T10 do doc 691). O `orcamento` caiu para 8–23 MB depois das alocações. Cada processo novo força despejo de alocações do DWM e do Explorer.

3. **A trava não vem do volume de churn.** Os ciclos 1 a 21 fizeram o mesmo trabalho sem travar.

**Hipótese (H, não provada):** a janela de risco é a primeira residência de um processo novo quando ela força despejo de alocações de processos que estão desenhando (DWM). O mecanismo continua desconhecido. A última drenagem do log do KMD é vizinhança, não causa (doc 582).

## 5. Correção feita antes desta corrida: o preflight do boot frio

O preflight recusava sempre a primeira tentativa por "reset a quente" (bit 19 do `RESET_REASON`).

A mineração mostrou que, em **71 de 71 boots** (06 a 10/10), a primeira leitura da CMOS veio com o bit 19 ligado, inclusive depois de "Desligar". As 45 leituras seguintes nos mesmos boots vieram sem ele, porque a leitura limpa o bit. O "0x40000400 depois de boot frio" do doc 625 era a **segunda** leitura.

`tools/prepare-corrida263-t2-linha-de-base-processo-novo.ps1` passou a conferir o boot frio pelo log do Windows:
- o último desligamento pedido antes do boot (evento 1074) tem de ser "Desligar";
- o boot atual não pode ter registrado um desligamento sujo (evento 41).

A regra foi testada nos 8 boots recentes: acertou os 4 frios e recusou os 4 que vieram de trava. O `RESET_REASON` fica só como registro.

## 6. Recuperação e pendências

- **Estado:** a BC-250 está em **Código 43** pelo fusível (TRIPPED, 2 boots, `0xC0000001`), e o BasicDisplay está em Código 0. O rollback do watchdog não rodou (trava dura).
- **Antes do próximo bind:** `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset` e boot frio.
- **NÃO VALIDADO EM HARDWARE:** o mecanismo da trava e o efeito isolado da T2. A T2 também não mostrou regressão visível nem fault.
- **Próximo:**
  1. **263b**: mesmo pacote e mesma carga, para fechar a taxa com n=2 (teto de 3, doc 692).
  2. **T5 (mineração, offline)**: a sequência do KMD no processo 5040 e nos filhos que não travaram (despejos de quais processos, FLUSH_TLB de quais VMIDs, alocações da RADV) entre 20:44:42.5 e 20:44:43.0, com cuidado com a amostragem do log.
