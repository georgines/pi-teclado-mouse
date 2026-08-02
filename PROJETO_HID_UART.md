# Projeto HID USB composto controlado por UART

## Resumo

Este documento especifica a futura conversão do projeto para C++17 em um Raspberry Pi Pico W. A placa funcionará como um dispositivo USB HID composto de teclado e mouse, controlado por comandos recebidos pela UART0.

O MCP e a integração com agentes de IA ficam fora desta primeira versão.

### Metas de desempenho

- UART a 921600 baud.
- Polling USB HID de 1 ms.
- ACK/NACK em até 2 ms normalmente.
- Atualização do OLED isolada no segundo núcleo para não bloquear USB ou UART.

## Hardware e pinagem

| Função | Interface | Pino |
|---|---|---|
| UART TX | UART0 | GP16 |
| UART RX | UART0 | GP17 |
| OLED SDA | I²C1 | GP14 |
| OLED SCL | I²C1 | GP15 |
| Alternar ABS/REL | GPIO com pull-up interno | GP6 |

O display será um SSD1306 I²C de 128×64 pixels. O firmware procurará primeiro o endereço `0x3C` e depois `0x3D`. A ausência ou falha do display não poderá impedir o funcionamento do USB ou da UART.

## Arquitetura

### Distribuição entre os núcleos

- Núcleo 0: TinyUSB, UART, parser, filas HID e leitura do botão.
- Núcleo 1: inicialização e renderização do OLED.
- O estado apresentado na tela será transferido entre os núcleos por snapshots protegidos, mantendo o bloqueio curto.
- Não haverá alocação dinâmica durante a operação normal.

### USB HID

O dispositivo terá duas interfaces HID:

1. Teclado boot HID, com modificadores e até seis teclas comuns simultâneas (6KRO).
2. Mouse com dois IDs de relatório declarados desde a enumeração:
   - Relativo: deltas X/Y assinados de 16 bits.
   - Absoluto: X/Y entre 0 e 32767.

O mouse aceitará cinco botões, roda vertical e roda horizontal. O descritor não incluirá gamepad nem controle multimídia.

### Botão de modo

O GP6 será ativo em nível baixo, com pull-up interno e debounce de 30 ms.

- Cada pressionamento alternará entre os modos absoluto e relativo.
- A troca de modo enviará um relatório neutro e soltará os botões do mouse, evitando estados presos.
- Ao entrar no modo relativo, a posição virtual acumulada será zerada.
- A alteração gerará um evento assíncrono pela UART.

## Display

