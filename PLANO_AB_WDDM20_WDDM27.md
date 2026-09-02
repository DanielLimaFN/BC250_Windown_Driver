# Ensaio A/B WDDM 2.0 versus 2.7 — BC-250

Data: 2026-09-01  
Candidato: 3.70.0.0  
Telemetria: schema 131  
Estado: **NÃO VALIDADO EM HARDWARE**

## Objetivo

Isolar se o Código 43 pós-`StartDevice` é provocado pelo contrato adicional de WDDM 2.1–2.7 ou por uma inconsistência comum ao caminho display+render.

O ensaio usa o mesmo SYS, os mesmos callbacks e o mesmo bring-up. A única variável é o contrato anunciado de forma coerente em três superfícies:

1. `DRIVER_INITIALIZATION_DATA.Version`;
2. `DXGK_WDDMDEVICECAPS.WDDMVersion`;
3. `DXGK_DRIVERCAPS.WDDMVersion`.

## Correções metodológicas

- O teste `active_miniport_test` agora examina a estrutura efetivamente construída.
- Callbacks essenciais de base, display, render, GPUVA, TDR e interrupção são conferidos por ponteiro.
- Campos reservados são obrigatoriamente nulos.
- Combinações cruzadas 2.0/2.7 são recusadas com `STATUS_INVALID_PARAMETER`.
- As duas respostas de capacidade são testadas em 2.0 e 2.7 no mesmo binário.
- O campo `V5ContractVersion` publica 20 ou 27 no schema 131.
- Valores diferentes de 20 e 27 falham fechado antes de `DxgkInitialize`.

## Matriz de decisão

| Resultado WDDM 2.0 | Resultado WDDM 2.7 | Conclusão |
|---|---|---|
| Código 0, avança além do estágio 3 | Código 43 no estágio 3 | Falha localizada nos requisitos 2.1–2.7 |
| Código 43 no mesmo estágio 3 | Código 43 no estágio 3 | Causa comum: display/children/VidPn ou contrato base 2.0 |
| Código 0 | Código 0 | Contrato corrigido; avançar para D3DKMT/UMD |
| Falha antes de `StartDevice` | qualquer | Divergência na seleção/versão; não interpretar como falha do silício |

## Sequência física mínima

1. Recuperar o estado atual para `BasicDisplay / Código 0`.
2. Instalar 3.70 com `RenderNode=0`.
3. Validar Código 0, P1 e P2.
4. Selecionar WDDM 2.0:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\set-bc250mcdm-wddm-contract.ps1" -ContractVersion 20 -Execute
```

5. Preparar `RenderNode=1`, reiniciar e inspecionar.
6. Preservar a evidência completa antes de qualquer recuperação.
7. Somente se necessário, repetir o mesmo procedimento com `-ContractVersion 27`.

O histórico 3.67–3.69 já fornece o braço WDDM 2.7: Código 43, `STATUS_INTERNAL_ERROR`, estágio 3. O primeiro ensaio físico do 3.70 deve ser WDDM 2.0.

## Critério de sucesso

Código 0 não basta. O braço será considerado avanço somente se:

- `V5ContractVersion=20`;
- `V5RegistrationMode=1`;
- dispositivo `bc250mcdm / Código 0`;
- `DRIVERCAPS` ou outro estágio posterior a `V5PostStartStage=3` for alcançado;
- P1/P2 permanecerem fisicamente válidas;
- nenhum resultado for fabricado por CPU.

Até isso ocorrer: **NÃO VALIDADO EM HARDWARE PARA RENDER WDDM**.

