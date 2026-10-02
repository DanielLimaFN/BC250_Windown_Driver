# Corrida 174 — Avanço do FLUSH_TLB (T3.4), Menu Iniciar Renderizado e Análise do Ponto de Parada

> **Data:** 29/09/2026 13:22 | **Candidato:** T3.4 (DriverVer `3.319.1.0`, SYS `DB65069BDC46111F40EFBA23801429554A8BC54ED9AE3834C4AC49335ECFE597`)  
> **Pasta de Evidência:** `docs/evidence/2026-09-29_13-22-21-DESKTOP-HQAG6T4-lab-binding`  
> **Log recuperado:** 10.767.805 bytes de offset 4274432498, SHA-256 `EF2CA0CDBE742261159A23E6C3FAB0F303967320AFDB7A884C57C4BB2AD4FF5C`  
> **Duração medida pelo QPC:** 81,4 segundos.

---

## 1. Relatório de Governança (Obrigatório segundo AGENTS.md / GEMINI.md)

### 1.1 Houve avanço?
**SIM, avanço funcional e estrutural comprovado.**
1. **FLUSH_TLB (T3.4) totalmente resolvido:** Na Corrida 173 anterior, a máquina congelou aos 63,0 s com a thread de paginação na CPU10 travada dentro de `FLUSH_TLB` (`PAGING_DDI 0x0C` ANTES). Na Corrida 174, com a simplificação para 3 passos alinhada ao kernel Linux (`gmc_v10_0.c:289-315`) e a remoção das 14 operações MMIO em `RangeLow/High`, **18.743 chamadas de `FLUSH_TLB` completaram com sucesso imediato (`status=0x00000000`, ~6 µs)**. Nenhuma chamada de `FLUSH_TLB` ficou presa.
2. **Vazão do DWM quase dobrou:** De 301 quadros (Corrida 173) para **593 quadros apresentados** com **5 `BYTE_PROOF PASS`**.
3. **Menu Iniciar acelerado por hardware:** O processo `StartMenuExperienceHost.exe` carregou a UMD, compilou shaders sem falha, renderizou 261 draw calls via Vulkan/D3D11 e apresentou 9 quadros completos (`hr=0x00000000`).

---

### 1.2 Sucessos
- **Duração da Sessão:** 81,4 s (+29% sobre a Corrida 173).
- **Quadros do DWM:** 593 `Present` (+97% vs Corrida 173) com 5 `BYTE_PROOF PASS`.
- **Estabilidade do SDMA:** 3.253 IBs executados, 1.916 Fills, 1.484 Transfers, 0 `IB_TRIPOD TIMEOUT`, 0 `Blt Timeout`, 0 `GFXHUB_FAULT`.
- **Integridade da Memória:** 0 eventos degradados no anel EVT.1 da GPU (48 válidos, 0 vazios, integridade XOR de 100% — a DRAM e o memory controller não decaíram).
- **Comportamento do Menu Iniciar:** O `StartMenuExperienceHost.exe` completou 173 flushes de lote Vulkan, gerou pixels comprovados (`GRADE: NonZero=7973/12800`) e apresentou 9 frames ao DWM via `pfnPresentCb` com `hr=0x00000000`.

---

### 1.3 Falhas
- **Congelamento aos 81,4 s após a abertura do Menu Iniciar:** O sistema congelou sem BugCheck logo após o Menu Iniciar solicitar alocações adicionais de superfície.
- **Assinatura de Parada nas Migalhas (VRAM DX.66):**
  - `CPU12 camada=INTERNA seq=1172794 sitio=VRAM_PT_READ detalhe=0x000A8D fase=DEPOIS(concluido) <- ULTIMA DA MAQUINA`.
  - `CPU15 camada=PAGINACAO seq=1172792 sitio=PAGING_DDI detalhe=0x00000C fase=DEPOIS(concluido)`.
  - `CPU13 camada=SUBMISSAO seq=1172791 sitio=SUBMIT_DDI detalhe=0x000014 fase=ANTES(acesso NAO concluido)`.
- **Diagnóstico:**
  - O SDMA estava completamente desocupado (`EVT1_EM_VOO: ultimo submit seq=11128 fence=3253 -> CONCLUIDO`).
  - Todas as leituras MMIO de registradores e VRAM completaram com sucesso (`DEPOIS`).
  - A CPU13 parou durante a preparação de submissão do DMA buffer para a alocação `00001B4F000007FB` (64 KB, preenchimento com zeros gerado pelo VidMm para o menu Iniciar).

---

### 1.4 Veredito
**SUCESSO PARCIAL SIGNIFICATIVO.**
O problema que causou o congelamento na Corrida 173 (`FLUSH_TLB` com sobrecarga de 14 registradores MMIO e logs sob lock) está **refutado e resolvido**. A fluidez do sistema e a capacidade de suportar janelas D3D11 e render do menu Iniciar foram amplamente estendidas.

---

### 1.5 Recuperação e Pendências
- **Recuperação:** Reset físico do gabinete preservou as migalhas na VRAM. O fusível anti-boot-loop disparou como previsto (`State=2, Boots=2`), mantendo o sistema em Código 43 seguro.
- **Pendência:** Resetar o fusível e investigar a rotina de validação e submissão da paginação na CPU13 para alocações novas geradas por aplicativos em execução (Faixa T3.5).
