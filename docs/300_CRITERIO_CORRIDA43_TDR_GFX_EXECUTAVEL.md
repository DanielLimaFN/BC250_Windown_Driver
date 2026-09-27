# 300 — Critério prévio da Corrida 43 — restart GFX executável e fences abortadas

> **Data da faixa offline:** 20/09/2026  
> **Candidato:** lab `3.201.1.0`, `DISPLAY_RENDER_LAB`, WDDM 2.7, schema 135.  
> **Pacote:** `driver/wdk/package/output_lab/`  
> **Estado:** assinado e auditado; **NÃO INSTALADO; NÃO VALIDADO EM HARDWARE**.

## 1. Por que não repetir a Corrida 42

A Corrida 42 provou o upload genérico de row pitch, mas o primeiro IB gráfico parou em
`RPTR=24/WPTR=32`. O restart então aceitou apenas readbacks de registrador como prova de que
o CP_RB0 voltara. Cinco ciclos depois, as pendências WDDM cresceram e o Windows terminou em
`VIDEO_TDR_FAILURE (0x116)`.

Repetir o mesmo binário não separaria nenhuma hipótese. O candidato 3.201.1.0 muda somente o
contrato de recuperação necessário para tornar o próximo resultado atribuível:

1. zera também o writeback de RPTR, não apenas o WPTR shadow;
2. remove committed e reservas interrompidos do rastreador sem emitir
   `DXGK_INTERRUPT_DMA_COMPLETED`;
3. devolve os pins GPUVA associados às entradas abortadas;
4. emite `NOP x4 + RELEASE_MEM` no CP_RB0 depois do reset e só aceita o restart quando
   `RPTR == WPTR == 12` e a GPU substitui o poison pela fence esperada;
5. mantém a admissão de trabalho fechada em qualquer falha;
6. faz o boot preamble falhar fechado quando sua própria fence/RPTR não conclui.

O MEC continua sendo validado apenas por readback nessa rotina. O log agora diz isso
explicitamente; não atribuir prova executável pós-reset ao MEC.

## 2. Etapa obrigatória antes do bind: dump da Corrida 42

O PDB privado exato do SYS `3.200.1.0` **não foi arquivado** e o arquivo de trabalho já foi
substituído pelo build seguinte. Portanto, não usar o PDB atual para atribuir linhas privadas
ao dump antigo. A atribuição de módulo e a pilha com símbolos públicos ainda devem ser
extraídas em PowerShell elevado:

```powershell
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
$out = '.\docs\evidence\2026-09-20_00-03-31-DESKTOP-HQAG6T4-lab-binding\memory-dmp-public-analysis.txt'
& $cdb -y 'srv*C:\Symbols*https://msdl.microsoft.com/download/symbols' `
  -z 'C:\Windows\MEMORY.DMP' `
  -c '.bugcheck; ln fffff803475ad770; !analyze -v; lm; kP; q' 2>&1 |
  Tee-Object -FilePath $out
```

Arquivar a saída junto da evidência da Corrida 42. Até essa análise existir, não atribuir o
Arg2 `fffff803475ad770` a módulo algum. Se o dump apontar corrupção anterior ao callback de
restart, reavaliar a Corrida 43 antes de instalar o candidato.

### Resultado da etapa

Concluída. Relatório SHA-256
`36CD8FD6B8788F6297D63100F748074D895CCB766E39B4E39D210D146B97BD0F`:

- bucket `0x116_IMAGE_bc250mcdm.sys`;
- Arg2 dentro de `bc250mcdm.sys`, RVA `+0x1D770`;
- pilha do scheduler esperando conclusão de preempção;
- nenhum indicador de AV, IRQL, verifier ou bucket de corrupção de memória.

O dump confirma a responsabilidade do miniport e não apresenta um bloqueador novo anterior
ao callback de restart. A Corrida 43 fica liberada pelo critério de dump, mantendo todos os
riscos e gates abaixo.

## 3. Identidade congelada do candidato

