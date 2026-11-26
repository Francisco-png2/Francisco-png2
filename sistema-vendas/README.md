# Sistema de Gerenciamento de Vendas (C + PostgreSQL/libpq)

Sistema em **linguagem C (C99)** para cadastro e gerenciamento de **clientes**, **produtos** e **vendas**, com relatórios gerenciais. Os dados são **armazenados em um banco PostgreSQL**, acessado pela biblioteca oficial **libpq**.

## Pré-requisitos

- Servidor **PostgreSQL** em execução.
- **libpq** com os arquivos de desenvolvimento:
  - Ubuntu/Debian: `sudo apt install libpq-dev`
  - Fedora: `sudo dnf install libpq-devel`
  - macOS: `brew install libpq`
  - Windows: o instalador do PostgreSQL já inclui a libpq (`include/`, `lib/` e `bin/libpq.dll`).

## 1. Criar o banco de dados

Uma única vez, no `psql` (como usuário `postgres`):

```sql
CREATE DATABASE vendas;
CREATE DATABASE vendas_teste;  -- opcional, usado por "make test"
```

O programa cria as tabelas automaticamente ao conectar. O arquivo `sql/schema.sql` traz o mesmo esquema para consulta ou criação manual.

## 2. Compilar

```bash
make            # gera ./sistema
```

Sem `make`:

```bash
gcc -std=c99 -Wall -I"$(pg_config --includedir)" -o sistema \
    src/dados.c src/relatorios.c src/main.c -L"$(pg_config --libdir)" -lpq
```

No Windows (MinGW), ajuste o caminho da sua versão do PostgreSQL:

```bat
gcc -std=c99 -Wall -I"C:\Program Files\PostgreSQL\16\include" -o sistema.exe ^
    src\dados.c src\relatorios.c src\main.c -L"C:\Program Files\PostgreSQL\16\lib" -lpq
```

Para executar no Windows, a pasta `C:\Program Files\PostgreSQL\16\bin` (onde está a `libpq.dll`) precisa estar no `PATH`.

## 3. Executar

A conexão é informada em uma *string de conexão* da libpq:

```bash
./sistema --conexao "host=localhost dbname=vendas user=postgres password=SUA_SENHA" --exemplo
```

- `--exemplo` insere 5 clientes, 6 produtos e 40 vendas se o banco estiver vazio. Use só na primeira vez.
- Sem `--conexao`, o programa usa a variável de ambiente `VENDAS_CONEXAO` ou, se ela não existir, `host=localhost dbname=vendas`.
- A libpq também lê as variáveis padrão `PGUSER`, `PGPASSWORD`, `PGHOST` e `PGPORT`.

## Testes automatizados

```bash
make test   # usa o banco vendas_teste; ATENÇÃO: apaga os dados desse banco
```

Para usar outra conexão nos testes: `VENDAS_TESTE_CONEXAO="host=localhost dbname=vendas_teste user=postgres password=..." make test`.

## Funcionalidades

| Módulo | Operações |
|---|---|
| Clientes | cadastrar, listar/pesquisar (nome, CPF, e-mail), ordenar (nome, ID, data de cadastro), consultar, alterar, excluir |
| Produtos | cadastrar, listar/pesquisar (nome, descrição), ordenar (nome, ID, preço, estoque), consultar, alterar, excluir |
| Vendas | registrar venda com vários itens, listar, detalhar, cancelar (devolve os itens ao estoque) |

Regras de negócio e uso do banco:
- CPF obrigatório, com 11 dígitos e único (restrição `UNIQUE`). Pode ser digitado com pontos e traço.
- A venda é registrada em uma **transação** (`BEGIN`/`COMMIT`/`ROLLBACK`). Se faltar estoque de algum item, nada é gravado.
- Os produtos da venda são travados com `SELECT ... FOR UPDATE`, para que duas vendas simultâneas não usem o mesmo estoque.
- O **preço unitário é gravado no momento da venda**: mudar o preço de um produto não altera vendas antigas.
- O estoque é baixado ao vender e devolvido ao cancelar, também dentro de uma transação.
- **Chaves estrangeiras** impedem excluir clientes ou produtos que já constam em vendas.
- Todos os comandos usam **parâmetros** (`PQexecParams` com `$1`, `$2`, ...), o que protege contra *SQL injection*.
- Dinheiro é `NUMERIC(12,2)` no banco e centavos (`long long`) no programa, sem erros de arredondamento.

## Relatórios

Todos calculados com consultas SQL (`JOIN`, `GROUP BY`, `SUM`, `COUNT`, `AVG`):

| # | Relatório | O que mostra |
|---|---|---|
| 1 | Melhores clientes | ranking por **quantidade de compras** ou por **valor total** |
| 2 | Clientes com maior valor em compras | valor total, nº de compras e ticket médio |
| 3 | Produtos mais vendidos | unidades vendidas e faturamento por produto |
| 4 | Vendas por período | vendas entre duas datas (DD/MM/AAAA) |
| 5 | Faturamento por período | total, nº de vendas, ticket médio e detalhamento por dia |
| 6 | Histórico de compras do cliente | todas as compras de um cliente, item a item |
| 7 | Desempenho por produto | nº de vendas, unidades e valor gerado (inclui produtos nunca vendidos) |
| 8 | Produtos com maior faturamento | faturamento e participação % no total |

## Estrutura

```
sistema-vendas/
├── Makefile
├── sql/schema.sql                  # esquema do banco (tabelas, chaves, índices)
├── src/
│   ├── dados.h / dados.c           # conexão libpq, transações e CRUD
│   ├── relatorios.h / relatorios.c # consultas SQL dos 8 relatórios
│   └── main.c                      # menus, leitura de dados e exibição em tabelas
└── tests/testes.c                  # testes automatizados
```

## Modelo de dados

```
clientes    (id PK, nome, cpf UNIQUE, email, telefone, endereco, criado_em)
produtos    (id PK, nome, descricao, preco, estoque)
vendas      (id PK, cliente_id FK → clientes, data, total)
itens_venda (id PK, venda_id FK → vendas, produto_id FK → produtos, quantidade, preco_unitario)
```
