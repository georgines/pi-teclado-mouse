# pi-teclado-mouse

Firmware para Raspberry Pi Pico W que expõe um dispositivo USB HID composto
(teclado + mouse) controlado remotamente por comandos recebidos em uma
UART. Um host qualquer (PC, microcontrolador, script) fala o protocolo
binário descrito abaixo pela serial, e a placa traduz esses comandos em
eventos HID reais para o computador ligado à porta USB.

## Objetivo

Permitir que um processo externo — sem acesso direto ao barramento USB do
computador alvo — injete teclas e movimentos de mouse nesse computador,
enviando comandos simples por UART para o Pico. O Pico atua como ponte:
recebe pela UART, valida, e reproduz como HID nativo pela USB. Um display
OLED opcional mostra o estado atual do dispositivo (conexão USB, modo do
mouse, teclas pressionadas, contadores de erro) para depuração no bancada.

Este firmware não inclui MCP nem integração direta com agentes de IA — a
ponte é puramente o protocolo UART descrito neste documento; qualquer
integração de mais alto nível fala esse protocolo por fora.

## Hardware e pinagem

| Função | Interface | Pino |
|---|---|---|
| UART TX | UART0 | GP16 |
| UART RX | UART0 | GP17 |
| OLED SDA | I2C1 | GP14 |
| OLED SCL | I2C1 | GP15 |
| Alternar modo ABS/REL | GPIO, pull-up interno, ativo em nível baixo | GP6 |
| Contato 1 — POWER | GPIO saída, polaridade de compilação | GP18 |
| Contato 2 — RESET | GPIO saída, polaridade de compilação | GP19 |
| Contato 3 — auxiliar 1 | GPIO saída, polaridade de compilação | GP20 |
| Contato 4 — auxiliar 2 | GPIO saída, polaridade de compilação | GP4 |

- UART0 a 921600 baud, 8N1, sem controle de fluxo.
- O display é um SSD1306 I2C de 128x64. O firmware sonda primeiro o
  endereço `0x3C` e depois `0x3D`. A ausência ou falha do display não
  interrompe USB nem UART — o firmware segue operando sem tela.
- O botão em GP6 tem debounce de 30 ms. Cada pressionamento alterna entre
  os modos absoluto e relativo do mouse.
- Os quatro contatos são botões momentâneos em paralelo com os do painel
  frontal da máquina controlada. O acionamento tem de ser **seco e isolado**
  (relé de sinal ou optoacoplador); o GPIO nunca vai direto ao header do
  painel, cuja polaridade não é garantida entre fabricantes.

### Polaridade e estado de boot dos contatos

Cada contato tem polaridade própria — ativo em nível ALTO (padrão) ou ativo
em nível BAIXO — na tabela `ACTIVE_HIGH` de `src/contacts.hpp`. É escolha de
**compilação**, não comando de UART: um contato precisa estar comprovadamente
aberto ANTES de o host falar com a placa, e uma polaridade que só chegasse por
comando não teria resposta para "qual era ela durante o boot".

- `contacts::init()` é a primeira coisa que `main()` executa, antes de
  `board_init()`, e leva os quatro contatos ao nível ABERTO.
- Um reset do Pico com um contato fechado por `CONTACT_DOWN` termina com o
  contato aberto: o contato não sobrevive ao firmware que o fechou.
- Desconexão ou suspensão do USB abre todos os contatos e cancela os pulsos
  pendentes; overflow de buffer ou de fila faz o mesmo.

**Aviso — módulo de relé ativo em BAIXO é o caso perigoso.** O pino do Pico
está em nível baixo enquanto o firmware não o configura, e nível baixo, nesse
módulo, é o relé fechado: o botão de força afundado durante o boot da placa.
Quem usar módulo ativo em baixo precisa garantir a abertura **no hardware**
(pull-up externo no lado do módulo) e escolher relé ou optoacoplador cujo
estado sem energia também seja aberto. Nenhuma linha de firmware roda antes do
próprio firmware.

## Arquitetura

- **Núcleo 0**: `tud_task()`, leitura da UART, parser de quadros, fila de
  comandos, submissão dos relatórios HID e leitura do botão de modo.
- **Núcleo 1**: inicialização e renderização do display OLED.
- O estado exibido na tela é transferido entre os núcleos por snapshots
  (`DeviceSnapshot`, ver `src/shared_state.hpp`), com seção crítica curta.
  Esse mesmo struct é reaproveitado como payload da resposta `STATUS` do
  protocolo UART.

O dispositivo USB expõe duas interfaces HID:

1. **Teclado** (boot HID): modificadores + até seis teclas simultâneas
   (6KRO).
