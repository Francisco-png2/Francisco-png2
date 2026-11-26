"""Interface de linha de comando (menus interativos) do sistema de vendas."""

from datetime import date, datetime

from . import relatorios as rel
from . import repositorios as repo
from .repositorios import ErroNegocio


# ------------------------------------------------------------------ utilitários
def moeda(valor) -> str:
    texto = f"{float(valor or 0):,.2f}"
    return "R$ " + texto.replace(",", "_").replace(".", ",").replace("_", ".")


def data_br(texto_iso: str) -> str:
    return datetime.strptime(texto_iso[:19], "%Y-%m-%d %H:%M:%S").strftime("%d/%m/%Y %H:%M")


def tabela(linhas, colunas):
    """colunas: lista de (titulo, chave_ou_funcao)."""
    if not linhas:
        print("\n  (nenhum registro encontrado)\n")
        return
    valores = []
    for linha in linhas:
        valores.append([str(f(linha) if callable(f) else linha[f]) if (callable(f) or linha[f] is not None) else "-"
                        for _, f in colunas])
    larguras = [max(len(t), *(len(v[i]) for v in valores)) for i, (t, _) in enumerate(colunas)]
    sep = "  +" + "+".join("-" * (w + 2) for w in larguras) + "+"
    print(sep)
    print("  | " + " | ".join(t.ljust(w) for (t, _), w in zip(colunas, larguras)) + " |")
    print(sep)
    for v in valores:
        print("  | " + " | ".join(c.ljust(w) for c, w in zip(v, larguras)) + " |")
    print(sep)


def perguntar(rotulo, obrigatorio=False, padrao=None):
    while True:
        sufixo = f" [{padrao}]" if padrao not in (None, "") else ""
        valor = input(f"  {rotulo}{sufixo}: ").strip()
        if not valor and padrao is not None:
            return padrao
        if valor or not obrigatorio:
            return valor
        print("  ! Campo obrigatório.")


def perguntar_int(rotulo, padrao=None, minimo=None):
    while True:
        valor = perguntar(rotulo, obrigatorio=padrao is None, padrao=padrao)
        try:
            numero = int(valor)
            if minimo is not None and numero < minimo:
                raise ValueError
            return numero
        except (TypeError, ValueError):
            print("  ! Informe um número inteiro válido.")


def perguntar_float(rotulo, padrao=None):
    while True:
        valor = perguntar(rotulo, obrigatorio=padrao is None, padrao=padrao)
        try:
            return float(str(valor).replace(",", "."))
        except ValueError:
            print("  ! Informe um valor numérico válido (ex.: 19,90).")


def perguntar_data(rotulo, padrao: date = None) -> date:
    while True:
        valor = perguntar(rotulo + " (DD/MM/AAAA)", padrao=padrao.strftime("%d/%m/%Y") if padrao else None)
        try:
            return datetime.strptime(valor, "%d/%m/%Y").date()
        except ValueError:
            print("  ! Data inválida.")


def menu(titulo, opcoes):
    print(f"\n=== {titulo} ===")
    for chave, (texto, _) in opcoes.items():
        print(f"  {chave}. {texto}")
    print("  0. Voltar" if titulo != "SISTEMA DE GERENCIAMENTO DE VENDAS" else "  0. Sair")
    return input("Escolha: ").strip()


def executar_menu(titulo, opcoes, conn):
    while True:
        escolha = menu(titulo, opcoes)
        if escolha == "0":
            return
        acao = opcoes.get(escolha)
        if acao is None:
            print("  ! Opção inválida.")
            continue
        try:
            acao[1](conn)
        except ErroNegocio as e:
            print(f"  ! {e}")
        except ValueError as e:
            print(f"  ! Valor inválido: {e}")
        input("\n  [Enter para continuar]")


# ------------------------------------------------------------------ clientes
COLS_CLIENTE = [("ID", "id"), ("Nome", "nome"), ("CPF", "cpf"), ("E-mail", "email"),
                ("Telefone", "telefone"), ("Endereço", "endereco")]


def cli_cadastrar_cliente(conn):
    nome = perguntar("Nome", obrigatorio=True)
    cpf = perguntar("CPF", obrigatorio=True)
    email = perguntar("E-mail")
    telefone = perguntar("Telefone")
    endereco = perguntar("Endereço")
    novo = repo.cadastrar_cliente(conn, nome, cpf, email, telefone, endereco)
    print(f"  ✔ Cliente cadastrado com ID {novo}.")


