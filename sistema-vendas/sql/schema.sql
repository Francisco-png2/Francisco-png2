-- Sistema de Gerenciamento de Vendas - esquema do banco PostgreSQL
--
-- 1) Criar o banco (uma única vez), conectado como superusuário (ex.: postgres):
--      CREATE DATABASE vendas;
--      CREATE DATABASE vendas_teste;   -- opcional, usado por "make test"
--
-- 2) As tabelas abaixo são criadas AUTOMATICAMENTE pelo programa ao conectar.
--    Este arquivo serve como documentação, ou para criá-las manualmente:
--      psql -d vendas -f sql/schema.sql

CREATE TABLE IF NOT EXISTS clientes (
    id         SERIAL PRIMARY KEY,
    nome       VARCHAR(100) NOT NULL,
    cpf        CHAR(11)     NOT NULL UNIQUE,
    email      VARCHAR(100),
    telefone   VARCHAR(30),
    endereco   VARCHAR(150),
    criado_em  TIMESTAMP(0) NOT NULL DEFAULT LOCALTIMESTAMP(0)
);

CREATE TABLE IF NOT EXISTS produtos (
    id         SERIAL PRIMARY KEY,
    nome       VARCHAR(100)  NOT NULL,
    descricao  VARCHAR(200),
    preco      NUMERIC(12,2) NOT NULL CHECK (preco >= 0),
    estoque    INTEGER       NOT NULL DEFAULT 0 CHECK (estoque >= 0)
);

CREATE TABLE IF NOT EXISTS vendas (
    id         SERIAL PRIMARY KEY,
    cliente_id INTEGER       NOT NULL REFERENCES clientes(id) ON DELETE RESTRICT,
    data       TIMESTAMP(0)  NOT NULL DEFAULT LOCALTIMESTAMP(0),
    total      NUMERIC(12,2) NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS itens_venda (
    id             SERIAL PRIMARY KEY,
    venda_id       INTEGER       NOT NULL REFERENCES vendas(id) ON DELETE CASCADE,
    produto_id     INTEGER       NOT NULL REFERENCES produtos(id) ON DELETE RESTRICT,
    quantidade     INTEGER       NOT NULL CHECK (quantidade > 0),
    preco_unitario NUMERIC(12,2) NOT NULL CHECK (preco_unitario >= 0),
    UNIQUE (venda_id, produto_id)
);

CREATE INDEX IF NOT EXISTS idx_vendas_cliente ON vendas(cliente_id);
CREATE INDEX IF NOT EXISTS idx_vendas_data    ON vendas(data);
CREATE INDEX IF NOT EXISTS idx_itens_produto  ON itens_venda(produto_id);
