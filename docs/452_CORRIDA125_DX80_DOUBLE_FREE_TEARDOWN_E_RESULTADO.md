# Corrida 125 — Candidato DX.80: Cache de Framebuffers Funcional no Silício, Falha por Double Free no Teardown de RTV e Recuperação R1

> **Data:** 25/09/2026 — 15:05:40  
> **Candidato:** DX.80 / `3.284.1.0` (INF fonte `3.284.0.0`)  
> **Pasta de Evidência:** [`docs/evidence/2026-09-25_15-05-40-DESKTOP-HQAG6T4-lab-binding`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-25_15-05-40-DESKTOP-HQAG6T4-lab-binding)  
> **Duração da Sessão:** ~23 s até crash do DWM / rollback seguro (15:05:40 – 15:06:05 DWM; rollback concluído às 15:09:54)  
> **Veredito:** **Parcial / Falha Operacional com Diagnóstico Preciso** (Desfecho D do Critério 451)

---

## 1. Relato Obrigatório das 5 Perguntas

### 1. Houve Avanço?
**Sim (avanço arquitetural com diagnóstico exato de falha no silício).**
O mecanismo de cache de framebuffers e views de RTV foi exercitado no silício e comprovou seu funcionamento: o DWM atingiu 6 **Cache Hits** consecutivos com **zero flushes síncronos** e **zero esperas de fence** durante rotações de backbuffer.
A causa da interrupção precoce foi isolada com precisão absoluta de linha de código: um **Double Free** em `Bc250UmdVkDestroyRtv`, onde os handles de `VkFramebuffer` e `VkImageView` eram destruídos no loop de `FbCache` e, logo em seguida, destruídos novamente pelas variáveis legadas `pRtv->VkFb` e `pRtv->VkView`.

### 2. Sucessos
- **Cache Hits Comprovados no Silício:**
  - O DWM registrou 3 backbuffers em cache (`Count=3`) e 6 acertos consecutivos (`Hits=1..6 Misses=2`), operando a swapchain sem nenhum flush intermediário.
- **Renderização e Apresentação do Harness:**
  - A janela do triângulo Direct3D 11 abriu e executou 16 quadros (`PRESENT_OK`, atingindo 1,8 FPS na janela) antes de ser afetada pela queda do compositor do sistema.
- **Rollback Seguro R1:**
  - O guardião online detectou o erro do harness e realizou o rollback online no mesmo boot, confirmando `BasicDisplay` Código 0 em 5/5 amostras consecutivas (15:09:47–51), sem necessidade de reboot.
- **Hardware Íntegro:**
  - Zero BugChecks (0x119, 0x116, 0x133 ausentes), zero travamentos de GPU ou perda de comunicação com o OTG/SDMA.

### 3. Falhas Observadas
- **Crash do DWM por Double Free:**
  - No arquivo [`bc250_umd_log_1208.txt`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-25_15-05-40-DESKTOP-HQAG6T4-lab-binding/bc250_umd_log_1208.txt), às 15:06:05.831, ao destruir um RTV (`Bc250UmdVkDestroyRtv`), o código executou:
    1. Destruição de `FbCache[0].Fb` (`0x005404E0`).
    2. Logo após o loop, destruição de `pRtv->VkFb` (que continha o mesmo handle `0x005404E0`).
  - O driver Vulkan (RADV) encontrou um handle já liberado, gerando falha de violação de acesso imediata e encerrando o processo `dwm.exe`.
- **Interrupção do Harness de Teste:**
  - No quadro 17, o `bc250-d3d11-conformance.exe` recebeu `DXGI_ERROR_DEVICE_REMOVED` (`0x887A0005`) devido à morte do processo DWM do Windows.
  - O runner interrompeu o teste e acionou a proteção do watchdog.

### 4. Veredito
**Parcial / Falha Operacional com Diagnóstico Preciso.**
O objetivo principal da DX.80 (reutilizar framebuffers na rotação de swapchain sem flush) funcionou plenamente enquanto o RTV esteve ativo. A falha ocorreu no momento da destruição do RTV por conta de uma destruição redundante de handles já contidos no cache.

### 5. Recuperação e Pendências
- **Recuperação:** Concluída com sucesso (`RECOVERY_OK_ONLINE`), máquina segura em `BasicDisplay` Código 0.
- **O que Continua NÃO VALIDADO EM HARDWARE:**
  - Janela completa de 180 s e 600 s sob o cache DX.80 corrigido.
  - Taxa do DWM a 10–20+ FPS sustentados sem double-free.
  - 60 FPS estáveis e jogos Direct3D 12/Vulkan.
- **Próximo Passo Justificado:**
  - Corrigir imediatamente o teardown de `Bc250UmdVkDestroyRtv` anulando `pRtv->VkFb` e `pRtv->VkView` durante o percurso do `FbCache`, garantindo destruição única (`Single-Destroy Guarantee`).
  - Criar o candidato **DX.80b** (`3.285.0.0` / `3.285.1.0`), auditar e rodar a Corrida 126.

---

## 2. Rastreamento e Provas dos Logs

### Prova dos Cache Hits no Silício (Log DWM 1208):
```text
15:05:57.029 [pid=1208 dwm.exe] [VK_RES] RTV_SEGUE_RECURSO: rtv=... Count=2 Hits=0 Misses=1
15:05:57.443 [pid=1208 dwm.exe] [VK_RES] RTV_SEGUE_RECURSO: rtv=... Count=3 Hits=0 Misses=2
15:05:57.771 [pid=1208 dwm.exe] [VK_RES] RTV_SEGUE_RECURSO (CACHE HIT): rtv=... Hits=1 Misses=2
15:05:57.898 [pid=1208 dwm.exe] [VK_RES] RTV_SEGUE_RECURSO (CACHE HIT): rtv=... Hits=2 Misses=2
15:06:04.719 [pid=1208 dwm.exe] [VK_RES] RTV_SEGUE_RECURSO (CACHE HIT): rtv=... Hits=3 Misses=2
15:06:04.828 [pid=1208 dwm.exe] [VK_RES] RTV_SEGUE_RECURSO (CACHE HIT): rtv=... Hits=4 Misses=2
15:06:05.113 [pid=1208 dwm.exe] [VK_RES] RTV_SEGUE_RECURSO (CACHE HIT): rtv=... Hits=5 Misses=2
15:06:05.340 [pid=1208 dwm.exe] [VK_RES] RTV_SEGUE_RECURSO (CACHE HIT): rtv=... Hits=6 Misses=2
```

### Prova da Linha Fatal de Teardown:
```text
15:06:05.831 [pid=1208 dwm.exe] [VK_RES] Bc250UmdVkDestroyRtv: view=0000000005E166B0, passClear=0000000003ACA840, passLoad=0000000003ACAAE0, fb=00000000005404E0 (CachedFbs=1)
```
O loop destruiu `FbCache[0].Fb` (`0x005404E0`) e em seguida tentou destruir `pRtv->VkFb` (`0x005404E0`), levando ao crash.
