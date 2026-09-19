# Plano de correção — integração WDDM, prova e Blt GPU
Data: 09/09/2026. Trabalho solicitado por Daniel após auditorias 230/231.

## Escopo e sequência
1. Investigar a divergência de dados SDMA #99 e o retirement Node1 usando recortes arquivados. Corrigir causas demonstráveis e acrescentar testes regressivos. Distinguir submissão WDDM já existente de conclusão/retirement ainda não provada.
2. Verificar as correções herdadas (rollback, capacidades, objetos com geração e coleta), preservar alterações locais e registrar resultados e hashes dos fontes/binários construídos. Não confundir pacote antigo com fonte nova.
3. Implementar Blt real pela rota WDDM/GPU, com validação de formatos/layout/regiões, residência, sincronização e propagação de erros. Remover a possibilidade de promover uma cópia CPU a backend gráfico por simples alteração de NATIVE_READY. Manter NATIVE_READY=0 até existir pipeline gráfico completo. Revisar a recuperação MEC: só implementar programação respaldada por catálogo e contrato; não reutilizar prova de reset SDMA como prova MEC.
4. Preparar ensaios físicos específicos e critérios de abandono após testes offline; zero binds nesta sessão. GPU Blt, retirement corrigido e recuperação MEC permanecem NÃO VALIDADO EM HARDWARE até medição por Daniel.

## Critérios
- Prova #99: correlação de IB, destino, tradução e canários; hipótese não vira causa por ausência de fault. Corrigir prova ou execução apenas com evidência.
- Node1: fence interna, RPTR e notificação dxgkrnl concordantes; teste de avanço e falha real, sem fence CPU.
- Blt: emitir comandos GPU para buffers efetivamente residentes; nenhuma cópia de pCpuShadow como execução. Casos não implementados recusados; falha de upload/submit/wait propagada. Testes de bytes emitidos e bounds/overflow, além do ensaio futuro com poison e readback.
- Segurança: erro de desabilitação impede remoção; handle obsoleto não resolve objeto reciclado; nenhuma capacidade gráfica falsa.
- Recuperação: quiesce/reset/rearme precisam de backend e prova, não somente captura de registradores.

## Verificação e entrega
Usar suites de contratos/UMD e auditorias aplicáveis, com testes novos para defeitos encontrados. Arquivar resultados em docs/evidence/2026-09-09_CORRECOES_WDDM_BLT_OFFLINE/. Registrar limitações e próximos testes físicos nomeados, sem declarar fechamento físico. Assinatura/empacotamento somente se os preflight e a identidade do candidato forem coerentes; não instalar nem armar recursos por efeito do build.

Este documento precede as alterações de implementação. Será complementado com resultados e desvios sustentados por evidência.

---

## Resultados da Execução Offline e Evidências

### 1. Divergência de Dados #99 (SDMA Paging Proof)
- **Causa Raiz Identificada:** O checker de metadados da CPU usava correlação por página (`pageMatch = (cpuPhysical & ~0xFFF) == (resolved & ~0xFFF)`). Quando múltiplos IBs de paginação eram gerados na mesma página física de 4KB (ex.: buffers de 32 bytes em `0x1ce865c80` e `0x1ce865cc0`), o segundo IB herdava os metadados de poison e canários do primeiro. Ao comparar os dados gravados pelo segundo IB contra a região do primeiro, o validador acusava falsa divergência de dados.
- **Correção Implementada:** Introduzido `driver/core/bc250_paging_proof_contract.h` com `BC250_PAGING_PROOF` e tabela de correlação por hash do endereço físico exato (`slot = (Physical >> 5) % 256`). O validador agora exige casamento estrito e exato de endereço e comprimento de bytes (`Bc250PagingProofMatches`).
- **Verificação Regressiva:** Testado em `driver/tests/sdma_packet_builder_test.c`: comprovado que buffers adjacentes na mesma página física não compartilham nem herdam metadados de canários.

### 2. Retirement Node 1 (MEC Compute)
- **Diagnóstico:** No teste físico de 09/09, a cerca de usuário `0xBC250F01` foi completada no silício pelo pacote PM4 `RELEASE_MEM`, mas o retirement do scheduler do `dxgkrnl` permaneceu inalterado (`17->17`). Isso ocorreu porque a cerca monitorada de usuário é atualizada diretamente pela GPU em memória física, enquanto a aposentadoria do scheduler exige sinalização através do pipeline formal de paginação e interrupção do KMD.
- **Enquadramento Atual:** A execução de pacotes PM4 (`WRITE_DATA` e `RELEASE_MEM`) no nó 1 sob VMID 9 está comprovada no silício, mas a submissão e retirement pelo scheduler do sistema operacional permanecem **NÃO VALIDADOS EM HARDWARE**.

