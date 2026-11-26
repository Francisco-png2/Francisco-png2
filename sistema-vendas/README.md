# Sistema de Gerenciamento de Vendas (C)

Sistema em **linguagem C (C99)** para cadastro e gerenciamento de **clientes**, **produtos** e **vendas**, com relatórios gerenciais. Usa apenas a biblioteca padrão do C, sem dependências externas. Os dados são **armazenados em arquivos** e continuam disponíveis entre execuções.

## Como compilar e executar

Com `make` (Linux, macOS, MSYS2/MinGW):

```bash
make            # gera o executável ./sistema
make test       # compila e executa os testes automatizados
./sistema --exemplo   # primeira vez: popula com dados de exemplo
./sistema             # uso normal
```

Sem `make` (qualquer compilador C, incluindo Windows/MinGW ou Code::Blocks):

```bash
gcc -std=c99 -Wall -o sistema src/dados.c src/relatorios.c src/main.c
```

Opções: `--dados PASTA` (pasta dos arquivos, padrão `dados/`) e `--exemplo` (insere 5 clientes, 6 produtos e 40 vendas se a base estiver vazia).

## Funcionalidades

| Módulo | Operações |
|---|---|
| Clientes | cadastrar, listar/pesquisar (nome, CPF, e-mail), ordenar (nome, ID, data de cadastro), consultar, alterar, excluir |
| Produtos | cadastrar, listar/pesquisar (nome, descrição), ordenar (nome, ID, preço, estoque), consultar, alterar, excluir |
| Vendas | registrar venda com vários itens, listar, detalhar, cancelar (devolve os itens ao estoque) |

Regras de negócio:
- CPF obrigatório, com 11 dígitos e único por cliente (pode ser digitado com pontos e traço).
- A venda é "tudo ou nada": se faltar estoque de algum item, nada é gravado.
- O **preço unitário é gravado no momento da venda**, então mudar o preço de um produto não altera vendas antigas.
- O estoque é baixado ao vender e devolvido ao cancelar.
- Clientes e produtos que já constam em vendas não podem ser excluídos.
- Valores em dinheiro são guardados em **centavos** (`long long`) para evitar erros de arredondamento.

## Relatórios

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
├── src/
│   ├── dados.h / dados.c           # estruturas, arquivos e CRUD (regras de negócio)
│   ├── relatorios.h / relatorios.c # cálculo dos 8 relatórios
│   └── main.c                      # menus, leitura de dados e exibição em tabelas
└── tests/testes.c                  # testes automatizados
```

## Armazenamento

Na pasta `dados/`, um registro por linha, campos separados por `|`:

```
clientes.txt : id|nome|cpf|email|telefone|endereco|data_cadastro
produtos.txt : id|nome|descricao|preco_centavos|estoque
vendas.txt   : id|cliente_id|data|total_centavos
itens.txt    : venda_id|produto_id|quantidade|preco_unitario_centavos
```

Os arquivos são gravados após cada alteração, primeiro em um arquivo temporário que depois substitui o original. Assim os dados não se corrompem se o programa for interrompido no meio de uma gravação.
