"""Operações de cadastro, consulta, alteração e exclusão de clientes, produtos e vendas."""

import sqlite3
from datetime import datetime


class ErroNegocio(Exception):
    """Erro de regra de negócio (dados inválidos, estoque insuficiente etc.)."""


def _somente_digitos(texto: str) -> str:
    return "".join(c for c in (texto or "") if c.isdigit())


# ---------------------------------------------------------------- clientes
CAMPOS_CLIENTE = ("nome", "cpf", "email", "telefone", "endereco")
ORDENACAO_CLIENTE = {"id": "id", "nome": "nome COLLATE NOCASE", "cadastro": "criado_em"}


def cadastrar_cliente(conn, nome, cpf, email=None, telefone=None, endereco=None) -> int:
    nome = (nome or "").strip()
    cpf = _somente_digitos(cpf)
    if not nome:
        raise ErroNegocio("O nome do cliente é obrigatório.")
    if len(cpf) != 11:
        raise ErroNegocio("O CPF deve conter 11 dígitos.")
    try:
        with conn:
            cur = conn.execute(
                "INSERT INTO clientes (nome, cpf, email, telefone, endereco) VALUES (?, ?, ?, ?, ?)",
                (nome, cpf, email or None, telefone or None, endereco or None),
            )
    except sqlite3.IntegrityError:
        raise ErroNegocio(f"Já existe um cliente com o CPF {cpf}.") from None
    return cur.lastrowid


def buscar_cliente(conn, cliente_id):
    return conn.execute("SELECT * FROM clientes WHERE id = ?", (cliente_id,)).fetchone()


def listar_clientes(conn, filtro=None, ordenar_por="nome"):
    ordem = ORDENACAO_CLIENTE.get(ordenar_por, "nome COLLATE NOCASE")
    sql = "SELECT * FROM clientes"
    params = ()
    if filtro:
        sql += " WHERE nome LIKE ? OR cpf LIKE ? OR email LIKE ?"
        termo = f"%{filtro}%"
        params = (termo, termo, termo)
    return conn.execute(f"{sql} ORDER BY {ordem}", params).fetchall()


def alterar_cliente(conn, cliente_id, **campos):
    dados = {k: v for k, v in campos.items() if k in CAMPOS_CLIENTE and v is not None}
    if not dados:
        return
    if buscar_cliente(conn, cliente_id) is None:
        raise ErroNegocio(f"Cliente {cliente_id} não encontrado.")
    if "nome" in dados and not dados["nome"].strip():
        raise ErroNegocio("O nome do cliente é obrigatório.")
    if "cpf" in dados:
        dados["cpf"] = _somente_digitos(dados["cpf"])
        if len(dados["cpf"]) != 11:
            raise ErroNegocio("O CPF deve conter 11 dígitos.")
    sets = ", ".join(f"{k} = ?" for k in dados)
    try:
        with conn:
            conn.execute(f"UPDATE clientes SET {sets} WHERE id = ?", (*dados.values(), cliente_id))
    except sqlite3.IntegrityError:
        raise ErroNegocio(f"Já existe um cliente com o CPF {dados.get('cpf')}.") from None


def excluir_cliente(conn, cliente_id):
    if buscar_cliente(conn, cliente_id) is None:
        raise ErroNegocio(f"Cliente {cliente_id} não encontrado.")
    try:
        with conn:
            conn.execute("DELETE FROM clientes WHERE id = ?", (cliente_id,))
    except sqlite3.IntegrityError:
        raise ErroNegocio("Não é possível excluir um cliente que possui vendas registradas.") from None


# ---------------------------------------------------------------- produtos
CAMPOS_PRODUTO = ("nome", "descricao", "preco", "estoque")
ORDENACAO_PRODUTO = {
    "id": "id",
    "nome": "nome COLLATE NOCASE",
    "preco": "preco",
    "estoque": "estoque",
}


def _validar_produto(dados):
    if "nome" in dados and not str(dados["nome"]).strip():
        raise ErroNegocio("O nome do produto é obrigatório.")
    if "preco" in dados:
        dados["preco"] = round(float(dados["preco"]), 2)
        if dados["preco"] < 0:
            raise ErroNegocio("O preço não pode ser negativo.")
    if "estoque" in dados:
        dados["estoque"] = int(dados["estoque"])
        if dados["estoque"] < 0:
            raise ErroNegocio("O estoque não pode ser negativo.")


def cadastrar_produto(conn, nome, preco, estoque=0, descricao=None) -> int:
    dados = {"nome": (nome or "").strip(), "preco": preco, "estoque": estoque}
    _validar_produto(dados)
    with conn:
        cur = conn.execute(
            "INSERT INTO produtos (nome, descricao, preco, estoque) VALUES (?, ?, ?, ?)",
            (dados["nome"], descricao or None, dados["preco"], dados["estoque"]),
        )
    return cur.lastrowid


def buscar_produto(conn, produto_id):
    return conn.execute("SELECT * FROM produtos WHERE id = ?", (produto_id,)).fetchone()


