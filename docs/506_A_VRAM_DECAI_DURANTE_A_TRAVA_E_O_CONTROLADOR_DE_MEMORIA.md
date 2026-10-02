# 506 — A VRAM decai durante a trava: o controlador de memória para

> 28/09/2026, offline, sobre as Corridas 152 e 154–157. Nenhum código mudou.

## 1. O dado (P0: contagens dos dumps DX.66 no boot seguinte a cada trava)

| Corrida | Tempo travada até o reset (log e evento 41, aprox.) | Página de migalhas (VRAM 0xFA00000) |
|---|---|---|
| 154 | ~44 s | presente; 10 slots com sequência impossível |
| 155 | ~71 s | presente; 23 slots; dword de estado corrompido |
| 152 | ~73 s | presente; 27 slots; estado corrompido |
| 156 | ~81 s | magic certo, **versão 2 → 0x200042**; o leitor rejeita |
| 157 | ~48 min | **magic virou lixo**: página inteira perdida |

**As sequências "impossíveis" não são boots antigos:**
- `Bc250CrumbArm` zera a página inteira ao armar (P1, código);
- elas são **potências de dois** ou um valor real com **um bit alto trocado**: 34.123.550 − 2²⁵ = 569.118, a 52 da última sequência 569.170 da 155;
- os detalhes têm um bit só ligado (`0x400000`, `0x080000`, `0x80000C` = FLUSH_TLB com o bit 23 trocado).

Isso é **perda de carga de célula de DRAM**, e ela **cresce com o tempo travado**.

**Correção de registro das Corridas 152 e 155:** a entrada "CPU04 PAGINACAO ... fase=ANTES" não pode ser lida como acesso pendente. A fase ANTES (`0xB0`) e a DEPOIS (`0xA0`) diferem num único bit, e a entrada tem outros bits comprovadamente trocados.

## 2. O que isso significa (depende de uma resposta do Daniel)

**Se o reinício foi pelo botão reset, sem cortar a energia:**
- a memória ficou energizada e **sem refresh durante a trava**;
- quem dá o refresh é o controlador de memória do SoC;
- conclusão: **o controlador de memória parou**.

Na BC-250 a GDDR6 é a memória única, da GPU e **da CPU**. Com o controlador parado:
- a CPU não busca instruções nem dados, e tudo congela;
- não há DPC watchdog (nada roda), nem atalho de teclado, nem WHEA;
- a tela fica preta, porque o scanout lê da mesma memória.

Isso explica **todos** os sinais medidos, inclusive por que nenhuma instrumentação nossa de CPU nomeou o ponto.

**Se a energia foi cortada**, o decaimento pode ter acontecido com a placa desligada, e esta leitura não vale.

## 3. O que pode parar um controlador de memória numa APU AMD (H, a pesquisar com proveniência)

A troca de estado do relógio ou da energia da memória (memória, fabric, estados de baixo consumo), comandada pelo firmware de gerenciamento (SMU), é o suspeito natural:
- é disparada por **atividade**, o que casa com a correlação "mais paginação, mais trava" (0/2 com KD lento, 0/2 com M1, 5/6 depois da FPS.1);
- numa troca do relógio da memória, **o controlador de display precisa colaborar**: segurar o scanout e sinalizar que a troca é permitida. Hoje o display está no estado deixado pelo GOP (UEFI), e o nosso driver não participa desse protocolo;
- **aqui o DCN pode estar envolvido**, não como defeito de programação, mas como parte que não é consultada numa troca de relógio.

Isso **não está provado** e não se confirma sem proveniência. Nada no repositório descreve hoje esse protocolo para o GC 10.1.3 / Cyan Skillfish.

## 4. Próximo passo
1. **Pergunta ao Daniel (decide a leitura):** como foi o reinício em cada trava: botão reset, botão de ligar segurado, ou corte de energia?
2. **Se foi reset:** pesquisa de proveniência, só leitura e offline:
   - como o amdgpu upstream trata troca de relógio de memória e de fabric no Cyan Skillfish (`cyan_skillfish_ppt.c`, SMU 11.8);
   - quais registradores **somente leitura** do DCN e do SMU mostram o estado dessa troca.

   Depois, um candidato que só **lê** e registra esse estado a cada batimento. Nenhuma escrita, nem em SMU nem em DCN (a governança proíbe escrita de DCN fora da P9).
3. A instrumentação da "última operação de paginação" (doc 505 §5) fica em espera: com a memória decaindo, ela precisaria de checksum, e perde valor se a causa for de plataforma.