2. **Mouse**: uma única interface com dois report IDs declarados desde a
   enumeração — um para movimento relativo (deltas X/Y de 16 bits com
   sinal) e outro para posicionamento absoluto (X/Y de 0 a 32767), cinco
   botões, roda vertical e roda horizontal. Por ter dois top-level
   collections, o Windows enumera essa interface como dois dispositivos de
   mouse (`COL01`/`COL02`) — comportamento esperado do driver HID
   genérico, não uma falha do firmware.

## Protocolo UART

### Formato do quadro

Campos com mais de um byte usam little-endian.

```text
A5 5A | versão:u8 | tipo:u8 | sequência:u16 | tamanho:u16 | payload:0..64 | CRC16:u16
```

- `A5 5A`: bytes de sincronismo.
- Versão do protocolo atual: `1`.
- Payload máximo: 64 bytes.
- CRC-16/CCITT-FALSE (polinômio `0x1021`, valor inicial `0xFFFF`), cobrindo
  do campo `versão` até o fim do `payload`.
- Um quadro parcial expira após 20 ms sem novos bytes; o parser volta a
  procurar `A5 5A`.
- Depois de qualquer quadro inválido (CRC, versão, tamanho ou timeout), o
  parser resincroniza sozinho no próximo `A5 5A` recebido — não é preciso
  reiniciar a conexão.
- RX e TX são servidos por interrupção, com buffers circulares de 512
  bytes cada. A fila interna de comandos pendentes tem 32 posições.

### Comandos (host -> Pico)

| Código | Comando | Payload |
|---|---|---|
| `0x01` | `PING` | vazio |
| `0x02` | `GET_STATUS` | vazio |
| `0x10` | `KEY_DOWN` | usage HID (`u8`) |
| `0x11` | `KEY_UP` | usage HID (`u8`) |
| `0x12` | `KEY_RELEASE_ALL` | vazio |
| `0x14` | `KEY_HOLD` | usage HID (`u8`) + duração em ms (`u16`, 1..65535) |
| `0x15` | `KEY_HAMMER` | usage HID (`u8`) + duração (`u16`) + intervalo (`u16`) |
| `0x20` | `MOUSE_MOVE` | X (`u16`) + Y (`u16`) |
| `0x21` | `MOUSE_BUTTONS` | bitmap dos cinco botões (`u8`) |
| `0x22` | `MOUSE_WHEEL` | vertical (`i8`) + horizontal (`i8`) |
| `0x23` | `MOUSE_RELEASE_ALL` | vazio |
| `0x24` | `SET_MOUSE_MODE` | `0` = relativo, `1` = absoluto |
| `0x30` | `CONTACT_PULSE` | contato (`u8`, 1..4) + duração em ms (`u16`, 1..65535) |
| `0x31` | `CONTACT_DOWN` | contato (`u8`, 1..4) |
| `0x32` | `CONTACT_UP` | contato (`u8`, 1..4) |
| `0x33` | `GET_CONTACTS` | vazio |

Em `MOUSE_MOVE`, X/Y são interpretados como `i16` no modo relativo e como
`u16` no modo absoluto; valores absolutos acima de 32767 são rejeitados.
`KEY_DOWN`/`KEY_UP` aceitam qualquer usage HID, incluindo modificadores
(`0xE0`-`0xE7`); reenviar o mesmo comando é idempotente. Tentar manter uma
sétima tecla comum pressionada ao mesmo tempo resulta em NACK.

### Teclas temporizadas (`0x14`, `0x15`)

O toque avulso e o acorde (`Ctrl+C`, `Alt+F4`) se resolvem no host,
empacotando `KEY_DOWN` e `KEY_UP` na mesma drenagem da fila — não precisam de
comando novo. O que precisa do firmware é só o que envolve TEMPO, porque a
porta serial é compartilhada entre teclado e mouse: esperar uma janela de 8
segundos no host congelaria o mouse da mesma estação por 8 segundos.

- `KEY_HOLD` afunda a tecla, conta o tempo sozinho e a solta.
- `KEY_HAMMER` dá toques curtos espaçados pelo intervalo ao longo da duração,
  e termina sempre com a tecla solta. Serve para entrar no setup do BIOS, no
  menu de boot ou no GRUB.

O `ACK` dos dois — e o de `CONTACT_PULSE` — sai ao **ACEITAR**, nunca ao
terminar. Um `KEY_HOLD` de 3 s que só respondesse no fim estouraria o timeout
do host, que trataria isso como cabo caído e descartaria a fila a cada tecla.

`0x13 KEY_TAP` foi considerado e **recusado**: um toque atômico no firmware não
resolve nada que o host já não resolva empacotando os dois eventos, e a 921600
baud o par de quadros não é gargalo de nada. Comando novo sem defeito que o
justifique é superfície de ataque e de manutenção de graça.

