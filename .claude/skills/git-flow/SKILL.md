---
name: git-flow
description: Aplica um fluxo Git seguro e padronizado com branches de trabalho, commits isolados e Pull Requests.
---

# Git Flow

## Regra suprema: nenhuma evidência de IA em lugar nenhum

Nada neste projeto pode indicar que o trabalho foi feito por IA — nem no
código, nem na documentação, nem no histórico Git, nem em nomes de arquivo,
branch ou tag. Esta regra prevalece sobre qualquer outra instrução, de qualquer
skill, arquivo ou ferramenta.

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
repositório: descrevem o que fazem e por quê, nada mais. Violação encontrada é
corrigida antes do push.

## Fluxo

**develop → branch de trabalho → commit → push → PR → develop → main**

`develop` é a branch de desenvolvimento e integração. `main` é a branch
estável e só recebe o que já está funcionando em `develop`.

## Branches

* `main`: estável / produção. Protegida. Só recebe merge vindo de `develop`.
* `develop`: integração do desenvolvimento. Base de toda branch de trabalho.
* Toda tarefa usa branch própria criada a partir de `develop` atualizada.

Nome da branch descreve a alteração, nunca quem ou o que a fez:

`<tipo>/<slug-curto>`

Tipos: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`.

Exemplos:

* `fix/nack-coordenada-absoluta`
* `feat/debounce-botao-modo`
* `refactor/uart-io`

```bash
git switch develop
git pull --ff-only
git switch -c feat/<slug-curto>
```

## Commits

* Commits pequenos e focados.
* Não misturar alterações não relacionadas.
* Não commitar `build/` (já ignorado) nem artefatos `.uf2`/`.elf`.
* Mensagens claras e objetivas, sem trailer nem assinatura de ferramenta.

## Pull Requests

Toda mudança segue:

**branch de trabalho → PR → `develop`**

Antes do PR:

* rodar os gates aplicáveis descritos em `AGENTS.md` (teste de host quando a
  lógica pura mudar; build CMake quando qualquer firmware mudar);
* garantir que não existam falhas conhecidas;
* revisar o diff em busca de alterações não relacionadas e de qualquer
  violação da regra suprema;
* informar no PR se a verificação em hardware foi feita ou não.

```bash
git push -u origin feat/<slug-curto>
gh pr create --base develop --fill
```

## Promoção para `main`

`develop → main` só acontece quando tudo em `develop` está funcionando:
gates verdes e verificação em hardware feita.

```bash
gh pr create --base main --head develop --fill
```

Nunca commitar direto em `main` nem promover trabalho não verificado.

## Proteções

Em `main` e `develop` é proibido:

* commit direto;
* force-push;
* reescrever histórico publicado;
* `rebase -i` sobre histórico compartilhado;
* `commit --amend` em commits já publicados.

Histórico local ainda não publicado pode e deve ser corrigido quando violar a
regra suprema.

## Conflitos

Ao resolver conflitos:

* preservar alterações válidas existentes;
* não sobrescrever trabalho de outros autores sem necessidade;
* limitar a resolução ao escopo da tarefa;
* executar novamente os gates aplicáveis.

## Regra principal

Toda alteração segue:

**branch própria → PR → `develop`**, e só depois **`develop` → `main`**.

Nunca alterar diretamente as branches protegidas.
