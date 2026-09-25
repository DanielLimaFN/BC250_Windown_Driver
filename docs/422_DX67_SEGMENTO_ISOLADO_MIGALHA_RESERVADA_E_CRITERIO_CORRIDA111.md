# DX.67 offline: segmento isolado, migalha reservada e Corrida 111

> **Data:** 24/09/2026. **Base:** Corrida 110, doc 421. Nenhum bind ocorreu com este candidato; o efeito no silicio segue **NAO VALIDADO EM HARDWARE**.

## Alteracao

**Variavel comportamental:** o segmento 1 do VidMm termina antes da primeira regiao interna em uso. `Bc250ActiveQueryRuntimeCaps` percorre os dois alocadores (MMU global e bootstrap), trunca `localEnd` quando encontra uma regiao no intervalo e recusa `QUERYSEGMENT4` se uma regiao invade a base ou esvazia o segmento. A reserva herdada do inventario tambem participa do limite. Nenhum registrador DCN foi escrito.

**Instrumentacao:** a pagina DX.66 mudou de 0x0FE80000 (dentro do TMR observado) para **0x0FA00000**. O inventario a reserva como pagina persistente visivel a CPU **antes** do preflight do bootstrap e da publicacao do segmento. Assim os alocadores nao a reutilizam, e o segmento termina nela ou abaixo dela. O arme exige encontrar a reserva exata no alocador do bootstrap; tambem recusa sobreposicao em qualquer outro registro. A leitura no StartDevice anterior ao fusivel continua.

O endereco e a retencao apos reinicio a quente precisam de medicao na Corrida 111. A reserva do inventario pode reprovar o StartDevice se conflitar com uma regiao herdada nao vista na Corrida 110; essa e uma falha fechada, a registrar como E.

## Portoes offline

- `build-contract.bat`: EXIT=0; build de laboratorio apos mutacoes: EXIT=0.
- Auditoria de seguranca e auditoria do pacote: PASS.
- Mutacoes: DX.56 6/6, DX.61 7/7, DX.62–65 22/22, DX.66 9/9, DX.67 6/6. Os scripts restauraram bytes e conferiram SHA-256.
- Pacote lab assinado **3.271.1.0**, perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, GfxWaveProbe=1 como na 110. SYS `2B34CEEDAF0510CC82B13AF6A0E658ED2F1A2684DC2D3AB94B1F04C3E4D6EB31`; INF `AFA786BA0201F3F171E8C70E12B449590616C8C40F4CB0BB4F1A78D2A669487C`; CAT `ECD616A570BA051DBFB491043CF5E16C30A7427C7D82EB20EB2AE0388345FB97`; UMD inalterada `B6AB6788ABBFD77FD6EE66D008E336BF4C4AA8B3B265013CC70B8E9EBD751E0B`.
- Preflight somente leitura: `tools/prepare-corrida111-dx67.ps1`; runner elevado: `tools/EXECUTAR_CORRIDA111_DX67_600S.cmd`, 600 s. O preflight exige hashes e Radeon Software impedida de abrir. BIOS UMA permanece em 512 MB.

## Criterio escrito antes da Corrida 111

**Controle obrigatorio:** `QUERYSEGMENT4` publica `SegBase` e `SegBytes` com teto fisico `SegBase + SegBytes <= 0x0FA00000`; `[DX67_VRAM_MAPA]` registra as regioes internas; `[DX66_MIGALHA] ARMADA` em `VRAM 0xFA00000` e BAR fisico coerente com o GOP; cada `SetRootPageTable` convertido para offset de VRAM cai abaixo do teto. Radeon Software ausente. Se qualquer controle falhar, desfecho E antes de interpretar estabilidade.

| Desfecho | Assinatura | Implicacao |
|---|---|---|
| **A** | Controles completos; desktop visivel e sem degradacao durante **600 s**; guardiao retorna BasicDisplay/0 em cinco amostras **no mesmo boot** | Estabilidade de 600 s demonstrada neste candidato. A sobreposicao era uma causa plausivel, mas uma unica corrida nao prova causalidade exclusiva. |
| **B** | Controles completos; travamento duro antes de 600 s | A sobreposicao nao era condicao necessaria suficiente para explicar todos os travamentos. Reiniciar a quente sem cortar energia e ler as migalhas no boot novo; A/B/C do doc 419 classificam a diagnostica, separadamente. |
| **E — fora do mapa** | Controle incompleto, StartDevice recusado, `ARME RECUSADO`, TDR/BugCheck, outro comportamento | Descrever literalmente; nao atribuir A nem B. |

Desfecho parcial nao fecha o ensaio. `Present` 60/60, Codigo 0 ou exit 0 isolados nao fecham A. Se a maquina travar, o guardiao nao pode recuperar um kernel parado no mesmo boot; no boot seguinte a leitura das migalhas ocorre antes do fusivel. O Daniel faz reinicio a quente para preservar a possibilidade de retencao, depois deixa o guardiao restaurar BasicDisplay. Nao apagar manualmente `CLEAN_BOOT_REQUIRED`.

**Antes da corrida:** desligamento completo com corte de energia para cumprir o marcador de boot limpo. Depois do bind, em caso de travamento, apenas reinicio a quente. O primeiro `ANTERIOR AUSENTE` no bind limpo e esperado; a leitura relevante e a do boot seguinte.

| Item | Estado |
|---|---|
| Corrida 110 analisada | ✅ |
| DX.67, reserva e portoes offline | ✅ |
| Pacote 3.271.1.0 e preflight | ✅ |
| Segmento limpo medido no hardware | 🔜 |
| Desktop sustentado 600 s | 🔜 |
| Causa do travamento duro | 🟡 |
