# UART não responde a comandos — handoff de debug

**Status: RESOLVIDO (2026-08-02).** Ver seção "Resolução" no final do
documento. O restante do documento é o histórico de investigação, mantido
para referência.

~~**Status:** hardware/fiação comprovadamente OK. Firmware completo não responde
a nenhum comando UART (PING incluso). Bug isolado a algo específico do
firmware `dev_hid_composite`, ainda não identificado com certeza.~~

**Regra importante:** não modificar nada dentro do SDK vendorizado
(`~/.pico-sdk/sdk/2.3.0/...` / Windows `C:\Users\Usuario\.pico-sdk\sdk\2.3.0\...`).
Só mexer nos arquivos do projeto (`main.cpp`, `src/*`, `CMakeLists.txt`, etc.)
— isso inclui usar helpers de CMake que o SDK expõe para o projeto chamar
(ex: `pico_enable_stdio_uart(...)`), mas não editar arquivos dentro da pasta
do SDK.

## O que está confirmado funcionando

1. **Fiação física está boa nas duas direções, a 921600 baud.** Provado com
   um firmware mínimo de diagnóstico, `uart_echo_test.cpp` (já existe no
   projeto, tem target CMake próprio `uart_echo_test`): manda `"PICOALIVE\n"`
   a cada 1s sem precisar de nenhum input, e ecoa de volta qualquer byte
   recebido. Rodando esse firmware, o heartbeat chegou limpo a cada ~1s e
   todo marcador `"TESTE\n"` mandado do PC voltou ecoado sem nenhuma
   corrupção, dezenas de vezes seguidas.
2. **USB HID enumera perfeitamente** com o firmware completo
   (`dev_hid_composite`): `VID_CAFE&PID_4004`, duas interfaces HID (teclado
   `MI_00`, mouse com 2 collections `MI_01&COL01`/`COL02` = report IDs
   relativo/absoluto), todos os nós com `ProblemCode = 0` no Windows.
3. **A porta serial certa é a COM10** (canal B de um FTDI FT2232H; COM9 é o
   canal A, sem uso). Confirmado pelo usuário.

## O que já foi descartado como causa

Testado com o firmware completo (`dev_hid_composite`), sempre com a mesma
fiação que funcionou perfeitamente no `uart_echo_test`:

- **Baud rate errado / sinal degradado a 921600**: testado a 115200 baud
  também (mudança temporária em `src/uart_io.cpp`, já revertida para 921600).
  Mesmo resultado (nenhuma resposta válida). Descartado.
- **Bug na configuração da interrupção da UART** (`irq_set_exclusive_handler`
  / `uart_set_irq_enables`): testado desabilitando a IRQ por completo
  (`irq_set_enabled(UART0_IRQ, false)`) e adicionando um fallback de polling
  direto dentro de `uart_io::poll()`. Mesmo resultado (nenhuma resposta).
  Descartado — **essa mudança ainda está no código, ver seção "Estado atual
  dos arquivos" abaixo**.
- **Deadlock de `critical_section` entre core0/core1** (usado em
  `src/shared_state.cpp` para o snapshot do OLED): testado comentando a
  chamada `multicore_launch_core1(oled_display::core1_main)` em `main.cpp`
  (core1 nunca é lançado). Mesmo resultado (nenhuma resposta). Descartado —
  **essa mudança também ainda está no código**.

Ou seja: nem IRQ nem multicore explicam o silêncio. O bug está em outro
lugar, presente mesmo na versão mais simplificada (single-core, UART em
polling puro).

## Hipótese líder, ainda não confirmada nem testada

`board_init()` (bsp do TinyUSB para rp2040, chamado logo no início de
`main()`, ANTES de `uart_io::init()`) muito provavelmente reconfigura a
**mesma UART0** para uso como stdio:

