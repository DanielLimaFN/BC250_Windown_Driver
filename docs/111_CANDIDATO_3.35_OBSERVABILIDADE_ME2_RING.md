# Candidato 3.35 — observabilidade do ME2 e prefixo do ring

Data local: 30/08/2026
Pacote preparado: **3.35.0.0 / SchemaVersion 106**
Veredito: **NÃO VALIDADO EM HARDWARE**

## Escopo pequeno desta entrega

Nenhuma escrita nova de hardware e nenhuma alteracao no PM4 submetido.

1. `CP_MEC_ME2_HEADER_DUMP` (`dw 0x0E2F`, BAR5 `0x0823C`) foi acrescentado
   somente a allowlist de leitura. O ME1 permanece como controle.
2. Os dois dumps sao drenados uma unica vez, em rajadas separadas de quatro
   leituras, depois do polling.
3. `ShaderRingDword0..7` publica o prefixo relido da memoria do ring depois da
   barreira e do readback integral.
4. O teste de transporte exige leitura aceita e escrita recusada no ME2.
5. Nenhuma observacao nova virou `abort`; o gate fisico continua sendo o tripe.

## Validacao offline

- `driver\wdk\build-contract.bat`: `EXIT=0`.
- Código de kernel compilado sob `/W4 /WX` sem warnings.
- Auditoria DDI: `PASS` estrutural.
- Auditoria de segurança: `PASS`.
- Auditoria do pacote LAB: `PASS`.
- Strings UTF-16 do ME2 e `ShaderRingDword0/7` encontradas no SYS assinado.

## Artefatos

```text
DriverVer           08/30/2026,3.35.0.0
Schema              106
recipe cp/vmid0/sdma 1 / 0 / 0
Package SYS SHA-256 C5D2428DFA24056664EA7A4D255D39966CBE573F37DC9D43D2C252C55FC66DA8
Package INF SHA-256 767F53F5605EC5A42BA6B2D844BFDA151102BD95503EA0C342E87A09ED788929
Package CAT SHA-256 84A6C19C29A5ED532401C0994A07A885FE38304B50F8A6FFD70572F70613BAEB
```

O hash do SYS 3.34 era
`6C5CFD3253A13C46D9048917FF719541131A01D4B0D624107D97D59F5BD74C1B`;
portanto o pacote 3.35 nao reutilizou o binario anterior.

## Ensaio fisico — somente depois de autorizacao do operador

A maquina ainda deve ser tratada como vinculada ao 3.34. Executar na ordem:

1. `rollback-bc250mcdm-lab.ps1`.
2. Reboot manual obrigatorio.
3. `quarantine-bc250mcdm-lab-residue.ps1 -Execute -IAcceptExactResidueQuarantine`.
4. `audit-bc250mcdm-prebinding.ps1 -LabPackage` e exigir `PASS`.
5. `bind-bc250mcdm-lab.ps1 -EnableShaderDispatch -Execute -IAcceptBlackScreenRisk`.
6. `confirm-bc250mcdm-lab.ps1` com o token impresso.
7. `test-shader-dispatch-acceptance.ps1`.

Uma unica submissao fisica por boot.

## Perguntas que o 3.35 deve responder

- O ME2 devolve o primeiro header real do ring?
- `ShaderRingDword0` e `HqdActiveMecMe2HeaderDump0` correspondem?
- O primeiro dword permanece `0xC0027600`?
- `RPTR=1` e `MEC2_DECODING_PACKET` se repetem em boot frio?

Somente se essas observacoes coincidirem deve ser preparado um candidato
posterior alterando o bit compute dos headers PM4.

**NÃO VALIDADO EM HARDWARE.**
