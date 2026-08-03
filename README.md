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

- UART0 a 921600 baud, 8N1, sem controle de fluxo.
- O display é um SSD1306 I2C de 128x64. O firmware sonda primeiro o
  endereço `0x3C` e depois `0x3D`. A ausência ou falha do display não
  interrompe USB nem UART — o firmware segue operando sem tela.
- O botão em GP6 tem debounce de 30 ms. Cada pressionamento alterna entre
  os modos absoluto e relativo do mouse.

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
| `0x20` | `MOUSE_MOVE` | X (`u16`) + Y (`u16`) |
| `0x21` | `MOUSE_BUTTONS` | bitmap dos cinco botões (`u8`) |
| `0x22` | `MOUSE_WHEEL` | vertical (`i8`) + horizontal (`i8`) |
| `0x23` | `MOUSE_RELEASE_ALL` | vazio |
| `0x24` | `SET_MOUSE_MODE` | `0` = relativo, `1` = absoluto |

Em `MOUSE_MOVE`, X/Y são interpretados como `i16` no modo relativo e como
`u16` no modo absoluto; valores absolutos acima de 32767 são rejeitados.
`KEY_DOWN`/`KEY_UP` aceitam qualquer usage HID, incluindo modificadores
(`0xE0`-`0xE7`); reenviar o mesmo comando é idempotente. Tentar manter uma
sétima tecla comum pressionada ao mesmo tempo resulta em NACK.

### Respostas e eventos (Pico -> host)

| Código | Tipo | Significado |
|---|---|---|
| `0x80` | `ACK` | comando validado e aceito |
| `0x81` | `NACK` | comando recusado, com código de erro |
| `0x82` | `STATUS` | resposta a `GET_STATUS`, snapshot completo do estado |
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
g++ -std=c++17 -Wall -Wextra -Isrc tests/test_uart_protocol.cpp src/uart_protocol.cpp src/hid_state.cpp -o /tmp/t && /tmp/t
```

Cobre CRC16, sincronismo/resincronização, timeout, validação de versão e
tamanho, além de 6KRO, troca de modo e validação de botões/faixa absoluta
do mouse. Não substitui teste em hardware real — o protocolo só pode ser
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
| `src/hid_usb.*` | submissão dos relatórios HID à pilha TinyUSB |
| `src/mode_button.*` | leitura debounced do botão de troca de modo (GP6) |
| `src/oled_display.*` | inicialização e desenho no SSD1306 (núcleo 1) |
| `src/shared_state.*` | snapshot compartilhado entre núcleos |
| `tests/test_uart_protocol.cpp` | testes de host do protocolo e do estado HID |
| `docs/` | documentação de projeto e de investigações de bugs |