- `lib/tinyusb/hw/bsp/rp2040/family.c` linha ~162-166:
  ```c
  #ifdef UART_DEV
    bi_decl(bi_2pins_with_func(UART_TX_PIN, UART_RX_PIN, GPIO_FUNC_UART));
    uart_inst = uart_get_instance(UART_DEV);
    stdio_uart_init_full(uart_inst, CFG_BOARD_UART_BAUDRATE, UART_TX_PIN, UART_RX_PIN);
  #endif
  ```
- `lib/tinyusb/hw/bsp/rp2040/board.h`: `UART_DEV` = `PICO_DEFAULT_UART`,
  `UART_TX_PIN`/`UART_RX_PIN` = `PICO_DEFAULT_UART_TX_PIN`/`_RX_PIN`. Esse
  `#define` não tem nenhum guard — está sempre presente.
- Para a board `pico_w`
  (`src/boards/include/boards/pico_w.h`): `PICO_DEFAULT_UART = 0` (ou seja,
  **uart0, a mesma instância física que o projeto usa** para o protocolo,
  ainda que em pinos diferentes — GP0/GP1 da stdio vs GP16/GP17 do projeto).

`uart_io::init()` roda DEPOIS de `board_init()` e reconfigura `uart0`
(`uart_init`, `gpio_set_function` nos pinos certos, etc.), então em teoria
deveria "tomar posse" do periférico de volta. Mas isso ainda não foi
verificado punho a punho — a suspeita é que o driver `pico_stdio_uart`
(vinculado por padrão via `pico_stdlib`) deixa algum estado residual (driver
de stdio registrado, possível handler/uso da mesma UART) que atrapalha.

**Próximo passo sugerido:** no `CMakeLists.txt` do projeto (não no SDK),
tentar `pico_enable_stdio_uart(dev_hid_composite 0)` (e possivelmente
`pico_enable_stdio_usb(dev_hid_composite 0)` também, já que
`CFG_TUD_CDC=0` torna isso irrelevante mas por completude) para impedir que
`pico_stdio_uart` seja linkado/inicializado. **Atenção**: como a chamada em
`family.c` não tem guard além de `#ifdef UART_DEV` (que não depende de
`pico_stdio_uart` estar linkado), é possível que desabilitar
`pico_enable_stdio_uart` cause ERRO DE LINK (símbolo
`stdio_uart_init_full` não resolvido) dentro de `tinyusb_board`. Se isso
acontecer, a função do helper `pico_enable_stdio_uart` precisa ser
inspecionada (`grep -rn "function(pico_enable_stdio_uart" ~/.pico-sdk/sdk/2.3.0/`
— **só leitura, não editar**) pra entender exatamente o que ela faz e achar
uma forma de desativar por completo o uso de UART0 pelo stdio sem quebrar o
link. Isso ainda não foi investigado até o fim (foi interrompido pelo
usuário nesse ponto).

Outras hipóteses ainda não testadas, caso a de stdio não seja a causa:

- Alguma chamada em `usb_descriptors.cpp` ou em `hid_usb.cpp` travando/
  demorando demais dentro do loop principal antes de `uart_io::poll()` rodar
  (mas USB enumera limpo, o que sugere que o loop principal está rodando
  normalmente).
- Bug de verdade no `FrameParser`/`uart_protocol.cpp` que só aparece com
  bytes vindos de hardware real (os testes de host em
  `tests/test_uart_protocol.cpp` passam 100%, mas usam vetores construídos à
  mão — não descarta um bug de timing/estado só visível com bytes reais).
- Conflito de uso de GPIO16/17 por outra parte do código (revisar todo
  `grep -rn "16\|17" src/ main.cpp` por segurança, ainda que a config atual
  pareça correta).

## Estado atual dos arquivos (mudanças de diagnóstico, precisam ser revertidas depois)

*(Histórico — já revertido, ver seção "Resolução".)*

1. ~~**`main.cpp`** (~linha 47-49): a chamada
   `multicore_launch_core1(oled_display::core1_main);` está COMENTADA.
   Precisa voltar a ser chamada assim que o core1/OLED for testado de novo —
   isso é parte real do spec, não pode ficar desligado no firmware final.~~

