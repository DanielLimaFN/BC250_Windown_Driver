# 661 — L1: Linux com `amdgpu.dc=0` (DCN como o GOP deixou), roteiro e critério

**Data:** 08/10/2026. **Natureza:** ensaio de referência fora do nosso driver; não conta como corrida e não valida nada nele.
**Script:** `tools/linux/bc250-l1-dc0.sh` (versão L1.1, LF, `bash -n` OK). **NÃO EXECUTADO.**
**Antecedentes:** doc 552 (Linux com `amdgpu` completo, 256 MB, sem trava em 8,5 min, desfecho L-B), doc 658 §2.2 e doc 660 §6.

## 1. A pergunta

No Windows, o nosso driver faz quase tudo como o `amdgpu` depois da paridade P1–P7, mas **não é dono do DCN**: o display fica como o GOP deixou e nós só copiamos quadros para o scanout. O `amdgpu` completo (doc 552) não trava.

**Se o `amdgpu` também não for dono do DCN, a placa trava?**

- Com `dc=0`, o `amdgpu` cuida de GFX, SDMA0/1, MMHUB, GART, IH e SMU, mas não carrega o bloco de display (DM).
- O OTG/HUBP seguem com o estado do GOP, como no nosso driver.

## 2. O que muda em relação ao doc 552 (e por quê)

| Item | Doc 552 | L1 | Por quê |
|---|---|---|---|
| Display | DM do `amdgpu` (DCN programado pelo Linux) | **nenhum**; DCN como o GOP deixou | é a variável do ensaio |
| Desktop | KDE no DP-1 | **não há**: com `dc=0` não existe conector, nem console depois do módulo carregar | consequência de `dc=0` (H, confirmada por `drm-conectores.txt`) |
| Compositor | kwin | **weston headless com renderer GL** (2560×1080) | compõe na GPU sem tela, papel do DWM |
| Clientes | vkcube, apps | vkcube (Vulkan, wayland), `weston-simple-egl` e `glmark2-es2-wayland` se existirem | 3D + composição |
| Paginação | 300 s de `amdgpu_evict_vram` a cada 2 s | idem, de 120 s até o fim (480 s numa sessão de 600 s) | a classe da nossa trava (FILL/TRANSFER no SDMA) |
| Operação | operador no desktop | **sozinho**: o script roda sem tela e **reinicia a máquina no fim** | não há como ver nada depois do módulo carregar |
| DCN | não lido | OTG0 frame/posição a cada 2 s; foto de OTG/HUBP/AUX no começo, a cada 30 s e no fim, pelo debugfs `amdgpu_regs` (só leitura, offsets do `bc250_dcn_probe_contract.h`) | mostra se o OTG continua varrendo durante o ensaio |
| Prova de carga | — | a fence do GFX tem de andar em 10 s (`CARGA_NA_GPU OK`); fences de GFX/SDMA0/SDMA1 a cada 2 s | Lei 4: sem isso o ensaio não mede a GPU |

**Diferença que fica:** sem DM, o `amdgpu` provavelmente libera a memória do framebuffer do GOP (stolen VGA) no fim da inicialização e a reutiliza. O OTG vai varrer memória que o Linux usa para outras coisas, então a tela mostra lixo. No nosso driver o scanout é reservado (H, por memória do código do `amdgpu`; confirmar no `amdgpu_vram_mm.txt` e no que a tela mostrar).

## 3. Roteiro automático (recomendado): `tools/linux/bc250-l1-auto.sh`

Exige Fedora **instalado** em disco, com GRUB e BLS. Numa sessão Live, use o roteiro manual (§3b).

1. No Fedora normal, com rede: `sudo bash /caminho/do/pendrive/linux/bc250-l1-auto.sh preparar`. O script:
   - instala `weston` e `vulkan-tools` se faltarem (e `glmark2` se houver);
   - copia o kit para `/opt/bc250-l1` e põe os dados em `/var/lib/bc250-l1`;
   - lê a UMA da CMOS;
   - põe o usuário em `render`/`video`, para o weston abrir a GPU sem sessão gráfica;
   - cria a entrada do GRUB `bc250-l1`, cópia da padrão com `amdgpu.dc=0 systemd.unit=multi-user.target bc250.l1=1` e `version` baixa, para nunca virar a padrão;
   - instala dois serviços;
   - arma o **próximo boot, uma vez só** (`grub2-reboot`);
   - **desliga a máquina**.
2. Espere alguns segundos e ligue (boot frio). A sessão 1 roda sozinha, sem tela. **A máquina desliga sozinha no fim (~11–12 min) = não travou.**
3. Ligue de novo: sessão 2, mesma coisa.
4. Ligue de novo: boot normal. O serviço de coleta gera `~/BC250_L1_RESULTADO.txt` e `~/bc250-l1-resultado-*.tar.gz`, e desfaz tudo (entrada, serviços, grupos).
5. **Se ligou e não desligou em ~15 min:** travou. Anote a hora e dê Reset. Como a entrada é de uma vez só, o boot seguinte é o normal; a coleta pós-trava roda sozinha, empacota e para o ensaio.
6. Traga o `.tar.gz` para `docs/evidence/`.

**Outros comandos:**
- `status`: em que passo está.
- `controle`: uma sessão com o `dc` normal, só se a L1 travar e depois da análise.
- `cancelar`: desfaz tudo e mantém os dados.

