# 594 — P1: silêncio da BAR, coleta automática no boot, e critério da Corrida 219

**Data:** 05/10/2026 · **Candidato:** lab **3.367.1.0** · **Base:** [doc 593](593_CORRIDA218_UC1_FUNCIONOU_D2_DE_PE_TRAVA_DURA_IGUAL_E_INVENTARIO_DE_PARIDADE_COM_O_AMDGPU.md) §4 (paridade com o amdgpu, item P1)
Trabalho offline. **NÃO VALIDADO EM HARDWARE.**

## 1. O que a P1 tira (o amdgpu não faz nada disso em regime)

Levantamento por leitura de código. Cada item abaixo é acesso da CPU à VRAM ou ao MMIO feito só para instrumentação.

| Fonte | Antes | Volume | Na P1 |
|---|---|---|---|
| **Migalhas DX.66** (`bc250_mmio_breadcrumb.h`) | 4 escritas UC na página de VRAM `0xFA00000` a **cada acesso a registrador**, inclusive o polling do RPTR do SDMA, em todas as CPUs | o maior: milhares de escritas por segundo | a página **não é armada**; o espelho em RAM (`g_Bc250CrumbMirror`) continua indo para o rastro em disco a cada 100 ms (`BCR1`) |
| **Eventos EVT1** (mesma página) | 12 escritas UC na VRAM por submissão ou retirada | um por evento | espelho novo em RAM (`g_Bc250CrumbEvtMirror`); o rastro grava os 16 eventos mais recentes por tique como registros **`BEV1`** |
| **Amostragem de VRAM do Present** (`Bc250DisplaySampleSurface`) | 256 a 1.024 dwords lidos da VRAM pela BAR, 2 a 3 vezes por Present | por quadro | devolve FALSE sem ler, só na imagem do driver. Os chamadores já tratam isso (`SAMPLE_UNAVAILABLE`): a cópia do Present continua |
| **BYTE_PROOF integral** | ~11 MB lidos da VRAM pela BAR nos Presents 1, 5, 10, 25 e 45 | 5 rajadas | `BYTE_PROOF SKIPPED (P1 silencio da BAR)`; a prova fica só na engine (RPTR==WPTR e fence) |
| **Sondas do rastro** (DX.90 GFX, I.PLAT, I.SDMA0, I.CG) | leituras de registrador a cada 100 ms (CG a cada 10 s) | 10 por segundo | desligadas no ramo da P1; os contadores de software (`BPG1`) ficam |

**O que fica, por ser funcional e não instrumentação:**
- o polling de 1 ms do RPTR (é a P3, interrupção);
- o flush de HDP nos laços de espera;
- o anel e as fences do GFX na VRAM;
- a cópia de 11 MB do Present para o scanout;
- a leitura do contador de quadros do OTG no vsync.

Esses itens entram no inventário do doc 593 como próximos (P1b: HDP no polling com a D2 ativa; P7: anel do GFX em GTT).

**Efeitos colaterais declarados:**
- os atalhos do Present que dependem de amostra deixam de agir (DX.33, que não apaga o scanout numa troca de modo com a primária vazia, e DX.32, que não repete uma cópia redundante). Pode haver um piscar a mais na troca de modo, e cópias repetidas;
- o rastro perde os batimentos de registrador do GPU. Na trava, a referência passa a ser o último `BCR1`/`BEV1` e o `writer_tick`.

**Provas offline:**
- `build-contract` EXIT=0;
- build de laboratório "TODOS OS TESTES APROVADOS (0 falhas)";
- auditorias de segurança e do pacote: PASS;
- portão novo `audit_p1_silencio_da_bar` com **6 mutações reprovadas**: chave em 0, página armada, amostragem lendo a BAR, BYTE_PROOF integral, sonda no ramo da P1, ausência de `BEV1`;
- o decodificador lê `BEV1` (`gpu_events_ram`), testado com registro sintético no layout de 72 bytes, inclusive tirando duplicatas entre tiques;
- as strings novas estão no SYS assinado.

| Arquivo | SHA-256 |
|---|---|
| SYS | `B47B6170CB0C8400044AA915C13A0E6EA15A984ECC1B349D4F19C7E5EEEDD52B` |
| INF | `638D0636B1F5ED3CCC3E53614EB72A53587B2140A52E65A2541CDA3ED38F299F` |
| CAT | `3073A5ED17FC6D5AFFFC680324F2335B28EE6BA03FC18EDEC756DFACAE7BCDED` |
| UMD / RADV | inalteradas (`8782A5E5…` / `80EBB248…`) |

