# Corrida 23:15 e candidato 3.105.1.0 — MEC, TLB e ciclo de vida do MMIO

## Veredito

**NÃO VALIDADO EM HARDWARE.** Foram corrigidos defeitos concretos de sincronização e encerramento. Não se demonstrou ainda a causa exata de o IB virtual do Node1 não concluir; não declarar Node1, desktop ou jogos resolvidos.

Evidência da corrida: [análise](evidence/2026-09-08_23-15-50-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md), [log completo desta corrida](evidence/2026-09-08_23-15-50-DESKTOP-HQAG6T4-lab-binding/display-log-post-run.txt), [confirmação do rollback](evidence/2026-09-08_23-15-50-DESKTOP-HQAG6T4-lab-binding/post-rollback-inspection.json).

- P0: cinco tripés SDMA completos, 11 transações TLB status=0 e nenhuma status=8. Isso não prova ausência de corrida de registradores em todos os acessos.
- P0: Node1 instala VMID9, publica 68 bytes em VA 0x11000; WPTR=216/RPTR=204 e fence/dados não comprovados. TDR reinicia SDMA, seguido de novos tripés SDMA. Watchdog restaura BasicDisplay/Código 0 às 23:18:53.
- P0: StopDevice encontra MMIO state=0 ao restaurar MMU global.

## Defeitos e correções

1. **Desmapeamento prematuro, causa da falha de restauração sustentada por código e log.** `Bc250SubmissionEngineOffline` chamava `Bc250SubmissionEngineReleaseResources`, que desmapeava `Core.MmioTransport`. Depois, StopDevice tentava `Bc250GcvmGlobalMmuRestore` usando o mesmo transporte vazio. O engine agora libera seus recursos e deixa o MMIO do adaptador para `Bc250DxgkDdiStopDeviceReadonly`, que já possuía o desmapeamento final. Ordem: parar DPC, desligar MEC, restaurar VMID9, restaurar MMU global, encerrar core/desmapear MMIO.
2. **A trava do candidato 3.104 era incompleta.** `Bc250ActiveFlushVmidTlb` estava protegido, mas `Bc250SubmissionApplyCoherency` ainda escrevia REQ 0xA2D0 e lia ACK compartilhado sem TlbLock. Agora um wrapper protege toda a transação e libera a trava em todos os retornos, inclusive falha. Ordem de locks: engine Lock -> TlbLock; a invalidação VMID8/9 não adquire engine Lock. A existência do acesso concorrente possível é verificável no código; atribuir o hang das 23:15 a ele continua **H**.
3. **Diagnóstico observável, sem fabricar conclusão.** O descritor de quatro dwords é lido da memória do anel. Após 100 ms de fence virtual pendente, uma captura por fence lê ACTIVE, HQD_ERROR, HQ_STATUS0, WPTR, VMID e IB_CONTROL, com máscara indicando leituras válidas. Guarda-se a fotografia e o descritor correspondente para gravação em disco no StopDevice após drenar o DPC, antes de liberar recursos. Logs em DISPATCH_LEVEL sozinhos não chegam ao arquivo: `Bc250DisplayLog` só faz I/O em PASSIVE_LEVEL. Adicionada também caminhada CPU das page tables do IB Node1. Nada disso substitui prova GPU.

Nenhum formato PM4, valor de VMID, controle de faults ou registrador DCN foi alterado. A composição do descritor compute (VALID, tamanho, VMID no bit 24) foi comparada com [AMD/Linux gfx_v10_0_ring_emit_ib_compute](https://codebrowser.dev/linux/linux/drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c.html#8678) e não apresentou divergência que justificasse alterá-la.

## Limites que continuam abertos

- `Bc250ActiveTdrQuiesceLifecycle` e `Bc250ActiveTdrRestartLifecycle` executam recuperação SDMA. **Não provam recuperação MEC/Node1.** A mensagem histórica de adaptador pronto não fecha essa perna. Não repetir ensaio de hang induzido para tentar provar MEC.
- A captura pendente mede estado da fila, não execução de dados. O teste físico de saída continua `B7_UMD_NODE1_TRIPOD` com fence exata sobre poison, dados byte a byte e anel drenado.
- Falhas na desativação HQD/restauração VMID9 continuam sendo caminhos de erro; não afirmar teardown universalmente corrigido por uma compilação.
- O backend D3D10/11 continua desabilitado. Tela preta permanece um resultado esperado; o teste não valida desktop ou jogos.

## Validação offline

Build contratual completo e build de laboratório executados pelo script canônico. `audit-bc250mcdm-safety.py`, `audit-umd-node1-submission.py` e `test-mec-mmio-safety.py` verificam a alteração. O teste de mutações troca textos somente em memória: reintroduzir unmap, remover acquire/release, chamar transação sem wrapper, omitir restauração MMU ou omitir arquivamento passivo deve ser recusado.

## Próxima corrida: teto de uma, executada pelo Daniel

Pacote **3.105.1.0 assinado e auditado, não instalado**, em `driver/wdk/package/output_lab`:

- SYS SHA-256: `19F5D13030D7B7BEF08D2D8A935F8F0A62D783AF1A11531C901A455E809DFBC2`.
- UMD: `35FBD78186D300D84F25DB76D1AF1C3025C7FECA3211CA422D35619B116AF214` (sem alteração).
- CAT: `D06EE3EE40584CB0D9883C7DF9465824B7121482C34681630E7B2F140CA2C3E2`.
- INF: `6ECFA0A408894CBD930187E69E58593A65818BBD4B8EB1B3041BC26482A07012`.

Build final EXIT=0; auditorias de segurança, UMD e pacote PASS; seis mutações recusadas e implementação atual aceita. As strings de diagnóstico foram verificadas no SYS assinado. Logs, manifesto e hashes das fontes estão em [evidência offline](evidence/2026-09-08_MEC_TLB_MMIO_OFFLINE/). A leitura final do dispositivo confirmou BasicDisplay/Código 0; não houve bind nesta sessão.

Depois de boot frio e preflight de recuperação/fusível do binder, usar o candidato 3.105.1.0 com o mesmo teste Node1 e watchdog de 180 segundos. Não armar TDR induzido nem fallback Plano A.

```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180
```

Critérios: (a) tripé Node1 completo, (b) sem restore MMU com transporte vazio, (c) rollback confirmado BasicDisplay/Código 0. Se o Node1 continuar sem concluir, **não repetir**: minerar `MEC_PENDING_ARCHIVE`, `MEC_IB_DESCRIPTOR_ARCHIVE` e `PT_WALK NODE1_IB`, conservando a distinção entre leitura inválida e registrador zero. Se houver crash, usar o dump da nova corrida. A instalação e a corrida não foram executadas nesta sessão.
