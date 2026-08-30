---
name: orchestrate
description: Orquestra alterações de código com TDD, três agentes independentes, contexto mínimo e gates de qualidade.
argument-hint: "[tarefa]"
---

# Orchestrate

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
* arquivo de trabalho de agente no controle de versão (`agents/`,
  `.claude/RESUME.md`, planos de tarefa, documentos de prompt). Esses arquivos
  podem existir livremente na máquina de quem trabalha no projeto — o que não
  podem é ser versionados. Mantê-los no `.gitignore`.

Os arquivos de instrução (`AGENTS.md`, `CLAUDE.md`, `.claude/skills/`) existem
para fazer cumprir esta regra e não podem, eles próprios, atribuir autoria a
IA.

Código, documentação e commits são escritos como do autor humano do
repositório: descrevem o que fazem e por quê, nada mais. Antes de qualquer
commit ou push, revisar o diff procurando violações desta regra.


Fluxo padrão:

**PLAN → RED → GREEN → REVIEW → VERIFY → FINALIZE → DONE**

Cada tarefa utiliza exatamente três agentes:

- **TESTADOR** — RED e VERIFY
- **DESENVOLVEDOR** — GREEN e correções
- **REVISOR** — REVIEW

Os papéis nunca são acumulados.

Os mesmos três agentes permanecem durante toda a tarefa. Novos agentes só são
criados quando houver nova tarefa, mudança significativa de escopo ou reinício
deliberado da implementação.

## Contexto do projeto

Firmware C++ para Pico W (USB HID composto controlado por protocolo binário
sobre UART). `README.md` é a fonte de verdade do protocolo e da pinagem;
`AGENTS.md` define os gates e a verificação em hardware.

Camadas, para decidir o que cada agente precisa ver:

* **lógica pura, testável no host** — `src/uart_protocol.*`, `src/hid_state.*`,
  `src/crc16.hpp`. É aqui que TDD real acontece.
* **glue de hardware** — `src/uart_io.*`, `src/hid_usb.*`, `src/mode_button.*`,
  `src/oled_display.*`, `src/shared_state.*`, `main.cpp`, `usb_descriptors.cpp`.
  Não tem teste de host; valida-se por build + verificação em hardware.

Comportamento novo que puder ser expresso na lógica pura deve ser testado lá,
mesmo que o pedido tenha nascido de um sintoma na glue.

## 1. PLAN — Orquestrador

Antes de alterar código:

1. definir escopo;
2. definir requisitos e critérios de aceite;
3. localizar implementação, testes e WIP existentes;
4. identificar arquivos e contratos relevantes (protocolo em `README.md`);
5. determinar os gates aplicáveis;
6. executar apenas verificações mínimas necessárias para conhecer o estado atual.

Quando `graphify-out/graph.json` existir, usar primeiro:

```bash
graphify query "<tarefa ou comportamento>"
```

Sem esse arquivo, ignorar o graphify e usar `README.md`, `git` e os arquivos
conhecidos. Evitar exploração ampla do repositório.

Criar ou atualizar:

```text
agents/CURRENT_TASK.md
```

Formato compacto:

```text
Tarefa:
Escopo:
Requisitos:
Critérios de aceite:

Arquivos relevantes:
Contratos relevantes:

Testes relacionados:
Gates finais:

Estado atual:
```

Esse arquivo é a fonte principal de contexto compartilhado entre os agentes.

Não incluir histórico de conversa, raciocínio extenso ou conteúdo desnecessário.

### Implementação preexistente

Se o comportamento solicitado já estiver implementado e os testes relacionados
já passarem:

```text
PREEXISTING_IMPLEMENTATION
```

Não executar GREEN artificialmente. Seguir direto para **REVIEW → VERIFY**.

Nunca fabricar um RED.

---

## 2. RED — Testador

O TESTADOR recebe apenas:

* `agents/CURRENT_TASK.md`;
* `tests/test_uart_protocol.cpp`;
* contratos necessários (trecho relevante do `README.md`);
* arquivos diretamente relevantes.

Deve:

1. escrever ou ajustar testes para o comportamento esperado, em
   `tests/test_uart_protocol.cpp`;
2. executar somente o teste de host;
3. confirmar que pelo menos uma asserção nova falha pelo motivo esperado.

```bash
g++ -std=c++17 -Wall -Wextra -Isrc tests/test_uart_protocol.cpp \
    src/uart_protocol.cpp src/hid_state.cpp -o /tmp/t && /tmp/t
```

Em ciclo de **correção de defeito**, o TESTADOR também reproduz o defeito no
hardware antes do GREEN — grava o firmware atual e registra o quadro enviado e
a resposta/efeito errado observado. Sem isso não há como distinguir "consertei
o que o teste dizia" de "consertei o que o usuário via". Funcionalidade nova não
tem o que reproduzir e pula este passo. Sem hardware disponível, registrar
`SEM_HARDWARE` e seguir com o teste de host.

