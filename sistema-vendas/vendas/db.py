"""Conexão e criação do esquema do banco de dados (SQLite)."""

import sqlite3
from pathlib import Path

CAMINHO_PADRAO = Path(__file__).resolve().parent.parent / "vendas.db"

ESQUEMA = """
CREATE TABLE IF NOT EXISTS clientes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    nome        TEXT    NOT NULL,
    cpf         TEXT    NOT NULL UNIQUE,
    email       TEXT,
    telefone    TEXT,
    endereco    TEXT,
    criado_em   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE TABLE IF NOT EXISTS produtos (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    nome        TEXT    NOT NULL,
    descricao   TEXT,
    preco       REAL    NOT NULL CHECK (preco >= 0),
    estoque     INTEGER NOT NULL DEFAULT 0 CHECK (estoque >= 0),
    criado_em   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE TABLE IF NOT EXISTS vendas (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    cliente_id  INTEGER NOT NULL REFERENCES clientes(id) ON DELETE RESTRICT,
    data        TEXT    NOT NULL,
    total       REAL    NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS itens_venda (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    venda_id        INTEGER NOT NULL REFERENCES vendas(id) ON DELETE CASCADE,
    produto_id      INTEGER NOT NULL REFERENCES produtos(id) ON DELETE RESTRICT,
    quantidade      INTEGER NOT NULL CHECK (quantidade > 0),
    preco_unitario  REAL    NOT NULL CHECK (preco_unitario >= 0)
);

CREATE INDEX IF NOT EXISTS idx_vendas_cliente ON vendas(cliente_id);
CREATE INDEX IF NOT EXISTS idx_vendas_data    ON vendas(data);
CREATE INDEX IF NOT EXISTS idx_itens_venda    ON itens_venda(venda_id);
CREATE INDEX IF NOT EXISTS idx_itens_produto  ON itens_venda(produto_id);
"""


def conectar(caminho=CAMINHO_PADRAO) -> sqlite3.Connection:
    """Abre a conexão, ativa chaves estrangeiras e garante que o esquema exista."""
    conn = sqlite3.connect(str(caminho))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    conn.executescript(ESQUEMA)
    return conn
