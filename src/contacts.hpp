#pragma once
#include <cstdint>

// Quatro contatos secos em paralelo com os botões do painel frontal da
// máquina controlada: 1 POWER, 2 RESET, 3 e 4 auxiliares.
//
// O acionamento tem de ser seco e isolado (relé de sinal ou optoacoplador) —
// nunca o GPIO ligado direto no header do painel, cuja polaridade não é
// garantida entre fabricantes.
//
// A polaridade é de COMPILAÇÃO, uma entrada por contato, porque um contato
// precisa estar comprovadamente aberto ANTES de o host falar com a placa: uma
// polaridade que só chegasse por comando não teria resposta para "qual era ela
// durante o boot".
//
// ATENÇÃO ao módulo ativo em nível BAIXO: o pino do Pico está em nível baixo
// enquanto o firmware não o configura, e nível baixo, nesse módulo, é o relé
// FECHADO — o botão de força afundado durante o boot da placa. Quem usar
// módulo ativo em baixo precisa garantir a abertura no hardware (pull-up
// externo no lado do módulo); nenhuma linha de firmware roda antes do próprio
// firmware.
namespace contacts {

constexpr uint8_t COUNT = 4;

// Nível que FECHA cada contato. Índice 0 = contato 1.
constexpr bool ACTIVE_HIGH[COUNT] = {true, true, true, true};

// Leva os quatro contatos ao nível ABERTO. Chamada antes de qualquer outra
// inicialização, para que um reset com um contato fechado termine com o
// contato aberto.
void init();

bool valid(uint8_t contact);           // 1..4
void set(uint8_t contact, bool closed);
void open_all();

// Bitmap do estado ELÉTRICO lido de volta dos pinos (bit 0 = contato 1,
// 1 = fechado), não uma sombra da intenção do firmware.
uint8_t bitmap();

} // namespace contacts
