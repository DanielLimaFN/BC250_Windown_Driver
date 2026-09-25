# Corrida 110 (DX.66): travamento, sobreposicao do segmento e migalha recusada

> **Data:** 24/09/2026; bind 20:59:51, lab 3.270.1.0. **Evidencia:** `docs/evidence/2026-09-24_20-59-51-DESKTOP-HQAG6T4-lab-binding/`.
> **Criterio previo:** doc 419, secao 4. **Resultado:** E (fora do mapa): a pagina de diagnostico nao armou. A recuperacao `RECOVERY_OK_ONLINE` ocorreu no boot seguinte.

## 1. Houve avanco?

**Funcional: nao. Diagnostico: sim.** Houve novo travamento duro antes de 600 s, como na 109. O rastro confirmou que a pagina de migalhas escolhida pela DX.66 estava dentro de uma reserva interna de 4 MiB; o KMD recusou o arme. A analise do mapa encontrou 3 MiB de sobreposicao entre o segmento WDDM e essa reserva. A causalidade da sobreposicao para o travamento permanece **H**.

## 2. Sucessos e medidas

- **P0:** harness 60/60 `Present` aceitos, zero falhos, exit 0; tres selftests RADV com `prova_5de5=True`. Sao provas do escopo do harness, nao de 600 s de desktop nem de pixel no monitor.
- **P0:** rastro DX.65 com 3.547 registros v2 validos, zero invalidos. O boot seguinte comeca no indice 3.530, identificado pela queda de `writer_tick` 3.530 para 1.
- **P0:** ultimo registro do boot da corrida: `writer_tick=3530`, `latest_sequence=1563676`, `active_count=1`, `FLUSH_TLB` estagio 5/operacao 12, sem batimento posterior a ultima paginacao. Com o boot registrado em 20:57:28,5 e `snapshot_time_100ns=5052459932`, o ultimo rastro e **aproximadamente 21:05:53,7**, cerca de 6 min 3 s apos o bind. Isso delimita o ultimo registro duravel, nao o instante exato da falha.
- **P0:** segmento publicado `[0x00A8C000, 0x0FE00000)`; regiao TMR do alocador bootstrap `[0x0FB00000, 0x0FF00000)`. A intersecao mede 3 MiB. Tabelas raiz do VidMm foram localizadas em 0x0FD85000, 0x0FDCA000 e 0x0FDD7000, dentro da reserva.

## 3. Falhas

- **P0:** travamento duro antes do prazo de 600 s; nao ha prova de TDR ou BugCheck nesta evidencia. A maquina precisou de novo boot para a recuperacao.
- **P0:** `[DX66_MIGALHA] ARME RECUSADO`: a pagina fixa antiga 0x0FE80000 sobrepoe a regiao de papel TMR. As linhas `ANTERIOR AUSENTE` com magic variavel nao testam retencao de VRAM: a pagina nunca esteve armada nessa corrida.
- **P0:** o limite de segmento 0x0FE00000 nao excluia todas as regioes internas; o VidMm recebeu espaco em uso pelo alocador do driver.
- **H:** essa sobreposicao pode explicar o travamento, mas nao foi isolada por uma corrida corrigida. Tambem nao esta provado que a regiao de papel TMR corresponde ao TMR vivo do PSP.

## 4. Veredito

**Falha frente ao objetivo de 600 s. Desfecho E frente ao diagnostico DX.66.** `Present` 60/60 e exit 0 do harness nao alteram esse veredito. A localizacao do acesso MMIO continua sem prova porque as migalhas nao foram armadas.

## 5. Recuperacao e pendencias

O guardiao registrou `RECOVERY_OK_ONLINE` as 21:11:55, com cinco amostras consecutivas BasicDisplay Codigo 0, **no boot novo** (`boot_ticks` diferente do armado). Portanto, nao houve R1. O marcador `CLEAN_BOOT_REQUIRED` permanece; antes de novo bind, desligamento completo com corte de energia. Desktop sustentado 600 s, causa do travamento, pixels do destino, velocidade e jogo continuam **NAO VALIDADOS EM HARDWARE**.

**Proximo passo:** DX.67 exclui regioes internas do segmento e reserva uma pagina propria para as migalhas antes do bootstrap. Corrida 111, criterio no doc 422. Manter UMA da BIOS em 512 MB e Radeon Software desligada para preservar as variaveis de controle.

## Provas principais

- `display-log-this-bind-RECUPERADO.txt`: 53.081.180 bytes, SHA-256 `2E34F78A28E60AE5E0C862768836FD57CE1461ECEE1BBEDD6A755CD4317F4F26`.
- `paging-trace-this-bind.bin`: 1.957.944 bytes, SHA-256 `591D70BD8D14475AC41192009FD73AA0B0A6563512A11E3831BEAD9DEF75C81B`.
- `safe-bind-final.json`: cinco amostras BasicDisplay/0 no boot seguinte.

| Item | Estado |
|---|---|
| Janela D3D11 e desktop 180 s (corrida 107) | ✅ |
| Desktop 600 s (corrida 110) | ❌ |
| Sobreposicao segmento/TMR identificada | ✅ P0 |
| Sobreposicao como causa do travamento | 🟡 H |
| Migalhas armadas e lidas apos reset | ❌ |
| Recuperacao no mesmo boot apos travamento duro | ❌ |
| Corrida 111 com segmento isolado | 🔜 |