**Scripts:** `tools/prepare-corrida219-p1-silencio-da-bar.ps1` e `tools/EXECUTAR_CORRIDA219_P1_SILENCIO_DA_BAR_600S.cmd`.

## 2. Coleta automática no boot (pedido do Daniel)

- **`tools/coleta-automatica-no-boot.ps1`:** roda como SYSTEM a cada boot. Espera 90 s para o driver despejar as migalhas no log. Se o boot anterior terminou **sujo** (Kernel-Power 41, EventLog 6008 ou BugCheck 1001 depois do início deste boot) e a última pasta `*-lab-binding` ainda não foi coletada, roda o `coletar-pos-trava.ps1` nela e grava `coleta-automatica.json`. Só lê e copia.
- **Quando pula:** boot limpo; pasta já coletada (automática ou manual, com `reset-reason-pos-trava.txt`); pasta deste boot; pasta de mais de 12 h antes.
- **Transcrição de cada boot:** `C:\BC250_COLETA_AUTOMATICA.log`.
- **`tools/registrar-coleta-automatica.ps1`:** rodar **uma vez, elevado**. Registra a tarefa "BC250 coleta pos-trava no boot" (SYSTEM, ao iniciar, nível mais alto) e grava onde está o python para o SYSTEM. `-Remover` desfaz.
- **Verificação offline do detector:** neste boot (o da trava da 218), os eventos 41 (10:11:22) e 6008 (10:11:26) estão presentes. É exatamente o sinal que o script usa.
- **Limite:** o que estava só na RAM da máquina no instante da trava não volta. O que a coleta salva é tudo o que já estava em disco ou no CMOS: o motivo do reset, os logs da UMD em `Windows\Temp`, a cauda do log do KMD e do rastro, e o dump, se houver. Por isso a P1 grava migalhas e eventos no rastro em disco a cada 100 ms.

## 3. Critério da Corrida 219 (Lei 10)

**Pré-condição de validade:**
- `[DX66_MIGALHA] NAO ARMADA (P1 ...` no log;
- `BYTE_PROOF SKIPPED (P1` nos Presents 1, 5, …;
- zero linhas `GFX_TRIPE_VEREDITO` vindas do batimento: as do retire continuam, ver a nota;
- `[D2_ANEL_SISTEMA] ATIVO` e as duas linhas do MEC na GART, para manter a base da 218;
- `gpu_events_ram` não nulo no rastro decodificado.

> Nota: `GFX_TRIPE_VEREDITO` também sai da retirada do GFX, que é funcional. O que importa é a ausência de `gpu_heartbeat` com registros no tique, o que se confere pelo decodificador.

Desfechos:

- **A — 600 s sem trava dura.**
  - *Implicação:* o acesso de instrumentação à VRAM/MMIO era causa (ou parte dela). Próximo: devolver um item por corrida, começando pela página de migalhas, até achar o culpado, e só então decidir o que fica.
- **B — trava dura com a mesma assinatura.**
  - *Implicação:* a instrumentação sai das causas suficientes. Seguir no inventário: P1b (HDP no polling com a D2), P2 (doorbell do WPTR do SDMA0), P3 (interrupção).
- **C — regressão funcional sem trava** (Present falhando, tela que não atualiza, TDR novo).
  - *Implicação:* algum caminho dependia da amostragem. Corrigir esse ponto e repetir.
- **D — o StartDevice ou a D2 falham.**
  - *Implicação:* defeito de montagem desta faixa; o log diz onde.
- **E — fora do mapa:** descrever antes de julgar.

**Condição do rollback**, se a janela chegar ao fim: fecha no mesmo boot, sem o laço, com o log parando de crescer e `RECOVERY_OK_ONLINE`. É o primeiro teste da LC.1.

## 4. Pendências

- **Daniel:**
  1. registrar a coleta automática (uma vez, elevado): `tools\registrar-coleta-automatica.ps1`;
  2. recuperação, reset do fusível e boot frio;
  3. rodar `tools\EXECUTAR_CORRIDA219_P1_SILENCIO_DA_BAR_600S.cmd`.
- **NÃO VALIDADO EM HARDWARE:**
  - a P1;
  - a coleta automática disparando de verdade num boot depois de trava;
  - a LC.1 no teardown;
  - 600 s sem trava com 256 MB.
