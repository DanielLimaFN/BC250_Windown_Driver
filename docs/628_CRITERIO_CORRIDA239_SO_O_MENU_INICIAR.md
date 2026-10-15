# 628 — Critério da Corrida 239: só o menu Iniciar

**Escrito antes do ensaio (Lei 10), 07/10/2026, offline.** Continua o [doc 627](627_CORRIDA238_NAO_FOI_PARADA_600S_SEM_TRAVA_COM_USO_LEVE_E_PROXIMO_PASSO.md) §4.

## 1. Pergunta

Abrir e fechar o menu Iniciar, sem mais nada, basta para a trava dura voltar?

**Por que o menu:** a 236 travou aos ~45 s com o menu aberto; a maioria das travas desde a 199 veio ao abrir o menu ou a Pesquisa (doc 624 §1); a 237 (parada) e a 238 (uso leve sem menu) passaram ~600 s. Isso é correlação (H), não causa.

## 2. Candidato e roteiro

- **Mesmo pacote da 236–238**, lab **3.384.1.0**, sem nenhum byte mudado (SYS `DA42466A…`, INF `5C339014…`, CAT `3D4FF2FB…`, UMD `8782A5E5…`, RADV `C6BEC54B…`). Única variável contra a 237: o menu.
- **Antes:** fechar Claude, Edge, Configurações e o Gerenciador de Tarefas e **desligar** (não reiniciar).
- **Script:** `tools\EXECUTAR_CORRIDA239_SO_MENU_600S.cmd`, elevado. O preflight (`prepare-corrida239-so-menu-600s.ps1`) agora **recusa**:
  - reset a quente antes do bind (bit 19 do `RESET_REASON` lido logo antes; doc 625 §1);
  - `Claude`, `msedge`, `msedgewebview2`, `SystemSettings`, `Taskmgr`, `chrome` ou `firefox` abertos.

  Testado contra a leitura de antes da 238: as duas travas teriam barrado aquela corrida.
- **Uso:** 20 s depois do desktop, abrir o menu (clique), esperar desenhar ou 10 s, fechar com Esc, esperar 30 s; repetir até o rollback (~15 ciclos). Não digitar no menu (abre a Pesquisa), não abrir app.

## 3. Desfechos

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | 600 s sem trava, `RECOVERY_OK_ONLINE`; logs de AppContainer mostram o `StartMenuExperienceHost` ativo ao longo da corrida e nenhum outro app | O menu sozinho não basta. Próximo da bissecção: só a Pesquisa (digitar no menu), mesmo pacote |
| B | Trava dura com a assinatura de sempre (GPU ociosa no último tique, sem BugCheck), só com o menu em uso | O menu basta. Próximo: o que o menu faz que o desktop parado não faz (alocações novas de tela cheia, superfícies compartilhadas, rajadas de paginação), por mineração do rastro e dos logs do `StartMenuExperienceHost` antes de nova corrida |
| C | Outro app carregou a UMD, ou o menu não foi exercitado (sem log do `StartMenuExperienceHost` em regime) | Inválida; repetir |
| D | Código 43 no início, TDR, tela azul | Defeito próprio; não conta |
| E | Fora do mapa | Descrever antes de julgar |

**Ressalva estatística (doc 627 §3):** com a taxa de fundo de ~1 trava a cada ~390 s, um 600 s isolado sem trava tem ~21% de chance por acaso. Desfecho A com n=1 é indício, não prova; desfecho B informa mais (uma trava refuta "o menu não basta").

**Medidas nos dois desfechos:** ciclos de menu até o fim; IBs, Presents, `[PT_GPU_SPLIT]` e `ActiveOpenAlloc RECUSADO` por minuto contra a 237 e a 236; se travar, a distância (em segundos, pelo `snapshot_time_100ns`) entre a última abertura do menu e o último tique do rastro.

**Teto:** 2 corridas só com o menu (239 e, se A, uma repetição) antes de passar à Pesquisa.

**NÃO VALIDADO EM HARDWARE:** a causa da trava; que ela dependa do menu.