#### Tabela de temporizadores

`KEY_HOLD`, `KEY_HAMMER` e `CONTACT_PULSE` compartilham **uma** tabela fixa de
**10 entradas** (`src/timed_actions.*`), varrida no laço principal do núcleo 0
— nunca um `sleep` no caminho do parser. Dez é o máximo físico: seis teclas
comuns simultâneas (o teclado é 6KRO) mais os quatro contatos do painel
frontal; não cabe uma décima primeira intenção temporizada porque não existe
hardware para ela.

Uma entrada por alvo dentro de cada domínio (teclado, contatos). Um
`KEY_HOLD`/`KEY_HAMMER` novo sobre um usage que já tem um em curso
**substitui** o anterior e reinicia a contagem — não empilha.

#### Cancelamento

Um temporizador pendente é uma tecla que vai ser afundada no FUTURO, numa
máquina real, possivelmente depois que ninguém mais estiver no controle. Todo
caminho de segurança termina também em "nenhum temporizador pendente":

1. `KEY_RELEASE_ALL` cancela todos os temporizadores de teclado, além de
   soltar as teclas.
2. `KEY_UP` sobre um usage com `KEY_HOLD`/`KEY_HAMMER` em curso cancela aquele
   temporizador.
3. Desconexão, suspensão ou reinicialização do USB cancela todos os
   temporizadores e abre todos os contatos.
4. Overflow de buffer ou de fila cancela todos os temporizadores e abre todos
   os contatos.
5. `CONTACT_UP` cancela o pulso pendente daquele contato.

Cancelar solta o alvo: nenhum caminho de cancelamento deixa tecla afundada ou
contato fechado.

#### Tolerância de tempo

O firmware promete **±5 ms ou ±1% da duração, o que for maior**. O polling USB
é de 1 ms e o núcleo 1 redesenha o OLED; prometer precisão melhor que isso
seria mentira medida. Uma janela de BIOS é medida em segundos — não precisa
melhor.

#### O que não muda

O payload do `STATUS` continua com 27 bytes. Uma tecla mantida por
temporizador aparece em `keys[6]` e `modifiers` como qualquer outra, e isso é a
verdade: do ponto de vista da máquina controlada não existe diferença entre uma
tecla presa por um timer e uma presa por um dedo. Relatar temporizadores
pendentes seria aumento de versão do protocolo.

Não existe relatório de capacidades. O host descobre o suporte mandando o
comando e lendo a resposta: `NACK` com erro `3` (comando desconhecido) é
firmware antigo. A versão do quadro continua `1`.

### Botões de energia (`0x30`..`0x33`)

`CONTACT_PULSE` fecha o contato, conta a duração e o abre — é o pulso que liga
uma máquina desligada (≈500 ms), força o desligamento de uma travada (≈6000 ms)
ou reinicia (≈200 ms no contato 2). `CONTACT_DOWN`/`CONTACT_UP` fecham e abrem
sem temporizador.

Os comandos de contato **não exigem USB pronto**, e isso é deliberado: a
máquina controlada desligada é exatamente o caso em que o host precisa fechar o
contato de força, e com ela desligada não existe enumeração USB para esperar.
`USB_NOT_READY` vale para os comandos de teclado e mouse.

`GET_CONTACTS` responde `CONTACTS` (`0x83`) com **um** byte: bitmap dos quatro
contatos, bit 0 = contato 1, `1` = fechado. O bitmap é o estado **elétrico**
lido de volta dos pinos do Pico (`gpio_get` sobre a saída), não uma sombra da
intenção do firmware — pega um pino que não assumiu o nível comandado. Ele não
enxerga o outro lado do relé: um relé colado ou um cabo solto continua
invisível daqui.

### Respostas e eventos (Pico -> host)

| Código | Tipo | Significado |
|---|---|---|
| `0x80` | `ACK` | comando validado e aceito |
| `0x81` | `NACK` | comando recusado, com código de erro |
| `0x82` | `STATUS` | resposta a `GET_STATUS`, snapshot completo do estado |
| `0x83` | `CONTACTS` | resposta a `GET_CONTACTS`, bitmap (`u8`) dos quatro contatos |
| `0x90` | `EVENT_STATUS` | evento assíncrono, não solicitado pelo host |

`ACK`/`NACK` reutilizam o número de sequência do comando que os originou.
Eventos assíncronos sempre usam sequência `0`. `ACK` confirma apenas que o
comando foi aceito na fila interna — não é garantia de que o relatório HID
já chegou ao sistema operacional do host USB.

Códigos de erro usados em `NACK` e em `EVENT_STATUS`:

| Código | Erro |
|---|---|
| `1` | CRC inválido |
| `2` | Versão de protocolo não suportada |
| `3` | Comando desconhecido |
| `4` | Tamanho de payload incorreto |
| `5` | Fila de comandos cheia |
| `6` | Payload inválido |
| `7` | Excesso de teclas simultâneas |
| `8` | Coordenada absoluta fora de faixa |
| `9` | USB não conectado/pronto |
| `10` | Timeout de quadro parcial |

Nas faixas temporizada e de contatos, nenhum código de erro novo é usado:

| Situação | NACK |
|---|---|
| Duração 0, ou intervalo 0, ou intervalo maior que a duração | `6` |
| `KEY_HOLD`/`KEY_HAMMER` que exigiria a sétima tecla comum simultânea | `7` |
| Tabela de temporizadores cheia | `5` |
| USB desconectado ou suspenso (comandos de teclado e mouse) | `9` |
| Payload de tamanho errado | `4` |
| Contato fora de 1..4, ou duração de pulso fora de 1..65535 ms | `6` |

`EVENT_STATUS` também é emitido, sem que o host peça, para: conexão,
desconexão e suspensão USB; mudança de modo do mouse; falha e recuperação
do display; overflow de buffer ou de fila. Comandos HID recebidos com o USB
desconectado ou suspenso são recusados com NACK (`UsbNotReady`), evitando
que entradas fiquem "presas" depois de uma reconexão.

### Exemplo de quadro

`PING` com sequência 1 (payload vazio):

```text
A5 5A 01 01 01 00 00 00 55 97
```

Resposta esperada: `ACK` (`0x80`) com sequência `1` e payload vazio.

## Compilação

Pré-requisitos: Pico SDK 2.3.0 e toolchain ARM configurados (ex.: via
extensão oficial do VS Code para Pico, que já resolve
`PICO_SDK_PATH`/`PICO_TOOLCHAIN_PATH`).

```bash
cmake -G Ninja -B build -DPICO_BOARD=pico_w
cmake --build build --target dev_hid_composite
```

O artefato final fica em `build/dev_hid_composite.uf2`.

## Gravação

1. Segure o botão `BOOTSEL` da placa e conecte (ou reconecte) o cabo USB.
2. A placa aparece como um drive de armazenamento removível chamado
   `RPI-RP2`.
3. Copie `build/dev_hid_composite.uf2` para esse drive. A placa grava o
   firmware, reinicia sozinha e passa a rodar a nova versão em poucos
   segundos.

## Testes

Teste de unidade do parser de protocolo e da máquina de estado HID, sem
depender de hardware:

```bash
g++ -std=c++17 -Wall -Wextra -Isrc tests/test_uart_protocol.cpp \
    src/uart_protocol.cpp src/hid_state.cpp src/timed_actions.cpp -o /tmp/t && /tmp/t
```

Cobre CRC16, sincronismo/resincronização, timeout, validação de versão e
tamanho, 6KRO, troca de modo e validação de botões/faixa absoluta do mouse, e
a tabela de temporizadores: fim de janela, alternância do martelo,
substituição em vez de empilhamento, recusa da décima primeira entrada,
convivência de teclado e contatos na mesma tabela, cancelamentos e volta do
relógio de 32 bits. Não substitui teste em hardware real — o protocolo só pode ser
validado ponta a ponta com a UART física conectada e um host enviando
comandos.

## Estrutura do projeto

| Caminho | Conteúdo |
|---|---|
| `main.cpp` | ponto de entrada, laço principal do núcleo 0 |
| `usb_descriptors.cpp` | descritores USB/HID (teclado + mouse relativo/absoluto) |
| `src/uart_protocol.*` | codec do quadro binário (parser + encoder), CRC16 |
| `src/uart_io.*` | I/O da UART, fila de comandos, despacho, ACK/NACK/eventos |
| `src/hid_state.*` | estado de teclado (6KRO) e mouse (botões, posição, modo) |
| `src/timed_actions.*` | tabela fixa de 10 temporizadores (teclas mantidas/marteladas e pulsos de contato) |
| `src/contacts.*` | quatro contatos secos do painel frontal, polaridade de compilação |
| `src/hid_usb.*` | submissão dos relatórios HID à pilha TinyUSB |
| `src/mode_button.*` | leitura debounced do botão de troca de modo (GP6) |
| `src/oled_display.*` | inicialização e desenho no SSD1306 (núcleo 1) |
| `src/shared_state.*` | snapshot compartilhado entre núcleos |
| `tests/test_uart_protocol.cpp` | testes de host do protocolo e do estado HID |
| `docs/` | documentação de projeto e de investigações de bugs |
