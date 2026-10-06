# 584 — Corrida 213: Código 43 no bind, sem uma linha de log do KMD (log de 5 GB fragmentado)

**Data:** 04/10/2026, bind às 20:53:34 · **Candidato:** lab **3.361.1.0** (D2) · **Critério:** [doc 583](583_D2_ANEL_E_WORK_DO_SDMA0_NA_RAM_PELA_ABERTURA_AGP_E_CRITERIO_CORRIDA213.md) §4
**Evidência:** `docs/evidence/2026-10-04_20-53-34-DESKTOP-HQAG6T4-lab-binding/` (sondas do registro, baseline do log) e `C:\BC250_PAGING_TRACE.bin` a partir de 386.475.880.

## 1. Medido (P0)

- **Bind com Código 43.** A sonda do registro diz `V5StartDeviceStatus=0xC0000483` (`STATUS_DEVICE_HARDWARE_ERROR`); hash instalado igual ao do pacote. Sem tela azul, sem dump.
- **O KMD não escreveu nenhuma linha** em `C:\BC250_DISPLAY_LOG.txt`, nem a primeira do DriverEntry (`[DX56_PAGING_TRACE] START`).
  - A última escrita do arquivo é das 16:11:57, ainda da Corrida 212.
  - O driver rodou, e outros arquivos dele foram gravados às 20:53:45: o rastro durável (49.344 bytes novos, ~24 tiques de 100 ms) e o fusível.
- **O log está com 5.001.646.961 bytes em 293.583 fragmentos** de 1–2 clusters (`fsutil file queryextents`). O driver abre, acrescenta uma linha, faz flush e fecha, a cada linha, há semanas.
- **Sondas:** faltam todas as que o StartDevice grava depois do ponto da D2 no fonte, entre elas `GcvmBacking*` (linha 10351) e `PspLoadFw*` (linha 10499). A D2 é chamada nas linhas 8798–8840.
  - Isso põe a falha **em algum ponto antes da linha 10351**. As sondas não dizem onde.
- **Migalhas de MMIO (rastro):** as duas últimas são `SDMA0_STATUS` lido (seq. 272) e `SDMA0_RB_CNTL` escrito (seq. 274). São compatíveis com **dois** caminhos:
  1. a D2 ligou, o StartDevice falhou depois, e o teardown liberou a RAM (minha função de liberação lê `STATUS` por último, e o teardown escreve `RB_CNTL`);
  2. o NOP de prova na RAM estourou o tempo (o diagnóstico de `SYNC_NOP TIMEOUT` lê `STATUS` por último), o rollback para a VRAM também falhou, e a D2 devolveu `STATUS_DEVICE_HARDWARE_ERROR`.

## 2. Veredito

**Inconclusivo, entre C e D do doc 583. Nenhum desfecho pode ser atribuído sem o log.**
- O código `0xC0000483` sai tanto da recusa da MMU global e do rollback da D2 quanto de várias etapas posteriores (GFX online, CSB, preâmbulo).
- A ausência do log é um **defeito de host separado**.
  - **H forte:** o NTFS recusa novos appends num arquivo com fragmentação extrema (`STATUS_FILE_SYSTEM_LIMITATION`).
  - As ACLs do log e do rastro são iguais, e há 87 GB livres no C:.
  - Não foi testado elevado: **NÃO PROVADO**.

## 3. Recuperação e próximo passo

1. Deixar o watchdog fazer o rollback, ou rodar `tools\BC250_RECOVERY.cmd` elevado.
2. **Elevado:** testar o append no log velho (se falhar com "limitação do sistema de arquivos", a hipótese fica P0) e renomeá-lo.
   - O script de recuperação de cauda trata offset maior que o arquivo como "nada faltando". As pastas antigas já têm as caudas recuperadas.
3. Boot frio e **o mesmo pacote 3.361.1.0**, sem rebuild. Com log novo, a linha `[D2_ANEL_SISTEMA]` diz qual dos dois caminhos aconteceu.
   - O caminho de falha da D2 é fechado: Código 43, sem tela azul.

> **CORREÇÃO (doc 585):** a fragmentação NÃO é a causa. O log some porque DriverEntry/AddDevice/StartDevice rodam com APCs desligadas e todas as linhas vão para um anel em RAM que só é drenado depois de um StartDevice bem-sucedido. A falha da 213 é a D2: dois `SYNC_NOP TIMEOUT` (anel na RAM e rollback na VRAM), reconstruídos das migalhas.

## 4. Complemento (04/10 21:04)

- **O append elevado no log velho FUNCIONOU.** O arquivo passou de 5.001.646.961 para 5.001.646.975 bytes (+14, o `teste-append`). Isso **enfraquece** a hipótese da fragmentação: um append em modo usuário, elevado, no fim do arquivo passou.
- Mesmo assim, a causa de o KMD não ter escrito continua desconhecida. O driver usa `ZwCreateFile` com `FILE_APPEND_DATA` + `FILE_SYNCHRONOUS_IO_NONALERT`, sob SYSTEM, e o caminho não foi testado nesse modo.
- O log foi renomeado para `C:\BC250_DISPLAY_LOG_ate_2026-10-04.txt`. O driver cria um arquivo novo.
- A placa voltou ao BasicDisplay (OK).
- Se a próxima corrida também ficar sem log, o defeito está no caminho de escrita do driver, e não no arquivo.

**NÃO VALIDADO EM HARDWARE:** a D2 (nem ativação nem recusa foram observadas no log) e 600 s sem trava com 256 MB.