2. ~~**`src/uart_io.cpp`**:~~
   - ~~Linha ~268: `irq_set_enabled(UART0_IRQ, false)` — deveria ser `true`
     (era assim antes do diagnóstico). Precisa reverter para usar IRQ de
     verdade, já que o spec pede explicitamente "RX e TX usarão
     interrupções".~~
   - ~~Linhas ~272-287 (início de `poll()`): tem um bloco de polling direto
     (`while (uart_is_readable(UART_ID)) {...}` e o flush de TX) que foi
     ADICIONADO só para o diagnóstico. Se a causa raiz acabar sendo outra
     coisa (não a IRQ), esse bloco deveria ser removido depois que o IRQ
     voltar a ser usado — caso contrário RX/TX ficam sendo servidos duas
     vezes (por polling E por IRQ), o que pode gerar bytes duplicados ou
     condição de corrida entre o handler de IRQ e o `poll()` escrevendo nos
     mesmos `rx_head`/`tx_tail`.~~

3. ~~**`build/`**: o build atual (`cmake --build build`) já reflete essas
   mudanças de diagnóstico. A placa está com esse firmware de diagnóstico
   gravado agora mesmo (BOOTSEL foi usado a última vez pra gravar essa
   versão).~~

## Ferramentas de teste disponíveis

