#pragma once
#include <cstdint>
#include <cstddef>

// Tabela fixa de ações temporizadas, varrida pelo laço do núcleo 0. Lógica
// pura: não toca GPIO nem HID, não bloqueia e não aloca. tick() devolve as
// transições que venceram e quem chama as aplica.
//
// Dez entradas porque dez é o máximo físico: seis teclas comuns simultâneas
// (o teclado é 6KRO) mais os quatro contatos do painel frontal. A mesma
// tabela serve as duas faixas — teclado (0x14/0x15) e contatos (0x30).

constexpr size_t MAX_TIMED_ACTIONS = 10;

enum class TimedKind : uint8_t { KeyHold, KeyHammer, ContactPulse };

struct TimedEvent {
    TimedKind kind;
    uint8_t target;  // usage HID, ou número do contato (1..4)
    bool press;      // true = afundar tecla / fechar contato
};

class TimedActions {
public:
    // Agendamento. Uma entrada por alvo dentro do mesmo domínio (teclado ou
    // contatos): um agendamento novo sobre um alvo que já tem um em curso
    // SUBSTITUI o anterior e reinicia a contagem — não empilha. Retorna false
    // apenas quando a tabela está cheia (nenhum estado é alterado).
    //
    // O chamador é quem afunda a tecla / fecha o contato ao aceitar; a tabela
    // já nasce com a intenção "pressionada" e só cuida das transições futuras.
    bool schedule_key_hold(uint8_t usage, uint16_t duration_ms, uint32_t now_ms);
    bool schedule_key_hammer(uint8_t usage, uint16_t duration_ms, uint16_t interval_ms,
                             uint32_t now_ms);
    bool schedule_contact_pulse(uint8_t contact, uint16_t duration_ms, uint32_t now_ms);

    // Cancelamentos. Escrevem em `out` os eventos de soltura/abertura das
    // entradas que estavam com o alvo pressionado, para que nenhum caminho de
    // cancelamento deixe tecla afundada ou contato fechado.
    size_t cancel_key(uint8_t usage, TimedEvent* out, size_t max_out);
    size_t cancel_contact(uint8_t contact, TimedEvent* out, size_t max_out);
    size_t cancel_keys(TimedEvent* out, size_t max_out);
    size_t cancel_contacts(TimedEvent* out, size_t max_out);
    size_t cancel_all(TimedEvent* out, size_t max_out);

    // Varredura: aplica o tempo decorrido e devolve as transições vencidas.
    // `max_out` deve ser ao menos MAX_TIMED_ACTIONS.
    size_t tick(uint32_t now_ms, TimedEvent* out, size_t max_out);

    size_t active_count() const;
    bool has_key(uint8_t usage) const;

private:
    struct Entry {
        bool active = false;
        TimedKind kind = TimedKind::KeyHold;
        uint8_t target = 0;
        uint32_t end_ms = 0;   // fim da janela
        uint32_t next_ms = 0;  // próxima troca de nível (só martelo)
        uint16_t half_ms = 0;  // meio período do martelo
        bool pressed = false;  // nível atual da intenção
    };

    bool schedule(TimedKind kind, uint8_t target, uint16_t duration_ms, uint16_t interval_ms,
                  uint32_t now_ms);
    size_t cancel_where(bool contacts, bool keys, uint8_t target, bool by_target,
                        TimedEvent* out, size_t max_out);

    Entry entries_[MAX_TIMED_ACTIONS];
};

extern TimedActions g_timed_actions;
