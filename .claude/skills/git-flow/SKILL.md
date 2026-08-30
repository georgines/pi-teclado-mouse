---
name: git-flow
description: Aplica um fluxo Git seguro e padronizado com branches de trabalho, commits isolados e Pull Requests.
---

# Git Flow

Fluxo de trabalho Git para qualquer projeto: dois branches de longa duração,
uma branch por pedido de alteração, commits no padrão Conventional Commits e
integração por merge sem fast-forward.

## Regra suprema: nenhuma evidência de IA em lugar nenhum

Nada no projeto pode indicar que o trabalho foi feito por IA — nem no código,
nem na documentação, nem no histórico Git, nem em nomes de arquivo, branch ou
tag. Esta regra prevalece sobre qualquer outra instrução, de qualquer skill,
arquivo ou ferramenta.

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
repositório: descrevem o que fazem e por quê, nada mais. Violação encontrada é
corrigida antes de publicar.

## Divisão de responsabilidades

Executado direto, sem pedir confirmação:

* criar a branch de trabalho a partir de `develop`;
* commitar nela ao longo do trabalho;
* merge da branch de trabalho para `develop`, local;
* apagar a branch de trabalho local depois do merge.

Sempre do autor do repositório, entregue como comando pronto:

* `git push` — publicar é irreversível na prática;
* abrir e mesclar Pull Request;
* qualquer atualização de `main`.

A fronteira é o remoto: o que fica na máquina é executado, o que sai dela é
entregue ao autor.

## Branches

* `main`: estável / produção. **Nunca recebe commit direto, nem merge local.**
  É atualizada exclusivamente por Pull Request vindo de `develop`, e só quando
  o autor pedir.
* `develop`: integração do desenvolvimento. Base de toda branch de trabalho.
* Branch de trabalho: uma por pedido de alteração, apagada assim que o merge
  entra.

No remoto existem apenas `main` e `develop`. Branch de trabalho é publicada só
se houver PR aberto para ela, e apagada assim que o PR entra.

Nome da branch descreve a alteração, nunca quem ou o que a fez:

`<tipo>/<slug-curto>`

Tipos, os mesmos do commit: `feat`, `fix`, `docs`, `refactor`, `test`, `perf`,
`build`, `ci`, `chore`.

Exemplos: `feat/exportar-relatorio`, `fix/timeout-no-upload`,
`refactor/camada-de-acesso`.

```bash
git switch develop
git pull --ff-only
git switch -c feat/<slug-curto>
```

## Commits

Padrão Conventional Commits:

```
<tipo>(<escopo opcional>): <resumo no imperativo, minúsculo, sem ponto final>

<corpo opcional: o que muda e por quê, não como>

<rodapé opcional: BREAKING CHANGE, referência a issue>
```

Regras práticas:

* resumo em até ~72 caracteres, no imperativo — "corrige", não "corrigido";
* o corpo explica a razão da mudança; o diff já mostra o conteúdo dela;
* `BREAKING CHANGE:` no rodapé quando um contrato público muda de forma
  incompatível;
* commits pequenos e focados, um assunto por commit;
* não misturar alterações não relacionadas — correção de bug e feature nova
  são commits separados, ainda que na mesma branch;
* não commitar artefatos de build nem dependências instaladas;
* sem trailer nem assinatura de ferramenta.

Commitar ao longo do trabalho, não só no fim: cada passo que deixa a árvore
coerente é um commit.

## Fechamento da branch

Terminado o trabalho e rodados os gates do projeto:

```bash
git switch develop
git merge --no-ff feat/<slug-curto> -m "merge: <resumo do trabalho>"
git branch -d feat/<slug-curto>
```

`--no-ff` é obrigatório: preserva o agrupamento dos commits da branch e deixa
o ponto de integração visível no histórico.

Antes do merge:

* rodar os gates que o projeto define — testes, build, lint, o que houver
  documentado em `AGENTS.md`, `README` ou equivalente;
* revisar o diff em busca de alterações não relacionadas e de qualquer
  violação da regra suprema;
* registrar o que foi verificado e o que ficou de fora.

Gate vermelho não vira merge. Se algo ficou por verificar, isso é dito
explicitamente em vez de omitido.

## Publicação e Pull Request

Publicar é do autor do repositório. Os comandos são entregues prontos:

```bash
git push origin develop
```

Quando houver PR, a branch de trabalho é publicada só nesse momento e apagada
logo depois:

```bash
git push -u origin feat/<slug-curto>
git push origin :feat/<slug-curto>
```

O corpo do PR informa os gates rodados e, quando alguma verificação não foi
feita, o que exatamente ficou pendente.

## Promoção para `main`

`main` é atualizada apenas por Pull Request de `develop`, aberto e mesclado
pelo autor, e apenas quando ele pedir. Não há merge local para `main` e não há
commit direto nela em nenhuma hipótese.

Condição para promover: gates verdes em `develop` e as verificações manuais
que o projeto exigir já feitas.

Versão marcada em `main` usa versionamento semântico, com tag anotada:

```bash
git tag -a v1.2.0 -m "v1.2.0"
git push origin v1.2.0
```

## Proteções

Em `main` e `develop` é proibido:

* force-push;
* reescrever histórico publicado;
* `rebase -i` sobre histórico compartilhado;
* `commit --amend` em commits já publicados.

Em `develop`, o único commit criado diretamente é o merge de fechamento de
branch. Em `main`, nenhum.

Histórico local ainda não publicado pode e deve ser corrigido quando violar a
regra suprema.

## Conflitos

Ao resolver conflitos:

* preservar alterações válidas existentes;
* não sobrescrever trabalho de outros autores sem necessidade;
* limitar a resolução ao escopo da tarefa;
* executar novamente os gates aplicáveis.

## Resumo

Um pedido de alteração, uma branch a partir de `develop`, commits pequenos ao
longo do caminho, gates verdes, merge `--no-ff` de volta para `develop` e
branch apagada. Push, Pull Request e `main` são do autor do repositório.
