# Firmware da BC-250

Estes arquivos são entradas de laboratório, não blobs implicitamente confiáveis.

Antes de uma carga, o consumidor deve conferir nome, tamanho, SHA-256, versão de
IP, limites do payload e CRC-32 contra `manifest.json`. A validação estrutural
não prova que o PSP aceitou o firmware nem que a engine passou a executá-lo.

Regere o manifesto na raiz do projeto:

```powershell
python tools/fwinspect.py firmware --output firmware/manifest.json
```

Os estados devem permanecer separados:

1. arquivo estruturalmente válido;
2. transporte PSP concluído;
3. firmware aceito;
4. firmware ativo, demonstrado pela engine física.

Sem o quarto estado e as demais provas aplicáveis: **NÃO VALIDADO EM HARDWARE**.
