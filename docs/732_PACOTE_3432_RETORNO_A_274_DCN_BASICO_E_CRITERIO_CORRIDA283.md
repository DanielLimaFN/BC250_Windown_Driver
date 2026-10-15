# 732 — Pacote lab 3.432.1.0: Retorno à Corrida 274 (DCN Básico Comprovado) e Critério da Corrida 283

**Data:** 13/10/2026, offline.  
**Decisão do Daniel pós-Corrida 282:** Desativar por completo as tentativas de alteração de temporização dinâmica (D19) e chaveamento de pad AUX (D12), retornando rigorosamente à configuração comprovada e estável da **Corrida 274**, que já provou vídeo limpo, sem artefatos e com flip direto no HUBP0 a 60 FPS (zero cópia de 11 MB).

---

## 1. O que foi Construído e Configurado

- `driver\wdk\build-contract.bat`: todas as suítes e contratos passaram com **OK** (`DCN FLIP CONTRACT`, `DCN EXT CONTRACT`, etc.).
- `tools\build-bc250mcdm-lab-bringup.ps1`: imagem de laboratório construída em `driver\wdk\output_lab\bc250mcdm.sys` (844.800 bytes; bring-up ativo confirmado).
- **DriverVer fonte subido para 3.432.0.0** em `bc250mcdm.inf`, gerando o pacote **3.432.1.0** para instalar sobre o 3.431.1.0 da Corrida 282 sem rejeição de PnP.
- `tools\package-bc250mcdm-lab.ps1`: executado com `-EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -DebugTrace -DcnFlip -DcnExt` (sem `-ArmAuxHwMode` e sem `-ArmVTotalRefreshTest`).
- Auditorias:
  - `tools\audit-bc250mcdm-lab-package.ps1`: **PASS**
  - `python tools\audit-bc250mcdm-safety.py`: **PASS**

---

## 2. Candidato e Parâmetros (Comparativo com as Corridas 274 e 282)

| Parâmetro | Corrida 274 | Corrida 282 | Corrida 283 (Atual) | Efeito no Hardware |
|---|:---:|:---:|:---:|---|
| `DcnFlip` | 1 | 1 | **1** | Flip direto no HUBP0 (zero cópia SDMA) |
| `DcnExt` | 125 (0x7D) | 381 (0x17D) | **125 (0x7D)** | DCN Básico: IRQ + Cursor HW + DPMS + Timing nativo no VidPn |
| `D19` (V_TOTAL) | **DESATIVADO** | ATIVADO | **DESATIVADO** | `OTG0_V_TOTAL` intocado; sem mexer no link DP herdado do GOP |
| `D12` (AUX PAD HW) | **DESATIVADO** | DESATIVADO | **DESATIVADO** | Sem chaveamento de pad AUX |
| `DebugTrace` | 1 | 1 | **1** | Coleta de diagnósticos para investigação da trava |

### Hashes do Pacote 3.432.1.0 (`driver\wdk\package\output_lab\`):
- `bc250mcdm.sys` = `C250C67C638BDF3098C39CD0E8469E20D702F4E94FC927185F351725CC2315FC`
- `bc250mcdm.inf` = `5464EEA1B05E1CB8B2316ABBC145EFDDD32D1767441B08C1F989D9654E25364E`
- `bc250mcdm.cat` = `A7980499D93F4E05B8984332B98A8C2472A156957933E78700C8D44DD52FCB1E`
- `bc250umd.dll` = `B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66`
- RADV `vulkan_radeon.dll` = `B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D`

Lançador: [`tools\EXECUTAR_CORRIDA283_RETORNO_274_DCN_BASICO_180S.cmd`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/EXECUTAR_CORRIDA283_RETORNO_274_DCN_BASICO_180S.cmd)  
Preflight: [`tools\prepare-corrida283-retorno-274-dcn-basico.ps1`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/prepare-corrida283-retorno-274-dcn-basico.ps1)

---

## 3. Como Funciona a Desativação e Como Reativar Futuramente

No sistema de empacotamento (`package-bc250mcdm-lab.ps1`), os recursos experimentais são controlados por parâmetros separados:
- **Para manter o DCN seguro da Corrida 274:** basta usar `-DcnFlip -DcnExt` (gera `DcnExt = 125`).
- **Para reativar D12 no futuro:** basta adicionar `-ArmAuxHwMode -IAcceptAuxPadRegisterWrite` (adiciona `0x80`).
- **Para reativar D19 no futuro:** basta adicionar `-ArmVTotalRefreshTest -IAcceptVTotalRegisterWrite` (adiciona `0x100`).

Como nenhum switch experimental foi passado, o pacote gerado possui `DcnExt = 125` garantido no INF e no manifesto, deixando D12 e D19 completamente inertes no código.

---

## 4. Critério da Corrida 283 (Lei 10)

**A — Vídeo Limpo e Estável (Esperado):**  
O bind ocorre, o desktop surge imediatamente limpo no monitor DisplayPort sem flash corrompido e sem artefatos. O cursor por hardware move suavemente. O flip no HUBP0 roda a 60 FPS com zero cópia de 11 MB. Confirma que a causa única da tela preta das Corridas 275–282 era o D19.

**B — Vídeo Limpo e Trava Dura Histórica:**  
O vídeo sobe perfeito, mas o sistema congela após abrir janelas ou rodar a T3 (comportamento conhecido de submissão/paginação). Prova que o display está curado e permite voltar o foco para P1→P8.

**C — Artefatos Persistem Mesmo com D19 Fora:**  
Se a tela continuar com artefatos mesmo sem D19 (improvável, pois 269–274 foram limpas). Indicaria que algum resíduo elétrico no monitor exige desligamento físico da tomada antes do boot.

**D — Regressão Funcional:**  
Qualquer falha em flip, cursor HW ou DPMS.

**E — Fora do Mapa.**

---

## 5. Roteiro de Execução para o Operador

1. **Desligar completamente o PC** (Desligar no Windows, esperar os coolers pararem).
2. Ligar a máquina.
3. Executar como Administrador: `tools\EXECUTAR_CORRIDA283_RETORNO_274_DCN_BASICO_180S.cmd`.
4. Observar se o desktop aparece **100% limpo e sem artefatos**.
