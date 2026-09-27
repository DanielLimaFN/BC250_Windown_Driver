# Corrida 111 (DX.67): segmento limpo, travamento duro e migalhas B

> **Corrida:** 24/09/2026, bind 21:50:58, lab 3.271.1.0, SYS `2B34CEEDAF0510CC82B13AF6A0E658ED2F1A2684DC2D3AB94B1F04C3E4D6EB31`.
> **Evidencia:** `docs/evidence/2026-09-24_21-50-58-DESKTOP-HQAG6T4-lab-binding/`.
> **Criterio previo:** doc 422. **Desfecho:** B, controles completos e travamento duro antes de 600 s. Migalhas: B do doc 419 (todos os slots recentes em DEPOIS).

## 1. Houve avanco?

**Funcional: nao. Diagnostico: sim.** O corte DX.67 foi exercitado no hardware: o segmento 1 termina em 0x0FA00000, fora das regioes internas. As tabelas raiz observadas ficaram inteiramente abaixo desse teto. Ainda assim, houve o mesmo tipo de travamento duro. A sobreposicao medida na Corrida 110 nao e condicao necessaria para o travamento observado na 111; pode ter sido um defeito adicional, mas nao explica todos os travamentos.

## 2. Sucessos e medidas

- **P0:** `[DX67_VRAM_MAPA] segmento [0xA8C000, 0xFA00000)`, `SegBytes=0xEF74000`. A pagina de migalhas foi reservada em `[0xFA00000,+0x1000)` e o TMR em `[0xFB00000,+0x400000)`. Nao ha sobreposicao entre o segmento publicado e essas regioes.
- **P0/P1:** 298 `McdmSetRootPageTable` no primeiro boot. O maior fim calculado (`SegBase + Offset + NumEntries*8`) e `0xF9E9000`, abaixo de `0xFA00000`; zero tabela raiz observada cruza o teto. A aritmetica de 8 bytes por entrada e a do contrato de page table.
- **P0:** `[DX66_MIGALHA] ARMADA` em VRAM 0xFA00000, fisico 0xCFA00000; no boot apos o reset, `ANTERIOR PRESENTE`, mesmo nonce, estado `EM_EXECUCAO(sem StopDevice)`. Portanto, esta pagina de VRAM sobreviveu ao reinicio a quente pelo botao da placa.
- **P0:** 12 slots CPU00–CPU11 com fase `DEPOIS(concluido)`. Ultimo global: CPU04 seq. 5.159.142, `SDMA_REG_READ` offset relativo 0xFCC, correspondente a BAR5+0x3FCC (`HDP_MISC_CNTL`). O acesso registrado foi concluido; ele nao localiza o travamento.
- **P0:** rastro DX.65 recuperado com 3.650 registros v2 validos, zero invalidos. O boot novo comeca no indice 3.630. O ultimo registro do boot da corrida tem `writer_tick=3630`, `latest_sequence=1838360`, `active_count=1`, `FLUSH_TLB` estagio 5/operacao 12, sem batimento posterior. Boot 21:47:11,5 + `snapshot_time_100ns=6034437418` = **aproximadamente 21:57:14,9**, ~6 min 17 s apos o bind. Este e o ultimo registro duravel, nao necessariamente o instante exato em que a tela parou.
- **P0:** harness D3D11 60/60 `Present` aceitos, zero falhos, exit 0; tres selftests RADV `prova_5de5=True`. Nao provam estabilidade de 600 s.
- **Relato do Daniel:** "uns 6 a 7 minutos antes de travar, sem artefatos"; interpretado como travamento cerca de 6–7 minutos apos o inicio, com desktop antes legivel e sem artefatos.

## 3. Falhas e limites

- **P0:** travamento duro antes dos 600 s. O Daniel reiniciou pelo botao da placa. Nao apareceu `ResetFromTimeout`, `RestartFromTimeout` ou `GFXHUB_FAULT` no log recuperado. Nenhum novo BugCheck foi identificado nesta evidencia.
- **P0:** os slots DX.66 em `DEPOIS` nao identificam o ponto da falha. O ultimo log KMD antes do boot seguinte termina apos `CTX_PROBE` do Node0, sem provar que a funcao parou naquela linha: a cauda do arquivo pode perder dados em cache.
- **Limite da instrumentacao:** a DX.66 nao marca todas as leituras de VRAM por CPU (PTEs, fence/writeback do GFX/MEC/SDMA fora dos helpers cobertos), nem chamadas do dxgkrnl ou outros drivers. A fase DEPOIS enfraquece a hipotese de uma leitura MMIO *instrumentada* pendente no momento do reset, mas nao exclui caminho nao coberto ou perda/ordem de escrita da migalha.
- **H descartada como explicacao suficiente:** a sobreposicao segmento/TMR da 110. **H ainda aberta:** um acesso a VRAM/MMIO fora dos sitios DX.66, deadlock de kernel, ou outro caminho de GPU; nenhum foi localizado.

## 4. Veredito

**Falha** no objetivo de desktop 600 s; **desfecho B** do doc 422. O controle do segmento e das migalhas passou; por isso a observacao do travamento apos a DX.67 e informativa. O subdiagnostico das migalhas e **B** do doc 419: `EM_EXECUCAO` e todos os slots recentes em `DEPOIS`.

## 5. Recuperacao e pendencias

O guardiao confirmou BasicDisplay Codigo 0 em cinco amostras as 22:01:07–11, `RECOVERY_OK_ONLINE` as 22:01:13, **no boot seguinte** (`armed_boot_ticks=639258940315000000`, `boot_ticks=639258948285000000`). Nao houve recuperacao R1 no boot travado. Atualmente BasicDisplay esta em Codigo 0 e `CLEAN_BOOT_REQUIRED` foi rearmado; nao apagar o marcador. Antes de novo bind, exigir desligamento completo com corte de energia.

**Proximo trabalho offline:** ampliar a instrumentacao persistente para leituras de VRAM e estagios de submissao/paginacao ainda cegos, com slots separados para que migalhas internas de alta frequencia nao sobrescrevam a fase de uma operacao externa. Preparar critério antes de qualquer nova corrida; nao usar a linha final de log ou o `FLUSH_TLB` como localizacao por si sós. Desktop 600 s, causa do travamento, pixels do destino, velocidade e jogos continuam **NAO VALIDADOS EM HARDWARE**.

## Provas arquivadas

- `display-log-this-bind-RECUPERADO.txt`: 48.570.268 bytes, SHA-256 `4B720BD00E0800B55CC6BB50EDA87319FFB0999550C8B4683F8EFDF0F5EB726C`. O boot novo comeca na linha 358.840.
- `paging-trace-this-bind-RECUPERADO.bin`: 2.014.800 bytes, SHA-256 `374F99288FDB839CECCAE25BA2B424BE90D0335E7C055F180E0360468715F9C6`.
- `safe-bind-final.json` e `umd-post-recovery-manifest.json`: resultado do guardiao e cinco logs UMD preservados.

| Item | Estado |
|---|---|
| Segmento sem sobreposicao com reservas internas | ✅ P0 |
| Migalhas armadas e recuperadas apos reset a quente | ✅ P0 |
| Leitura MMIO instrumentada pendente localizada | ❌ slots em DEPOIS |
| Desktop 600 s | ❌ travou ~6 min 17 s |
| Recuperacao no mesmo boot | ❌ |
| Instrumentacao dos pontos cegos | 🔜 |
