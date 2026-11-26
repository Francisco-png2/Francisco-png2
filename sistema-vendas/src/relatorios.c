/*
 * relatorios.c - Relatórios gerenciais calculados com SQL no PostgreSQL.
 */
#define __USE_MINGW_ANSI_STDIO 1

#include "relatorios.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int periodo_valido(const char *inicio, const char *fim)
{
    return inicio && fim && strlen(inicio) == 10 && strlen(fim) == 10 && strcmp(inicio, fim) <= 0;
}

/* ------------------------------------------------------------ clientes */

static int consultar_clientes(BaseDados *bd, const char *ordem, int limite, RelCliente **saida)
{
    char sql[1024], slim[16];
    const char *p[1] = {slim};
    PGresult *r;
    int i, n;
    *saida = NULL;
    snprintf(slim, sizeof slim, "%d", limite > 0 ? limite : 1000000);
    snprintf(sql, sizeof sql,
             "SELECT c.id, c.nome, COUNT(v.id) AS qtd_compras, "
             "       (SUM(v.total) * 100)::bigint AS valor_total, "
             "       (ROUND(AVG(v.total), 2) * 100)::bigint AS ticket_medio "
             "FROM clientes c JOIN vendas v ON v.cliente_id = c.id "
             "GROUP BY c.id, c.nome "
             "ORDER BY %s, c.nome LIMIT $1",
             ordem);
    r = bd_executar(bd, NULL, sql, 1, p);
    if (r == NULL) return -1;
    n = PQntuples(r);
    *saida = calloc((size_t)n + 1, sizeof **saida);
    if (*saida == NULL) { PQclear(r); return -1; }
    for (i = 0; i < n; i++) {
        RelCliente *x = &(*saida)[i];
        x->cliente_id = pg_int(r, i, 0);
        pg_txt(r, i, 1, x->nome, sizeof x->nome);
        x->qtd_compras = pg_int(r, i, 2);
        x->valor_total = pg_ll(r, i, 3);
        x->ticket_medio = pg_ll(r, i, 4);
    }
    PQclear(r);
    return n;
}

int rel_melhores_clientes(BaseDados *bd, int por_valor, int limite, RelCliente **saida)
{
    return consultar_clientes(bd,
                              por_valor ? "valor_total DESC, qtd_compras DESC"
                                        : "qtd_compras DESC, valor_total DESC",
                              limite, saida);
}

int rel_clientes_maior_valor(BaseDados *bd, int limite, RelCliente **saida)
{
    return consultar_clientes(bd, "valor_total DESC, qtd_compras DESC", limite, saida);
}

/* ------------------------------------------------------------ produtos */

/* somente_vendidos = 1 usa JOIN (só produtos com vendas); 0 usa LEFT JOIN (todos). */
static int consultar_produtos(BaseDados *bd, int somente_vendidos, int produto_id,
                              const char *ordem, int limite, RelProduto **saida)
{
    char sql[1536], slim[16], spid[16];
    const char *p[2] = {spid, slim};
    PGresult *r;
    int i, n;
    *saida = NULL;
    snprintf(spid, sizeof spid, "%d", produto_id);
    snprintf(slim, sizeof slim, "%d", limite > 0 ? limite : 1000000);
    snprintf(sql, sizeof sql,
             "SELECT p.id, p.nome, "
             "       COUNT(DISTINCT i.venda_id) AS qtd_vendas, "
             "       COALESCE(SUM(i.quantidade), 0) AS unidades, "
             "       (COALESCE(SUM(i.quantidade * i.preco_unitario), 0) * 100)::bigint AS faturamento, "
             "       COALESCE(ROUND(100.0 * SUM(i.quantidade * i.preco_unitario) / "
             "         NULLIF((SELECT SUM(quantidade * preco_unitario) FROM itens_venda), 0), 2), 0) "
             "FROM produtos p %s JOIN itens_venda i ON i.produto_id = p.id "
             "WHERE $1::int = 0 OR p.id = $1::int "
             "GROUP BY p.id, p.nome "
             "ORDER BY %s, p.nome LIMIT $2",
             somente_vendidos ? "" : "LEFT", ordem);
    r = bd_executar(bd, NULL, sql, 2, p);
    if (r == NULL) return -1;
    n = PQntuples(r);
    *saida = calloc((size_t)n + 1, sizeof **saida);
    if (*saida == NULL) { PQclear(r); return -1; }
    for (i = 0; i < n; i++) {
        RelProduto *x = &(*saida)[i];
        x->produto_id = pg_int(r, i, 0);
        pg_txt(r, i, 1, x->nome, sizeof x->nome);
        x->qtd_vendas = pg_int(r, i, 2);
        x->unidades = pg_int(r, i, 3);
        x->faturamento = pg_ll(r, i, 4);
        x->participacao = atof(PQgetvalue(r, i, 5));
    }
    PQclear(r);
    return n;
}

