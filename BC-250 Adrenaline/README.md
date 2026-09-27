# BC-250 Adrenaline

Aplicativo desktop para Windows, com interface inspirada no Windows 11 e dados inteiramente simulados. Criado na pasta solicitada, independente do driver.

## Abrir

Clique duas vezes em **Abrir BC-250 Adrenaline.cmd**. Se o pacote estiver presente, ele abre `dist/BC-250-Adrenaline-win32-x64/BC-250-Adrenaline.exe`. Mantenha a pasta do executável inteira: o EXE depende dos arquivos que o acompanham. Não é necessário instalar Node.js para executar o pacote.

Para desenvolvimento, com Node.js instalado:

```powershell
npm ci
npm start
```

`npm run preview` abre um servidor local em http://127.0.0.1:4173 para revisão no navegador. `npm run pack` gera a pasta portátil para Windows x64. O aplicativo desktop usa Electron, com controles reais de janela do Windows e conteúdo local. Ele não usa WinUI; o visual Fluent é implementado na interface.

## Experiência

1. Primeiro acesso: dashboard de instalação, compatibilidade fictícia, progresso, cancelamento e sucesso explícito como simulação.
2. Conclusão: popup opcional de café com Pix, cartão e cripto, valores de R$ 10, R$ 25 e R$ 50. O usuário pode simular o apoio, recusar ou fechar com Escape.
3. Dashboard completa: mapa térmico interativo, leituras atualizadas, 34 sensores com mínimo/máximo da sessão, histórico dos últimos 60 pontos, desempenho, detalhes do sistema e driver.
4. Reabertura: o estado salvo leva diretamente à dashboard. Para testar um driver já instalado sem executar o fluxo, abra Sensores antes de instalar e clique em **Simular driver já instalado**.
5. Configurações: repetir instalação, escolher tema claro/escuro/sistema, alterar intervalo (700 ms, 1 s ou 2 s), cenário, monitor e NVMe.

Cenários: uso cotidiano, repouso, jogo, carga elevada e indisponibilidade. Alteram somente a fonte mock. A pausa congela os dados e preserva o instante da última amostra. Exportação JSON inclui `source: mock` e identificação de amostra simulada no nome do arquivo. Valores indisponíveis usam `null`, aparecem como “—” e não contaminam mínimos/máximos.

## Referência analisada

Fontes locais: `../referencias/BC250-Telemetry/README.md`, `hardware.md`, `web/v2/memory.js` e `../referencias/bc250-thermal.svg`.

| Referência Linux | Nesta prévia Windows |
| --- | --- |
| PMBus: CPU/GPU VIN, VOUT, IOUT, POUT e temperatura VRM | 10 sensores simulados e potência total derivada |
| hwmon: temperaturas e clocks CPU/GPU, GPU PPT | Sensores, cartões e gráficos |
| hwmon: NVMe, NCT T14/T15, RPM e PWM | Sensores e resumo de refrigeração |
| Serviço opcional GDDR6: 8 chips, média e hotspot | 8 leituras individuais e agregados derivados |
| Topologia: núcleos físicos e CUs ativos | Sistema e desempenho, marcados como simulados |
| Overclock: alvos CPU, faixa GPU, limite e estado | Perfil de frequências somente leitura |
| Idade/validade e erros dos sensores | Estado indisponível explícito, sem inventar zero |

Uso CPU/GPU e memória ocupada são extensões da demonstração, não campos disponíveis no contrato Linux original. Valores `raw` da memória permanecem `null`: nenhum valor de registrador é inventado.

O coletor Linux depende de sysfs/hwmon, I2C/PMBus (algumas leituras exigem modificação física) e um serviço opcional para as temperaturas GDDR6. Essas capacidades não são automaticamente transportáveis para o Windows. Aqui, `telemetry.js` isola a fonte mock; a integração real terá de fornecer as mesmas leituras com origem, disponibilidade e validade verificáveis.

## SVG

O SVG original está em `assets/bc250-thermal.svg`. `assets/board.js` incorpora uma cópia textual para funcionamento totalmente local, inclusive sem servidor. IDs preservados:

- `cpu`: cor pela maior leitura válida entre CPU e GPU. O die é compartilhado; não é um sensor físico adicional.
- `ram-1` a `ram-8`: cor individual, seleção com mouse/Enter/espaço e temperatura em texto acessível.
- `dp-monitor-name` e `nvme-info`: alterados com `textContent`, inclusive quando o nome contém caracteres XML.

A instalação usa uma faixa compacta sobre código aberto. Cores: azul em 20 °C, amarelo em 70 °C, rosa em 120 °C, cinza sem leitura. **Escala visual, não limites de segurança.** A numeração é ilustrativa, sem mapeamento validado a canais reais. Ao alterar o SVG fonte, regenere `board.js` com `ConvertTo-Json` como string JavaScript; não interpole XML sem escape.

## Limites desta entrega

Tudo é mockado por solicitação: detecção do driver, instalação, status, sensores, monitor/NVMe e pagamento. Nenhum script de bind, registro, serviço, dispositivo, driver ou registrador é acessado. Nenhum dado financeiro é coletado. O EXE é um pacote de desenvolvimento não assinado. O fluxo de instalação do driver não é um instalador do aplicativo no sistema.

**NÃO VALIDADO EM HARDWARE**: esta entrega valida uma experiência de interface, não funcionamento, aceleração ou telemetria física do driver BC-250.

## Validação

```powershell
npm test
npm run test:ui
```

`tests/telemetry.test.cjs`: quatro testes de coerência, falta de leitura, retenção/min-max e escala térmica. `tests/ui.cjs`: teste funcional no Edge headless, sem alterar o perfil pessoal do navegador. Percorre instalação/cancelamento/sucesso, três meios de doação, persistência, simulação de driver existente, sensores/filtro/exportação, SVG/cores/texto, pausa, temas, indisponibilidade e layout a 1024 px. Capturas locais em `artifacts/`.

Referência BC250-Telemetry de onlinermm e fansteori, licença MIT preservada em `assets/BC250-Telemetry-LICENSE.txt`. SVG fornecido pelo proprietário do projeto. Projeto independente, sem afiliação à AMD ou à Microsoft.

Teste do pacote Windows: `node tests/native.cjs`. Confere abertura do EXE, isolamento de renderer, SVG, minigráficos e contraste do tema escuro em perfil próprio de teste.

Ajuste visual: cartões compactos, navegação recolhida abaixo de 900 px, layout validado a partir de 640 px e símbolo angular vermelho/branco. Fonte do ícone: `assets/icon.svg`; `node tools/render-icon.cjs` gera PNG e ICO de 16 a 256 px.