Quando a alteração for exclusivamente de glue de hardware e não houver
comportamento expressável no teste de host, registrar `RED_NAO_APLICAVEL` com a
justificativa; o ciclo passa a depender de build + verificação em hardware.
Isso é exceção, não caminho padrão.

RED válido:

```text
RED_CONFIRMED
```

Se todos os testes passarem porque o comportamento já existe:

```text
PREEXISTING_IMPLEMENTATION
```

Resumo do resultado:

```text
STATUS:
FILES:
FAILURE:
EXPECTED:
ACTUAL:
```

---

## 3. GREEN — Desenvolvedor

Somente após `RED_CONFIRMED` (ou `RED_NAO_APLICAVEL` justificado).

O DESENVOLVEDOR recebe:

* `agents/CURRENT_TASK.md`;
* testes criados ou alterados;
* resumo do RED;
* arquivos de produção necessários.

Deve:

1. implementar a menor alteração necessária;
2. fazer os testes relacionados passarem;
3. preservar o contrato do protocolo (códigos de comando, resposta e erro em
   `README.md`) — mudança de contrato exige atualizar o `README.md` no mesmo
   diff;
4. evitar duplicação e complexidade desnecessária.

Durante GREEN, executar apenas o teste de host. O build CMake fica para o
VERIFY, salvo quando a alteração não tiver cobertura de host.

É proibido:

* remover testes;
* ignorar testes;
* alterar expectativas para esconder falhas;
* enfraquecer validações para obter aprovação.

GREEN concluído:

```text
GREEN_COMPLETE
```

---

## 4. REVIEW — Revisor

O REVISOR começa pelo diff:

```bash
git diff
```

Recebe:

* `agents/CURRENT_TASK.md`;
* diff da alteração;
* testes modificados;
* contratos afetados.

Não deve explorar o repositório inteiro sem necessidade.

Avaliar:

* correção;
* aderência aos requisitos e ao contrato do protocolo;
* arquitetura e separação entre lógica pura e glue de hardware;
* legibilidade;
* coesão e acoplamento;
* duplicação;
* tratamento de erros (NACK correto em vez de falha silenciosa);
* segurança de concorrência entre ISR, núcleo 0 e núcleo 1;
* manutenibilidade e complexidade.

Resultado:

```text
APPROVED
```

ou:

```text
REJECTED
```

Em caso de rejeição, retornar somente problemas acionáveis:

```text
STATUS: REJECTED

FINDINGS:
- arquivo:linha — problema

REQUIRED_FIXES:
- correção necessária
```

O REVISOR não implementa correções.

### Repair loop

Se REVIEW reprovar: **REVIEW → DESENVOLVEDOR → REVIEW**. Não criar novos
agentes. Repetir até `APPROVED` ou até o orquestrador concluir que houve
mudança significativa de escopo.

---

## 5. VERIFY — Testador

Após REVIEW aprovado, o mesmo TESTADOR do RED executa a validação final.

Gates automáticos aplicáveis:

```bash
# lógica pura alterada
g++ -std=c++17 -Wall -Wextra -Isrc tests/test_uart_protocol.cpp \
    src/uart_protocol.cpp src/hid_state.cpp -o /tmp/t && /tmp/t

# qualquer firmware alterado
cmake -G Ninja -B build -DPICO_BOARD=pico_w
cmake --build build --target dev_hid_composite
```

O build deve terminar sem warnings novos. Gates caros ficam aqui, não são
repetidos em RED e GREEN.

### Verificação em hardware — obrigatória

Gate automático verde não é prova de que o firmware funciona na placa. O teste
de host usa vetores construídos à mão: ele valida o parser, não a UART física,
nem a enumeração USB, nem o timing entre os dois núcleos.

Depois — e **somente depois** — de todos os gates automáticos passarem, o
TESTADOR grava e usa o dispositivo de verdade, seguindo a seção "Verificação em
hardware" do `AGENTS.md`:

1. grava `build/dev_hid_composite.uf2` via `BOOTSEL`/`RPI-RP2`;
2. confirma a enumeração do HID composto no host USB;
3. envia quadros pela UART e confere `ACK`/`NACK`/`STATUS` e o efeito real —
   no mínimo `PING`, `GET_STATUS` e um comando da área alterada;
4. exercita OLED e botão GP6 quando a alteração puder afetá-los;
5. registra o que observou, incluindo os quadros trocados.

**Gate automático vermelho encerra o VERIFY ali:** `VERIFY_FAILED`, sem gravar
placa.

Sem hardware disponível: `VERIFY_PARCIAL: sem hardware`, com os gates
automáticos verdes registrados. Não é aprovação e não permite `DONE` — o ciclo
fica pendente da verificação. Não existe "aprovado com ressalva".

Resultado:

```text
VERIFY_APPROVED
```

ou:

```text
VERIFY_FAILED
```

Em caso de falha:

```text
STATUS: VERIFY_FAILED

GATE:
FAILURE:
REQUIRED_FIX:
```

### Repair loop