def cli_listar_clientes(conn):
    filtro = perguntar("Filtrar por nome/CPF/e-mail (vazio = todos)")
    ordem = perguntar("Ordenar por (nome/id/cadastro)", padrao="nome")
    tabela(repo.listar_clientes(conn, filtro, ordem), COLS_CLIENTE)


def cli_consultar_cliente(conn):
    c = repo.buscar_cliente(conn, perguntar_int("ID do cliente"))
    if c is None:
        raise ErroNegocio("Cliente não encontrado.")
    tabela([c], COLS_CLIENTE + [("Cadastro", "criado_em")])


def cli_alterar_cliente(conn):
    cid = perguntar_int("ID do cliente")
    c = repo.buscar_cliente(conn, cid)
    if c is None:
        raise ErroNegocio("Cliente não encontrado.")
    print("  (Enter mantém o valor atual)")
    repo.alterar_cliente(
        conn, cid,
        nome=perguntar("Nome", padrao=c["nome"]),
        cpf=perguntar("CPF", padrao=c["cpf"]),
        email=perguntar("E-mail", padrao=c["email"] or ""),
        telefone=perguntar("Telefone", padrao=c["telefone"] or ""),
        endereco=perguntar("Endereço", padrao=c["endereco"] or ""),
    )
    print("  ✔ Cliente alterado.")


def cli_excluir_cliente(conn):
    cid = perguntar_int("ID do cliente")
    if perguntar("Confirma a exclusão? (s/n)").lower() == "s":
        repo.excluir_cliente(conn, cid)
        print("  ✔ Cliente excluído.")


# ------------------------------------------------------------------ produtos
COLS_PRODUTO = [("ID", "id"), ("Nome", "nome"), ("Descrição", "descricao"),
                ("Preço", lambda r: moeda(r["preco"])), ("Estoque", "estoque")]


def cli_cadastrar_produto(conn):
    nome = perguntar("Nome", obrigatorio=True)
    descricao = perguntar("Descrição")
    preco = perguntar_float("Preço")
    estoque = perguntar_int("Estoque inicial", padrao=0, minimo=0)
    novo = repo.cadastrar_produto(conn, nome, preco, estoque, descricao)
    print(f"  ✔ Produto cadastrado com ID {novo}.")


def cli_listar_produtos(conn):
    filtro = perguntar("Filtrar por nome/descrição (vazio = todos)")
    ordem = perguntar("Ordenar por (nome/id/preco/estoque)", padrao="nome")
    tabela(repo.listar_produtos(conn, filtro, ordem), COLS_PRODUTO)


def cli_consultar_produto(conn):
    p = repo.buscar_produto(conn, perguntar_int("ID do produto"))
    if p is None:
        raise ErroNegocio("Produto não encontrado.")
    tabela([p], COLS_PRODUTO)


def cli_alterar_produto(conn):
    pid = perguntar_int("ID do produto")
    p = repo.buscar_produto(conn, pid)
    if p is None:
        raise ErroNegocio("Produto não encontrado.")
    print("  (Enter mantém o valor atual)")
    repo.alterar_produto(
        conn, pid,
        nome=perguntar("Nome", padrao=p["nome"]),
        descricao=perguntar("Descrição", padrao=p["descricao"] or ""),
        preco=perguntar_float("Preço", padrao=p["preco"]),
        estoque=perguntar_int("Estoque", padrao=p["estoque"], minimo=0),
    )
    print("  ✔ Produto alterado.")


def cli_excluir_produto(conn):
    pid = perguntar_int("ID do produto")
    if perguntar("Confirma a exclusão? (s/n)").lower() == "s":
        repo.excluir_produto(conn, pid)
        print("  ✔ Produto excluído.")


