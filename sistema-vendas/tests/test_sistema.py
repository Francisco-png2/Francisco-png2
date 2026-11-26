import sys
import unittest
from datetime import date, datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from vendas import db  # noqa: E402
from vendas import relatorios as rel  # noqa: E402
from vendas import repositorios as repo  # noqa: E402
from vendas.repositorios import ErroNegocio  # noqa: E402


class Base(unittest.TestCase):
    def setUp(self):
        self.conn = db.conectar(":memory:")
        self.ana = repo.cadastrar_cliente(self.conn, "Ana", "111.111.111-11")
        self.bruno = repo.cadastrar_cliente(self.conn, "Bruno", "22222222222")
        self.caneta = repo.cadastrar_produto(self.conn, "Caneta", 2.50, estoque=100)
        self.caderno = repo.cadastrar_produto(self.conn, "Caderno", 20.00, estoque=50)

    def tearDown(self):
        self.conn.close()


class TestCadastros(Base):
    def test_cpf_normalizado_e_unico(self):
        self.assertEqual(repo.buscar_cliente(self.conn, self.ana)["cpf"], "11111111111")
        with self.assertRaises(ErroNegocio):
            repo.cadastrar_cliente(self.conn, "Outra", "11111111111")

    def test_validacoes(self):
        with self.assertRaises(ErroNegocio):
            repo.cadastrar_cliente(self.conn, "", "33333333333")
        with self.assertRaises(ErroNegocio):
            repo.cadastrar_cliente(self.conn, "X", "123")
        with self.assertRaises(ErroNegocio):
            repo.cadastrar_produto(self.conn, "X", -1)

    def test_alterar_e_listar(self):
        repo.alterar_cliente(self.conn, self.ana, nome="Ana Maria", email="a@x.com")
        self.assertEqual(repo.buscar_cliente(self.conn, self.ana)["nome"], "Ana Maria")
        repo.alterar_produto(self.conn, self.caneta, preco=3)
        self.assertEqual(repo.buscar_produto(self.conn, self.caneta)["preco"], 3.0)
        self.assertEqual([c["nome"] for c in repo.listar_clientes(self.conn, "Bru")], ["Bruno"])
        self.assertEqual(
            [p["nome"] for p in repo.listar_produtos(self.conn, ordenar_por="preco")],
            ["Caneta", "Caderno"],
        )

    def test_nao_exclui_cliente_com_venda(self):
        repo.registrar_venda(self.conn, self.ana, [(self.caneta, 1)])
        with self.assertRaises(ErroNegocio):
            repo.excluir_cliente(self.conn, self.ana)
        repo.excluir_cliente(self.conn, self.bruno)
        self.assertIsNone(repo.buscar_cliente(self.conn, self.bruno))