int rel_produtos_mais_vendidos(BaseDados *bd, int limite, RelProduto **saida)
{
    return consultar_produtos(bd, 1, 0, "unidades DESC, faturamento DESC", limite, saida);
}

int rel_produtos_maior_faturamento(BaseDados *bd, int limite, RelProduto **saida)
{
    return consultar_produtos(bd, 1, 0, "faturamento DESC", limite, saida);
}

int rel_desempenho_produtos(BaseDados *bd, int produto_id, RelProduto **saida)
{
    return consultar_produtos(bd, 0, produto_id, "qtd_vendas DESC, faturamento DESC", 0, saida);
}

/* ------------------------------------------------------------ vendas */

int rel_vendas_periodo(BaseDados *bd, const char *inicio, const char *fim, Venda **saida)
{
    const char *p[2] = {inicio, fim};
    *saida = NULL;
    if (!periodo_valido(inicio, fim)) return -1;
    /* data < fim + 1 dia: inclui o último dia inteiro (até 23:59:59) */
    return bd_ler_vendas(bd_executar(bd, NULL,
                                     "SELECT " SQL_VENDA_CAMPOS
                                     "FROM vendas v JOIN clientes c ON c.id = v.cliente_id "
                                     "WHERE v.data >= $1::date AND v.data < $2::date + 1 "
                                     "ORDER BY v.data, v.id", 2, p),
                         saida);
}

Status rel_faturamento_periodo(BaseDados *bd, const char *inicio, const char *fim,
                               RelFaturamento *saida)
{
    const char *p[2] = {inicio, fim};
    PGresult *r;
    Status st;
    int i;
    memset(saida, 0, sizeof *saida);
    if (!periodo_valido(inicio, fim)) return ERRO_VALIDACAO;

    r = bd_executar(bd, &st,
                    "SELECT COUNT(*), (COALESCE(SUM(total), 0) * 100)::bigint, "
                    "       (COALESCE(ROUND(AVG(total), 2), 0) * 100)::bigint "
                    "FROM vendas WHERE data >= $1::date AND data < $2::date + 1", 2, p);
    if (r == NULL) return st;
    saida->qtd_vendas = pg_int(r, 0, 0);
    saida->faturamento = pg_ll(r, 0, 1);
    saida->ticket_medio = pg_ll(r, 0, 2);
    PQclear(r);

    r = bd_executar(bd, &st,
                    "SELECT to_char(data::date, 'YYYY-MM-DD'), COUNT(*), "
                    "       (SUM(total) * 100)::bigint "
                    "FROM vendas WHERE data >= $1::date AND data < $2::date + 1 "
                    "GROUP BY data::date ORDER BY data::date", 2, p);
    if (r == NULL) return st;
    saida->n_dias = PQntuples(r);
    saida->dias = calloc((size_t)saida->n_dias + 1, sizeof *saida->dias);
    if (saida->dias == NULL) { PQclear(r); return ERRO_MEMORIA; }
    for (i = 0; i < saida->n_dias; i++) {
        pg_txt(r, i, 0, saida->dias[i].dia, sizeof saida->dias[i].dia);
        saida->dias[i].qtd_vendas = pg_int(r, i, 1);
        saida->dias[i].faturamento = pg_ll(r, i, 2);
    }
    PQclear(r);
    return OK;
}

int rel_historico_cliente(BaseDados *bd, int cliente_id, RelHistorico **saida)
{
    char sid[16];
    const char *p[1] = {sid};
    PGresult *r;
    int i, n;
    *saida = NULL;
    snprintf(sid, sizeof sid, "%d", cliente_id);
    r = bd_executar(bd, NULL,
                    "SELECT v.id, to_char(v.data, 'YYYY-MM-DD HH24:MI:SS'), p.nome, i.quantidade, "
                    "       (i.preco_unitario * 100)::bigint, "
                    "       (i.quantidade * i.preco_unitario * 100)::bigint, "
                    "       (v.total * 100)::bigint "
                    "FROM vendas v "
                    "JOIN itens_venda i ON i.venda_id = v.id "
                    "JOIN produtos p ON p.id = i.produto_id "
                    "WHERE v.cliente_id = $1 "
                    "ORDER BY v.data DESC, v.id DESC, i.id", 1, p);
    if (r == NULL) return -1;
    n = PQntuples(r);
    *saida = calloc((size_t)n + 1, sizeof **saida);
    if (*saida == NULL) { PQclear(r); return -1; }
    for (i = 0; i < n; i++) {
        RelHistorico *x = &(*saida)[i];
        x->venda_id = pg_int(r, i, 0);
        pg_txt(r, i, 1, x->data, sizeof x->data);
        pg_txt(r, i, 2, x->produto, sizeof x->produto);
        x->quantidade = pg_int(r, i, 3);
        x->preco_unitario = pg_ll(r, i, 4);
        x->subtotal = pg_ll(r, i, 5);
        x->total_venda = pg_ll(r, i, 6);
    }
    PQclear(r);
    return n;
}
