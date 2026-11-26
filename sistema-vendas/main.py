"""Ponto de entrada do Sistema de Gerenciamento de Vendas.

Uso:
    python main.py                 # abre o menu usando vendas.db
    python main.py --db outro.db   # usa outro arquivo de banco
    python main.py --exemplo       # popula o banco com dados de exemplo
"""

import argparse
import random
from datetime import datetime, timedelta

from vendas import db
from vendas import repositorios as repo
from vendas.cli import principal


def popular_exemplo(conn):
    if repo.listar_clientes(conn):
        print("O banco já possui dados; exemplo não aplicado.")
        return
    clientes = [
        ("Ana Souza", "11111111111", "ana@email.com"),
        ("Bruno Lima", "22222222222", "bruno@email.com"),
        ("Carla Mendes", "33333333333", "carla@email.com"),
        ("Diego Rocha", "44444444444", "diego@email.com"),
        ("Elisa Prado", "55555555555", "elisa@email.com"),
    ]
    produtos = [
        ("Notebook", 3500.00), ("Mouse", 79.90), ("Teclado", 149.90),
        ("Monitor 24\"", 899.00), ("Headset", 249.90), ("Webcam", 199.00),
    ]
    cids = [repo.cadastrar_cliente(conn, n, c, e) for n, c, e in clientes]
    pids = [repo.cadastrar_produto(conn, n, p, estoque=500) for n, p in produtos]
    rnd = random.Random(42)
    agora = datetime.now()
    for _ in range(40):
        itens = [(pid, rnd.randint(1, 3)) for pid in rnd.sample(pids, rnd.randint(1, 3))]
        data = agora - timedelta(days=rnd.randint(0, 90), hours=rnd.randint(0, 10))
        repo.registrar_venda(conn, rnd.choice(cids), itens, data)
    print("Dados de exemplo inseridos: 5 clientes, 6 produtos, 40 vendas.")


def main():
    parser = argparse.ArgumentParser(description="Sistema de Gerenciamento de Vendas")
    parser.add_argument("--db", default=db.CAMINHO_PADRAO, help="arquivo do banco SQLite")
    parser.add_argument("--exemplo", action="store_true", help="popula com dados de exemplo")
    args = parser.parse_args()

    conn = db.conectar(args.db)
    try:
        if args.exemplo:
            popular_exemplo(conn)
        principal(conn)
    except (KeyboardInterrupt, EOFError):
        print("\nAté logo!")
    finally:
        conn.close()


if __name__ == "__main__":
    main()
