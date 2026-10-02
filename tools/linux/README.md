# Guia de Operação no Linux — BC-250 (Frente L)

Este diretório contém o kit completo para executar o ensaio de referência no Linux e destravar a VRAM real da placa BC-250.

---

## 🎯 O que este kit resolve

1. **Ajuste Real da VRAM na CMOS (Fim do gargalo de 512 MB / 256 MB BAR):**
   A BC-250 (BIOS P3.00/P5.00) ignora o menu da BIOS e lê a VRAM da CMOS APCB (portas `0x72`/`0x73`). Com a ferramenta integrada, podemos gravar `UMA_SIZE = 2048` ou `4096` MB diretamente.
2. **Diagnóstico da Trava Dura do Controlador de Memória:**
   Mede se a placa congela sob carga de aceleração com o driver oficial (`amdgpu` + Mesa). Se congelar, a causa é térmica/elétrica/hardware. Se não congelar, coletamos o gabarito oficial de registradores, clocks e temperaturas.
3. **Captura do EDID Real por DisplayPort:**
   O Linux lê o EDID oficial do monitor via canal AUX do DisplayPort, arquivando o binário exato para o driver Windows.
4. **Extração da PowerPlay Table (SMU):**
   Copia a tabela binária inteira da SMU com limites de tensão, clock e temperatura da Cyan Skillfish.

---

## 🛠️ Passo 1: Preparar o Pendrive

1. Baixe o **Fedora 43 Workstation Live** (apontado pela comunidade da BC-250 por trazer kernel atualizado e Mesa 25.1+ com suporte pronto à Cyan Skillfish 2).
2. Grave em um pendrive com o Rufus ou BalenaEtcher.
3. Copie esta pasta inteira `tools/linux/` para um **segundo pendrive formatado em FAT32 ou NTFS** (ou uma partição gravável do mesmo pendrive), para que os resultados sejam gravados em armazenamento persistente e sobrevivam caso ocorra um congelamento.

---

## 🚀 Passo 2: Inicialização

1. Desligue o computador da tomada por alguns segundos (boot frio).
2. Ligue e entre no boot menu do BIOS (tecla F11 ou Del).
3. Selecione o pendrive do Fedora e escolha a **opção normal de boot** (NÃO use "Troubleshooting / Basic Graphics Mode", pois o modo básico desativa o `amdgpu`).

---

## 💻 Passo 3: Execução no Terminal

Abra o terminal no Linux e navegue até a pasta gravável no pendrive onde estão os scripts:

```bash
cd /run/media/liveuser/SEU_PENDRIVE/linux   # ou a pasta correspondente
```

### Cenário 0 (primeiro sempre): o `amdgpu` assumiu a placa?
```bash
sudo bash bc250-linux-referencia.sh --verificar-amdgpu
```
*Diz se falta módulo, firmware ou Mesa, ou se tudo está instalado e o probe falhou. Nesse
segundo caso (o da coleta de 01/10) ele lê, só para leitura, o último 1 MiB do carveout da UMA
e procura a tabela de IP discovery. Sem `amdgpu` ativo o teste de estabilidade não mede a GPU.*

Se faltar algo da pilha (instala **só o que falta**, sem dependências fracas, sem docs, limpa o
cache e recusa com menos de 300 MB livres):
```bash
sudo bash bc250-linux-referencia.sh --instalar
```
Depois, boot frio e `--verificar-amdgpu` de novo.

### Cenário A: Ler o tamanho atual da VRAM na CMOS
```bash
sudo bash bc250-linux-referencia.sh --ler-vram
```
*Ele exibirá a assinatura da CMOS, o checksum e o valor atual de `UMA_SIZE` (ex: 512).*

### Cenário B: Ajustar a VRAM para 2048 MB ou 4096 MB
```bash
sudo bash bc250-linux-referencia.sh --set-vram 2048
# ou para 4 GB:
sudo bash bc250-linux-referencia.sh --set-vram 4096
```
*Após gravar, desligue a máquina completamente (power cycle) para que a BIOS/ABL inicialize com o novo tamanho.*

### Cenário C: Captura Total de Dados (Inventário rápido, ~30s)
Se você só quiser capturar todos os registradores, EDID, PowerPlay table e BARs sem esperar o teste longo:
```bash
sudo bash bc250-linux-referencia.sh --apenas-dados
```

### Cenário D: Teste Completo de Estabilidade (600s com estresse e monitor)
```bash
sudo bash bc250-linux-referencia.sh -d 600
```
*O script coletará todo o inventário, iniciará gravação a cada 2s com `sync` em disco, oferecerá carga automática (vkcube, depois 300 s de **carga de paginação** — despejo da VRAM a cada 2 s, que força cópias no SDMA, a mesma classe da trava do driver Windows — e glmark2 se houver), e monitorará temperaturas de GPU (edge, junction, GDDR6 memory), CPU e tensões. Se o `amdgpu` não estiver ativo, ele avisa e pergunta antes de seguir.*

---

## 🚨 Se a máquina travar / congelar durante o teste

1. Anote se a tela apagou ou se a imagem congelou.
2. Pressione o botão Reset ou desligue a máquina.
3. Dê boot novamente no pendrive do Linux.
4. Abra o terminal e rode:
   ```bash
   sudo bash bc250-linux-referencia.sh --pos-trava
   ```
*O script lerá os arquivos salvos em disco imediatamente antes do congelamento (último batimento, temperatura exata e últimas mensagens do dmesg) e gerará o laudo pericial.*

---

## 📦 Passo 4: Trazer os dados para o Windows

O script gera um arquivo compactado `bc250-linux-YYYYMMDD-HHMMSS.tar.gz`.
Basta copiar esse arquivo ou a pasta gerada para o Windows no repositório em:
`docs/evidence/`