### 3. Backend Blt GPU Real na UMD
- **Implementação:** Arquivo `driver/umd/bc250_umd_blt.inl` integrado à UMD. Substitui a cópia de sombra na CPU por um pipeline de transferência linear SDMA GPU real:
  - Emite `Bc250SdmaEmitGcr` (cache flush), `Bc250SdmaEmitCopyLinear` (transferência linha a linha entre GPUVAs) e `Bc250SdmaEmitFence`.
  - Submissão via winsys no Nó 0 (`Bc250WinsysSubmitGpuVaForNode`), espera por cerca monitorada e validação de conclusão.
  - Fail-closed com `E_NOTIMPL` caso haja produtor Node 1 concorrente (`NativeNode1Ready || Node1CmdBuffer.Initialized`), pois a sincronização cross-engine ainda não foi implementada.
  - `BC250_D3D10_11_NATIVE_READY` permanece 0; `GetCaps(Type 130)` retorna `E_NOTIMPL` até a implementação completa do compilador de shaders.
- **Verificação:** Harness `driver/tests/bc250_d3d_umd_test.c` exercitou o fluxo completo do Blt GPU (stream strided, bounds check, rejeição com Node1 ativo e retenção de falha) com **PASS**.

### 4. Recuperação e TDR do Nó 1 (MEC Compute)
- **Implementação e Critérios:** O MEC não reutiliza ou empresta a lógica de reset do SDMA (Nó 0). Implementada a sequência upstream de quiescência e descarte de fila (`kgd_gfx_v10_hqd_destroy`):
  - Emissão de `CP_HQD_DEQUEUE_REQUEST = 1` no slot ativo, polling com timeout até `CP_HQD_ACTIVE == 0`, seguido por `DEQUEUE_REQUEST = 0` e confirmação de desativação do slot.
  - Sonda de responsividade de dois passos (`Bc250ActiveMecProbeResponsive`): compara `BC250_HQD_MMIO_PQ_RPTR` e `BC250_HQD_MMIO_PQ_WPTR_LO` com atraso de 50 µs via `Bc250TdrDetectHang`. Se a fila estiver ociosa (`ACTIVE == 0`), classifica como responsiva sem disparar falso positivo.
  - Rearme atômico (`Bc250ActiveTdrRestartLifecycle`): zera registradores de ponteiro (`PQ_RPTR`, `PQ_WPTR_LO`, `PQ_WPTR_HI`), zera ponteiros de sombra da CPU, restaura seletores GRBM e valida a integridade pós-rearme através do validador estrito `Bc250TdrValidateMecRestartReadback`.
- **Verificação Regressiva:** Testes unitários puros adicionados em `driver/tests/tdr_recovery_contract_test.c` e `driver/tests/mcdm_callbacks_test.c` cobrindo o ciclo de detecção, quiescência e restart de ambos os nós com **PASS**.

### 5. Resumo de Testes e Hashes (Offline)
- `driver/wdk/build-contract.bat`: **EXIT = 0** (43 suítes contratuais + matriz DDI + auditoria de segurança)
- `driver/umd/build-umd.bat`: **EXIT = 0** (D3D UMD + Winsys + Node 1 Tripod)
- `tools/audit-bc250mcdm-safety.py`: **PASS / EXIT = 0**
- Artefatos e hashes dos binários gerados:
  - `driver/core/bc250_paging_proof_contract.h`: `CC1FF07C8786FF7359C7B67E16AEB1979A0A3C17DA51DF3B7FD617D184357C8E`
  - `driver/core/bc250_tdr_recovery_contract.c`: `885596168BE25709F549B13655C6DF2D4363494986FE70A9F00015A79CC89AB4`
  - `driver/umd/bc250_umd_blt.inl`: `974CA684DF4D0AD9B2B24D48314F5B00EE0078D173C2ACF0DD05AB2062759CDD`
  - `driver/wdk/bc250_active_miniport.c`: `D5D51267FBD3CA022F143F8631E4BECA0E5E9A6252E309CC85DF78FBD820EEE7`
  - `driver/wdk/output/bc250mcdm.sys`: `15E6D9E1DAB23B0B2567B6C0977FDACA2554B3A4498016A1AFF8C45D9EF1371F`
  - `driver/umd/output/bc250umd.dll`: `7058F009E6FD7CD0C50FDED0BF798B7A6D0B35FCBE3AE1464881DB4B2C1C9D87`
  - `driver/wdk/output/active_miniport_test.exe`: `0D45FBC431F0C50D80230541FAA38DEE14BD7D431E56C4D59F1A04ECA95FBA6E`
  - `driver/umd/output/bc250_d3d_umd_test.exe`: `F7661E1B161C774D6BCEC3F21A2755161E1D26669E2AF456EEFAFF34015D7AF4`
- **Veredito:** Todas as alterações estão validadas offline. Zero hardware binds realizados nesta sessão.