# ------------------------------------------------------------------ vendas
def cli_registrar_venda(conn):
    cid = perguntar_int("ID do cliente")
    cliente = repo.buscar_cliente(conn, cid)
    if cliente is None:
        raise ErroNegocio("Cliente não encontrado.")
    print(f"  Cliente: {cliente['nome']}")
    itens = []
    print("  Adicione os itens (ID do produto vazio para finalizar).")
    while True:
        valor = perguntar("ID do produto")
        if not valor:
            break
        try:
            produto = repo.buscar_produto(conn, int(valor))
        except ValueError:
            produto = None
        if produto is None:
            print("  ! Produto não encontrado.")
            continue
        qtd = perguntar_int(
            f"Quantidade de '{produto['nome']}' ({moeda(produto['preco'])}, estoque {produto['estoque']})",
            minimo=1,
        )
        itens.append((produto["id"], qtd))
    if not itens:
        print("  Venda cancelada (nenhum item).")
        return
    venda_id = repo.registrar_venda(conn, cid, itens)
    cli_mostrar_venda(conn, venda_id)
    print(f"  ✔ Venda {venda_id} registrada.")


def cli_mostrar_venda(conn, venda_id=None):
    venda_id = venda_id or perguntar_int("ID da venda")
    venda, itens = repo.buscar_venda(conn, venda_id)
    if venda is None:
        raise ErroNegocio("Venda não encontrada.")
    print(f"\n  Venda #{venda['id']} — {data_br(venda['data'])} — Cliente: {venda['cliente']}")
    tabela(itens, [("Produto", "produto"), ("Qtd", "quantidade"),
                   ("Preço unit.", lambda r: moeda(r["preco_unitario"])),
                   ("Subtotal", lambda r: moeda(r["subtotal"]))])
    print(f"  TOTAL: {moeda(venda['total'])}")


def cli_listar_vendas(conn):
    tabela(repo.listar_vendas(conn), [("ID", "id"), ("Data", lambda r: data_br(r["data"])),
                                      ("Cliente", "cliente"), ("Total", lambda r: moeda(r["total"]))])


def cli_cancelar_venda(conn):
    vid = perguntar_int("ID da venda")
    if perguntar("Confirma o cancelamento? Os itens voltarão ao estoque (s/n)").lower() == "s":
        repo.cancelar_venda(conn, vid)
        print("  ✔ Venda cancelada.")


# ------------------------------------------------------------------ relatórios
def _periodo():
    hoje = date.today()
    inicio = perguntar_data("Data inicial", padrao=hoje.replace(day=1))
    fim = perguntar_data("Data final", padrao=hoje)
    return inicio, fim


def r_melhores_clientes(conn):
    crit = perguntar("Critério (quantidade/valor)", padrao="quantidade")
    limite = perguntar_int("Quantos exibir", padrao=10, minimo=1)
    tabela(rel.melhores_clientes(conn, crit, limite),
           [("ID", "id"), ("Cliente", "nome"), ("Nº compras", "qtd_compras"),
            ("Valor total", lambda r: moeda(r["valor_total"]))])


def r_clientes_maior_valor(conn):
    limite = perguntar_int("Quantos exibir", padrao=10, minimo=1)
    tabela(rel.clientes_maior_valor(conn, limite),
           [("ID", "id"), ("Cliente", "nome"), ("Valor total", lambda r: moeda(r["valor_total"])),
            ("Nº compras", "qtd_compras"), ("Ticket médio", lambda r: moeda(r["ticket_medio"]))])


def r_produtos_mais_vendidos(conn):
    limite = perguntar_int("Quantos exibir", padrao=10, minimo=1)
    tabela(rel.produtos_mais_vendidos(conn, limite),
           [("ID", "id"), ("Produto", "nome"), ("Unidades", "unidades"),
            ("Faturamento", lambda r: moeda(r["faturamento"]))])


def r_vendas_periodo(conn):
    inicio, fim = _periodo()
    linhas = rel.vendas_por_periodo(conn, inicio, fim)
    tabela(linhas, [("ID", "id"), ("Data", lambda r: data_br(r["data"])), ("Cliente", "cliente"),
                    ("Itens", "itens"), ("Total", lambda r: moeda(r["total"]))])
    print(f"  {len(linhas)} venda(s) — total {moeda(sum(r['total'] for r in linhas))}")


def r_faturamento_periodo(conn):
    inicio, fim = _periodo()
    resumo, por_dia = rel.faturamento_periodo(conn, inicio, fim)
    tabela(por_dia, [("Dia", lambda r: datetime.strptime(r["dia"], "%Y-%m-%d").strftime("%d/%m/%Y")),
                     ("Nº vendas", "qtd_vendas"), ("Faturamento", lambda r: moeda(r["faturamento"]))])
    print(f"  Período {inicio:%d/%m/%Y} a {fim:%d/%m/%Y}")
    print(f"  Vendas: {resumo['qtd_vendas']}  |  Faturamento: {moeda(resumo['faturamento'])}"
          f"  |  Ticket médio: {moeda(resumo['ticket_medio'])}")


