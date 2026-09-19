# Handoff 247 — Marco 3: render continuou sob tela preta; DWM caiu; recovery endurecida

**Data:** 2026-09-12  
**Corrida:** `2026-09-12_11-04-22-DESKTOP-HQAG6T4-lab-binding`  
**Escopo desta faixa:** mineração P0 da corrida e correções offline do ensaio.  
**Estado das correções:** **NÃO VALIDADO EM HARDWARE**.

## Veredito

O relato visual foi “mostrou apenas 1 vez o frame, e depois tela preta”. Isso não
significa que a GPU parou após um frame. O harness produziu e apresentou centenas
de quadros enquanto a tela permanecia preta:

- byte proof inicial: 8/8 zonas PASS;
- apresentação WDDM inicial: PASS;
- Node 1: 373 submits aceitos e 373 aposentados no recorte do runner;
- parada: `Sub=390`, `Ret=390`, `RPTR=WPTR=4680`;
- SDMA: `Submitted=372`, `Completed=372`, `Fallbacks=0`;
- GCVM: `FAULT STATUS=0`;
- ruptura no frame 373: Escape `0xC00002B6` (`STATUS_DEVICE_REMOVED`);
- janela de 16,27 s: 7 eventos `Application Error` de `dwm.exe` e 8 relatórios WER,
  todos na assinatura `e0464645`/`KERNELBASE.dll` já conhecida;
- a UMD registrou `OpenAdapter10/10_2 -> E_NOTIMPL`, com
  `D3D11LabPromotion` desarmado.

Portanto, a hipótese de travamento MEC/SDMA está refutada nesta corrida. A tela preta
é compatível com a tempestade de fail-fast do DWM. A relação causal entre essa
tempestade e o `STATUS_DEVICE_REMOVED` final ainda precisa de uma experiência que
isole uma variável; não é declarada como prova.

## Defeitos do ensaio encontrados e corrigidos offline

1. O veredito condicionava `render_byte_proof` e `wddm_present_called` ao exit code
   final. Assim, uma falha posterior apagava provas monotônicas já registradas.
   Agora essas duas provas saem diretamente do stdout; `swapchain_completed` e
   `process_exit_zero` continuam falsos quando a sequência não termina.
2. A contagem de “crashes do DWM” somava `Application Error` e o relatório WER do
   mesmo incidente. Agora há campos distintos: `dwm_crash_events` e
   `dwm_wer_events`.
3. `EXECUTAR_MARCO3.cmd` dependia exclusivamente do watchdog após o tripé. Nesta
   corrida o host terminou com `bc250mcdm`, Código 43, e sem tarefa watchdog ativa.
   O lançador agora chama `BC250_RECOVERY.cmd` incondicionalmente depois que o binder
   retorna, inclusive em falha, exige exit zero e só então publica
   `C:\BC250_REQUIRE_CLEAN_BOOT.json`.
4. O preflight do mesmo lançador comparava `DateTime.Ticks` do marcador com
   `FileTime`; foi corrigido para `ToUniversalTime().Ticks`. O `.cmd` foi normalizado
   para CRLF.

## Gates offline

- `build-contract.bat`: PASS;
- `audit-bc250mcdm-safety.py`: PASS;
- parser do runner PowerShell: PASS;
- gate novo `audit_marco3_closes_its_own_transaction`: exige prova parcial preservada,
  rollback posterior ao bind, falha fechada do recovery e marcador após recovery;
- estado final recuperado pelo operador e conferido: `BasicDisplay`, Código 0, sem
  serviço/binário experimental e sem fusível.

## Próximo passo

Não repetir o mesmo Marco 3. O próximo candidato deve isolar a parede DWM: comparar
o perfil RADV com a promoção D3D11 controlada já existente, mantendo a substituição
de Draw fail-closed. Isso mede se manter um device D3D válido elimina o ciclo de
fail-fast sem fingir rasterização. O backend completo continua sendo
DXBC → SPIR-V → RADV/ACO; a promoção não fecha esse item.
