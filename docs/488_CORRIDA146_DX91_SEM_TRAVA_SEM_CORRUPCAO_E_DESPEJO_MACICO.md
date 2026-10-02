# 488 — Corrida 146 (DX.91, com KDNET): sem trava, sem corrupção; a correção não chegou a agir; despejo maciço de VRAM medido

> **Corrida:** 27/09/2026. Bind às 22:42:27 após boot frio, com KDNET ativo; harness às 22:45:28. Rollback online (`RECOVERY_OK_ONLINE`). **Candidato:** DX.91 / 3.297.1.0.
> **Evidência:** `docs/evidence/2026-09-27_22-42-27-DESKTOP-HQAG6T4-lab-binding` (logs `-FINAL`); saída do WinDbg colada pelo Daniel.
> **Relato do Daniel:** "rodou tudo certinho, desktop visível em todo o teste, o triângulo também; só no WinDbg deu uns erros".

## 1. Relato obrigatório
**1. Houve avanço?** Funcional, sim: foram 600 s com desktop e harness corretos, sem a corrupção da 145. A correção da DX.91, porém, **não foi exercitada**.

**2. Sucessos (P0, exceto o visual):**
- Paginação 86.776/86.776; 2.401 Presents no KMD, 5 `BYTE_PROOF PASS`; harness 60/60.
- Zero TDR, fault ou `DEVICE_REMOVED`.
- Visual correto durante todo o teste, pelo relato (P2).
- **`DX91_PAGING_MOVE` funcionou:** 21.240 linhas (7.342 TRANSFER, 12.833 FILL, 1.065 DISCARD).

**3. Falhas e observações:**
- **Despejo maciço de VRAM (P0):** 7.340 das 7.342 cópias foram `LOCAL_TO_SYSTEM` (`Dir=0`) e só 2 `SYSTEM_TO_LOCAL`. Foram **~2.459 MB despejados em 600 s**. O segmento local de ~245 MB é pequeno demais para DWM + Explorer + harness.
- 6.710 `PUBLICACAO_FALHA` (journal no teto).
- **WinDbg:** paradas de Code Integrity. O `msedgewebview2.exe` recusou carregar `C:\BC250_LIB\libvkd3d-shader-1.dll`, que não é assinada. Seguiram-se `int 3` em modo usuário no mesmo processo, e `g` continuou. Não é falha do KMD. É uma **pendência de produto**: as DLLs de que a UMD depende (`libvkd3d-shader-1.dll` e `vulkan_radeon.dll`) ficam fora do pacote assinado. Processos com política de assinatura (Edge/WebView) não conseguem usar o D3D11 da nossa UMD.

**4. Veredito frente ao doc 487 §3: D.**
- Não houve corrupção nem `RANGE_SCAN` algum: zero resoluções estritas falharam. A condição que ativava o fallback cruzado não se formou.
- O harness começou com N≈690–750, perto do limite de validade.
- A DX.91 **não está validada nem refutada**.

**5. Recuperação e pendências:**
- BasicDisplay Código 0 no mesmo boot; `CLEAN_BOOT_REQUIRED` armado.
- **NÃO VALIDADOS EM HARDWARE:** o efeito da DX.91 e a causa da trava.

## 2. Padrão que aparece (H)
| Corrida | KD | Harness com Explorer grande | Trava |
|---|---|---|---|
| 139, 140, 143, 144 | não | sim | **sim** (4/4) |
| 145, 146 | **sim** | sim | não (0/2) |

- O depurador pode estar mudando o tempo o suficiente para esconder uma **condição de corrida**. Amostra pequena (2), e a 146 também tinha a DX.91. Isso é H.
- O próximo passo que separa as variáveis é a DX.91 **sem** KD.

## 3. Próximos passos
1. **Corrida 147:** DX.91 sem KD (`bcdedit /debug off`, feito pelo Daniel, e boot frio), com o mesmo runner. Separa "KD esconde a trava" de "a DX.91 curou".
2. **Frente de memória (produto):** o despejo de 2,4 GB em 10 min mede o custo real dos ~245 MB. Usar os 512 MB (segundo segmento só da GPU) e a RAM do sistema como segmento de verdade reduz o despejo. Trabalho offline.
3. **Empacotamento:** levar `libvkd3d-shader-1.dll` e `vulkan_radeon.dll` para dentro do pacote assinado (INF/CAT), para processos com política de assinatura.