def listar_produtos(conn, filtro=None, ordenar_por="nome"):
    ordem = ORDENACAO_PRODUTO.get(ordenar_por, "nome COLLATE NOCASE")
    sql = "SELECT * FROM produtos"
    params = ()
    if filtro:
        sql += " WHERE nome LIKE ? OR descricao LIKE ?"
        params = (f"%{filtro}%", f"%{filtro}%")
    return conn.execute(f"{sql} ORDER BY {ordem}", params).fetchall()


def alterar_produto(conn, produto_id, **campos):
    dados = {k: v for k, v in campos.items() if k in CAMPOS_PRODUTO and v is not None}
    if not dados:
        return
    if buscar_produto(conn, produto_id) is None:
        raise ErroNegocio(f"Produto {produto_id} não encontrado.")
    _validar_produto(dados)
    sets = ", ".join(f"{k} = ?" for k in dados)
    with conn:
        conn.execute(f"UPDATE produtos SET {sets} WHERE id = ?", (*dados.values(), produto_id))


def excluir_produto(conn, produto_id):
    if buscar_produto(conn, produto_id) is None:
        raise ErroNegocio(f"Produto {produto_id} não encontrado.")
    try:
        with conn:
            conn.execute("DELETE FROM produtos WHERE id = ?", (produto_id,))
    except sqlite3.IntegrityError:
        raise ErroNegocio("Não é possível excluir um produto que já foi vendido.") from None


# ---------------------------------------------------------------- vendas
FORMATO_DATA = "%Y-%m-%d %H:%M:%S"


def registrar_venda(conn, cliente_id, itens, data=None) -> int:
    """Registra uma venda.

    itens: lista de tuplas (produto_id, quantidade). Produtos repetidos são somados.
    data: datetime opcional (padrão: agora).
    O preço unitário é gravado no momento da venda e o estoque é baixado.
    """
    if buscar_cliente(conn, cliente_id) is None:
        raise ErroNegocio(f"Cliente {cliente_id} não encontrado.")
    agrupados = {}
    for produto_id, quantidade in itens:
        quantidade = int(quantidade)
        if quantidade <= 0:
            raise ErroNegocio("A quantidade de cada item deve ser maior que zero.")
        agrupados[produto_id] = agrupados.get(produto_id, 0) + quantidade
    if not agrupados:
        raise ErroNegocio("A venda deve conter pelo menos um item.")

    data = (data or datetime.now()).strftime(FORMATO_DATA)
    with conn:  # transação: tudo ou nada
        linhas = []
        for produto_id, quantidade in agrupados.items():
            produto = buscar_produto(conn, produto_id)
            if produto is None:
                raise ErroNegocio(f"Produto {produto_id} não encontrado.")
            if produto["estoque"] < quantidade:
                raise ErroNegocio(
                    f"Estoque insuficiente para '{produto['nome']}' "
                    f"(disponível: {produto['estoque']}, solicitado: {quantidade})."
                )
            linhas.append((produto_id, quantidade, produto["preco"]))

        total = round(sum(q * p for _, q, p in linhas), 2)
        venda_id = conn.execute(
            "INSERT INTO vendas (cliente_id, data, total) VALUES (?, ?, ?)",
            (cliente_id, data, total),
        ).lastrowid
        for produto_id, quantidade, preco in linhas:
            conn.execute(
                "INSERT INTO itens_venda (venda_id, produto_id, quantidade, preco_unitario) "
                "VALUES (?, ?, ?, ?)",
                (venda_id, produto_id, quantidade, preco),
            )
            conn.execute(
                "UPDATE produtos SET estoque = estoque - ? WHERE id = ?", (quantidade, produto_id)
            )
    return venda_id


def buscar_venda(conn, venda_id):
    venda = conn.execute(
        "SELECT v.*, c.nome AS cliente FROM vendas v JOIN clientes c ON c.id = v.cliente_id "
        "WHERE v.id = ?",
        (venda_id,),
    ).fetchone()
    if venda is None:
        return None, []
    itens = conn.execute(
        "SELECT i.*, p.nome AS produto, i.quantidade * i.preco_unitario AS subtotal "
        "FROM itens_venda i JOIN produtos p ON p.id = i.produto_id WHERE i.venda_id = ? "
        "ORDER BY i.id",
        (venda_id,),
    ).fetchall()
    return venda, itens


def listar_vendas(conn):
    return conn.execute(
        "SELECT v.id, v.data, v.total, c.nome AS cliente FROM vendas v "
        "JOIN clientes c ON c.id = v.cliente_id ORDER BY v.data DESC, v.id DESC"
    ).fetchall()


def cancelar_venda(conn, venda_id):
    """Exclui a venda e devolve os itens ao estoque."""
    venda, itens = buscar_venda(conn, venda_id)
    if venda is None:
        raise ErroNegocio(f"Venda {venda_id} não encontrada.")
    with conn:
        for item in itens:
            conn.execute(
                "UPDATE produtos SET estoque = estoque + ? WHERE id = ?",
                (item["quantidade"], item["produto_id"]),
            )
        conn.execute("DELETE FROM vendas WHERE id = ?", (venda_id,))
