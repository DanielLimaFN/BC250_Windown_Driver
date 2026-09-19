# Conferência da análise da corrida 21:20:13 — 09/09/2026

**Parecer:** ausência de backend Draw confirmada; alegações de 0x119 extinto, 100% de estabilidade e recordes físicos desta corrida não são sustentadas pela mineração apresentada.

## Método reproduzível

Pasta: `docs/evidence/2026-09-09_21-20-13-DESKTOP-HQAG6T4-lab-binding/`.

O `display-log-baseline.json` registra start_offset=6776778, capturado às 21:20:13.983. O arquivo `bc250_display_log_full_pre_rollback.txt` tem 7445413 bytes. Conferido SHA-256 contra o arquivo do watchdog: `3A163B5C3CFAF268944C8DED575A64579287C5B17E4C6D5EB4B97A9C6872971E`, coincidente.

O recorte novo tem 668635 bytes e começa na linha 94574. As contagens abaixo são ocorrências de texto, não automaticamente eventos físicos. O log pode limitar/omitir mensagens; ausência de linha não prova ausência de operação.

## A contagem citada é histórica

| Padrão | Arquivo integral | Recorte do bind |
|---|---:|---:|
| CommitVidPn, qualquer menção | 77 | 17 |
| SetVidPnSourceAddress | 62 | 2 |
| VSYNC | 26935 | 8 |
| Prefixo [Present # | 5307 | 6 |
| SDMA=YES | 219 | 0 |
| IB EXECUTADO (INDIRECT) | 779 | 102 |

As 26935 linhas VSYNC são **ControlInterrupt, Enable=0/1**, não contagem de interrupções físicas recebidas. As 6 linhas Present do recorte descrevem o mesmo Present #2; contar cada linha como nova apresentação é incorreto. Há 9 entradas CommitVidPn, com 8 mensagens de transição para VidPn vazio. O arquivo integral contém 41 STARTDEVICE AUTORIZADO.

Não confirmei 218 execuções consecutivas pelo número de mensagens: existem 102 linhas explícitas de execução INDIRECT no recorte. A diferença pode envolver limitação de logs; FenceId final não é contagem de conclusões provadas.

## Contraprovas ignoradas na análise

- Linha 98066 do log integral, dentro do bind: IB_TRIPE_VEREDITO #99: **DADOS_DIVERGEM**, cinco canários, Divergentes=5; RPTR/WPTR e fence coincidem, mas os cinco valores continuam poison. Isso não é um falso mismatch de endereço IB_PA_MATCH: é uma reprovação da perna de dados cuja causa ainda precisa ser investigada.
- Linha 98522: Present #2 **REJECT source contract**, origem SlotSeg=0.
- 56 linhas `[McdmCreateAllocation] Result=0xC000009A` e 8 recusas CreateDevice/CreateProcess no recorte.
- `bc250_umd_log_4824.txt:6102`, e mais três ocorrências: DrawIndexed recusado por ausência de compilador/shader-state/draw PM4. Há também falhas Bc250WinsysAllocate na UMD.
- O recorte não contém BYTE_PROOF. Portanto a linha de frame branco citada do arquivo integral não pode ser atribuída a este bind sem demonstrar outra proveniência.

## O que realmente é favorável

Watchdog registra ROLLED_BACK, exit_code=0, às 21:23:17; seu log confirma BasicDisplay Código 0 restaurado. Isso sustenta recuperação bem-sucedida nesta janela. Há execuções INDIRECT e 47 linhas IB_TRIPE_VEREDITO com TRIPE_PROVADO no recorte. Nenhuma linha TDR_QUIESCE ou TIMEOUT foi encontrada nele.

Não houve análise nova de dumps/eventos do sistema nem ensaio físico nesta conferência. A ausência de um bugcheck num ensaio não extingue uma classe de bugchecks.

**Decisão:** implementar o backend gráfico continua sendo necessário. A corrida também exige investigar alocação, residência/Present e a prova de dados #99. “Puramente ausência do rasterizador” e “kernel completamente dominado” são conclusões excessivas. Desktop acelerado e jogos: **NÃO VALIDADO EM HARDWARE**.

