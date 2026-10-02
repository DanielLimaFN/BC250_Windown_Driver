# 516 — Corrida 160: 600 s sem trava (mesmo pacote da 159); o menu Iniciar não abre porque um pixel shader não traduz; tabela de objetos cheia aos 229 s

> Corrida de 28/09/2026. Bind às 18:17:31, candidato 3.308.1.0 (o mesmo da 159), harness aos 30 s. Terminou em 600 s com rollback online (`RECOVERY_OK_ONLINE`, BasicDisplay Código 0).
>
> Evidência:
> - `docs/evidence/2026-09-28_18-17-31-DESKTOP-HQAG6T4-lab-binding/`;
> - log do menu Iniciar em `C:\Users\Daniel\AppData\Local\Packages\Microsoft.Windows.StartMenuExperienceHost_cw5n1h2txyewy\AC\Temp\BC250_UMD_LOG_7024.txt`.

## 1. Relato (cinco pontos)

**1. Houve avanço?** Sim, de diagnóstico: a causa do menu Iniciar ficou nomeada. Na estabilidade, **inconclusivo**. É a primeira corrida sem trava desde a 153, mas a 159, com o mesmo pacote, travou.

**2. Sucessos (P0):**
- 600 s sem trava, zero fault, zero TDR, zero timeout do SDMA.
- 7.891 Presents.
- Harness 60/60 a **1,68 FPS**, com 444 ms em `pfnPresentCb`:

  | Dentro de `pfnPresentCb` | Valor |
  |---|---|
  | IBs de paginação | 26,8 |
  | Esperando conclusão do SDMA | 32,2 ms |
  | FLUSH_TLB | 146,8 ms |
  | Resto (VidMm) | 265,5 ms |

- As 42 `PUBLICACAO_FALHA` são todas contenção (`PublishStatus=16`).

**3. Falhas (P0):**
- **O menu Iniciar não aparece.** O `StartMenuExperienceHost.exe` (pid 7024) carregou a nossa UMD às 18:26:14 e fez 29 tentativas de desenho, todas `DrawIndexedInstanced: RECUSADO -- ... PIPELINE_NAO_OBTIDA_OU_COMPILADA`. Causa: **um pixel shader SM 4.0 de 89 tokens falha na tradução** (`SHADER_TRANSLATOR: traducao TokenStream falhou (status=3)` = `BC250_DXBC_SPIRV_TRANSLATOR_FAILED`), e depois vem `CreateShaderModule RECUSADO`. O vertex shader do mesmo desenho traduz e o pipeline dele compila. O vkd3d-shader recusou o PS, e o nosso código **não registra a mensagem de erro dele**.
- **Tabela de objetos do KMD cheia aos ~229 s:** 5 `CREATE_ALLOC_RECUSADO`, com `status=0xC000009A` e `ObjStatus=3` = `BC250_MCDM_OBJECT_NO_SPACE`. O teto é `BC250_MCDM_MAX_OBJECTS = 4096`. Quais processos receberam a recusa não está identificado.
- **Lacuna de coleta:** 772 logs da UMD em `AppData\Local\Packages\*\AC\Temp` (menu Iniciar, Pesquisa, Central de Ações, CBS). O binder nunca os copiou, porque os apps isolados (AppContainer) não escrevem no `%TEMP%` normal.
- O "100% de CPU" do Gerenciador de Tarefas é a **utilidade do processador** (94–99%), enquanto o tempo real ficou em 6–21%. É contador, não saturação.

**4. Veredito:**
- Contra o doc 513: esta corrida teria assinatura A, mas **uma limpa depois de uma B com o mesmo pacote não fecha nada**. A DX.97 não é a correção (a 159 travou com ela), e esta limpa é compatível com o acaso (a taxa medida era de 5 travas em 6).
- O menu Iniciar ganhou causa com nome.

**5. Recuperação:** rollback online limpo. **NÃO VALIDADO EM HARDWARE:** a causa da trava; a correção do shader.

## 2. Próximos passos, por custo
1. **Shader do menu Iniciar (offline, sem corrida):** gravar em arquivo os tokens do shader que falha e a mensagem do vkd3d-shader. Reproduzir a tradução offline com a mesma DLL (`C:\BC250_LIB\libvkd3d-shader-1.dll`), corrigir o empacotamento ou a assinatura, e testar offline antes de qualquer corrida.
2. **Binder:** coletar também os `BC250_UMD_LOG_*` de `AppData\Local\Packages\*\AC\Temp`.
3. **Tabela de objetos:** registrar o dono e a ocupação na recusa. Avaliar teto maior ou vazamento (slots mortos que não voltam).
4. **Trava:** ensaio com `powercfg` sem C-states (decisão do Daniel), ou DX.98.
