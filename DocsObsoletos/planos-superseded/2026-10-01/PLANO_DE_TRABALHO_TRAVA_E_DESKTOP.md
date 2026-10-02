# Plano de trabalho: trava dura na partida, desktop e desempenho

> Escrito em 28/09/2026, depois da Corrida 165, para a próxima IA que seguir este projeto.
> É um plano de trabalho. **Nada aqui foi validado em hardware, exceto onde está marcado P0.**
> Fonte única do que já foi provado: [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md). Leis e forma do relato: [`CLAUDE.md`](CLAUDE.md).
> Histórico detalhado desta fase: docs 496 a 518 (lista na §8).

---

## 0. Como usar este documento

1. Leia antes o `CLAUDE.md` inteiro, em especial as Leis 1 a 10 e a "Diretriz vigente" sobre o relato de corridas. Elas mandam sobre qualquer coisa escrita aqui.
2. Leia a §1 (regras de operação) e a §2 (estado atual). Não pule: várias tarefas dependem de fatos da §2.
3. Escolha a tarefa pela ordem da §6. Cada tarefa da §4 tem:
   - **Objetivo**;
   - **Por quê**, com a evidência e o nível de confiança;
   - **Como fazer**, com arquivos, linhas e comandos;
   - **O que esperar**, com os desfechos já escritos no formato da Lei 10;
   - **Pronto quando**;
   - **Custo**: se gasta corrida física, que é o recurso escaso.
4. Depois de cada corrida, siga a §3.3 (análise) e a §3.4 (relato), e atualize a BASE e o doc da faixa.
5. Números de linha citados valem para o código de 28/09/2026. Se o arquivo mudou, procure pelo nome da função ou pela string citada.

**Vocabulário de confiança (Lei 8):**
- **P0**: provado no silício ou medido em corrida;
- **P1**: lido no código ou no upstream, com fonte;
- **P2**: observação parcial ou relato;
- **H**: hipótese;
- **Q**: quarentena, proibido usar.

---

## 1. Regras de operação (não negociáveis)