Todas em `C:\Users\Usuario\AppData\Local\Temp\` (Windows, acessível via
PowerShell — WSL não tem acesso direto às portas COM, por isso os scripts
são `.ps1` rodados via `powershell.exe` a partir do WSL):

- **`pico_serial_test.ps1 -PortName COM10`**: bateria de teste do protocolo
  completo (PING, GET_STATUS, KEY_DOWN/UP, MOUSE_MOVE, troca de modo, CRC
  corrompido, comando desconhecido, range absoluto inválido, release-all).
  Aceita `-Baud <n>` opcional (default 921600).
- **`pico_serial_watch.ps1 -PortName COM10 -DurationSec <n>`**: fica
  mandando PING em loop e mostra ao vivo se voltou ACK válido, lixo, ou
  silêncio — útil pra rodar em background enquanto mexe na fiação.
- **`pico_echo_watch.ps1 -PortName COM10 -DurationSec <n>`**: não tenta
  interpretar protocolo, só mostra bruto (hex + ascii) tudo que chega, e
  manda um marcador de teste a cada 2s. Foi o que confirmou o
  `uart_echo_test.cpp` funcionando 100%.
- **`pico_loopback_check.ps1`**: testa se COM9 e COM10 estão fisicamente
  ligados um no outro (não estão).

### Fluxo pra gravar firmware (BOOTSEL)

1. Pedir pro usuário segurar o BOOTSEL e resetar/reconectar o USB.
2. Confirmar que entrou em modo bootloader:
   `powershell.exe -NoProfile -Command "Get-Volume -DriveLetter F -ErrorAction SilentlyContinue | Format-Table DriveLetter, FileSystemLabel"`
   — deve aparecer `F` com label `RPI-RP2`.
3. Copiar o `.uf2`:
   `powershell.exe -NoProfile -Command "Copy-Item 'C:\Users\Usuario\Documents\pi-teclado-mouse\build\dev_hid_composite.uf2' -Destination 'F:\'"`
   (a placa reseta sozinha e já roda o firmware novo em ~2-3s).

### Fluxo de build

```
powershell.exe -NoProfile -Command "cd 'C:\Users\Usuario\Documents\pi-teclado-mouse'; & 'C:\Users\Usuario\.pico-sdk\cmake\v4.3.4\bin\cmake.exe' --build build --target dev_hid_composite"
```

(o build já foi configurado uma vez com `-G Ninja -DPICO_SDK_PATH=... -DPICO_TOOLCHAIN_PATH=... -DCMAKE_MAKE_PROGRAM=... -DPICO_BOARD=pico_w`,
não precisa reconfigurar de novo a menos que o `CMakeLists.txt` mude quais
targets/arquivos existem).

## Teste de unidade em host (não depende de hardware)

```
g++ -std=c++17 -Wall -Wextra -Isrc tests/test_uart_protocol.cpp src/uart_protocol.cpp src/hid_state.cpp -o /tmp/t && /tmp/t
```

Passa 100% — cobre CRC16, `FrameParser` (sync/CRC/timeout/versão/tamanho) e
`hid_state` (6KRO, troca de modo, validação de botões/range absoluto). Não
prova nada sobre o bug atual (que só aparece com hardware real), mas é bom
rodar depois de qualquer mudança pra garantir que a lógica pura não
regrediu.

## Resolução (2026-08-02)

A hipótese líder estava certa: `board_init()` (TinyUSB bsp) reconfigurava
UART0.

**Causa raiz:** `PICO_STDIO_UART` tem default global `1` no SDK (definido em
`pico_stdio/CMakeLists.txt`), e o projeto nunca desligava isso pro target
`dev_hid_composite`. Com `pico_stdio_uart` linkado, `LIB_PICO_STDIO_UART`
fica definido, o que faz `UART_DEV` (`board.h` do bsp rp2040) resolver pra
`PICO_DEFAULT_UART` = uart0, pinos `PICO_DEFAULT_UART_TX/RX_PIN` = GP0/GP1
(board `pico_w`). `board_init()` chama `stdio_uart_init_full(uart0, ...,
GP0, GP1)` — ou seja, GP0/GP1 ficam configurados como UART0 stdio ANTES de
`uart_io::init()` configurar GP16/GP17, no mesmo periférico físico uart0.

No RP2040 múltiplos GPIOs podem selecionar a mesma função de periférico
simultaneamente; a entrada RX do periférico é efetivamente um OR entre todos
os pinos com aquele funcsel. Com GP0 flutuante (sem conexão) também
selecionado como UART0 RX, o ruído nesse pino corrompia tudo que chegava via
GP16 — por isso nem o PING respondia, mesmo com fiação/wiring 100%
corretos. Isso também explica por que `uart_echo_test` sempre funcionou:
esse target não linka `tinyusb_board` nem chama `board_init()`, então nunca
sofria a colisão.

**Fix** (`CMakeLists.txt` do projeto, não no SDK):

```cmake
pico_enable_stdio_uart(dev_hid_composite 0)
```

Chamado logo após `add_executable(dev_hid_composite)`. Isso zera
`PICO_TARGET_STDIO_UART` nesse target, então `pico_stdio_uart` deixa de ser
linkado, `LIB_PICO_STDIO_UART` deixa de ser definido, e o bloco
`#ifdef UART_DEV` em `family.c` nem compila — sem risco de erro de link
(`stdio_uart_init_full` não referenciado).

**Diagnóstico revertido** (as 3 mudanças da seção "Estado atual dos
arquivos" acima, já confirmadas como não sendo a causa):
- `main.cpp`: `multicore_launch_core1(oled_display::core1_main);`
  reativado.
- `src/uart_io.cpp`: `irq_set_enabled(UART0_IRQ, true)` restaurado; bloco de
  polling direto duplicado removido de `poll()`.

**Validação em hardware real** (COM10, firmware gravado via BOOTSEL):
- `pico_serial_test.ps1 -PortName COM10`: bateria completa passou — PING,
  GET_STATUS, teclado, mouse relativo/absoluto, troca de modo, CRC
  corrompido (com resync limpo logo em seguida), comando desconhecido,
  range absoluto inválido, release-all.
- OLED físico confirmado ligado e exibindo status ao vivo
  (`mounted rel uart erro:2 ...`), com `erro:2` batendo exatamente com os
  dois erros intencionais do teste (1 CRC + 1 comando desconhecido) — prova
  que core1 roda sem deadlock e está sincronizado com o `DeviceSnapshot`
  publicado pelo core0.