Se VERIFY falhar: **VERIFY → DESENVOLVEDOR → VERIFY**. Se a correção alterar
comportamento ou arquitetura de forma relevante, executar REVIEW novamente antes
do próximo VERIFY. Não criar novos agentes para correções normais.

---

## 6. FINALIZE — Orquestrador

Executar somente após:

```text
REVIEW = APPROVED
VERIFY = VERIFY_APPROVED
```

### Atualizar Graphify (condicional)

Se `graphify-out/graph.json` existir, atualizar o grafo antes de fechar a
branch:

```bash
graphify update .
```

Se a atualização modificar arquivos versionados, incluí-los no commit da tarefa.
Sem grafo no repositório, pular este passo.

### Entrega

Seguindo a skill `/git-flow`, a entrega é o merge da branch da tarefa para
`develop`:

1. garantir que todas as alterações da tarefa estejam commitadas e que
   `build/` não entrou no commit;
2. rodar os gates aplicáveis;
3. merge `--no-ff` para `develop` e apagar a branch da tarefa.

```bash
git status
git switch develop
git merge --no-ff <branch-da-tarefa> -m "merge: <resumo da tarefa>"
git branch -d <branch-da-tarefa>
```

O relatório da tarefa informa os gates executados e o resultado da verificação
em hardware — inclusive o que ficou por verificar.

`git push`, Pull Request e qualquer atualização de `main` são do autor do
repositório. `main` nunca recebe commit direto nem merge local.

Se houver conflito com `main`:

* não declarar `DONE`;
* resolver apenas se a resolução for direta e não alterar requisitos;
* se a resolução exigir mudança funcional, voltar para REVIEW e VERIFY.

---

## 7. DONE

Declarar `DONE` somente quando:

* critérios de aceite estiverem atendidos;
* RED tiver sido confirmado quando aplicável;
* REVIEW estiver `APPROVED`;
* VERIFY estiver `VERIFY_APPROVED`, com a verificação em hardware executada e
  sua evidência no relatório;
* o grafo tiver sido atualizado, quando existir;
* alterações estiverem commitadas e enviadas;
* o PR contra `main` estiver aberto e sem conflitos.

Fluxo final:

```text
PLAN
  ↓
RED
  ↓
GREEN
  ↓
REVIEW ──rejected──→ GREEN
  │                    │
  └────approved────────┘
          ↓
        VERIFY ──failed──→ GREEN
          │                 │
          └────approved─────┘
                  ↓
              FINALIZE
                  │
          PR → main
                  ↓
                 DONE
```

---

## Contexto mínimo

Cada agente deve receber somente o necessário para seu papel.

Preferir:

* `agents/CURRENT_TASK.md`;
* `git diff`;
* a tabela de protocolo do `README.md`;
* arquivos diretamente afetados;
* resumo estruturado do agente anterior.

Evitar:

* reler todo o repositório;
* reler documentação não relacionada;
* repassar histórico completo de agentes;
* carregar logs extensos;
* repetir análise já registrada em `CURRENT_TASK.md`.

O estado da tarefa pertence ao repositório, não ao contexto de conversa do
agente.

---

## Gates por alteração

```text
src/uart_protocol.*, src/hid_state.*, src/crc16.hpp
→ teste de host + build + hardware

glue de hardware, main.cpp, usb_descriptors.cpp, tusb_config.h, CMakeLists.txt
→ build + hardware

lib/u8g2pico (vendorizado)
→ build + hardware, e justificar a mudança no PR

documentação apenas
→ nenhum gate de build
```

Não executar todos os gates em todas as etapas.

---

## Nova equipe

Criar três agentes novos somente quando ocorrer:

* nova tarefa;
* mudança significativa dos requisitos ou do escopo;
* redesign arquitetural;
* abandono da implementação atual;
* reinício explícito solicitado pelo usuário.

Falhas normais de REVIEW ou VERIFY usam repair loop com os agentes atuais.

---

## Registro

Registrar cada tarefa/ciclo em:

```text
agents/ORCHESTRATION_LOG.md
```

Formato compacto:

```text
Tarefa:
Status:

PLAN:
RED:
GREEN:
REVIEW:
VERIFY:
VERIFICACAO_HARDWARE:
GRAPHIFY:
PR:

Branch:
Arquivos alterados:
Testes:
Gates:

Testador:
Desenvolvedor:
Revisor:

Resultado:
```

Não registrar raciocínio interno, transcrições extensas ou saída completa de
comandos.

Git, testes e diffs são a fonte de detalhes técnicos.

---

## Regra central

**Teste antes da implementação quando houver comportamento novo e testável no
host.**

**Três papéis independentes por tarefa.**

**Correções usam repair loops, não novos agentes.**

**Contexto compartilhado deve ser pequeno e persistente.**

**Nenhum agente aprova o próprio trabalho.**

**Build verde não é hardware verificado.**

**Sem REVIEW + VERIFY + FINALIZE aprovados, não existe DONE.**
