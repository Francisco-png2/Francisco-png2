# Sistema de Gerenciamento de Vendas

Sistema em **Python 3** com banco de dados **SQLite** (sem dependências externas) para cadastro e gerenciamento de **clientes**, **produtos** e **vendas**, com relatórios gerenciais.

## Como executar

```bash
cd sistema-vendas
python3 main.py --exemplo   # primeira vez: cria vendas.db com dados de exemplo
python3 main.py             # uso normal (os dados ficam salvos em vendas.db)
python3 main.py --db outro.db
```

Testes:

```bash
python3 -m unittest discover -s tests -v
```

## Funcionalidades

| Módulo | Operações |
|---|---|
| Clientes | cadastrar, listar/pesquisar (nome, CPF, e-mail), ordenar, consultar, alterar, excluir |
| Produtos | cadastrar, listar/pesquisar, ordenar (nome, preço, estoque), consultar, alterar, excluir |
| Vendas | registrar venda com vários itens, listar, detalhar, cancelar (devolve ao estoque) |

Regras de negócio:
- CPF com 11 dígitos e único por cliente.
- A venda é gravada em uma **transação**: se algum item não tiver estoque, nada é gravado.
- O **preço unitário é registrado no momento da venda**, então alterar o preço de um produto não muda vendas antigas.
- O estoque é baixado ao vender e devolvido ao cancelar.
- Clientes e produtos que já constam em vendas não podem ser excluídos (integridade referencial).

## Relatórios

| # | Relatório | O que mostra |
|---|---|---|
| 1 | Melhores clientes | ranking por **quantidade de compras** ou **valor total** |
| 2 | Clientes com maior valor em compras | valor total, nº de compras e ticket médio |
| 3 | Produtos mais vendidos | unidades vendidas por produto |
| 4 | Vendas por período | vendas entre duas datas |
| 5 | Faturamento por período | total, nº de vendas, ticket médio e detalhamento diário |
| 6 | Histórico de compras do cliente | todas as compras de um cliente, item a item |
| 7 | Desempenho por produto | nº de vendas, unidades e valor gerado por produto |
| 8 | Produtos com maior faturamento | faturamento e participação % no total |

## Estrutura

```
sistema-vendas/
├── main.py               # ponto de entrada (menu interativo)
├── vendas/
│   ├── db.py             # conexão e esquema do banco
│   ├── repositorios.py   # CRUD de clientes, produtos e vendas
│   ├── relatorios.py     # consultas SQL dos relatórios
│   └── cli.py            # menus e telas no terminal
└── tests/test_sistema.py
```

## Modelo de dados

```
clientes (id, nome, cpf UNIQUE, email, telefone, endereco, criado_em)
produtos (id, nome, descricao, preco, estoque, criado_em)
vendas   (id, cliente_id → clientes, data, total)
itens_venda (id, venda_id → vendas, produto_id → produtos, quantidade, preco_unitario)
```
