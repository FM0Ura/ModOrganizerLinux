# Verificações do repositório

Em um ambiente virtual Python, instale as ferramentas de desenvolvimento e ative
os hooks do Git:

```sh
python -m pip install -r tools/quality/requirements.txt
pre-commit install
```

O hook roda sobre os arquivos preparados com `git add`. Se o
`clang-format` alterar um arquivo, revise o diff e adicione-o novamente.
O formatador usa a configuração do componente e pula arquivos sem
`.clang-format`. O Gitleaks verifica o conteúdo preparado para o commit.
O CI também examina os commits do PR, mesmo se o diff final já não
contiver o segredo.

## Executar localmente

Na raiz do repositório:

```sh
pre-commit run actionlint --all-files
mkdocs build --strict
mkdocs serve
cargo fmt --manifest-path mo2-vfs/Cargo.toml --check
cargo clippy --manifest-path mo2-vfs/Cargo.toml --locked --all-targets -- -D warnings
cargo audit --file mo2-vfs/Cargo.lock
```

`cargo audit` requer `cargo-audit` e cobre o lockfile do VFS.
O libloot é obtido separadamente pelo script de build.

Use Python 3.14 para reproduzir a resolução de dependências do build Linux.
Cada manifesto Python é auditado em um ambiente isolado pelo `pip-audit`.

```sh
pip-audit --strict -r deps/plugin_ai_assistant/plugin-requirements.txt
```

Para rodar o Pyright, instale os requisitos do assistente no ambiente ativo e
use o Node.js:

```sh
python -m pip install -r deps/plugin_ai_assistant/plugin-requirements.txt
npm exec --yes --package=pyright@1.1.407 -- pyright --pythonpath "$(command -v python)"
```

O Pyright reporta os erros existentes sem bloquear o CI nessa etapa.
O ambiente de análise usa os stubs de PyQt6 versionados no repositório.
O módulo nativo `mobase` é produzido pelo build do MO2.