class TestVendas(Base):
    def test_registro_baixa_estoque_e_grava_preco(self):
        vid = repo.registrar_venda(self.conn, self.ana, [(self.caneta, 4), (self.caderno, 1), (self.caneta, 1)])
        venda, itens = repo.buscar_venda(self.conn, vid)
        self.assertAlmostEqual(venda["total"], 5 * 2.5 + 20)
        self.assertEqual(len(itens), 2)
        self.assertEqual(repo.buscar_produto(self.conn, self.caneta)["estoque"], 95)
        # alterar o preço depois não muda a venda
        repo.alterar_produto(self.conn, self.caneta, preco=10)
        self.assertAlmostEqual(repo.buscar_venda(self.conn, vid)[0]["total"], 32.5)

    def test_estoque_insuficiente_nao_grava_nada(self):
        with self.assertRaises(ErroNegocio):
            repo.registrar_venda(self.conn, self.ana, [(self.caneta, 1), (self.caderno, 51)])
        self.assertEqual(repo.listar_vendas(self.conn), [])
        self.assertEqual(repo.buscar_produto(self.conn, self.caneta)["estoque"], 100)

    def test_cancelar_devolve_estoque(self):
        vid = repo.registrar_venda(self.conn, self.ana, [(self.caderno, 5)])
        repo.cancelar_venda(self.conn, vid)
        self.assertEqual(repo.buscar_produto(self.conn, self.caderno)["estoque"], 50)
        self.assertEqual(self.conn.execute("SELECT COUNT(*) FROM itens_venda").fetchone()[0], 0)

    def test_persistencia_em_arquivo(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            caminho = Path(d) / "t.db"
            c1 = db.conectar(caminho)
            repo.cadastrar_cliente(c1, "Zé", "99999999999")
            c1.close()
            c2 = db.conectar(caminho)
            self.assertEqual(len(repo.listar_clientes(c2)), 1)
            c2.close()


class TestRelatorios(Base):
    def setUp(self):
        super().setUp()
        v = repo.registrar_venda
        v(self.conn, self.ana, [(self.caneta, 10)], datetime(2026, 1, 5, 10))      # 25
        v(self.conn, self.ana, [(self.caneta, 2)], datetime(2026, 1, 20, 23, 59))  # 5
        v(self.conn, self.ana, [(self.caneta, 4)], datetime(2026, 2, 1, 9))        # 10
        v(self.conn, self.bruno, [(self.caderno, 3), (self.caneta, 1)], datetime(2026, 1, 10))  # 62.5

    def test_melhores_clientes(self):
        por_qtd = rel.melhores_clientes(self.conn, "quantidade")
        self.assertEqual((por_qtd[0]["nome"], por_qtd[0]["qtd_compras"]), ("Ana", 3))
        por_valor = rel.melhores_clientes(self.conn, "valor")
        self.assertEqual(por_valor[0]["nome"], "Bruno")

    def test_clientes_maior_valor(self):
        r = rel.clientes_maior_valor(self.conn)
        self.assertEqual([x["nome"] for x in r], ["Bruno", "Ana"])
        self.assertAlmostEqual(r[1]["valor_total"], 40)

    def test_produtos_mais_vendidos(self):
        r = rel.produtos_mais_vendidos(self.conn)
        self.assertEqual((r[0]["nome"], r[0]["unidades"]), ("Caneta", 17))

    def test_vendas_e_faturamento_periodo(self):
        jan = rel.vendas_por_periodo(self.conn, date(2026, 1, 1), date(2026, 1, 20))
        self.assertEqual(len(jan), 3)  # inclui a venda de 20/01 às 23:59
        resumo, por_dia = rel.faturamento_periodo(self.conn, date(2026, 1, 1), date(2026, 1, 31))
        self.assertEqual(resumo["qtd_vendas"], 3)
        self.assertAlmostEqual(resumo["faturamento"], 92.5)
        self.assertEqual(len(por_dia), 3)
        with self.assertRaises(ValueError):
            rel.vendas_por_periodo(self.conn, date(2026, 2, 1), date(2026, 1, 1))

    def test_historico_cliente(self):
        h = rel.historico_cliente(self.conn, self.bruno)
        self.assertEqual(len(h), 2)
        self.assertEqual({x["produto"] for x in h}, {"Caderno", "Caneta"})

    def test_desempenho_produtos(self):
        repo.cadastrar_produto(self.conn, "Régua", 5)
        r = {x["nome"]: x for x in rel.desempenho_produtos(self.conn)}
        self.assertEqual((r["Caneta"]["qtd_vendas"], r["Caneta"]["unidades"]), (4, 17))
        self.assertAlmostEqual(r["Caneta"]["faturamento"], 42.5)
        self.assertEqual(r["Régua"]["qtd_vendas"], 0)
        self.assertEqual(len(rel.desempenho_produtos(self.conn, self.caderno)), 1)

    def test_produtos_maior_faturamento(self):
        r = rel.produtos_maior_faturamento(self.conn)
        self.assertEqual(r[0]["nome"], "Caderno")
        self.assertAlmostEqual(r[0]["participacao"] + r[1]["participacao"], 100, places=1)


if __name__ == "__main__":
    unittest.main()
