# 475 — DX.88: log do KMD sem intercalação, investigação da textura compartilhada do Explorer e critério da Corrida 137

> **26/09/2026, offline, sem corrida.** Branch `p7-desktop-estabilidade`. **NÃO VALIDADO EM HARDWARE.**

## 1. Item offline 1 — intercalação de linhas no log do KMD (corrigido no código)
- **Sintoma (P0, Corrida 136):** uma linha `BC250_HANG ... ENTER` saiu com outra linha enfiada no meio do QPC, e o parser leu um `FLUSH_TLB` de 78,7 s que durou 36 ms.
- **Mecanismo (P1):** várias threads podiam estar ao mesmo tempo no caminho de arquivo de `Bc250DisplayLog`, cada uma drenando o anel em RAM em blocos de 4 KB. Os pedaços de uma linha partida na divisa de dois blocos iam para o arquivo por threads diferentes.
- **Correção:** dono único do arquivo por flag atômico (`g_Bc250LogFileOwner`), sem bloquear ninguém. Quem encontra o arquivo ocupado põe a linha no anel e segue; a dona drena o anel antes e depois da própria linha. Se o arquivo não abrir, a linha fica no anel em vez de sumir. Helpers `Bc250LogRingAppend` e `Bc250LogRingDrainToFile` (`driver/wdk/bc250_display_ddi.c`).
- Portão `audit_dx88_log_file_single_owner`, provado por mutação.

## 2. Achado colateral — hash do upstream quebrado pelo checkout
Os `checkout` do merge de 26/09, com `core.autocrlf=true`, reescreveram `inc/linux_ref/*` em CRLF, e o portão DX.27 (sha256 do upstream) reprovou. O conteúdo estava íntegro: sem `\r` o hash bate. Correção: `inc/linux_ref/** -text` no `.gitattributes`, e a cópia de trabalho restaurada do índice (LF). A auditoria do pacote e o preflight da 134 continuaram PASS.

## 3. Item offline 2 — textura compartilhada do Explorer (investigado, **não resolvido**)
- **P0:** nenhuma criação de recurso compartilhado deu certo em nenhuma corrida. Na 136 foram 461 tentativas, todas iguais: `Dim=3 1024x832 Fmt=87 Bind=0xA8 Misc=0x2`, `E_INVALIDARG`. O Explorer tenta de novo a cada ~45 s.
- **P0:** o KMD **aceita** a alocação (`Seen=1`, `KmdStatus=0`, `hAlloc=1`); a recusa vem depois, do dxgkrnl ou do runtime.
- **P0:** na 136 o Explorer teve as 461 recusas e **não caiu**; na 133/134 caiu 4 e 5 vezes com a mesma recusa. A recusa sozinha não derruba o Explorer; o fail-fast é ocasional.
- Documentação (Microsoft Learn): para recurso compartilhado, `D3DDDICB_ALLOCATE.hResource` tem de ser não nulo, e nós passamos. Não há exigência documentada de dados privados de recurso nem de `hResource` na saída do KMD. Hipóteses que restam (**H**): (a) falta de dados privados no nível do recurso; (b) alguma flag do `DXGK_ALLOCATIONINFO` que o KMD devolve e que o dxgkrnl não aceita em alocação compartilhada. O `d3d10umd` do Mesa não serve de referência (não tem KMD).
- **Por que não dá para fechar offline:** as linhas de `CreateAllocation` do KMD são amostradas e o ETW não tinha os eventos de alocação. **Instrumentação adicionada:** o ETW do DxgKrnl passa a incluir `Resource` (0x40) e `DriverEvents` (0x400), com máscara `0x17084C1` em `tools/bc250-hang-etw.ps1`.

## 4. Pacote 3.293.1.0 (DX.88)

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `0DDA39530EB3285A976FD764753FE118F284740CCAD45F8464706EE7FAFD278B` |
| `bc250mcdm.sys` fonte lab | `4FBBB92319F5B3200FA8BDB7F1DEE4F0ABAC54DB7F188EE51800629FA514E936` |
| `bc250umd.dll` (igual à 136) | `3A9731939A970F7386EA75E69277D11764BBEF3178AFC9D98343161A4B6F2A0A` |
| `bc250mcdm.inf` | `CF9CE1FE971C4B05D9168DBAD6E1B947E2C3960DD97BA1B8D623E7B19B7DE2C2` |
| `bc250mcdm.cat` | `A31D94E7A665DBD29C95608CB41A1E31335F9C0DA08D713AD9E2C2540DD1B7B7` |

`build-contract` e build de laboratório EXIT 0; auditoria de segurança e do pacote PASS; `prepare-corrida137-dx88.ps1` PASS.

## 5. Critério da Corrida 137 (600 s; mudança só na medição)
Runner: `tools\EXECUTAR_CORRIDA137_DX88_600S.cmd`, boot frio antes.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem trava, desktop visível, e nenhuma linha do KMD com QPC ou campo corrompido | Terceira janela de 600 s; logger confiável. Seguir para a textura compartilhada com os eventos do ETW. |
| **B** | Como A, e o ETW mostra o evento de recurso/alocação do DxgKrnl na recusa da textura compartilhada | A etapa da recusa fica nomeada; corrigir ali. |
| **C** | Trava ou BugCheck | Usar `BCR1`/dump; o logger novo não altera o caminho de GPU. |
| **D** | Linhas intercaladas persistem | A correção não cobriu outro caminho de escrita; procurar o outro escritor. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |
