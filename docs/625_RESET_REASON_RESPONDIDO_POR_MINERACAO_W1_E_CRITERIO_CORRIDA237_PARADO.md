# 625 — RESET_REASON respondido por mineração, roteiro do W1 e critério da Corrida 237 (parado no desktop)

**Data:** 06/10/2026, offline, zero corridas. Continua o [doc 624](624_PASSAGEM_DE_BASTAO_2026-10-06_TRAVA_DURA_DEPOIS_DA_PARIDADE_P4.md) §6–§7.
**Estado da máquina:** recuperação feita pelo Daniel (BasicDisplay ativo, serviço `bc250mcdm` ausente), commit `eeb3607` feito.

---

## 1. Hipótese 1 do doc 624 (RESET_REASON de controle): respondida sem ensaio

Todas as leituras do `S5_RESET_STATUS` que já estavam em disco (112: `docs/evidence/vram-cmos/*.txt` e `*/reset-reason-pos-trava.txt`):

| Valor lido | Releitura depois da limpeza W1C | Quantas | Quando |
|---|---|---:|---|
| `0x40080400` | `0x40000400` | 89 | depois de reset a quente: 24 de 25 coletas pós-trava, mais leituras depois de reboot e tela azul (227, doc 606) |
| `0x40000400` | `0x40000400` | 23 | leituras logo depois de boot frio (antes do bind) e uma coleta pós-trava |

**Conclusões (P0 para os fatos, P2 para a leitura):**
- **Os bits 30 ("parity error") e 10 são fixos:** estão nas 112 leituras, inclusive depois de boot frio, e não limpam com a escrita W1C que o Linux faz. Não carregam informação sobre a trava.
- **O bit 19 (`CF9=0x6`) marca reset a quente de qualquer origem.** O botão de reset depois da trava dá o mesmo valor que um reboot ou uma tela azul.
- **Nenhuma coleta pós-trava tem os bits 27 ("data fabric sync flood") ou 31 ("software sync flood").** Se o FCH registra sync flood através do reset por botão (não verificado), a trava não é um sync flood registrado.
- A leitura de controle depois de um reboot limpo **não é mais necessária**. A hipótese 1 sai da lista.

Colateral: `ClockSpeed=1750` e os tempos de GDDR6 da APCB são os mesmos nas 112 leituras (sem mudança de configuração de memória entre corridas).

## 2. Taxa de trava (base dos critérios abaixo)

Das 24 corridas com 256 MB (doc 624 §1): 20 travas em 4.262 s, mais 4 corridas acima de 600 s. Exposição ≈ 6.662 s → **uma trava a cada ~333 s** (estimador de taxa constante, H). Probabilidade de 600 s sem trava por acaso ≈ e^(−600/333) ≈ **16,5%** (observado: 4/24 = 16,7%). Duas seguidas por acaso: **~2,7%**.

## 3. W1 — a plataforma trava sem o nosso driver?

**Roteiro:** `tools\EXECUTAR_W1_PLATAFORMA_SEM_DRIVER.cmd` (elevado não é necessário). Boot frio com o BasicDisplay ativo, não usar a máquina por 45 min. O script recusa rodar se o adaptador não for o BasicDisplay ou se o `bc250mcdm` estiver em execução, e arquiva em `docs/evidence/<data>-W1-plataforma/` (manifesto, adaptadores, batimento a cada 2 s com `FlushFileBuffers`).

**Escopo:** CPU e controlador de memória (a mesma GDDR6 da GPU) sob carga contínua de todas as threads, 8 GB. **Não** exercita a GPU.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | `FIM veredito=SEM_ERRO_DE_DADOS`, exit 0, 45 min | A plataforma aguenta CPU+memória no máximo sem o nosso driver. A trava precisa da GPU trabalhando sob o nosso driver (somado à L-B no Linux) |
| B | Batimento para no meio (máquina congela), sem `FIM` | Trava de plataforma sem a GPU: fonte, calor ou memória. Muda toda a busca; repetir uma vez para confirmar |
| C | `ERRO DE DADOS` ou exit 3 | Memória corrompe sem o driver: problema de hardware/configuração de memória |
| D | Exit 2/4/5/6 (preparação) | Defeito do script ou de memória livre; corrigir e repetir, não conta |
| E | Algo fora do mapa | Descrever o observado antes de julgar |

### 3.1 Resultado do W1 (06/10 16:32–17:17): desfecho A

