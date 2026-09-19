# docs/252 — Corrida 18:21: padrão no monitor, `Journal NO_SPACE` e watchdog bloqueado

**Corrida:** `2026-09-12_18-21-04-DESKTOP-HQAG6T4-lab-binding`  
**Candidato executado:** `3.157.1.0`  
**Estado pós-reboot:** fusível TRIPPED, Boots=2, `0xC0000001`; dispositivo Código 43 com `bc250mcdm`.  
**Dump novo:** nenhum; `C:\Windows\MEMORY.DMP` continua sendo o arquivo das 13:12.

## O que a foto prova

O framebuffer RADV chegou fisicamente ao monitor. O harness mediu:

- byte proof `PASS`, 8/8 zonas;
- `D3DKMTPresent` chamado;
- submit aceito e aposentado;
- anel drenado;
- SDMA blt executado;
- `no_host_copy_fallback=True`;
- zero crash do DWM durante o recorte.

A ordem visual ciano/amarelo e azul/vermelho está trocada em relação aos nomes impressos pelo harness. O próprio fonte chama os pixels de BGRA8, mas os valores numéricos de `COLOR_YELLOW/CYAN` e `COLOR_RED/BLUE` estão rotulados ao contrário para a interpretação do scanout. Isso não invalida a atribuição à GPU; é dívida separada de formato/cor.

## Por que a C3.14 não fechou a parede

A C3.14 ampliou `BC250_GPUVA_SPACE_MAX_RANGES` para 1024, mas o produtor anterior permaneceu com `BC250_GPUVA_JOURNAL_MAX_CHANGES=256`.

O miniport divide cada UpdatePT em chunks de 256 entradas, porém o journal acumula os runs de todos os chunks até o FlushTlb. A corrida mediu updates esparsos com `Num=333`, `443` e `461`. O segundo chunk excedeu o journal, que devolveu `NO_SPACE`; o prefixo do primeiro chunk permaneceu pendente e pôde ser publicado como se fosse a transação inteira. No quinto submit:

```text
D3DKMTSubmitCommand status=0xC01E0200
vkQueueSubmit result=-4
```

Veredito: a C3.14 foi **insuficiente**, não refutada quanto ao snapshot. Faltava ampliar o journal e impedir publicação parcial.

## Por que o watchdog não recuperou

O watchdog foi armado e chegou ao prazo. Às 18:24 ele criou `bc250_display_log_full_pre_rollback.txt` com 1.033.647.295 bytes, mas seu estado permaneceu `ARMED`; não existem `watchdog-rollback.txt` nem transição `ROLLBACK_STARTED`.

O código tentava, nesta ordem:

1. copiar o log cumulativo ainda crescente;
2. calcular SHA-256;
3. contar todas as linhas com `Get-Content | Measure-Object`;
4. só então executar rollback.

Logo, a coleta best-effort bloqueou a ação de segurança. O CrashOnCtrlScroll estava corretamente armado nos dois drivers de teclado, mas o host não processou o atalho durante a paralisação; nenhum dump novo foi gerado.

## C3.15 implementada offline

- `BC250_GPUVA_JOURNAL_MAX_CHANGES`: 256 → **1024**;
- replay exato de 461 runs esparsos, estagiados como 256+205, e publicação integral;
- snapshot refeito para 252 existentes + 461 mudanças = **713 MAPPED**;
- overflow futuro aborta explicitamente o prefixo parcial;
- watchdog escreve `ROLLBACK_STARTED` e executa recuperação **antes** de qualquer coleta;
- após recuperar, arquiva somente uma cauda limitada a 64 MB;
- binder deixou de copiar novamente o log cumulativo integral;
- build integral e auditoria de segurança: `PASS`;
- gates de capacidade e limite de 64 MB provados por mutação.

## Próximo candidato

Pacote LAB `3.158.1.0`, ainda não instalado e não validado em hardware:

| Campo | SHA-256 |
|---|---|
| SYS fonte | `DAF00EF463CA7DE8EA0DF73AF3563FF47964AC5AF28C92326AFE2F3D9C9DEA8B` |
| SYS empacotado | `E0D17D239561CB5A4FC0F3789FB3268000D0162EDEC024C3FAA6785472F1A195` |
| UMD | `DD4DFED4DDC0A8E18E133FB2C82F1D2AE788F268799383BE6D91A9C36A5C13B1` |
| INF | `8BD6E8D1897E28D9222391A73F2523CF244D923A0CDD3E584F8C1BDE1B91653B` |
| CAT | `B62D5F6D311330419800F917A65AE81DB3C5386508F83D991EFED716BD1B8AF2` |

Antes de qualquer nova corrida: recuperar o Código 43, zerar o fusível e fazer boot frio. Não repetir `3.157.1.0`.
