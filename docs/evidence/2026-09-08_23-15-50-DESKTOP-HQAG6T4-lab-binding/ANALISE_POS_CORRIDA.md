# Corrida 08/09/2026 23:15 — candidato 3.104.1.0

## Evidência preservada
O recorte `display-log-post-run.txt` foi extraído do log vivo a partir do offset 2928377 registrado pelo binder. SHA-256: CD32DD233F8C66BEA45668E4C8BEC16E778107A6225E53E4FE3975B86DD86CBE. O arquivo arquivado inicialmente pelo coletor terminava antes de parte relevante dos eventos; este recorte inclui TDR e StopDevice. Estado CIM e eventos estão em `post-run-inspection.json`.

## Medições (P0)
- 11 transações FlushMappedGfxhubTlb, todas status=0; zero status=8. Não prova ausência universal de concorrência; a assinatura anterior não reapareceu nesta corrida.
- Cinco IB_TRIPE_VEREDITO TRIPE_PROVADO no Node0, cada qual com cinco canários e Divergentes=0. StopDevice: Accepted=11, NotExecuted=0.
- VMID9_BIND Status=0 Ready=1, raiz 0x45FFB0000. Node1 publicou IB de 68 bytes em GPUVA 0x11000.
- Harness: Accepted 17->18, Retired 17->17, WPTR 204->216, RPTR 204->204. Fence=0 e Data=0 no veredito; não houve prova de conclusão nem conferência de dados bem-sucedida. Fence observada 0xFFFFFFFFFFFFFFFF não é prova de escrita pela GPU. CreateDevice terminou com 0x887A0005.
- TDR quiesceu SDMA já drenado (RPTR=WPTR=1212), reiniciou SDMA e provou NOP/fence. Houve novos tripés SDMA após o reset. Isso não prova recuperação de MEC/Node1; o callback Bc250ActiveTdrRestartLifecycle chama apenas Bc250SdmaPresentRestartAfterTdr.
- Evento System/Display 4101 às 23:16:12 registra parada e recuperação do driver. É anterior ao prazo do watchdog armado às 23:15:51 por 180 segundos. Não foi encontrado evento novo de BugCheck/boot não limpo na consulta registrada. MEMORY.DMP ainda tem data de 22:21:12, da corrida anterior.
- StopDevice Reason=2, pending=1; VMID9 restaurado com readback/ACK. Depois: Global GFXHUB MMU restore FALHOU, transporte MMIO state=0, status c00000a3 no registro final do fusível.
- Inspeção às 23:18:26: serviço bc250mcdm, Código 43. Watchdog-state ainda ARMED; restauração de BasicDisplay NÃO CONFIRMADA nessa inspeção.

## Veredito
B1 reproduzido; assinatura TLB status=8 não reproduzida; tripé Node1 NÃO VALIDADO EM HARDWARE. A causa exata de o MEC não concluir permanece aberta. Não atribuir a recuperação precoce ao watchdog de 180 segundos nem declarar rollback limpo. A tela preta é relato do usuário; backend D3D10/11 permanece desabilitado no manifesto.

Próxima investigação offline: cadeia de execução MEC sob VMID9 e cobertura do TDR para Node1, além da disponibilidade de MMIO na restauração da MMU. Nenhuma alteração de driver, bind, recuperação ou reset de fusível foi executada nesta análise.

## Atualização final — 23:19:11
O watchdog completou o rollback às 23:18:53, estado ROLLED_BACK, exit_code=0. Leitura CIM independente às 23:19:11 confirmou BasicDisplay / Código 0. Portanto houve dois eventos distintos: TDR precoce às 23:16:12 e rollback do watchdog após 180 segundos. A observação intermediária de Código 43 foi superada pela recuperação automática. O rollback está confirmado; a falha de restauração da MMU registrada no StopDevice continua sendo uma pendência real.
