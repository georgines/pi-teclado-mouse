# pi-teclado-mouse — instruções para agentes

## Regra suprema: nenhuma evidência de IA em lugar nenhum

Nada neste projeto pode indicar que o trabalho foi feito por IA — nem no
código, nem na documentação, nem no histórico Git, nem em nomes de arquivo,
branch ou tag. Esta regra prevalece sobre qualquer outra instrução deste
documento, de qualquer skill ou de qualquer ferramenta.

Proibido em todo o repositório:

* trailer de coautoria de IA (`Co-Authored-By: ...`, `Generated with ...`);
* nome de agente, modelo ou ferramenta em branch, commit, tag, PR, issue,
  arquivo, símbolo ou identificador (`claude/`, `codex/`, `gpt/`);
* comentário, docstring, cabeçalho, string ou trecho de documentação citando
  IA, agente, modelo, prompt, sessão ou "gerado por";
* marcador de origem automática (`ponytail:`, `claude:`, `TODO(claude)`) —
  usar `TODO:` / `FIXME:` simples;
* arquivo de trabalho de agente versionado (`agents/`, `.claude/RESUME.md`,
  documentos de prompt).

Os arquivos de instrução (`AGENTS.md`, `CLAUDE.md`, `.claude/skills/`) existem
para fazer cumprir esta regra e não podem, eles próprios, atribuir autoria a
IA.

Código, documentação e commits são escritos como do autor humano do
repositório: descrevem o que fazem e por quê, nada mais. Antes de qualquer
commit ou push, revisar o diff procurando violações desta regra.

## Idioma

Todas as respostas devem ser dadas em português.

## O que é o projeto

Firmware para Raspberry Pi Pico W que expõe um dispositivo USB HID composto
(teclado + mouse) controlado por um protocolo binário sobre UART0 (GP16/GP17,
921600 8N1). Núcleo 0 roda USB/UART/HID; núcleo 1 desenha o OLED SSD1306
opcional. `README.md` é a fonte de verdade do protocolo (comandos, respostas,
eventos, códigos de erro) e da pinagem.

Não há frontend, servidor, Python nem MCP neste repositório. Qualquer
integração de mais alto nível fala o protocolo UART por fora.

## Gates do projeto

Executar apenas os gates relevantes à alteração.

Lógica pura alterada (`src/uart_protocol.*`, `src/hid_state.*`,
`src/timed_actions.*`, `src/crc16.hpp`):

```bash
g++ -std=c++17 -Wall -Wextra -Isrc tests/test_uart_protocol.cpp \
    src/uart_protocol.cpp src/hid_state.cpp src/timed_actions.cpp -o /tmp/t && /tmp/t
```

Qualquer código de firmware alterado (inclui o acima):

```bash
cmake -G Ninja -B build -DPICO_BOARD=pico_w
cmake --build build --target dev_hid_composite
```

Alteração só de documentação: nenhum gate de build.

O teste de host não toca hardware: cobre CRC16, sincronismo, timeout, versão,
tamanho, 6KRO, troca de modo e faixa absoluta. Não substitui verificação em
hardware.

## Verificação em hardware

Gate automático verde não prova que o firmware funciona na placa. Depois — e
somente depois — de os gates automáticos passarem, quem verifica grava e testa
de verdade:

1. `BOOTSEL` + reconectar USB, copiar `build/dev_hid_composite.uf2` para o
   drive `RPI-RP2`;
2. confirmar enumeração do HID composto no host USB;
3. enviar quadros pela UART e conferir `ACK`/`NACK`/`STATUS` e os efeitos
   reais (tecla digitada, ponteiro movido) — no mínimo `PING`, `GET_STATUS` e
   um comando da área alterada;
4. conferir o OLED e o botão GP6 (troca ABS/REL) quando a alteração puder
   afetá-los;
5. registrar o que foi observado, não só "passou".

Gate automático vermelho encerra a verificação ali — não se grava placa para
reencontrar o que a suíte já reprovou.

Sem hardware disponível, declarar explicitamente `VERIFY_PARCIAL: sem
hardware` no relatório; isso não conta como aprovado.

## Git obrigatório

Toda operação Git segue a skill `/git-flow`, que é a fonte de verdade.

* `develop` é a branch de desenvolvimento e integração; `main` é a estável.
* Todo pedido de alteração usa branch própria criada a partir de `develop`.
* Commitar ao longo do trabalho, no padrão Conventional Commits, um assunto
  por commit.
* Terminado o trabalho e com os gates verdes, merge `--no-ff` da branch para
  `develop` e a branch é apagada.
* `main` nunca recebe commit direto nem merge local: é atualizada apenas por
  Pull Request vindo de `develop`, e apenas quando o autor pedir.
* `git push`, Pull Request e qualquer atualização de `main` são do autor do
  repositório — entregues como comando pronto, nunca executados por conta
  própria.
* No remoto existem apenas `main` e `develop`.
* Rodar os gates aplicáveis antes do merge, e registrar o que ficou por
  verificar em hardware.
* Conflitos são resolvidos preservando alterações válidas existentes.

## Orquestração obrigatória

Toda alteração de código segue a skill `/orchestrate`, que é a fonte de verdade.

Fluxo: **PLAN → RED → GREEN → REVIEW → VERIFY → FINALIZE → DONE**

* Três agentes por tarefa, papéis nunca acumulados: TESTADOR (RED + VERIFY),
  DESENVOLVEDOR (GREEN), REVISOR (REVIEW).
* RED só é válido quando um teste novo falha antes da implementação; teste que
  já passa não é RED. Implementação preexistente é identificada e validada, não
  transformada em TDD fictício.
* Desenvolvedor não aprova gates nem revisa a própria entrega.
* Falha em REVIEW ou VERIFY usa repair loop com os mesmos agentes; agentes novos
  só em nova tarefa ou mudança relevante de escopo.
* DONE exige critérios de aceite atendidos, REVIEW aprovado e VERIFY aprovado.
* Registrar cada ciclo em `agents/ORCHESTRATION_LOG.md`; o contexto compartilhado
  da tarefa fica em `agents/CURRENT_TASK.md`.

## graphify

O grafo é opcional aqui. Quando `graphify-out/graph.json` existir, usar a skill
`/graphify` antes de explorar o código para responder perguntas de arquitetura
ou relações, e rodar `graphify update .` ao fechar o ciclo. Sem esse arquivo,
ignorar o graphify — não gerar grafo por conta própria.