Evidência: `docs/evidence/2026-10-06_16-32-37-DESKTOP-HQAG6T4-W1-plataforma/` (boot às 16:31, BasicDisplay ativo, exe `6F4D3F3D…`).
- **45 min sem trava e sem erro de dados (P0):** 61.523 voltas de 16 threads sobre 8 GB, `erros=0` nos 1.321 batimentos, `FIM veredito=SEM_ERRO_DE_DADOS`, exit 0. Vazão média ~43,8 GB/s (escrita+leitura).
- **Dois buracos no batimento** (t=1690→1694 s e **t=2207→2226 s, ~19 s**). Não são pausa da máquina: no buraco de 19 s as threads de trabalho avançaram 417 voltas (≈22/s, o ritmo normal). Atrasou só a thread do batimento (`Sleep` + gravação com `FlushFileBuffers`), disputando CPU com 16 threads em 16 processadores lógicos. Nenhum evento de Sistema entre 17:00:30 e 17:10. O pico de "391,8 GB/s" é o mesmo buraco (bytes acumulados divididos por 2 s).
- **Implicação:** CPU e controlador de GDDR6 aguentam carga máxima por 45 min sem o nosso driver. Somado à L-B (Linux, GPU paginando, sem trava), a trava precisa da GPU trabalhando sob o nosso driver. **Escopo:** o W1 não exercita a GPU; não exclui um defeito de plataforma que só a GPU dispara.

## 4. Corrida 237 — 600 s parado no desktop

**Pergunta:** a trava dura precisa de uso (menu, Pesquisa, apps) ou vem só com o tempo ligado?

**Candidato:** o **mesmo pacote da 236**, lab **3.384.1.0**, sem nenhum byte mudado (SYS `DA42466A…`, INF `5C339014…`, CAT `3D4FF2FB…`, UMD `8782A5E5…`, RADV `C6BEC54B…`). As mudanças offline de 06/10 (mensagem do `[P4_SDMA1]` e contadores de descarte) **não** entram: só mudam log, e ficam para o próximo candidato com mudança de comportamento (3.385.x). **Única variável contra a 236: a carga.**

**Roteiro:** `tools\EXECUTAR_CORRIDA237_PARADO_600S.cmd`, elevado, depois de boot frio. Anotar a hora do desktop e **não tocar em mouse nem teclado** até o rollback. O preflight (`prepare-corrida237-parado-600s.ps1`) exige os hashes da 236, CMOS em 256 MB aplicados e **monitor, suspensão, hibernação e proteção de tela que não disparem em 15 min** (medido hoje: tudo em "nunca", plano Alto desempenho).

**Pré-condição de validade:** zero toque depois do desktop, relatado pelo Daniel. A 229 (600 s sem abrir o menu, 7.718 IBs) **não** foi parada: teve uso leve. Ela fica como indício, não como amostra desta pergunta.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | 600 s sem trava, rollback do watchdog (`RECOVERY_OK_ONLINE` ou equivalente), Daniel não tocou | Indício de que a trava precisa de uso (16,5% de chance por acaso). **Repetir parado (Corrida 238)**: duas seguidas levam o acaso a ~2,7%, e então a busca vira bissecção por carga (menu, Pesquisa, tipo de alocação, rajadas de paginação) |
| B | Trava dura com a assinatura de sempre (GPU ociosa no último tique, sem BugCheck), Daniel não tocou | **O uso não é necessário.** A trava vem com o tempo ligado e a carga de fundo do DWM. Próximo: W1 (se ainda não rodou) e a corrida "máquina limpa" (rastro, logs da UMD e coleta no mínimo) |
| C | Daniel precisou tocar, ou algo abriu sozinho (atualização, notificação com janela) | Inválida para a pergunta; anotar o que foi e repetir |
| D | Falha de outra classe: Código 43 no início, TDR, tela azul | Analisar como defeito próprio; não conta para a pergunta |
| E | Algo fora do mapa | Descrever antes de julgar |

**Medidas que valem nos dois desfechos:** IBs, Presents e `[PT_GPU_SPLIT]` por minuto contra a 236 (quanto a carga caiu de fato), tempo pelo `snapshot_time_100ns` do rastro (doc 624 §5.5), e a hora da trava contra a hora do desktop.

**Teto e abandono (Lei 9):** no máximo 2 corridas paradas (237 e 238). Se a 237 der B, a 238 não roda.

**Ordem proposta:** W1 primeiro (não instala driver) → boot frio → Corrida 237.

**NÃO VALIDADO EM HARDWARE:** W1, Corrida 237, a causa da trava.