**Testado offline (08/10, Git Bash com comandos simulados):** a máquina de estados (2 sessões OK → coleta; trava na sessão 2 → coleta pós-trava) e a cópia da entrada BLS. **Não testado num Fedora real**: `grub2-reboot` com BLS, o serviço no boot, o weston sem sessão e o `amdgpu` com `dc=0` são hipótese até a primeira execução. Se algo falhar no `preparar`, ele para com o motivo; se falhar numa sessão, o `sessao-N-console.txt` diz o quê.

## 3b. Roteiro manual (Daniel)

**Preparação, uma vez, num boot normal do Linux com rede:**
1. Copie `tools/linux/` para o pendrive gravável (o mesmo da coleta do doc 552).
2. Instale o que falta: `sudo dnf install -y weston vulkan-tools` (`glmark2` opcional). Numa sessão Live sem persistência, a instalação se perde no reboot e tem de ser refeita em cada boot, já no console do passo 4 (o cabo de rede tem de estar ligado).
3. A CMOS em 256 MB (como em todas as corridas desde a 199): `sudo bash bc250-linux-referencia.sh --ler-vram`.

**Sessão (repetir para a sessão 2):**
4. Boot frio. No menu do GRUB, tecle `e` na entrada normal e acrescente ao fim da linha `linux`:
   `amdgpu.dc=0 modprobe.blacklist=amdgpu systemd.unit=multi-user.target`
   e tecle Ctrl+X. A edição vale só para esse boot.
5. Faça login no console de texto com o seu usuário (não root).
6. `sudo bash /caminho/do/pendrive/linux/bc250-l1-dc0.sh -o /caminho/do/pendrive/l1 --sessao 1`
7. O script confere tudo e mostra a hora-limite. Tecle Enter: o `amdgpu` carrega, **a tela some ou congela** (esperado). Anote o que aparece.
8. Não mexa em nada. Em ~11–12 min a máquina **reinicia sozinha**, e isso significa que não travou.
9. **Se não reiniciar até a hora-limite** (duração + 4 min): aperte o Num Lock. Se o LED não muda, o kernel está morto. Anote a hora e dê Reset. No boot seguinte (normal): `sudo bash bc250-l1-dc0.sh -o /caminho/do/pendrive/l1 --pos-trava`.
10. Sessão 2: repetir de 4 a 9 com `--sessao 2`.
11. Traga as pastas `bc250-l1-*` para `docs/evidence/`.

**Controle (só se a L1 travar):** o mesmo script com `--controle-dc1`, boot com o `amdgpu` normal e `systemd.unit=multi-user.target` (sem `dc=0` nem blacklist). Ele separa "dc=0" de "esta carga sem desktop".

## 4. Critério (escrito antes, Lei 10)

Duas sessões de 600 s. **Pré-condição** de cada sessão:
- `dc_lido=0` em `eventos.txt`;
- `CARGA_NA_GPU OK`;
- `WESTON vivo` com renderer GL (não pixman) em `weston.txt`;
- ≥ 150 despejos de VRAM;
- fences de GFX e SDMA andando até o fim.

Sem a pré-condição, a sessão é **inválida** e não entra no desfecho.

| Desfecho | Assinatura (o que o disco vai mostrar) | Implicação |
|---|---|---|
| **A** | As duas sessões com `RESUMO.txt` e `FIM_ENSAIO`, sem timeout de anel, reset de GPU ou MCE no dmesg | Herdar o DCN do GOP **não basta** para travar a placa. A causa está no que ainda fazemos diferente do `amdgpu` fora do DCN (memória, VMID/VM de 4 níveis, MEC, ordem de inicialização) e o DCN sai da frente. Próximo: inventário da paridade restante (doc 593 §4) com essa restrição |
| **B** | Uma ou as duas sessões sem `RESUMO.txt`: o batimento para antes dos 600 s e a máquina não reinicia sozinha | Com o DCN herdado, **até o Linux trava**: o mecanismo provável é o DCN do GOP (ou o scanout lendo memória reutilizada). Rodar o controle `--controle-dc1`. Se o controle não travar, o próximo trabalho é o DCN básico (P9 antecipado, pela governança) ou reservar/parar o scanout do GOP |
| **C** | O `amdgpu` não sobe com `dc=0` (sem `renderD`, erro no `modprobe.txt`) ou a carga não chega à GPU | Ensaio impossível nessa forma; descrever o erro antes de propor outra via |
| **D** | Não trava, mas o dmesg traz timeout de anel ou reset de GPU recuperado | Anotar o anel e o momento; comparar com os nossos TDR (doc 629/630) antes de julgar |
| **E** | Fora do mapa | Descrever o observado antes de julgar |

**Desfecho parcial não conta:** uma sessão A e outra inválida não é A; faz-se outra sessão. Uma trava com a pré-condição incompleta (por exemplo, weston em pixman) é E, não B.

**Teto:** 2 sessões L1 + no máximo 1 de controle. **Abandono:** se as duas primeiras tentativas caírem em C, a L1 é desistida e o motivo vai para a BASE.

## 5. Riscos e limites

- Só leitura em registrador. As leituras do DCN pelo `amdgpu_regs` são do Linux, não do nosso driver, e podem ser desligadas com `--sem-dcn`.
- Sem tela, o que o operador vê não serve de evidência: a evidência é o disco (`monitor.csv` e `batimento.txt` com sync a cada 2 s, `dmesg-continuo.txt`).
- A carga não é igual à do Windows: não há menu Iniciar nem DWM. É o mais perto possível sem display. A rajada que antecede a nossa trava é composição + paginação, e as duas estão presentes.
- 2 × 10 min a mais não provam estabilidade (Lei 1). Só mostram que essa configuração não trava nessa janela, que é mais longa que a mediana das nossas travas (~4 min).

**NÃO EXECUTADO. Nada aqui foi validado em hardware.**