def r_historico_cliente(conn):
    cid = perguntar_int("ID do cliente")
    cliente = repo.buscar_cliente(conn, cid)
    if cliente is None:
        raise ErroNegocio("Cliente não encontrado.")
    linhas = rel.historico_cliente(conn, cid)
    print(f"\n  Histórico de {cliente['nome']}")
    tabela(linhas, [("Venda", "venda_id"), ("Data", lambda r: data_br(r["data"])),
                    ("Produto", "produto"), ("Qtd", "quantidade"),
                    ("Preço unit.", lambda r: moeda(r["preco_unitario"])),
                    ("Subtotal", lambda r: moeda(r["subtotal"]))])
    vendas = {r["venda_id"]: r["total_venda"] for r in linhas}
    print(f"  {len(vendas)} compra(s) — total gasto {moeda(sum(vendas.values()))}")


def r_desempenho_produtos(conn):
    valor = perguntar("ID do produto (vazio = todos)")
    tabela(rel.desempenho_produtos(conn, int(valor) if valor else None),
           [("ID", "id"), ("Produto", "nome"), ("Nº vendas", "qtd_vendas"),
            ("Unidades", "unidades"), ("Faturamento", lambda r: moeda(r["faturamento"]))])


def r_produtos_maior_faturamento(conn):
    limite = perguntar_int("Quantos exibir", padrao=10, minimo=1)
    tabela(rel.produtos_maior_faturamento(conn, limite),
           [("ID", "id"), ("Produto", "nome"), ("Faturamento", lambda r: moeda(r["faturamento"])),
            ("Unidades", "unidades"), ("% do total", lambda r: f"{r['participacao']:.2f}%")])


# ------------------------------------------------------------------ menus
def menu_clientes(conn):
    executar_menu("CLIENTES", {
        "1": ("Cadastrar", cli_cadastrar_cliente),
        "2": ("Listar / pesquisar", cli_listar_clientes),
        "3": ("Consultar por ID", cli_consultar_cliente),
        "4": ("Alterar", cli_alterar_cliente),
        "5": ("Excluir", cli_excluir_cliente),
    }, conn)


def menu_produtos(conn):
    executar_menu("PRODUTOS", {
        "1": ("Cadastrar", cli_cadastrar_produto),
        "2": ("Listar / pesquisar", cli_listar_produtos),
        "3": ("Consultar por ID", cli_consultar_produto),
        "4": ("Alterar", cli_alterar_produto),
        "5": ("Excluir", cli_excluir_produto),
    }, conn)


def menu_vendas(conn):
    executar_menu("VENDAS", {
        "1": ("Registrar venda", cli_registrar_venda),
        "2": ("Listar vendas", cli_listar_vendas),
        "3": ("Detalhar venda", cli_mostrar_venda),
        "4": ("Cancelar venda", cli_cancelar_venda),
    }, conn)


def menu_relatorios(conn):
    executar_menu("RELATÓRIOS", {
        "1": ("Melhores clientes", r_melhores_clientes),
        "2": ("Clientes com maior valor em compras", r_clientes_maior_valor),
        "3": ("Produtos mais vendidos", r_produtos_mais_vendidos),
        "4": ("Vendas por período", r_vendas_periodo),
        "5": ("Faturamento por período", r_faturamento_periodo),
        "6": ("Histórico de compras do cliente", r_historico_cliente),
        "7": ("Desempenho por produto", r_desempenho_produtos),
        "8": ("Produtos com maior faturamento", r_produtos_maior_faturamento),
    }, conn)


def principal(conn):
    opcoes = {
        "1": ("Clientes", menu_clientes),
        "2": ("Produtos", menu_produtos),
        "3": ("Vendas", menu_vendas),
        "4": ("Relatórios", menu_relatorios),
    }
    while True:
        escolha = menu("SISTEMA DE GERENCIAMENTO DE VENDAS", opcoes)
        if escolha == "0":
            print("Até logo!")
            return
        if escolha in opcoes:
            opcoes[escolha][1](conn)
        else:
            print("  ! Opção inválida.")
