# U8G2Pico

## Descrição

**U8G2Pico** é uma biblioteca wrapper para Raspberry Pi Pico que facilita o uso da biblioteca U8G2 com displays monochromáticos via I2C. Esta biblioteca fornece uma interface simplificada para configurar displays SSD1306 128x64 no Raspberry Pi Pico, mantendo toda a funcionalidade e compatibilidade da biblioteca U8G2 original.

## Características

- ✅ Interface simplificada para configuração de displays I2C
- ✅ Compatibilidade total com todas as funções da biblioteca U8G2
- ✅ Suporte nativo para Raspberry Pi Pico
- ✅ Configuração automática de pinos I2C
- ✅ Otimizado para displays SSD1306 128x64

## Instalação

### Instalação Semiautomática (Recomendada)

Use o instalador automático para configurar rapidamente a biblioteca em seu projeto:

**[🚀 Instalador U8G2Pico](https://github.com/georgines/instalador_u8g2pico)**

## Como Usar

### Configuração Básica

A biblioteca U8G2Pico simplifica drasticamente a configuração de displays. **Você só precisa chamar uma única função de configuração** e todas as outras funções são herdadas diretamente da biblioteca U8G2 original.

### Exemplo Completo

```c
#include "pico/stdlib.h"
#include "u8g2pico.h"

#define I2C_PORT i2c1
#define SDA_PIN 14
#define SCL_PIN 15
#define I2C_ADDR 0x3C

static u8g2pico_t u8g2pico;

int main() {
    stdio_init_all();

    // Configurar display - ÚNICA FUNÇÃO ESPECÍFICA DA U8G2PICO
    u8g2_Setup_ssd1306_i2c_128x64_noname_f_pico(&u8g2pico, I2C_PORT, SDA_PIN, SCL_PIN, U8G2_R0, I2C_ADDR);
    
    // Inicializar display - FUNÇÕES HERDADAS DA U8G2
    u8g2_InitDisplay(&u8g2pico);
    u8g2_SetPowerSave(&u8g2pico, 0);
    
    // Desenhar conteúdo - FUNÇÕES HERDADAS DA U8G2
    u8g2_ClearBuffer(&u8g2pico);
    u8g2_SetFont(&u8g2pico, u8g2_font_8bitclassic_tf);
    u8g2_DrawStr(&u8g2pico, 0, 25, "U8G2Pico");
    u8g2_SendBuffer(&u8g2pico);
    
    while(1) {
        // Seu código aqui
        sleep_ms(100);
    }
    
    return 0;
}
```
## Parâmetros de Configuração

### Função Principal: `u8g2_Setup_ssd1306_i2c_128x64_noname_f_pico`

```c
void u8g2_Setup_ssd1306_i2c_128x64_noname_f_pico(
    u8g2pico_t *u8g2pico,     // Ponteiro para a estrutura do display
    i2c_inst_t *i2c,          // Porta I2C (i2c0 ou i2c1)
    uint8_t sda_pin,          // Pino SDA
    uint8_t scl_pin,          // Pino SCL
    const u8g2_cb_t *rotation, // Rotação da tela (U8G2_R0, U8G2_R1, U8G2_R2, U8G2_R3)
    uint8_t address           // Endereço I2C do display (geralmente 0x3C ou 0x3D)
);
```

### Opções de Rotação

- `U8G2_R0` - Sem rotação (padrão)
- `U8G2_R1` - Rotação 90°
- `U8G2_R2` - Rotação 180°
- `U8G2_R3` - Rotação 270°

### Endereços I2C Comuns

- `0x3C` - Endereço padrão para a maioria dos displays SSD1306
- `0x3D` - Endereço alternativo

## Funções Disponíveis

Após a configuração inicial, **todas as funções da biblioteca U8G2 original estão disponíveis**:

### Funções de Inicialização
- `u8g2_InitDisplay()` - Inicializa o display
- `u8g2_SetPowerSave()` - Controla o modo de economia de energia

### Funções de Buffer
- `u8g2_ClearBuffer()` - Limpa o buffer
- `u8g2_SendBuffer()` - Envia o buffer para o display

### Funções de Desenho
- `u8g2_SetFont()` - Define a fonte
- `u8g2_DrawStr()` - Desenha texto
- `u8g2_DrawLine()` - Desenha linha
- `u8g2_DrawBox()` - Desenha retângulo preenchido
- `u8g2_DrawFrame()` - Desenha retângulo vazio
- `u8g2_DrawCircle()` - Desenha círculo
- E muitas outras...

Para uma lista completa das funções disponíveis, consulte a [documentação oficial da U8G2](https://github.com/olikraus/u8g2/wiki).

## Pinout Recomendado

| Display | Pico |
|---------|------|
| VCC     | 3.3V |
| GND     | GND  |
| SDA     | GP14 |
| SCL     | GP15 |

## Resolução de Problemas

### Display não inicializa
- Verifique as conexões dos pinos
- Confirme o endereço I2C usando um scanner I2C
- Verifique se a alimentação está correta (3.3V)

### Texto não aparece
- Certifique-se de chamar `u8g2_SendBuffer()` após desenhar
- Verifique se a fonte foi definida com `u8g2_SetFont()`
- Confirme que `u8g2_SetPowerSave(&u8g2pico, 0)` foi chamado

## Dependências

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [U8G2 Library](https://github.com/olikraus/u8g2) (incluída)

## Licença

Esta biblioteca mantém a mesma licença da biblioteca U8G2 original.

## Créditos

- **Oliver Kraus (@olikraus)** - Criador da biblioteca u8g2lib original

## Contribuição

Contribuições são bem-vindas! Por favor, abra issues ou pull requests no repositório do projeto.

---

**Nota:** Esta biblioteca é um wrapper simplificado. Para funcionalidades avançadas ou outros tipos de display, consulte a documentação completa da U8G2.
