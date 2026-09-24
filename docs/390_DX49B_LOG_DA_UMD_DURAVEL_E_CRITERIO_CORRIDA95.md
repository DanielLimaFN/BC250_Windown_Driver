# DX.49b — log da UMD durável; critério da Corrida 95

> 23/09/2026. Faixa **offline**, sem bind.
> **Base:** [Corrida 94](389_CORRIDA94_DX49_RESULTADO_TRAVAMENTO_E_LOG_DA_UMD_PERDIDO.md),
> que teve travamento duro, tela preta e **nenhum** log da UMD sobrevivendo.
> **Teto:** uma corrida, depois de desligamento completo.

## 1. A mudança (uma variável)

**`bc250_wddm_winsys.c`:**
- o handle do trace continua aberto uma vez por processo, agora **com**
  `FILE_FLAG_WRITE_THROUGH`;
- por linha, só `WriteFile`, e cada linha vai ao disco;
- os metadados (tamanho do arquivo) são descarregados na abertura e depois no
  máximo a cada 250 ms (`Bc250WinsysTraceFlushMetadataIfDue`), fora do caminho
  de cada linha.

**Comparação com a 93 e com a 94:**

| | Por linha | Travamento duro apaga o log? |
|---|---|---|
| **93** | `CreateFile` + `WriteFile` + `FlushFileBuffers` + `CloseHandle` | Não |
| **94** | Só `WriteFile` | **Sim** |
| **95** | Só `WriteFile`, write-through | Não deveria (é o que a corrida testa) |

**O resto da DX.49 fica igual:**
- lote de draws;
- staging persistente;
- dirty rects.

## 2. Verificação offline

- **Build:** `build-umd` EXIT=0 com todas as suítes, incluindo
  `umd_dx49_batch_test`. Os arquivos `BC250_UMD_LOG_*` foram gravados pelas
  suítes offline.
- **Portão DX.49, parte 3, reescrito:**
  - **exige:** write-through no handle, descarga periódica com teto de tempo,
    e que ela seja chamada;
  - **continua proibindo:** `CreateFile`/`FlushFileBuffers`/`CloseHandle`
    dentro de `Bc250WinsysTraceRaw`;
  - **mutações:** **19/19 reprovam**, com três novas da DX.49b.
- **Auditoria de segurança:** PASS.
- **SYS recompilado:** a suíte de mutações regravou `bc250_display_ddi.c` com
  conteúdo idêntico, e a guarda de frescor do empacotador exigiu rebuild.
  - **Fonte do KMD:** a mesma da 94.
  - **Hash:** o SYS sai diferente porque o binário carrega carimbo de build.

## 3. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.255.1.0` SYS `C8B469FC41C5F871265F51F23CA5EE7C280B94241FC6A3A253F9989859755EA6`, INF `1AF7EDD4…124A`, CAT `83FDA735…90C1`. Auditoria do pacote PASS |
| UMD | `725279BA4133A22BC751505BC8273A9CD670B3F9E2A6C736B34D6DE7912163F0` |
| RADV | inalterada (DX.42 `FCAA11FE…`) |
| Pré-voo | `tools/prepare-corrida95-dx49b.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA95_DX49B.cmd`, elevado, janela de 300 s, depois de **desligamento completo** (marcador do rollback de 22:17:27) |

## 4. Critério, escrito antes

**Controle:**
- `StartDevice LEAVE status=0x00000000`;
- no log do dwm.exe, `[DX49_BATCH] ANEL … criado`.

| | Assinatura | Implicação |
|---|---|---|
| **A** | Sem travamento, 60/60, zero TDR e presents/min do DWM **≥ 70**, com o Daniel relatando desktop mais fluido | A DX.49 paga a vazão; o travamento da 94 fica como evento isolado. |
| **B** | Sem travamento, estável, presents/min **< 70** | A DX.49 é estável, mas não dobrou. As assinaturas por parte do doc 388 dizem qual parte não pagou. |
| **C** | Travamento **e** logs `BC250_UMD_LOG_*` do dwm.exe presentes em `C:\Windows\Temp` depois do reboot | O travamento reproduz com a DX.49. A última linha da UMD e a do KMD nomeiam o ponto, e a próxima faixa ataca ali. Se reproduzir, reverter a parte 2 (lote) vira o controle. |
| **D** | Travamento **sem** log da UMD de novo | A durabilidade não pagou. O próximo passo é o log com `FlushFileBuffers` por linha, como na 93. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Se travar:** reinicie. Antes de qualquer outra coisa, eu copio os
`C:\Windows\Temp\BC250_UMD_LOG_*` para a pasta de evidência.

**Depois da corrida:** as 5 seções e a tabela-checklist atualizada.
