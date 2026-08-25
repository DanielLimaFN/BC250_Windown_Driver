# Firmware da BC-250

Estes arquivos são entradas de laboratório, não blobs implicitamente confiáveis.

Antes de uma carga, o consumidor deve conferir nome, tamanho, SHA-256, versão de
IP, limites do payload e CRC-32 contra `manifest.json`. A validação estrutural
não prova que o PSP aceitou o firmware nem que a engine passou a executá-lo.

Regere o manifesto na raiz do projeto:

```powershell
python tools/fwinspect.py firmware --output firmware/manifest.json
```

Nos headers GFX v1.0, `jt_offset` e `jt_size` sao contados em DWORDs. Para o
`cyan_skillfish2_mec.bin` atual, `0x1052C * 4 = 267440` bytes de MEC principal
e `0xE0 * 4 = 896` bytes de MEC_JT1; a soma fecha os 268336 bytes do payload.
`cyan_skillfish2_mec2.bin` nao deve ser tratado como a jump table. O gate usado
pelo pacote readonly e:

```powershell
python tools/audit-bc250-cp-manifest.py
```

O inspetor também decodifica o fallback `cyan_skillfish_gpu_info.bin`, quando
esse arquivo for fornecido. Com IP Discovery presente o kernel usa a tabela GC e
não precisa desse blob. O parser informa SE, SH/SE e CU máximo/SH, mas deixa
explicitamente
indefinida a quantidade de `GRBM_GFX_INDEX.INSTANCE_INDEX`: esse campo não existe
no payload `gpu_info` e não pode ser inventado a partir do total nominal de CUs.
Para capturar IP Discovery, a linha de topologia do `dmesg` e o eventual arquivo
instalado, use `tools/collect-linux-gpu-info.sh`; a coleta não acessa MMIO.

Os estados devem permanecer separados:

1. arquivo estruturalmente válido;
2. transporte PSP concluído;
3. firmware aceito;
4. firmware ativo, demonstrado pela engine física.

Sem o quarto estado e as demais provas aplicáveis: **NÃO VALIDADO EM HARDWARE**.
