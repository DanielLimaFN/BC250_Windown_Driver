# Execução sem evidência (2026-08-23 23:58:52)

Esta pasta está vazia de propósito documentado, não por acidente de coleta.

## O que aconteceu

O ensaio `test-cp-unhalt.ps1` reprovou o gate `B2_rlc_fora_da_rom` e chamou
`throw`. O `throw` estava dentro do `try` cujo `finally` só fazia teardown, e
todo o bloco de geração de relatório vinha **depois** — então a exceção
propagou, o script morreu, e nenhum arquivo foi gravado.

Resultado: a execução foi gasta e **o valor que reprovou o gate não foi
registrado**. Não se sabe, por esta pasta, quanto lia `RLC_EXEC_ROM_CODE` depois
da carga do PSP.

O que sobrou é o que o console mostrou:

```
  [ok  ] A_headers_coerentes
  [ok  ] B_tres_engines_haltadas_e_paradas
  [FALHA] B2_rlc_fora_da_rom
O RLC nao saiu da ROM. Sem esse pre-requisito, o unhalt do CP mediria sobre
base ausente.
```

## O que é fato sobre esta execução

Medido depois, não deduzido:

| Item | Valor |
|---|---|
| Último boot | 2026-08-23 23:26:11 |
| `test-rlc-bringup.ps1` (carregou firmware pelo PSP) | 2026-08-23 23:32:03 |
| `test-cp-unhalt.ps1` (esta execução) | 2026-08-23 23:58:52 |
| Houve reboot entre os dois? | **não** |

O bring-up das 23:32 já havia gasto este boot carregando o RLC pelo PSP
(`result.json` daquela pasta traz `reboot_required = true`). Esta execução, que
também carrega firmware pelo PSP, rodou em sessão suja — e o ensaio **não
recusou**, porque não tinha guarda de boot limpo, ao contrário de todos os
outros ensaios que carregam firmware.

## O que NÃO é fato

Que a sessão suja seja a causa da reprovação do `B2`. É o candidato principal,
e não está provado: a medição foi destruída junto com o relatório. A execução
seguinte, em boot limpo, é que decide.

## Correções aplicadas em 2026-08-24

1. **Preflight de boot limpo.** O ensaio agora varre `docs/evidence/` por
   qualquer `.json` que declare `reboot_required = true` com carimbo posterior ao
   último boot, e recusa antes de tocar em hardware. Verificado contra este
   estado real: detecta a execução das 23:32 e recusaria.
2. **A evidência sobrevive ao aborto.** O `try/finally` passou a ficar dentro de
   um `try/catch`: abortar é decisão do ensaio, perder a medição não é. O
   relatório declara `ABORTADO`, o motivo, e quantos gates foram medidos de
   quantos previstos.
3. **O gate `B2` distingue três estados.** "Medi 1", "medi 0" e "não consegui
   medir" eram a mesma reprovação. Agora `não consegui medir` é falha de
   instrumento declarada, não fato sobre o RLC.
4. **O ensaio declara `reboot_required`.** Sem esse campo no próprio relatório,
   o preflight do item 1 não teria como detectar uma repetição causada por ele
   mesmo.

Ver `docs/09_PONTO_DE_RETOMADA.md`, lições 21 e 22.