| Regra | Detalhe |
|---|---|
| Quem mexe no hardware é o Daniel | A IA compila, empacota, audita, prepara runners e analisa. **A IA nunca faz bind, reboot, instalação de driver nem desbind.** |
| Configuração do sistema é do Daniel | Nada de `bcdedit`, registro do sistema, crash keys, `powercfg` ou BIOS feito pela IA. A IA escreve o comando, e o Daniel executa e reverte. |
| Boot frio entre binds | Com corte de energia e o monitor **ligado antes** de ligar a placa. O estado da engine não cai com desbind (memória `bc250-boot-frio-entre-binds`). |
| Antes de todo bind | `tools\BC250_RECOVERY.cmd` e `tools\inspect-boot-fuse.ps1 -Reset`, elevados, e depois boot frio. |
| Não apagar à mão | `C:\BC250_REQUIRE_CLEAN_BOOT.json` e `refs/codex/...`. |
| Nunca | `del ...\bc*.sys`: o padrão casa com `bcmfn2.sys`, driver inbox da Microsoft. |
| DCN | Escrita em registrador de DCN é **proibida** fora da P9. Leitura é permitida. |
| Git | Sem commit e sem push, salvo pedido explícito do Daniel. |
| `C:\Windows\Temp` e `MEMORY.DMP` | Exigem cópia elevada feita pelo Daniel. Listagem vazia da sessão não prova ausência (memória `bc250-windows-temp-sem-acesso`). |
| Na trava | O Daniel aperta o **reset em 10 a 15 s**. A VRAM perde o conteúdo durante a trava, e o reset rápido preserva as migalhas (memória `bc250-migalhas-decaem-na-trava`). |
| Depurador | O boot com KDNET muda o tempo da máquina. O preparo recusa o bind com `DEBUG` no boot, salvo `BC250_ALLOW_KERNEL_DEBUG=1`. **O KD não para a trava dura** (§2.2): não gaste corrida com KD, dump ou ETW para ela. |
| Empacotador | **Não compila o SYS**: só copia. Sempre rode o build de laboratório antes (memória `bc250-empacotador-nao-compila-o-sys`). |
| Heredoc | Conteúdo com `\` ou apóstrofo passado por heredoc do Bash corrompe (virou backspace no runner da 164). Use o Write/Edit ou Python com bytes. |

---

## 2. Estado atual (28/09/2026, depois da Corrida 165)

### 2.1 O que está instalado e pronto

- **Último pacote:** 3.310.1.0 (SHADER.2), em `driver/wdk/package/output_lab/`.

  | Artefato | SHA-256 |
  |---|---|
  | `bc250mcdm.sys` assinado | `AE984EFCDB65AD75F4ABA46B6B9566391249D24497C56BF29DCD073B58CC6D78` |
  | `bc250umd.dll` | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
  | `bc250mcdm.inf` | `271291C0F38BE797131BE149BE7D68E45CE1A46A6A16CE437C41F73BC7F7E73F` |
  | `bc250mcdm.cat` | `E202FE0E5CCD49A8DEC4F807A8E6E00DAE664ECD29693330B98278FCF31FE0FF` |

- **KMD:** o código não muda desde a DX.97 (3.308); só foi reassinado.
- **DriverVer fonte** em `driver/wdk/package/bc250mcdm.inf`: `3.310.0.0`. **O próximo candidato precisa subir para `3.311.0.0`** antes de empacotar, senão dois artefatos diferentes carregam a mesma versão.
- **Preparo que fixa os hashes:** `tools/prepare-corrida162-shader2.ps1`.
- **Runners existentes:**

  | Runner | Uso |
  |---|---|
  | `tools/EXECUTAR_CORRIDA162_SHADER2_600S.cmd` | harness aos 30 s |
  | `tools/EXECUTAR_CORRIDA163_SHADER2_SEM_HARNESS_600S.cmd` | sem harness |
  | `tools/EXECUTAR_CORRIDA164_KD_COM_HARNESS_600S.cmd` | com KDNET, não usar mais |
  | `tools/EXECUTAR_CORRIDA165_MENU_ATIVIDADE_MINIMA_600S.cmd` | sem harness, operador parado |

- **Estado da máquina ao escrever:** BasicDisplay; fusível disparado pela trava da 165. O boot está sem `DEBUG` (o Daniel desligou o KDNET antes da 163).

### 2.2 A trava dura: o que se sabe

| Fato | Confiança | Fonte |
|---|---|---|
| A máquina inteira congela: tela preta, sem BugCheck mesmo depois de 48 min, sem WHEA | P0 | doc 505 (Corrida 157) |
| A VRAM perde conteúdo durante a trava, em proporção ao tempo travado (o controlador de memória para de atualizar) | P0 | doc 506, memória `bc250-migalhas-decaem-na-trava` |
| Ctrl+Scroll Lock (crash por teclado) não funciona na trava | P0 | Corrida 157 |
| Ctrl+Break no WinDbg (KDNET) **não para a máquina na trava**; no controle, com a máquina sã e o mesmo boot, funciona | P0 (controle P2, relato) | doc 518 §5, memória `bc250-kd-nao-para-a-trava-dura` |
| **As 19 travas das corridas de 27–28/09 caíram todas entre 27 e 196 s depois do bind.** As 8 limpas foram a ~600 s. Nenhuma travou depois de 200 s | P0 no corpus | doc 518 §6; `tools/mine-bc250-freeze-corpus.py` |
| Chance constante por FLUSH_TLB: **refutada**. As limpas fazem ~60–70 mil FLUSH_TLB em 200–600 s cada (~1,1 milhão no total) sem travar | P0 | doc 518 §6 |
| O volume de paginação nos primeiros 200 s é parecido em limpas e travadas (1–2,8 GB de FILL) | P0 | doc 518 §6 |
| O harness D3D11 **não é** o gatilho: a 163 travou sem ele | P0 | doc 518 §6 |
| Abrir processos **não é suficiente**: nas limpas, processos nasceram depois de 200 s sem travar (StartMenu aos 523 s, SearchApp aos 247 s, GPUQuery aos 390 s) | P0 | doc 518 §7 |
| Nenhum tipo de linha do log do KMD distingue as 400 linhas antes da trava das janelas das limpas no mesmo intervalo (maior contraste: `[DX93_ALLOC_MAP]`, 0,68 contra 0,38) | P0 (estatística fraca, n=19) | `mine-bc250-freeze-corpus.py --tags` |
| **Assinatura das migalhas, sempre a mesma:** última operação da máquina = `SDMA_REG_READ 0xFC4` concluída (CPU00, em geral); outra CPU em `PAGING_DDI detalhe=0xC` (FLUSH_TLB) na fase **ANTES** (acesso não concluído) | P0 | docs 511, 518 |
| `0xFC4` é deslocamento na janela de MMIO do SDMA (`BC250_SDMA0_MMIO_WINDOW_START = 0x3000`): o registrador é **`0x3FC4` = `HDP_READ_CACHE_INVALIDATE`**. É o read-back da invalidação em `Bc250SdmaPresentHdp`, que roda **a cada iteração** dos laços de espera do SDMA. Ser a última é esperado só pela frequência | P1 | `driver/wdk/bc250_sdma_present.c:70-137` |
| Já descartados como gatilho: toggle de `HDP_MISC` (158), leitura de tabela de página pela CPU (159, DX.97), mapeamento além da BAR (nunca rodou), Seg=2 cru (nenhuma PTE), mensagens ao SMU (o driver não manda), troca do VMID8 com IB em voo (DX.95) | P0/P1 | docs 510, 511, 515 |
| Estatística recente: 13 travas em 15 corridas desde a FPS.1 | P0 | BASE |

**Leitura atual (H):** o gatilho é algo **próprio da fase de partida** (os primeiros ~3 min do desktop sob o nosso driver) que trava a plataforma abaixo do Windows, com uma CPU presa num acesso que não completa. A investigação por software **dentro** da máquina travada está esgotada. O que resta:
- corrigir defeitos reais de coerência e memória **antes** de testar;
- bissecção por ensaio, com uma variável por corrida;
- as migalhas.

### 2.3 Desktop e desempenho

| Item | Estado | Fonte |
|---|---|---|
| Desktop do DWM na tela pelo nosso driver | P0 (corridas 134/136, 600 s em várias) | BASE |
| Menu Iniciar: shader traduz (`Buffer<T>` em `t0`), descritor `SRV_BUFFER=1`, 2 draws gravados, Present `hr=0`, trabalho GFX aposentado | P0 (Corrida 165) | doc 518 §7 |
| Menu Iniciar **visível** | ❌ a trava veio ~0,1 s depois do primeiro Present do menu, e o Daniel não viu o menu | doc 518 §7 |
| FPS do harness | ~1,6 FPS; `pfnPresentCb` ~445 ms por quadro, dominado por paginação (VRAM útil ~245 MB) | docs 497, 511 |

---

## 3. Procedimentos padrão

### 3.1 Montar um candidato (IA)

1. Faça a mudança com **uma variável** por candidato, sempre que a corrida for julgar uma hipótese.
2. Crie um gate em `tools/audit-bc250mcdm-safety.py` que trave a mudança, e **teste por mutação**: reverta a mudança num arquivo temporário e confirme que o gate reprova.
   - Veja `audit_shader2_srv_buffer_is_bound_and_named` como modelo.
   - O padrão de script de mutação: copiar o arquivo, trocar a string, rodar a auditoria, restaurar.
3. Suba o DriverVer fonte em `driver/wdk/package/bc250mcdm.inf` (`3.310.0.0` → `3.311.0.0`, e assim por diante).
4. Build e testes:
   ```
   cmd /c driver\umd\build-umd.bat
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
   python tools\audit-bc250mcdm-safety.py
   ```
   O `build-umd.bat` roda as suítes da UMD. O `build-bc250mcdm-lab-bringup.ps1` compila o SYS de laboratório (e roda o `build-contract`).
5. Empacote e audite:
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -ArmGfxWaveProbe -IAcceptSelectorWritesDuringTdr
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-lab-package.ps1
   ```
   **Nunca aceite pacote sem a auditoria PASS logo depois.** Uma cópia falha já deixou pacote inconsistente.
6. Confira **por string** que os binários assinados contêm o código novo: `grep -a -c "<string nova>" driver/wdk/package/output_lab/bc250mcdm.sys`, e o mesmo para `bc250umd.dll`.
7. Crie `tools/prepare-corridaNNN-<nome>.ps1`, copiando o `prepare-corrida162-shader2.ps1` e trocando os quatro hashes, o DriverVer do manifesto e os textos.
8. Crie `tools/EXECUTAR_CORRIDANNN_<nome>_600S.cmd`, copiando um runner existente. As variáveis de ambiente úteis:
   - `BC250_SKIP_D3D11_HARNESS=1`: sem harness;
   - `BC250_D3D11_START_DELAY=30`: harness aos 30 s;
   - `BC250_WATCHDOG_SECONDS=600`;
   - `BC250_ENABLE_HANG_ETW=1`;
   - `BC250_CAPTURE_MONITOR_EDID=1`.

   **`set` de variável que o preparo lê tem de vir antes da chamada do preparo.**
9. Rode o preparo em modo leitura (`powershell -File tools\prepare-corridaNNN-....ps1`): ele não faz bind e tem de terminar em EXIT 0.
10. Escreva o doc da faixa (`docs/5NN_...md`) com a mudança, as provas offline, os hashes e **o critério da corrida escrito antes** (Lei 10: assinatura separada da implicação, desfecho E, e desfecho parcial não conta).

### 3.2 A corrida (o Daniel executa)

Passe ao Daniel, nesta ordem, em prompt elevado:
```
C:\Users\Daniel\Desktop\NamixWDDM\tools\BC250_RECOVERY.cmd
powershell -ExecutionPolicy Bypass -File C:\Users\Daniel\Desktop\NamixWDDM\tools\inspect-boot-fuse.ps1 -Reset
(boot frio com o monitor ligado)
C:\Users\Daniel\Desktop\NamixWDDM\tools\EXECUTAR_CORRIDANNN_....cmd
```
Diga o que ele deve fazer na tela e lembre o reset em 10–15 s.

### 3.3 Análise depois da corrida (IA)

1. **Achar a pasta:** a mais recente em `docs/evidence/*-lab-binding`. Horários de boot e trava:
   ```
   Get-WinEvent -FilterHashtable @{LogName='System'; Id=6005,6008,41,1074; StartTime=(Get-Date).AddHours(-2)}
   ```
   O ID 41 marca reinício sem desligamento limpo.
2. **Recuperar o log do KMD da corrida.** Quando a máquina trava, o watchdog morre e a pasta fica truncada (`display-log-this-bind.txt` é um retrato de ~15 s). O log vivo `C:\BC250_DISPLAY_LOG.txt` é cumulativo. Leia o `start_offset` em `display-log-baseline.json` da pasta e copie do offset até o fim para `display-log-this-bind-RECUPERADO.txt`, com SHA-256.
   - A partir da **segunda** linha `AddDevice ENTER`, o conteúdo já é o boot seguinte (migalhas e fusível).
   - O binder também faz isso sozinho no bind seguinte (`tail-recovery-console.txt`).
