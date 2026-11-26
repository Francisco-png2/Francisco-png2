"""Relatórios gerenciais sobre os dados de vendas.

Períodos são recebidos como objetos `date` (inclusivos nas duas pontas).
"""

from datetime import date, timedelta


def _intervalo(inicio: date, fim: date):
    if inicio > fim:
        raise ValueError("A data inicial deve ser anterior ou igual à data final.")
    # vendas.data é 'AAAA-MM-DD HH:MM:SS'; o fim é exclusivo no dia seguinte
    return inicio.isoformat(), (fim + timedelta(days=1)).isoformat()


def melhores_clientes(conn, criterio="quantidade", limite=10):
    """Clientes com mais compras (criterio='quantidade') ou maior valor (criterio='valor')."""
    ordem = {
        "quantidade": "qtd_compras DESC, valor_total DESC",
        "valor": "valor_total DESC, qtd_compras DESC",
    }.get(criterio)
    if ordem is None:
        raise ValueError("Critério deve ser 'quantidade' ou 'valor'.")
    return conn.execute(
        f"""
        SELECT c.id, c.nome, COUNT(v.id) AS qtd_compras,
               ROUND(COALESCE(SUM(v.total), 0), 2) AS valor_total
        FROM clientes c JOIN vendas v ON v.cliente_id = c.id
        GROUP BY c.id
        ORDER BY {ordem}, c.nome
        LIMIT ?
        """,
        (limite,),
    ).fetchall()


def clientes_maior_valor(conn, limite=10):
    """Clientes com maior valor total de compras (inclui ticket médio)."""
    return conn.execute(
        """
        SELECT c.id, c.nome, ROUND(SUM(v.total), 2) AS valor_total,
               COUNT(v.id) AS qtd_compras, ROUND(AVG(v.total), 2) AS ticket_medio
        FROM clientes c JOIN vendas v ON v.cliente_id = c.id
        GROUP BY c.id
        ORDER BY valor_total DESC, c.nome
        LIMIT ?
        """,
        (limite,),
    ).fetchall()


def produtos_mais_vendidos(conn, limite=10):
    """Produtos com maior quantidade de unidades vendidas."""
    return conn.execute(
        """
        SELECT p.id, p.nome, SUM(i.quantidade) AS unidades,
               ROUND(SUM(i.quantidade * i.preco_unitario), 2) AS faturamento
        FROM produtos p JOIN itens_venda i ON i.produto_id = p.id
        GROUP BY p.id
        ORDER BY unidades DESC, faturamento DESC, p.nome
        LIMIT ?
        """,
        (limite,),
    ).fetchall()


def vendas_por_periodo(conn, inicio: date, fim: date):
    """Vendas realizadas no intervalo de datas."""
    a, b = _intervalo(inicio, fim)
    return conn.execute(
        """
        SELECT v.id, v.data, c.nome AS cliente, v.total,
               (SELECT SUM(quantidade) FROM itens_venda WHERE venda_id = v.id) AS itens
        FROM vendas v JOIN clientes c ON c.id = v.cliente_id
        WHERE v.data >= ? AND v.data < ?
        ORDER BY v.data, v.id
        """,
        (a, b),
    ).fetchall()


def faturamento_periodo(conn, inicio: date, fim: date):
    """Valor total obtido com vendas no período, com detalhamento diário."""
    a, b = _intervalo(inicio, fim)
    resumo = conn.execute(
        """
        SELECT COUNT(*) AS qtd_vendas, ROUND(COALESCE(SUM(total), 0), 2) AS faturamento,
               ROUND(COALESCE(AVG(total), 0), 2) AS ticket_medio
        FROM vendas WHERE data >= ? AND data < ?
        """,
        (a, b),
    ).fetchone()
    por_dia = conn.execute(
        """
        SELECT substr(data, 1, 10) AS dia, COUNT(*) AS qtd_vendas,
               ROUND(SUM(total), 2) AS faturamento
        FROM vendas WHERE data >= ? AND data < ?
        GROUP BY dia ORDER BY dia
        """,
        (a, b),
    ).fetchall()
    return resumo, por_dia


def historico_cliente(conn, cliente_id):
    """Histórico de compras de um cliente: uma linha por item comprado."""
    return conn.execute(
        """
        SELECT v.id AS venda_id, v.data, p.nome AS produto, i.quantidade,
               i.preco_unitario, ROUND(i.quantidade * i.preco_unitario, 2) AS subtotal,
               v.total AS total_venda
        FROM vendas v
        JOIN itens_venda i ON i.venda_id = v.id
        JOIN produtos p ON p.id = i.produto_id
        WHERE v.cliente_id = ?
        ORDER BY v.data DESC, v.id DESC, i.id
        """,
        (cliente_id,),
    ).fetchall()


def desempenho_produtos(conn, produto_id=None):
    """Quantas vezes cada produto foi vendido (nº de vendas e unidades) e o valor gerado.

    Inclui produtos nunca vendidos (com zeros).
    """
    sql = """
        SELECT p.id, p.nome,
               COUNT(DISTINCT i.venda_id) AS qtd_vendas,
               COALESCE(SUM(i.quantidade), 0) AS unidades,
               ROUND(COALESCE(SUM(i.quantidade * i.preco_unitario), 0), 2) AS faturamento
        FROM produtos p LEFT JOIN itens_venda i ON i.produto_id = p.id
    """
    params = ()
    if produto_id is not None:
        sql += " WHERE p.id = ?"
        params = (produto_id,)
    sql += " GROUP BY p.id ORDER BY qtd_vendas DESC, faturamento DESC, p.nome"
    return conn.execute(sql, params).fetchall()


def produtos_maior_faturamento(conn, limite=10):
    """Produtos que geraram maior faturamento, com participação percentual."""
    return conn.execute(
        """
        SELECT p.id, p.nome,
               ROUND(SUM(i.quantidade * i.preco_unitario), 2) AS faturamento,
               SUM(i.quantidade) AS unidades,
               ROUND(100.0 * SUM(i.quantidade * i.preco_unitario) /
                     (SELECT SUM(quantidade * preco_unitario) FROM itens_venda), 2) AS participacao
        FROM produtos p JOIN itens_venda i ON i.produto_id = p.id
        GROUP BY p.id
        ORDER BY faturamento DESC, p.nome
        LIMIT ?
        """,
        (limite,),
    ).fetchall()
