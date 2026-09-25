# DX.68 offline: migalhas em camadas e criterio da Corrida 112

> **Base:** Corrida 111, [resultado](423_CORRIDA111_DX67_SEGMENTO_LIMPO_TRAVAMENTO_E_MIGALHAS_B.md). **Data:** 24/09/2026. Nenhum bind com DX.68; efeito no silicio **NAO VALIDADO EM HARDWARE**.

## Por que esta mudanca

Na Corrida 111, o desktop permaneceu legivel por cerca de 6–7 minutos e depois travou sem artefatos. O segmento do VidMm nao cruzou as reservas internas. A pagina DX.66 sobreviveu ao reset, mas os 12 slots por CPU estavam em `DEPOIS`. Cada CPU tinha um unico slot: uma leitura interna frequente podia substituir a fase de uma DDI externa ainda em execucao. A ultima linha de log e o `FLUSH_TLB` duravel nao localizam a causa.

## Alteracao unica: observabilidade persistente

A pagina reservada de VRAM 0x0FA00000 mantem o mesmo endereco e o mesmo mapeamento UNCACHED. O formato passa a versao 2, com **tres bancos disjuntos de 32 slots**: acessos internos MMIO/VRAM, `DxgkDdiBuildPagingBuffer` e o corpo de `DxgkDdiSubmitCommandVirtual`. Cada banco usa o numero do processador; 96 slots ocupam 400 dwords com o cabecalho, dentro da pagina de 1024 dwords. As duas DDIs gravam `ANTES` antes do corpo e `DEPOIS` ao voltar, inclusive quando o status e' falha. O relatorio do boot seguinte identifica CPU, camada, sitio, detalhe e fase.

O corpo da DDI de paginacao foi colocado em funcao interna para cobrir todos os seus retornos, sem alterar sua decisao de status. A submissao usa o ponto de entrada de ciclo de vida ja existente. Os acessos MMIO, as operacoes de GPU, o mapa de VRAM, o perfil do pacote, a UMD e a Radeon Software permanecem com o contrato da 111. Uma migalha `ANTES` indica que a chamada estava em andamento no ultimo registro preservado; por si so **nao prova** que a propria DDI causou o travamento, nem exclui outra CPU.

## Portoes offline e candidato

- `driver/wdk/build-contract.bat`: EXIT=0; build lab: EXIT=0.
- Auditoria de seguranca e auditoria do pacote: PASS. Mutações DX.68 **4/4**, DX.67 **6/6**, DX.66 **9/9** reprovadas e restauradas byte a byte.
- Pacote lab assinado **3.272.1.0**: SYS `51BF07646E9998A48883AF3443B45DF33FB073AD8FF426B8D9560D6D022EF886`; INF `05D232ACE52F607247923462B34D178291635FD22470E90AF985DB40042AB859`; CAT `826C4FC150FB00D4C598173C541403901B181E19823A3BB3ED6939338968D68C`; UMD `B6AB6788ABBFD77FD6EE66D008E336BF4C4AA8B3B265013CC70B8E9EBD751E0B`.
- Preflight somente leitura `tools/prepare-corrida112-dx68.ps1`: PASS; runner elevado `tools/EXECUTAR_CORRIDA112_DX68_600S.cmd`. Nenhum bind foi feito.

## Criterio escrito antes da Corrida 112

**Controle:** boot frio com corte de energia apos `CLEAN_BOOT_REQUIRED`; Radeon Software impedida como na 111; pacote e RADV com hashes fixados; `[DX67_VRAM_MAPA]` ainda publica segmento abaixo de 0x0FA00000; `[DX66_MIGALHA] ARMADA` informa 96 slots/3 camadas e versao 2. Se um controle falhar, classificar E.

| Desfecho | Observacao exigida | Interpretacao |
|---|---|---|
| **A** | Controles completos, desktop visivel sem degradacao por 600 s e BasicDisplay/0 em cinco amostras no **mesmo boot** apos rollback | Estabilidade de 600 s neste candidato; nao fecha jogos ou causa historica. |
| **B** | Controles completos e travamento duro antes de 600 s; reset a quente preserva as migalhas | Falha funcional; classificar separadamente os bancos abaixo. |
| **E** | Controle incompleto, falha de StartDevice, migalha nao armada, TDR/BugCheck ou outro desfecho | Registrar literalmente, sem atribuir A ou B. |

**Leitura diagnostica se B:** banco `PAGINACAO` em `ANTES` identifica uma DDI de paginacao em andamento; banco `SUBMISSAO` em `ANTES` identifica uma submissao em andamento; os dois em `DEPOIS` deixam essas chamadas fora do ponto observavel e tornam prioritarios os acessos nao instrumentados e outras rotas de kernel. Se um banco nao tiver sequencia, ele nao registra chamada naquela CPU. Relacionar sequencias e CPU com o banco interno e o rastro DX.65. Nao atribuir causalidade apenas pela fase. `Present` 60/60, Codigo 0 e exit 0 isolados nao fecham A.

**Recuperacao:** em travamento, reinicio a quente para tentar preservar a pagina; o guardiao so pode agir no boot seguinte se o kernel parar. Nao apagar o marcador `CLEAN_BOOT_REQUIRED`. Antes de qualquer novo bind, desligar completamente e cortar a energia. Desktop 600 s, recuperacao R1 e causa do travamento seguem **NAO VALIDADOS EM HARDWARE**.

| Item | Estado |
|---|---|
| Corrida 111 analisada e prova arquivada | ✅ |
| DX.68 compilada, auditada e empacotada | ✅ offline |
| Boot frio apos Corrida 111 | ⏳ Daniel |
| Corrida 112 | ⏳ hardware |