3. **Duração até a trava:** a diferença de QPC (10 MHz) entre a primeira e a última `qpc=` antes do reboot. Ou rode `python tools/mine-bc250-freeze-corpus.py`, que atualiza a tabela de todas as corridas.
4. **Migalhas:** no boot seguinte, as linhas `[DX66_MIGALHA]`. `ANTERIOR PRESENTE` significa que sobreviveram; a linha `<- ULTIMA DA MAQUINA` e as `fase=ANTES(acesso NAO concluido)` são o que importa. `ANTERIOR AUSENTE` significa reset tardio: a VRAM decaiu.
5. **Logs da UMD:** um arquivo por processo:
   - `C:\Users\Daniel\AppData\Local\Temp\BC250_UMD_LOG_<pid>.txt` (Explorer, harness, navegador etc.);
   - `C:\Users\Daniel\AppData\Local\Packages\<pacote>\AC\Temp\BC250_UMD_LOG_<pid>.txt` (apps isolados: StartMenuExperienceHost, SearchApp, ShellExperienceHost). O binder copia esses para `umd-logs-appcontainer\` **só se chegar ao fim**; na trava, copie à mão para a pasta de evidência;
   - `C:\Windows\Temp\BC250_UMD_LOG_*` (dwm.exe): cópia elevada do Daniel.

   **Os arquivos são cumulativos por PID:** um PID reaproveitado mistura corridas. Filtre pela hora.
6. **Shaders recusados:** `BC250_SHADER_FAIL_<pid>_<hash>.bin` (formato BCSF, doc 517) no mesmo `%TEMP%` do processo, mais a linha `SHADER_TRANSLATOR: vkd3d resultado=... mensagens=[...]` no log. Desde a SHADER.2, o `.bin` é gravado uma vez e forçado ao disco.
7. Aplique o critério escrito antes, desfecho por desfecho.

### 3.4 Relato ao Daniel (obrigatório, `CLAUDE.md`)

1. Data e hora, candidato e pasta de evidência.
2. Os cinco pontos:
   1. houve avanço?
   2. sucessos, com métricas;
   3. falhas;
   4. veredito frente ao critério;
   5. recuperação e pendências, incluindo o que segue **NÃO VALIDADO EM HARDWARE**.
3. Rótulos P0/P1/P2/H.
4. A tabela ✅🟡❌🔜 do desktop mínimo (memória `bc250-tabela-checklist-apos-corrida`).
5. Linguagem simples: o Daniel pede explicações diretas.
6. Atualize a `BASE_DE_AVANCO.md` (uma linha no topo com link) e o doc da faixa.

### 3.5 Como julgar uma correção da trava (estatística)

- **Taxa-base recente: ~87% de travas (13 em 15).** Uma corrida limpa **não prova nada**: acontece ~1 vez em 8 sem mudança nenhuma.
- **Duas limpas seguidas** com o mesmo candidato: ~2% de chance sob a hipótese nula. Esse é o mínimo para chamar de "a trava mudou". **Três** é o que se deve pedir antes de declarar causa.
- **Uma trava** com o candidato já basta para dizer que a mudança **não é suficiente**, mas não prova que ela seja inútil.
- Toda corrida deve rodar **600 s**. Como as travas caem antes de 200 s, uma corrida que passa dos 200 s já é quase certamente limpa. Mesmo assim, deixe ir até o fim para medir o resto.
- Mantenha o **roteiro de uso igual** entre as corridas que se comparam (mesmo harness ligado ou desligado, mesmo que o operador faça). Na 165 o operador ficou parado e só clicou no menu; é o roteiro mais controlado.

---

## 4. Tarefas

### Bloco A: trava dura na partida (prioridade máxima; bloqueia todo o resto)

---

#### T1: o FLUSH_TLB não drena o cache de escrita do HDP (constante com o nome errado)

> **Atualização 28/09 (doc 519, candidato 3.311.1.0 pronto, NÃO VALIDADO EM HARDWARE):** a análise foi além do que está abaixo.
> - O `0x4A80` que este plano chama de flush do HDP é **`SDMA0_UTCL1_INV0`** (P1). O driver o zerava em todo laço de espera, e o flush de escrita real nunca aconteceu em lugar nenhum.
> - O flush real é `0x385C` (P1, base NBIO P0), confirmado pela leitura de `0x378C`.
> - O "P0 de que a escrita em `0x4A80` não trava sozinha" (mais abaixo) vale para `UTCL1_INV0`, não para o HDP.
> - A migalha "CPU04 em FLUSH_TLB, fase ANTES" da 165 não se sustenta (4.385 ENTER = 4.385 LEAVE).
> - Próximo passo: Corrida 166 com `tools/EXECUTAR_CORRIDA166_HDP1_600S.cmd` e o critério do doc 519 §5.

**Objetivo:** garantir que as tabelas de página escritas pela CPU pela BAR cheguem à VRAM **antes** de a GPU ser mandada relê-las.

**Por quê:**
- **P1, código.** Em `bc250_active_miniport.c`, `Bc250ActiveFlushMappedGfxhubTlb` (~linha 17201), o bloco DX.14 (~17257–17303) diz no comentário: *"Primeiro drenar, depois invalidar"*.
  - Mas ele escreve em `BC250_ACTIVE_REG_HDP_MEM_COHERENCY_FLUSH_CNTL`, definida na ~linha 15163 como **`0x00003FC4`**.
  - O catálogo (`docs/03_CATALOGO_DE_REGISTRADORES.md`, P1 pelo `hdp_5_0_0`) diz que `0x3FC4` é **`HDP_READ_CACHE_INVALIDATE`**: invalidação de leitura, não flush de escrita. O próprio comentário da constante admite que outros arquivos a chamam de `HDP_READ_CACHE_INVALIDATE`.
  - **O nome está errado, e o flush de escrita nunca acontece nesse caminho.**
- **P1, upstream.** O Linux (`amdgpu_vm_cpu.c`, `amdgpu_vm_cpu_commit`) chama `amdgpu_device_flush_hdp` depois de escrever tabelas de página pela CPU e antes de invalidar a TLB. É a escrita no registrador de flush de coerência do HDP (NBIO, via página remapeada), não a invalidação de leitura.
- **Por que casa com a trava (H):**
  - na partida, o VidMm cria e preenche muitas tabelas de página novas (VRAM enchendo pela primeira vez);
  - se a GPU buscar uma PDE/PTE velha, ela traduz para um endereço arbitrário;
  - um acesso da GPU a endereço físico inválido pode travar o data fabric, e a trava abaixo do SO casa com isso;
  - no regime normal, as tabelas já existem e são só atualizadas, o que dá janela menor.

  **É hipótese.** O projeto já mediu esse mecanismo (CPU escreve, GPU lê bytes velhos) numa fence em 10/09 (`FENCE_VISIBILIDADE antes_hdp=43DAFFFE depois_hdp=00001304`) e numa fault de GCVM na Corrida 60 (comentário no próprio código).
- O caminho do SDMA (`Bc250SdmaPresentHdp`, `bc250_sdma_present.c:125-137`) **já escreve** `0x4A80=0` com read-back em toda espera. Isso roda milhares de vezes por corrida sem incidente (P0 de que a escrita não trava sozinha). Esse flush acaba acontecendo **por acaso** quando uma espera de SDMA roda entre a escrita da tabela e o uso dela, mas não há ordem garantida.

**Como fazer:**
1. **Proveniência primeiro (Lei 7).**
   - `HDP_MEM_COHERENCY_FLUSH_CNTL` em `0x4A80` é **P2** no catálogo (linha ~769): o upstream usa a página remapeada, e o offset nativo não está num header local.
   - Busque em fonte real: `drivers/gpu/drm/amd/amdgpu/nbio_v2_3.c` (`nbio_v2_3_hdp_flush`, `nbio_v2_3_remap_hdp_registers`) e `drivers/gpu/drm/amd/include/asic_reg/nbio/nbio_2_3_offset.h` (`mmBIF_BX_PF_HDP_MEM_COHERENCY_FLUSH_CNTL`, com o `BASE_IDX` do NBIO), no GitHub do Linux ou do ROCm.
   - Confirme se `0x4A80` é o registrador nativo em bytes, com a base do NBIO deste chip. Registre a fonte no catálogo e suba o nível.
   - Se não fechar, use a mesma sequência que o caminho do SDMA já usa (P0 de execução segura), declarando a proveniência P2.
2. No `Bc250ActiveFlushMappedGfxhubTlb`, **antes** das invalidações por VMID:
   - escrever `0` no registrador de flush de coerência (`0x4A80`) e ler de volta (flush de escrita), como faz o upstream;
   - manter, se quiser, a invalidação `0x3FC4` depois, porque é inofensiva e não é o flush.
3. **Renomeie a constante** para o nome real (`BC250_ACTIVE_REG_HDP_READ_CACHE_INVALIDATE`) e crie uma nova para o flush, com a proveniência no comentário.
4. Verifique se há outros lugares que escrevem tabela de página pela CPU sem flush depois:
   - `Bc250McdmBuildCpuPageTableUpdate` em `bc250_mcdm_callbacks.c:672` escreve as PTEs;
   - `[PDE_WRITE]` e `ActiveUpdatePT` no miniport.

   O modelo certo é: toda escrita de tabela pela CPU, depois flush de escrita, depois invalidação de TLB.
5. Gate `audit_t1_hdp_write_flush_before_tlb_invalidate`:
   - exige a escrita em `0x4A80` antes do laço `Bc250ActiveFlushVmidTlb`;
   - proíbe o nome `HDP_MEM_COHERENCY_FLUSH_CNTL` associado a `0x3FC4`.

   Teste por mutação.
6. Log: uma linha `[HDP_PT_FLUSH] OK` **amostrada** (não por FLUSH_TLB: log na thread de paginação trava o VidMm, memória `bc250-log-na-thread-de-paginacao`); falha continua logada sempre.

**O que esperar (critério para o doc da faixa):**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | Duas corridas seguidas de 600 s sem trava, roteiro da 165, com `IB EXECUTADO` e tripé fechando | O flush de escrita das tabelas era o gatilho, ou parte dele. Pedir a terceira para chamar de causa. |
| B | Trava como antes (mesma assinatura das migalhas) | O flush não basta; manter a correção (é defeito real, P1) e seguir para T2/T3. |
| C | Sem trava, mas surgem faults de GCVM, `IB_TRIPOD TIMEOUT` ou fence no poison | Coerência mudou de lado; investigar antes de continuar. |
| E | Fora do mapa | Descrever antes de julgar. |

**Pronto quando:** a proveniência está registrada, o gate passa com mutação, o candidato foi empacotado e a corrida foi julgada.

**Custo:** offline + 2 corridas (ou 1 se travar logo).

---

#### T2: operações de HDP a cada iteração de espera, em várias CPUs ao mesmo tempo

**Objetivo:** reduzir e serializar as operações de HDP feitas pela CPU nos laços de espera do SDMA, como o upstream faz.

**Por quê:**
- **P1, código.** `Bc250SdmaPresentHdp` (flush `0x4A80` + read-back, e invalidação `0x3FC4` + read-back) é chamado em pelo menos 11 pontos de `bc250_sdma_present.c` (linhas ~353, 483, 506, 545, 638, 744, 760, 776, 903, 927, 945). Vários ficam **dentro** de laços de espera (tripé do INDIRECT, SyncNop, blt), e o `Bc250SdmaHdp` em `bc250_sdma_ring_work.c` faz o mesmo.
- **P0, migalhas.** No dump da 163, várias CPUs diferentes (CPU00, 02, 05, 06, 08, 09, 12, 14, 15) têm `SDMA_REG_READ 0xFC4` como último acesso. A invalidação do HDP roda **em paralelo** em muitas CPUs, enquanto outra CPU está no meio de um acesso da paginação.
- **P1, upstream.** O Linux não faz isso em laço de espera:
  - a fence e o rptr ficam em **memória do sistema** (writeback em GTT), e a CPU lê sem passar pelo HDP;
  - a invalidação do HDP só é feita antes de a CPU ler dados escritos pela GPU na VRAM, e o flush só depois de a CPU escrever na VRAM.
- **Relato externo (P2, doc 509 §4):** outro driver Windows da BC-250 trata a faixa BAR5 `0x3400–0x8100` como zona de congelamento para **escrita**, e `0x3FC4`, `0x3FCC` e `0x4A80` estão nela.
- **Por que casa com a partida (H):** é na partida que há mais submissões de paginação em paralelo, e portanto mais esperas e mais acessos simultâneos ao HDP.

**Como fazer:**
1. **Medir antes de mudar (offline):** contar quantas vezes `Bc250SdmaPresentHdp` é chamado por submissão e por segundo. Com um contador interlocked logado de forma amostrada, isso sai na próxima corrida, que pode ser a mesma de T1 sem mudar o comportamento. Ou estimar pelo número de polls: `IB_TRIPE_VEREDITO` já registra polls.
2. **Serializar:** um spinlock global (ou `InterlockedCompareExchange` num flag) em volta da sequência de HDP, para que nunca duas CPUs a façam ao mesmo tempo. É a mudança mínima, com uma variável.
3. **Reduzir:** fazer flush/invalidação só quando a leitura por MMIO do RPTR já casou com o alvo (antes de ler a fence na VRAM), e não a cada iteração. O histórico do projeto registra que "o flush já dispara no casamento de RPTR" em parte dos caminhos (CLAUDE.md, corrida 10/09 12:16): confirme ponto a ponto.
4. **Estrutural (maior, separado):** mover a fence e o writeback de RPTR do SDMA para memória do sistema, como o upstream, eliminando HDP do caminho de conclusão. Depende de ter memória do sistema mapeada à GPU com proveniência (ver M1, doc 492).
5. Um candidato por variável: primeiro (2), depois (3). Não junte com T1 na mesma corrida se T1 ainda não foi julgado.

**O que esperar:**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | Duas limpas seguidas, tripé intacto | A concorrência no HDP era o gatilho. |
| B | Trava igual | Não é a concorrência do HDP; manter só se não custar desempenho. |
| C | Sem trava, mas fences no poison ou `IB_TRIPOD TIMEOUT` | A redução quebrou a visibilidade; voltar ao flush por iteração nesse ponto. |
| E | Fora do mapa | Descrever antes de julgar. |

**Custo:** offline + 1–2 corridas por variável.

---

#### T3: conferir os achados "R" da auditoria interna (doc 509 §3), um a um

**Objetivo:** transformar cada achado relatado e **não conferido** em confirmado ou refutado, com fonte, e corrigir os confirmados.

**Por quê:** o doc 509 listou divergências do upstream que ninguém conferiu. Várias são mecanismos conhecidos de trava de plataforma. Estão em ordem de suspeita (H):

| # | Achado (doc 509 §3) | Por que é suspeito | Como conferir |
|---|---|---|---|
| T3.1 | A mesma VRAM mapeada **write-combined** (framebuffer inteiro, 256 MB) e **uncached** (migalhas, anel SDMA, anel GFX): aliasing de tipo de memória | Intel e AMD declaram aliasing WC/UC **não suportado**, podendo dar comportamento indefinido e até machine check ou trava. É um mecanismo clássico de trava dura. | Listar todo `MmMapIoSpace`/`MmMapIoSpaceEx` e as flags (`PAGE_WRITECOMBINE`/`PAGE_NOCACHE`) sobre BAR0 em `driver/wdk/`, e montar a tabela faixa → tipo. Se houver sobreposição, corrigir mapeando cada faixa uma vez só com um tipo, ou fazendo as migalhas e os anéis usarem sub-faixas do mesmo mapeamento. |
| T3.2 | Sem semáforo no engine de invalidação 17 do GFXHUB | O patch gfx1013 v33 do Linux adiciona o semáforo **para este chip** (13FE). Invalidação de TLB concorrente sem semáforo é um problema conhecido. | Achar o patch (lista amd-gfx, "gfx1013" / "cyan skillfish" / "vm_inv_eng semaphore") e comparar com `Bc250ActiveFlushVmidTlb`. Implementar o protocolo `*_INVALIDATE_ENG17_SEM` com timeout, só com offsets do `gc_10_1_0_offset.h` (memória `bc250-linux-ref-tem-offsets`). |
| T3.3 | `SDMA0_UTCL1_CNTL.FORCE_INVALIDATION` ligado por RMW em todo FLUSH_TLB e nunca desligado | Estado fora do upstream, deixado permanente. | Ler o código e o upstream `sdma_v5_0.c`; ler o valor de volta num probe somente leitura. |
| T3.4 | `GCVM_L2` inteiro invalidado a cada publicação de WPTR do MEC (upstream: só na init) | Invalidação massiva frequente, concorrente com a paginação. | Localizar a escrita, contar a frequência e comparar com `gfxhub_v2_0.c` local (`inc/linux_ref/gfxhub_v2_0.c`). |
| T3.5 | `GCVM_L2_CNTL` escrito como constante `0x00080E01`, zerando `EFFECTIVE_L2_QUEUE_SIZE` e outros campos | Muda a fila do L2 do VM. | Comparar com `gfxhub_v2_0_init_cache_regs` (local) e aplicar a fórmula SOC15 (Lei 6). |
| T3.6 | Troca de raiz do VMID7 com IB de outro contexto em voo | Mesma classe da falha que a DX.95 corrigiu no VMID8. | Ler `Bc250ActiveSetRootPageTableObject` e os sites de troca do VMID7; aplicar a mesma regra da DX.95. |
| T3.7 | Leitura do registro de objeto sem lock no submit em DISPATCH (registro rasgado dá raiz errada) | Raiz errada é endereço arbitrário para a GPU. | Ler o caminho do submit e pôr lock ou cópia consistente. |
| T3.8 | Laços longos com spinlock em DISPATCH (blt até 10.000 polls; SyncNop/Indirect até ~1 s) | Já é dívida declarada (A15). Não explica trava sem 0x133, mas soma à concorrência de T2. | Medir o máximo real pelo log (polls no `IB_TRIPE_VEREDITO`). |
| T3.9 | E/S de disco de log com o `ObjectLock` exclusivo em DDIs de aplicação | Latência e ordem; ver T5. | Ler. |

**Como fazer:** para cada linha:
1. ler o código;
2. achar a fonte upstream (local em `inc/linux_ref/` e `referencias/`, ou na web com link);
3. classificar como **Confirmado**, **Refutado** ou **A conferir**, com P0/P1/P2;
4. escrever o resultado num doc novo, ao estilo do doc 510.

Corrigir os confirmados em candidatos separados, **uma variável por corrida**, começando por T3.1 e T3.2.

**O que esperar:** a maioria vira P1 (divergência real) sem prova de que causa a trava. Cada correção confirmada é testada como em T1: duas limpas seguidas, ou trava igual.

**Custo:** a conferência é offline. Cada correção que muda hardware custa 1–2 corridas.

---

#### T4: fechar os defeitos confirmados da auditoria (doc 509 §2 e doc 510)

**Objetivo:** corrigir defeitos já confirmados que não mudam o comportamento do hardware no caminho normal.

| # | Defeito | Onde | Correção |
|---|---|---|---|
| 4.1 | `Bc250DisplayLog` dentro do callback em DIRQL (`Bc250SynchronizedVsyncCallback`, a cada 60 vsyncs), e o spinlock do anel de log acima de DISPATCH | `bc250_display_ddi.c` (~1482, ~1226) | Tirar o log do DIRQL: gravar contador e logar no DPC. |
| 4.2 | `Bc250ActiveReadVramU64` mapeia `BAR0 + VramOffset` sem limite (offset vindo de PDE) | `bc250_active_miniport.c` | Recusar offset ≥ tamanho da BAR, com log. Não rodou nas corridas (0 `[PT_READ]`), mas é mina. |
| 4.3 | Falha de FLUSH_TLB vira `STATUS_SUCCESS` antes do VidMm | `bc250_mcdm_callbacks.c` (~1578) | A DDI só admite SUCCESS/ALLOCATION_BUSY/INSUFFICIENT_DMA_BUFFER, então não dá para devolver erro. Tornar a divergência **observável** (contador e log amostrado) e evitar chegar ao erro. |
| 4.4 | O dreno de fences devolve `FALSE` depois de tirar entradas, quando uma geração diverge: entradas nunca notificadas | `bc250_active_miniport.c` (~11067) | Notificar o que já foi tirado antes de sair. |
| 4.5 | FENCE de 64 bits num único pacote de 5 dwords | `bc250_sdma_packet_builder.c`, `Bc250SdmaEmitFence64` | Dois FENCE de 32 bits (parte baixa e alta), como o upstream. Com valor < 2³² o bug é latente. |
| 4.6 | Segmento 2 codificado como endereço físico cru | encoder de PTE | Recusar Seg=2 com log (nenhuma PTE Seg=2 medida, mas é mina). |

**Como fazer:** um candidato pode juntar 4.1 a 4.6 **se** a corrida não estiver julgando outra hipótese de trava ao mesmo tempo. Se estiver, faça-os offline e empacote junto com o candidato seguinte já julgado. Cada item leva o seu gate e o seu teste de contrato (`driver/tests/`).

**O que esperar:** nada visível no caminho normal. Se a trava sumir só com T4, trate como desfecho E (fora do mapa) e repita.

**Custo:** offline; nenhuma corrida própria.

---

#### T5: diminuir o log do KMD (hoje ~4 GB, gravado linha a linha)

**Objetivo:** tirar a instrumentação do caminho de tempo da partida sem perder evidência.

**Por quê:**
- **P0:** `C:\BC250_DISPLAY_LOG.txt` passou de 4,2 GB.
- Na 160, o processo System escrevia ~24 MB/s em disco (ETW ~2 MB/s, mais o flush do log por linha, H).
- A partida é a fase de **mais log**, e é quando a trava acontece. O projeto já teve travas causadas pela instrumentação (DX.83, log com E/S de arquivo na thread de paginação: memória `bc250-log-na-thread-de-paginacao`; KD com DbgPrint: memória `bc250-kd-e-dbgprint-derrubam-a-paginacao`).

**Como fazer:**
1. Achar o escritor do log (`Bc250LogRingAppend` e a thread que grava o arquivo, em `bc250_display_ddi.c`) e medir quantas linhas por segundo saem na partida. Use o corpus: linhas por segundo pelo QPC.
2. Trocar o flush por linha por flush por bloco (ex.: a cada 64 KB ou 250 ms), **mantendo** as migalhas DX.66 (VRAM) como rede na trava.
3. Rebaixar as classes mais volumosas da partida (`[PT_WALK]`, `[PT_RANGE *]`, `IB_TRIPE_*` por submissão, `BC250_HANG ... PAGING_DDI` ENTER/LEAVE) para amostradas, **exceto** nas assinaturas de falha.

   Cuidado: ausência de linha amostrada não prova nada (memória `bc250-log-de-display-e-amostrado`), e o critério da corrida tem de ser respondível com o log reduzido.
4. Rotacionar ou truncar o log cumulativo com cuidado: o `recover-bc250mcdm-lost-tail.ps1` e o baseline usam offsets. Se mudar o formato, ajuste o binder.

**O que esperar:**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | Duas limpas seguidas | A instrumentação fazia parte do gatilho (mudança de tempo). |
| B | Trava igual | Não era o log; ganho de desempenho e de disco continua valendo. |
| E | Fora do mapa | Descrever antes de julgar. |

**Custo:** offline + 1–2 corridas. **Não misture com T1/T2 na mesma corrida.**

---

#### T6: ensaio de plataforma, sem os estados de economia da CPU e do fabric

**Objetivo:** separar "defeito do nosso driver" de "transição de energia da plataforma sob a nossa carga".

**Por quê (H, doc 507):** o controlador de memória para durante a trava. Transições de energia do fabric ou da memória (C-states, DF C-states, DPM de FCLK pelo SMU) são o candidato 1 abaixo do SO. O driver não manda mensagem ao SMU (P1 + P2), então não somos a origem de uma mensagem, mas a carga pode disparar a transição.

**Como fazer (o Daniel executa; a IA só escreve os comandos):**
1. Pelo Windows, prompt elevado:
   ```
   powercfg /setacvalueindex scheme_current sub_processor IDLEDISABLE 1
   powercfg /setactive scheme_current
   ```
   Para reverter depois:
   ```
   powercfg /setacvalueindex scheme_current sub_processor IDLEDISABLE 0
   powercfg /setactive scheme_current
   ```
2. Ou, pela BIOS (P3.00 na placa do Daniel): anotar o que existe em Advanced / AMD CBS (Global C-state Control, DF C-states, Power Supply Idle Control) e desligar **uma** opção por vez. O Daniel já disse que "não existe essa função na bios" para pelo menos uma opção: confirme com ele qual.
3. Mesmo pacote, mesmo roteiro (o da 165), sem mudar código.

**O que esperar:**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | Duas limpas seguidas | A transição de energia é o gatilho; a correção é de configuração ou firmware, e o driver precisa evitar a condição. |
| B | Trava igual | Descarta os C-states do processador; restam fabric, memória e padrão de tráfego (T1–T3). |
| E | Fora do mapa | Descrever antes de julgar. |

**Custo:** 1–2 corridas, sem código. Pode ser feito a qualquer momento, e é barato.

---

#### T7: DX.98, tabelas de página escritas pela GPU (SDMA) em vez da CPU pela BAR

**Objetivo:** tirar a CPU do caminho de escrita das tabelas de página, como o Linux e o macOS fazem quando a BAR é pequena.

**Por quê:**
- **P1:** o `amdgpu_vm.c` do Linux (e o X6000 do macOS, doc 512) atualiza as tabelas de página via SDMA quando a BAR não cobre a VRAM.
- Nós declaramos `DXGK_PAGETABLEUPDATE_CPU_VIRTUAL` (`bc250_mcdm_callbacks.c:2728`), e a CPU escreve pela BAR (ver T1).
- Eliminaria a classe inteira de problemas de coerência CPU→GPU nas tabelas.

**Bloqueio (Lei 7):** o rascunho (doc 514) depende de um pacote do SDMA sem fonte local. Antes de qualquer código:
1. Achar em fonte real os pacotes que o `amdgpu_vm_sdma.c` usa para escrever PTEs (`amdgpu_vm_sdma_update` → `amdgpu_vm_write_pte`/`set_pte_pde`, com `SDMA_OP_WRITE`, `SDMA_OP_PTEPDE` e `SDMA_OP_COPY` em `sdma_v5_0.c`: `sdma_v5_0_vm_write_pte`, `sdma_v5_0_vm_set_pte_pde`, `sdma_v5_0_vm_copy_pte`).
2. Conferir o encoding contra `navi10_sdma_pkt_open.h` (upstream) e registrar a proveniência no catálogo.
3. Só então implementar o ramo `DXGK_PAGETABLEUPDATE_GPU_VIRTUAL` no `BuildPagingBuffer` (contrato P1 no doc 514 §1), mantendo `CPU_VIRTUAL` onde o VidMm força (tabelas do processo de paginação).

**O que esperar:** é mudança grande. Faça **depois** de T1–T3, porque se T1 resolver, T7 vira otimização. Critério no molde da T1, mais tripé por PTE escrita (prova de bytes na tabela lida de volta).

**Custo:** muito offline + 2–3 corridas.

---

#### T8: tabela de objetos lota (teto de 4096)

**Objetivo:** saber quem enche a tabela de objetos de GPUVA e se isso acontece na partida.

**Por quê (P0, Corrida 160):** `NO_SPACE` aos 229 s. O teto foi elevado a 4096 na DX.94 (`BC250_GPUVA_SPACE_MAX_RANGES`, `driver/core/bc250_gpuva_space_contract.h`); o journal fica em 1024. Uma inserção recusada deixa a vista de software divergente do hardware (doc 509 §2.3 e 510 #9).

**Como fazer:** no ponto que devolve NO_SPACE, logar (uma vez por minuto) a ocupação por processo e por classe de alocação (o `DX93_ALLOC_MAP` já liga handle, classe e token). Conferir no corpus quantas corridas chegam ao NO_SPACE antes de travar: `grep NO_SPACE` nas pastas.

**O que esperar:** um dono dominante (provavelmente os BOs da RADV, como na atribuição da 150) ou crescimento sem liberação. Isso orienta M1b/M2 (T11).

**Custo:** offline; a medição vem de graça na próxima corrida.

---

### Bloco B: desktop

#### T9: menu Iniciar visível

**Objetivo:** ver o menu Iniciar abrir e fechar na tela, pelo nosso driver.

**Estado:** o caminho de GPU passou (P0, 165); não chegou a aparecer por causa da trava.

**Como fazer:** depende do Bloco A. Enquanto a trava existir:
- em toda corrida, o operador clica no menu **nos primeiros 20–30 s de desktop** (as travas vêm antes de ~200 s e muitas vezes antes de 60 s);
- conferir no log do StartMenuExperienceHost (AC\Temp) `SRV_BUFFER=1`, `DRAW GRAVADO`, `Present ... hr=0x00000000`, e perguntar ao Daniel se o menu **apareceu**.

**O que esperar:** menu visível = ✅ na tabela, pelo relato do operador (pixel sem prova automática: Lei 2; se possível, criar prova de bytes no alvo do menu, lendo a textura de volta na UMD).

**Custo:** nenhuma corrida própria; vai junto com as corridas do Bloco A.

#### T10: escopo que a SHADER.2 deixou de fora e outros shaders recusados

**Objetivo:** achar o próximo recurso de shader que falta, antes que ele vire mistério.

**Como fazer:**
1. Em cada corrida, varrer **todos** os logs da UMD (Temp do usuário, AC\Temp de todos os pacotes, cópia do `C:\Windows\Temp`) por:
   - `vkd3d resultado=`
   - `falhou (status=`
   - `[VK_DESCRIPTOR] RECUSADO`
   - `SRV_BUFFER_`
   - `TIPO_DE_DESCRITOR_NAO_SUPORTADO`
   - `[VK_PSO] ... falhou`
2. Para cada mensagem nova do vkd3d: reproduzir offline com `fxc` (`C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\fxc.exe`) e o adaptador real (modelo `repro_ps89.c`, descrito no doc 518; o harness compila `driver/umd/bc250_vkd3d_shader_adapter.c` com os objetos da ponte e do parser de `driver/umd/output`), e corrigir.
3. Dívidas declaradas da SHADER.2 (doc 518 §2):
   - buffer escrito só pela GPU (CopyResource de GPU, stream output): o conteúdo sai do shadow de CPU;
   - storage/UAV (`Sampled=2`) é recusado;
   - deslocamento de texel buffer exige múltiplo de 16.

**Custo:** offline; a coleta vem com as corridas.

---

### Bloco C: desempenho (FPS ~1,6)

#### T11: mais VRAM útil (M1b/M2)

**Objetivo:** tirar a tempestade de paginação, que domina o tempo de quadro.

**Por quê (P0, docs 497/511):** `pfnPresentCb` ~445 ms por quadro, dos quais ~110–147 ms em FLUSH_TLB e ~265–330 ms no VidMm, com 22–27 IBs de paginação por quadro. A causa é a VRAM útil de ~245 MB. Menos paginação na partida também pode reduzir a exposição à trava (H).

**Como fazer (doc 496):**
- **M1b:** os BOs que a RADV pede como GTT vão hoje para a VRAM (o blob BCA1 não carrega o domínio). Honrar o GTT esbarra em três caminhos do KMD que usam endereço físico de VRAM:
  - o gate de envio gráfico;
  - o `COMPOSITE_BLT`;
  - o buffer linear do Present.

  Resolver os três exige inspeção por mapeamento de sistema, cópia por GPUVA e tipo de memória explícito.
- **M2:** VRAM além do BAR. Só ~248 MB dos BOs despejados são `CpuVisible=0`, então rende pouco sem ReBAR.
- **ReBAR:** a placa suporta 256 MB, 512 MB e 1 GB (P0, probe), mas a BIOS do Daniel **não tem a opção** (BASE, linha do doc 496).

**O que esperar:** menos operações de paginação por quadro e FPS subindo. Critério: IBs de paginação por quadro e `pfnPresentCb` medidos com `tools/analyze-fps3-retire.py`.

**Custo:** muito offline + várias corridas. **Só depois do Bloco A**, porque cada corrida hoje trava antes de medir FPS em regime.

#### T12: FPS.2 e FPS.3c

Seguem o plano dos docs 497–503: FPS.1 (commit do journal), FPS.3a/3b (conclusão por timer de alta resolução) e FPS.3c. Medição com `tools/analyze-fps3-retire.py` sobre a pasta de evidência. Mesma restrição: depois do Bloco A.

---

## 5. Ferramentas

| Ferramenta | Para quê |
|---|---|
| `tools/mine-bc250-freeze-corpus.py [--tags]` | Tabela de todas as corridas (trava/limpa, duração, FLUSH_TLB por janela, migalhas) e, com `--tags`, o enriquecimento de linhas antes da trava com o tempo casado. Rode depois de cada corrida. |
| `tools/analyze-fps3-retire.py` | Latência de conclusão SDMA/GFX, `pfnPresentCb`, quebra por quadro. |
| `tools/recover-bc250mcdm-lost-tail.ps1` | Recupera o trecho do log vivo de pastas truncadas (o binder já chama). |
| `tools/audit-bc250mcdm-safety.py` | Portões de segurança; todo candidato passa. |
| `tools/audit-bc250mcdm-lab-package.ps1` | Auditoria do pacote. |
| `tools/inspect-boot-fuse.ps1 [-Reset]` | Estado do fusível anti-boot-loop. |
| `tools/BC250_RECOVERY.cmd` | Volta ao BasicDisplay. |

---

## 6. Ordem recomendada e árvore de decisão

1. **T1** (flush de escrita do HDP no FLUSH_TLB). É defeito confirmado por leitura, a correção é pequena e encaixa no padrão "só na partida". **Primeira corrida.**
2. Em paralelo, offline: **T3** (conferência dos achados R, começando por T3.1 aliasing WC/UC e T3.2 semáforo do ENG17) e **T4** (defeitos confirmados).
3. Se T1 der B (trava igual), vá para a correção confirmada mais suspeita de T3 (provavelmente **T3.1** ou **T3.2**) e depois **T2** (serializar o HDP).
4. Se continuar travando, faça **T6** (plataforma), que é barato e não exige código.
5. **T5** (log) quando os candidatos de mecanismo acabarem, ou antes, se a conferência mostrar que o log é pesado na partida.
6. **T7** (tabelas pela GPU) se nada acima resolver, ou como otimização depois.
7. Com a trava resolvida (três limpas seguidas): **T9**, **T10**, **T11** e **T12**.

**Regra de ouro:** uma variável por corrida; critério escrito antes (Lei 10); duas limpas seguidas antes de dizer "mudou"; uma trava já diz "não basta".

---

## 7. Armadilhas conhecidas desta fase

- **Classes amostradas do log:** muitas tags (`[UpdatePT …]`, `[McdmCreateProcess]`, `[SdmaRetire]`) só gravam as primeiras N linhas. Ausência não prova nada, e presença no começo da corrida é esperada. Compare sempre no mesmo intervalo de tempo.
- **Logs da UMD por PID são cumulativos:** o mesmo PID em corridas diferentes cai no mesmo arquivo. Filtre pela hora.
- **Arquivo gravado segundos antes da trava sai zerado** (o tamanho persiste, o conteúdo não). Grave evidência crítica com `FlushFileBuffers`.
- **O menu Iniciar (AppContainer) grava em `...\Packages\<pacote>\AC\Temp`**, não no `%TEMP%` do usuário.
- **`0xFC4` nas migalhas é deslocamento dentro da janela SDMA (base 0x3000):** o registrador é `0x3FC4`.
- **O preparo recusa boot com `DEBUG`:** para usar o KD, `BC250_ALLOW_KERNEL_DEBUG=1` **antes** da chamada do preparo no runner.
- **`!` e `>` no `.cmd`:** `>` precisa de `^>` no `echo`.

---

## 8. Documentos desta fase

| Doc | Conteúdo |
|---|---|
| 496 | M1b: bloqueios, rota ReBAR + M2 |
| 497–498 | FPS.1 e Corrida 151 |
| 499–504 | Corridas 152–155, DX.94, DX.95, FPS.3b |
| 505–507 | Trava abaixo do Windows, VRAM decaindo, pesquisa de controlador de memória/SMU/PSP |
| 508–510 | Prompt de auditoria externa, auditoria interna, validação |
| 511 | Corrida 158, migalhas nítidas, FPS medido |
| 512 | Referências macOS/Linux: tabelas de página pela GPU |
| 513–515 | DX.97 e Corrida 159 |
| 514 | DX.98 (rascunho) |
| 516–518 | Corrida 160, SHADER.1, Corridas 161–165, SHADER.2, mineração do corpus, KD |