- DriverVer: `09/20/2026,3.201.1.0`
- SYS fonte: `770BA96A73F4825521AE0DF8D9224019D3EBCC2094E6D8BDFBAC9591BD0AC691`
- SYS assinado: `A76A3DA62F1FC635CA516F6DFAB8054E5F3D4142B963F9A7994A7469ABA2E514`
- PDB privado: `8376AA54B40B039C24E2CA8DC9569B531308BF2E94189358AA0548FD72EC4542`
- UMD fonte: `3D54963BABA03B63AEB3648B2B9259E225703A18769B1B435FE1D5D528BA2FAC`
- UMD assinada: `934DBD84E04F10024F6B7CAB28FBE014CAA41D35E103600F160BBF395AB5236A`
- INF: `32DE2A66CDE7632CDB46ED6987366F57DD6FC29CB2E104E600752107D036ECA6`
- CAT: `FDA7D224E20AFB62AFA10261243A7FC2AE143839B0D1A0EE7227020D06369A54`

Auditorias offline concluídas:

- `driver/wdk/build-contract.bat`: PASS;
- `tools/audit-bc250mcdm-safety.py`: PASS;
- `tools/audit-bc250mcdm-lab-package.ps1`: PASS;
- `git diff --check`: sem erro; apenas avisos de normalização LF/CRLF.

O empacotador agora sela o SHA-256 do PDB no manifesto, e o binder copia PDB + manifesto de
símbolos para a pasta de evidência **antes** de qualquer operação destrutiva. Essa correção
não recupera o PDB perdido da Corrida 42; impede a repetição da lacuna.

## 4. Hipótese falsificável

**H43:** o callback anterior declarava o CP_RB0 recuperado sem fazê-lo executar. Se o soft
reset realmente deixou o motor utilizável, a sonda privada de 12 dwords será consumida, a
fence EOP substituirá o poison e a submissão GFX seguinte poderá avançar. Se a sonda não
concluir, o restart deve falhar fechado na primeira ocorrência, sem acumular novas pendências
sob uma declaração falsa de sucesso.

Essa hipótese trata a recuperação, não a causa do stall inicial em `RPTR=24`.

## 5. Critérios da única corrida

### Sucesso do contrato de restart

Todos são obrigatórios se houver TDR GFX:

1. `[TDR_FENCE_RECONCILE] PASS` com `NotificadasComoConcluidas=0`;
2. `[TDR_GFX_RESTART_PROOF] PASS` com `RPTR=12`, `WPTR=12`,
   `PoisonSubstituido=1` e fence diferente de `0x43DAFFFE`;
3. `[TDR_GFX_RESTART] CP_RB0 rearmado e comprovado por execucao fisica`;
4. uma submissão GFX posterior aposentada, com avanço de fence real;
5. pendências não crescem monotonicamente após o reset;
6. nenhum BugCheck e nenhuma segunda sequência de TDR causada pelo mesmo IB.

### Falha informativa e segura

Se `[TDR_GFX_RESTART_PROOF] FALHA` aparecer, o resultado é útil somente se:

- `McdmRestartFromTimeout` devolver `STATUS_DEVICE_NOT_READY`;
- não houver mensagem de adaptador pronto;
- a admissão de novas fences permanecer fechada;
- o log preservar RPTR, WPTR, fence observada/esperada e quantidade abortada.

BugCheck continua sendo falha do ensaio, mesmo quando a recusa foi honesta.

### Falha de contrato

Qualquer um encerra a interpretação:

- restart declarado sucesso sem `TDR_GFX_RESTART_PROOF PASS`;
- fence WDDM abortada notificada como concluída;
- `BOOT_PREAMBLE DrenoOk=0` seguido de `ONLINE SUCESSO`;
- RPTR writeback antigo reaparecer como estado inicial do anel rearmado;
- recuperação/rollback sem cauda integral do KMD.

## 6. O que esta corrida não fecha

Mesmo no melhor desfecho, continuam abertos:

- causa do stall inicial do primeiro IB gráfico;
- execução pós-reset do MEC;
- caso físico acolchoado 590x800;
- `pfnLockCb=0x80070057`;
- publicação atômica do journal GPUVA antes das PTEs físicas;
- desktop visível, frame completo, pixel correto e jogos.

BasicDisplay restaurado pelo watchdog é recuperação, não sucesso do WDDM BC-250.