Será utilizada a biblioteca [u8g2pico](https://github.com/georgines/u8g2pico), que fornece a integração da U8G2 com o Pico SDK e o SSD1306.

A tela mostrará:

- Estado USB: desconectado, montado ou suspenso.
- Modo atual: `ABS` ou `REL`.
- Estado da UART e contador resumido de erros.
- Modificadores e usages HID das teclas pressionadas.
- Coordenadas X/Y do mouse.
- No modo relativo, posição virtual acumulada e último delta.
- Estado dos cinco botões e das rodas.

A renderização utilizará o buffer completo da U8G2 no núcleo 1. A tela será redesenhada somente quando o estado mudar, respeitando um intervalo mínimo de 50 ms.

### Dependências fixadas

As bibliotecas serão vendorizadas para permitir compilações reproduzíveis e offline:

- `lib/u8g2pico`: commit `16a1428e51947db1ece39ffa825800fe92bff673`.
- `lib/u8g2pico/lib/u8g2`: commit `ab9e48b2228351e9476682a70b7f3ee4909cd585`.

Essa estrutura corresponde à esperada pelo [instalador oficial da u8g2pico](https://github.com/georgines/instalador_u8g2pico). As licenças e avisos das duas bibliotecas deverão ser preservados.

## Protocolo UART

### Formato do quadro

Todos os valores com mais de um byte usarão little-endian.

```text
A5 5A | versão:u8 | tipo:u8 | sequência:u16 | tamanho:u16 | payload:0..64 | CRC16:u16
```

- Versão inicial: `1`.
- Payload máximo: 64 bytes.
- CRC-16/CCITT-FALSE, polinômio `0x1021` e valor inicial `0xFFFF`.
- O CRC cobrirá os campos desde `versão` até o fim do payload.
- Um quadro parcial expirará após 20 ms.
- Depois de um quadro inválido, o parser voltará a procurar a sequência `A5 5A`.
- RX e TX usarão interrupções e buffers circulares fixos de 512 bytes.
- A fila de comandos terá capacidade para 32 entradas.

### Comandos

| Código | Comando | Payload |
|---|---|---|
| `0x01` | `PING` | Vazio |
| `0x02` | `GET_STATUS` | Vazio |
| `0x10` | `KEY_DOWN` | Usage HID `u8` |
| `0x11` | `KEY_UP` | Usage HID `u8` |
| `0x12` | `KEY_RELEASE_ALL` | Vazio |
| `0x20` | `MOUSE_MOVE` | X e Y, dois valores de 16 bits |
| `0x21` | `MOUSE_BUTTONS` | Bitmap completo dos cinco botões |
| `0x22` | `MOUSE_WHEEL` | Vertical `i8` e horizontal `i8` |
| `0x23` | `MOUSE_RELEASE_ALL` | Vazio |
| `0x24` | `SET_MOUSE_MODE` | `0=REL`, `1=ABS` |

No comando `MOUSE_MOVE`, X/Y serão interpretados como `i16` no modo relativo e como `u16` no modo absoluto. Valores absolutos acima de 32767 serão rejeitados.

`KEY_DOWN` e `KEY_UP` aceitarão usages HID, incluindo os modificadores de `0xE0` a `0xE7`. Comandos repetidos serão idempotentes. A tentativa de manter uma sétima tecla comum pressionada resultará em NACK.

### Respostas e eventos

| Código | Resposta | Significado |
|---|---|---|
| `0x80` | `ACK` | Comando validado e aceito na fila |
| `0x81` | `NACK` | Comando recusado, acompanhado do código de erro |
| `0x82` | `STATUS` | Estado completo solicitado por `GET_STATUS` |
| `0x90` | `EVENT_STATUS` | Mudança assíncrona de estado |

ACK e NACK reutilizarão o número de sequência do comando. Eventos assíncronos usarão sequência zero.

Os eventos informarão:

- Conexão, desconexão e suspensão USB.
- Mudança do modo do mouse.
- Falha ou recuperação do display.
- Overflow de buffer ou fila.
- Erros de CRC, tamanho, versão e comando.

Comandos HID recebidos com o USB desconectado ou suspenso serão rejeitados com NACK, evitando que entradas antigas sejam reproduzidas posteriormente.

## Segurança de estado

- Desconexão USB, overflow ou reinicialização limparão filas e estados locais de teclado e mouse.
- Erros de CRC ou quadros incompletos não alterarão o estado HID.
- Comandos `KEY_RELEASE_ALL` e `MOUSE_RELEASE_ALL` permitirão recuperação explícita.
- Não haverá reprodução de comandos armazenados depois de uma reconexão.
- O ACK significará que o comando foi aceito na fila, não uma confirmação de processamento pelo sistema operacional.

## Organização futura do firmware

- Converter os fontes da aplicação para C++17.
- Separar os módulos de USB HID, protocolo UART, estado de entrada, botão e display.
- Manter os callbacks TinyUSB com ligação C quando exigido pela biblioteca.
- Priorizar continuamente `tud_task()` e o envio dos relatórios HID no núcleo 0.
- Atualizar o CMake para ligar TinyUSB, UART, I²C, multicore e u8g2pico.

## Testes e critérios de aceitação

- Compilação integral em C++17 com Pico SDK 2.3.0.
- Enumeração no Windows 10/11 como teclado e mouse.
- Ausência das interfaces de gamepad e controle multimídia.
- Validação de CRC, timeout, ressincronização, sequência, payload inválido e fila cheia.
- Testes de pressionar e soltar teclas, modificadores, 6KRO e liberação de emergência.
- Testes de movimento absoluto, relativo, cinco botões e duas rodas.
- Alternância de modo pelo GP6 e pela UART.
- Reconexão e suspensão USB sem executar comandos antigos.
- Detecção do OLED nos dois endereços e operação normal sem display.
- Verificação de que uma atualização completa do OLED não aumenta a latência do caminho UART–USB.
- ACK/NACK normalmente emitido em até 2 ms e relatório HID submetido assim que o endpoint estiver disponível.

