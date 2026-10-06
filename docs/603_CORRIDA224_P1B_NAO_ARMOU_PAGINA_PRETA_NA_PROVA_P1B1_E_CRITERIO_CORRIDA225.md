# 603 — Corrida 224: a P1b não armou (página preta na prova); P1b.1 e critério da Corrida 225

**Corrida 224:** 05/10/2026 15:14:39 · lab **3.372.1.0** (SYS `C02E845E…`) · critério: [doc 602](602_P1B_CPU_NA_VRAM_PELA_DRAM_SEM_HDP_COMO_O_AMDGPU_EM_APU_E_CRITERIO_CORRIDA224.md) §4
**Evidência:** `docs/evidence/2026-10-05_15-14-39-DESKTOP-HQAG6T4-lab-binding/`, com a coleta automática do boot das 15:18:50.

## 1. O que aconteceu (P0)

**A P1b não armou:**
- `[P1B_VRAM_DRAM] NAO ATIVA: prova de alias falhou (iguais=1024/1024 nao-zero=0, BAR=0xC0000000 DRAM=0x460000000)`;
- a 1ª página do scanout estava **toda zero** (preta) pelos dois caminhos, então a prova não discriminava nada;
- a falha fechada funcionou: o driver ficou pela BAR com HDP (`[HDP1_FLUSH]` e `[HDP_PT_FLUSH]` no log), e a janela do display mapeou `Phys=0xC0000000`.

**Base mantida:** D2 ATIVO, `[P2_DOORBELL] ATIVA`, `[P7_GFX_GTT] ATIVO`, `DrenoOk=1`. Ou seja, a mesma configuração efetiva da 223.

**Trava dura aos ~189 s** (tique 1889), ao abrir a Pesquisa, com a mesma assinatura:
- último evento `RETIRE_SDMA` (fence 6614), tudo retirado;
- 6.614 IBs e 1.724 Presents;
- zero TDR, timeout, fault ou `DEVICE_REMOVED`;
- despejos de 3,8 MB nos 2 s finais;
- Kernel-Power 41, EventLog 6008, `RESET_REASON=0x40080400`.

**Teste de RAM após a trava:** 8.015 MB × 2 passadas, zero erros.

## 2. Veredito: desfecho C

A P1b não foi testada. **O defeito é meu:** escolhi como prova uma página que pode legitimamente ser zero. É mais uma vez a classe "zero contra zero não prova" (doc 266, `COMPOSITE_DATA_PROOF`), que eu mesmo já tinha catalogado.

**Informação parcial (P2):** os 1.024 dwords **iguais** pelos dois caminhos são compatíveis com a mesma memória. Mas zeros iguais também saem de duas memórias diferentes zeradas, então isso não prova nada.

A trava aos ~189 s é mais uma amostra da configuração da 223.

## 3. P1b.1 (candidato 3.373.1.0)

A prova agora **escreve**:
1. salva a última página da janela, `offset BAR − 4 KB` (a dummy da MMU, sem uso nesse ponto do StartDevice);
2. escreve o padrão A (`0xBC250000 ^ k·0x9E3779B1`) pela **DRAM**, faz `mfence` e lê pela **BAR**;
3. faz o mesmo com o padrão B (`~A`);
4. restaura o conteúdo original.

Só arma com 1.024/1.024 nos dois padrões. Isso só acontece se a mesma memória for vista pelos dois caminhos, e o segundo padrão também exclui uma leitura velha presa num cache do HDP.

**Provas offline:**
- `build-contract` EXIT=0;
- laboratório "TODOS OS TESTES APROVADOS (0 falhas)";
- auditoria de segurança PASS e auditoria do pacote PASS;
- string `alias provado por escrita` no SYS assinado.

**Portão:** 3 mutações novas reprovadas (um padrão só, sem restaurar, arma sem prova), somadas às 14 da P1b.

| Arquivo | SHA-256 |
|---|---|
| SYS | `6AB0D683A851F574F6CD81788B59C7FB2B4C7D6FCFCE96DA16CD48A21F7185C6` |
| INF | `82ABC703C92910C54578CA13C28F9FF4A12216A6209F16EC4DA5518B95F4E82E` |
| CAT | `40CA9624E6171803804A2C83643D5D8AEA1CC49E703D9804F5EBC09A09CD1B5B` |

**Scripts:** `tools/prepare-corrida225-p1b1-alias-por-escrita.ps1` e `tools/EXECUTAR_CORRIDA225_P1B1_VRAM_PELA_DRAM_600S.cmd`. O preflight passou inteiro, inclusive a CMOS, que ainda estava dentro dos 15 min.

## 4. Critério da Corrida 225

É o critério do doc 602 §4, sem mudança. A pré-condição passa a ser:
- `[P1B_VRAM_DRAM] ATIVO ... alias provado por escrita (padroes A e B 1024/1024 ...)`;
- zero `[HDP1_FLUSH]` e zero `[HDP_PT_FLUSH]` depois do arme.

Desfechos:
- **A — 600 s:** conta só com repetição.
- **B — trava igual:** a BAR/HDP sai das causas; próximo, P3.
- **C — não arma:** o log traz os acertos dos padrões A/B.
  - *A=0 e B=0:* a DRAM não é a mesma memória.
  - *A=1024 e B<1024:* o HDP segura leitura velha; aí ler pela BAR exige invalidar o HDP.
  - Ou falha de mapeamento.
- **D — corrupção ou artefatos novos.**
- **E — fora do mapa.**

## 5. Pendências

- **Daniel:** recuperação, fusível, boot frio, CMOS `-Ler` a menos de 15 min e `tools\EXECUTAR_CORRIDA225_P1B1_VRAM_PELA_DRAM_600S.cmd`.
- **NÃO VALIDADO EM HARDWARE:**
  - a P1b;
  - 600 s sem trava de forma repetível;
  - composição entre processos.
