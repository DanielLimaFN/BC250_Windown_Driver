> **SUBSTITUÍDO ANTES DE RODAR (23/09):** o candidato RADV e os scripts passaram para a DX.42 — ver [docs/370](370_DX42_IDENTIDADE_DO_PREAMBULO_E_CRITERIO_CORRIDA85.md). Executar `tools\EXECUTAR_CORRIDA85_DX42.cmd`; os nomes `...dx41` abaixo não existem mais.

# Corrida 85 — critério prévio DX.41: origem do preâmbulo e estabilidade GFX

> Preparada em 23/09/2026, **sem bind e sem corrida**. Operador físico: Daniel.
> Base comparativa: [Corrida 84](367_CORRIDA84_DX40_RESULTADO.md) e
> [análise DX.41](368_DX41_ANALISE_IB_ZERO_POS_CORRIDA84.md).
> Uma única execução; o wrapper mantém watchdog e rollback incondicional.

## Candidato fixado

| Componente | Valor esperado |
|---|---|
| KMD/UMD | Mesmo pacote lab da Corrida 84, `DriverVer=09/23/2026,3.246.1.0`, `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`; SYS `F1EE106C78517506F6625E08CAE5C9717D298B9CEBEF1E8198A3F8A0FE403796`, UMD `45BB28C3613F8E7D654B373E3E3D76669A58BD13C45876A301781AB5821396B9` (SHA-256). |
| RADV DX.41 | `build/src/amd/vulkan/vulkan_radeon.dll`, SHA-256 `5C4E770140DB3A0C2DF36A3F7CF11B2448789285CFFF54F2E0E677BCE0891DEE`. O wrapper copia para `C:\BC250_LIB` e confere o hash antes do bind. |
| Código RADV | [Patch espelhado](../external/radv-wddm/bc250-radv-wddm.patch), SHA-256 `B608DCB60F7A201922425256D975C41C6D14701D09BDD85F4C9694CAE8814F03`. |
| Pré-voo | `tools/prepare-corrida85-dx41.ps1` confere hashes e auditoria do pacote. PASS offline em 23/09; não instala o driver. |

O marcador `C:\BC250_REQUIRE_CLEAN_BOOT.json` estava **armado** na
preparação: `rollback_boot_ticks=639257764495000000` era igual ao boot
atual. Portanto, o wrapper recusará o bind até haver **desligamento completo
e novo boot**. Não usar `-Force` nem apagar o marcador. A RADV implantada
ainda era a DX.40 (`BF6A1EDA...`) na preparação; copiar DX.41 faz parte do
wrapper, depois do preflight de boot.

## Perguntas e observações esperadas

1. Em cada `layout=CONCAT`, procurar `RADV_CONCAT_PREAMBLE` com PID,
   `submit_id`, contexto, VA, `source_first`, `copied_first`, tamanho do
   prefixo zero na origem e na cópia, e `copied_dw`. Agrupar por
   **arquivo + contexto + submit_id**; o ID pode reiniciar em outro contexto.
2. Se `source_zero_prefix=180` e `copied_zero_prefix=180`, o preâmbulo já
   estava zerado na leitura CPU. Investigar criação, vida e mapeamento do CS
   persistente. Se a origem parecia íntegra e a cópia saiu zero, investigar
   BO/slot, mapeamento, coerência ou escrita concorrente. Como as leituras
   não são uma transação atômica, divergência exige olhar a sequência e
   repetir a medição antes de atribuir uma corrida de dados.
3. Quando o primeiro DW copiado for zero, esperar
   `layout=REJECT_ZERO_PREAMBLE`, `RADV_CONCAT_REJECT`, `.meta accepted=0`
   e **nenhum** `KMD_GFX_SUBMIT` correspondente. A recusa devolve
   `VK_ERROR_DEVICE_LOST` de propósito. Ela pode impedir o IB zerado de
   chegar ao CP, mas **não é sucesso funcional** nem prova a causa dos TDRs
   1–3 da corrida anterior.
4. Para cada TDR ou sequência limpa, cruzar VA/DW/FNV64 completos, contexto,
   VMID e fence RADV/KMD; conferir `KMD_GFX_SWITCH`, `CONTEXT_CONTROL`,
   RPTR/WPTR, fence observada, fault e ondas/PC. Se ondas com PC do IB novo
   aparecerem no VMID antigo, a hipótese de estado de contexto persiste.
   Ausência de TDR em uma execução só não a refuta.
5. Medir tela inteira e harness: `Present` aceitos entre 60, pixels,
   `DeviceRemovedReason`, TDR, BugCheck, estado final do driver. A base é
   Corrida 84: quadrado azul e triângulo no centro da tela preta,
   **1/60**, quatro TDRs. Código 0, hash coincidente e triângulo isolado
   não fecham desktop acelerado.

## Coleta e veredito pré-definidos

O binder faz uma cópia durante a janela. Após o rollback, o wrapper chama
`collect-bc250-gfx-ib-after-recovery.ps1`: compara SHA-256, preserva
versões iniciais divergentes como `.at-capture.snapshot`, arquiva os `.bin`,
`.meta` e fluxos completos, grava `gfx-ib-dump/post-recovery-collection.json`
e regenera `telemetry-index.md`. Arquivos órfãos, fluxo ausente, limite de
4 MiB ou hash divergente tornam a coleta incompleta; preservar o staging
para resgate. Se houver reboot/BugCheck, usar o recuperador da cauda KMD
antes de concluir; a passagem pós-rollback pode não ter rodado.

**Sucesso diagnóstico:** origem/cópia distinguidas quando o prefixo zero
reaparecer, com recusa antes do KMD, ou sequência íntegra documentada com
correlação das submissões em torno de cada falha; coleta pós-rollback
completa. **Parcial:** evidência nova útil, mas algum ponto temporal ou dump
ausente. **Inconclusivo:** candidato diferente, log truncado ou coleta
insuficiente. **Sucesso funcional** exige desktop íntegro, 60/60 quadros,
pixels corretos, nenhum device-lost/TDR/BugCheck, driver vivo ao final da
janela. Qualquer recusa do preâmbulo, TDR, tela preta ou 1/60 continua falha
funcional, mesmo com diagnóstico conclusivo.

## Execução pelo operador após boot frio

Abrir terminal **elevado** em `C:\Users\Daniel\Desktop\NamixWDDM`:

```cmd
tools\EXECUTAR_CORRIDA85_DX41.cmd
```

O preflight recusa candidato divergente; o wrapper existente verifica boot
frio, reseta o fusível, copia e confere RADV, inicia uma janela de 120 s,
executa o harness de 60 quadros e faz rollback. Não repetir automaticamente.
Após a execução, preservar a pasta de evidência informada e relatar
observação visual, avanço, sucessos, falhas, veredito, recuperação e o que
segue **NÃO VALIDADO EM HARDWARE**.
