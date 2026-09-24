# Corrida 84 — critério prévio para medir o fluxo DX.40

> 23/09/2026. Preparação offline. **Nenhum bind ou corrida executado.** Continuação do [build DX.40](365_DX40_BUILD_COMPLETO_FLUXO_RADV_LINUX_WDDM.md). Operador físico: Daniel.

## Bloqueio antes do ensaio

`C:\BC250_REQUIRE_CLEAN_BOOT.json` registra `CLEAN_BOOT_REQUIRED` após a recuperação da Corrida 83. O `rollback_boot_ticks` é `639257689705000000` e a leitura atual de `Win32_OperatingSystem.LastBootUpTime.ToUniversalTime().Ticks` devolveu **o mesmo valor**. Logo, o preflight do wrapper recusaria o bind neste boot. O arquivo `C:\BC250_BOOT_FUSE.dat` está ausente; o wrapper só o reseta **depois** de passar pelo preflight de boot frio. Não usar `-Force`, não apagar o marcador. Daniel precisa desligar completamente, ligar novamente e executar o wrapper elevado.

## Candidato fixado

| Item | Valor |
|---|---|
| Pacote | `driver/wdk/package/output_lab`, `DriverVer=09/23/2026,3.246.1.0`, `DISPLAY_RENDER_LAB`, WDDM 2.7, `GfxWaveProbe=1`; auditoria PASS |
| KMD SYS assinado | SHA-256 `F1EE106C78517506F6625E08CAE5C9717D298B9CEBEF1E8198A3F8A0FE403796` |
| UMD D3D11 assinada | SHA-256 `45BB28C3613F8E7D654B373E3E3D76669A58BD13C45876A301781AB5821396B9` |
| RADV construída | `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll`, SHA-256 `BF6A1EDAE5C6F64E5A0C5795878E5D187814EB91A1E5C0F3447074B6F7CEE14A` |
| RADV instalada antes da corrida | `C:\BC250_LIB\vulkan_radeon.dll`, SHA-256 `5988B38E86FEBB514C4C5F1D5FB5CE5A4B6FDD2B57668F2A1B6BACA3B1A0461F` (Corrida 83). O wrapper deve copiar a nova e provar igualdade de hash **antes** do bind. |

## Perguntas que a corrida precisa responder

1. O DWM gerou `bc250_flow_pid*.log`, dumps `.bin` e `.meta` completos, e o binder arquivou todos no mesmo diretório de evidência? O índice foi gerado e a cauda KMD está completa? Se não, identificar a falha de coleta; ausência de um evento não prova ausência da operação.
2. Para cada `submit_id` relevante, qual foi a sequência `RADV_QUEUE_SUBMIT_BEGIN` → `RADV_QUEUE_WAIT` (se houver) → `RADV_CS_LAYOUT` → `RADV_QUEUE_SUBMIT_END` → `RADV_QUEUE_SIGNAL` (se houver)? Qual chamada falhou, com qual status? `accepted=1` não prova execução.
3. Há uma correlação **única** por VA/DW/FNV64 entre `.meta` aceito e `KMD_GFX_SUBMIT` completo? Quais contexto, VMID, fence WDDM e fence do anel? Duplicata, dump ausente ou hash parcial são inconclusivos.
4. Em cada troca GFX, `KMD_GFX_SWITCH` mostra o predicado (`prev_vmid`, `vmid`, `switch`), `CONTEXT_CONTROL` e os dwords reais `ib_header`/`ib_control`? Comparar o bit 23 `VALID` com o emissor GFX10 Linux e registrar a diferença sem atribuir causalidade automática. Verificar RPTR/WPTR, fence observada e, se houver TDR, onda/PC/VMID/fault com ordem temporal.
5. Houve melhora funcional frente à [Corrida 83](360_CORRIDA83_DX37_RESULTADO.md), que teve desktop desorganizado, três TDRs e 0/60 quadros? Registrar estado visual informado pelo operador, Present/pixels, device-lost, BugCheck e recuperação. Código 0 ou Exit 0 isolado não fecham o ensaio.

## Veredito pré-definido

**Sucesso diagnóstico:** fluxo RADV preservado, correlação única ao KMD para as submissões que cercam cada falha ou sequência limpa, leitura KMD completa, cauda preservada e rollback verificado. **Parcial:** uma ou mais etapas/correlações faltam, mas há dados novos suficientes para reduzir hipóteses. **Inconclusivo:** coleta ausente/truncada ou candidato divergente. **Falha funcional:** desktop ainda incorreto, TDR, device-lost, 0/60 ou erro de pixels, mesmo com sucesso diagnóstico. O resultado funcional e o diagnóstico devem ser relatados separadamente.

Teto: **uma corrida** com o wrapper existente e rollback incondicional. Não repetir automaticamente. Se houver BugCheck ou reinicialização, recuperar a cauda com `tools/recover-bc250mcdm-lost-tail.ps1` antes da análise. Depois do ensaio, relatar avanço, sucessos, falhas, veredito, recuperação, lacunas e próximo passo; arquivar a evidência e atualizar `BASE_DE_AVANCO.md`.

## Execução pelo operador, após boot frio

Abrir terminal **elevado** em `C:\Users\Daniel\Desktop\NamixWDDM` e executar:

```cmd
tools\EXECUTAR_D3D11_NATIVO.cmd
```

O wrapper verifica boot frio, reseta o fusível, copia RADV com comparação SHA-256, faz o bind, limita o ensaio e aciona rollback. Não usar `-Force`.
