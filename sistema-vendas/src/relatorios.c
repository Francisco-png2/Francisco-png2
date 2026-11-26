/*
 * relatorios.c - Cálculo dos relatórios gerenciais.
 */
#include "relatorios.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ apoio */

static int cmp_ll_desc(long long a, long long b) { return (a < b) - (a > b); }

static int periodo_valido(const char *inicio, const char *fim)
{
    return inicio && fim && strlen(inicio) == 10 && strlen(fim) == 10 && strcmp(inicio, fim) <= 0;
}

static int na_data(const char *data, const char *inicio, const char *fim)
{
    return strncmp(data, inicio, 10) >= 0 && strncmp(data, fim, 10) <= 0;
}

/* ------------------------------------------------------------ clientes */

/* Monta o resumo de compras de cada cliente que comprou pelo menos uma vez. */
static int resumo_clientes(BaseDados *bd, RelCliente **saida)
{
    int i, j, n = 0;
    RelCliente *r = calloc((size_t)bd->n_clientes + 1, sizeof *r);
    *saida = r;
    if (r == NULL) return 0;
    for (i = 0; i < bd->n_clientes; i++) {
        const Cliente *c = &bd->clientes[i];
        RelCliente x;
        memset(&x, 0, sizeof x);
        for (j = 0; j < bd->n_vendas; j++) {
            if (bd->vendas[j].cliente_id == c->id) {
                x.qtd_compras++;
                x.valor_total += bd->vendas[j].total;
            }
        }
        if (x.qtd_compras == 0) continue;
        x.cliente_id = c->id;
        strcpy(x.nome, c->nome);
        x.ticket_medio = (x.valor_total + x.qtd_compras / 2) / x.qtd_compras;
        r[n++] = x;
    }
    return n;
}

static int cmp_cli_qtd(const void *a, const void *b)
{
    const RelCliente *x = a, *y = b;
    int r = y->qtd_compras - x->qtd_compras;
    if (!r) r = cmp_ll_desc(x->valor_total, y->valor_total);
    return r ? r : strcmp(x->nome, y->nome);
}

static int cmp_cli_valor(const void *a, const void *b)
{
    const RelCliente *x = a, *y = b;
    int r = cmp_ll_desc(x->valor_total, y->valor_total);
    if (!r) r = y->qtd_compras - x->qtd_compras;
    return r ? r : strcmp(x->nome, y->nome);
}

int rel_melhores_clientes(BaseDados *bd, int por_valor, int limite, RelCliente **saida)
{
    int n = resumo_clientes(bd, saida);
    qsort(*saida, (size_t)n, sizeof **saida, por_valor ? cmp_cli_valor : cmp_cli_qtd);
    return (limite > 0 && n > limite) ? limite : n;
}

int rel_clientes_maior_valor(BaseDados *bd, int limite, RelCliente **saida)
{
    return rel_melhores_clientes(bd, 1, limite, saida);
}

/* ------------------------------------------------------------ produtos */

/* Estatísticas de todos os produtos (inclusive os nunca vendidos). */
static int resumo_produtos(BaseDados *bd, RelProduto **saida)
{
    int i, j;
    long long total_geral = 0;
    RelProduto *r = calloc((size_t)bd->n_produtos + 1, sizeof *r);
    *saida = r;
    if (r == NULL) return 0;
    for (i = 0; i < bd->n_produtos; i++) {
        const Produto *p = &bd->produtos[i];
        r[i].produto_id = p->id;
        strcpy(r[i].nome, p->nome);
        for (j = 0; j < bd->n_itens; j++) {
            const ItemVenda *it = &bd->itens[j];
            if (it->produto_id != p->id) continue;
            r[i].qtd_vendas++; /* cada venda tem no máximo um item por produto */
            r[i].unidades += it->quantidade;
            r[i].faturamento += (long long)it->quantidade * it->preco_unitario;
        }
        total_geral += r[i].faturamento;
    }
    for (i = 0; i < bd->n_produtos; i++)
        r[i].participacao = total_geral ? 100.0 * (double)r[i].faturamento / (double)total_geral : 0;
    return bd->n_produtos;
}

/* Remove os produtos que nunca foram vendidos. */
static int somente_vendidos(RelProduto *r, int n)
{
    int i, k = 0;
    for (i = 0; i < n; i++)
        if (r[i].unidades > 0) r[k++] = r[i];
    return k;
}

static int cmp_prod_unidades(const void *a, const void *b)
{
    const RelProduto *x = a, *y = b;
    int r = y->unidades - x->unidades;
    if (!r) r = cmp_ll_desc(x->faturamento, y->faturamento);
    return r ? r : strcmp(x->nome, y->nome);
}

static int cmp_prod_faturamento(const void *a, const void *b)
{
    const RelProduto *x = a, *y = b;
    int r = cmp_ll_desc(x->faturamento, y->faturamento);
    return r ? r : strcmp(x->nome, y->nome);
}

static int cmp_prod_vendas(const void *a, const void *b)
{
    const RelProduto *x = a, *y = b;
    int r = y->qtd_vendas - x->qtd_vendas;
    if (!r) r = cmp_ll_desc(x->faturamento, y->faturamento);
    return r ? r : strcmp(x->nome, y->nome);
}

int rel_produtos_mais_vendidos(BaseDados *bd, int limite, RelProduto **saida)
{
    int n = resumo_produtos(bd, saida);
    n = somente_vendidos(*saida, n);
    qsort(*saida, (size_t)n, sizeof **saida, cmp_prod_unidades);
    return (limite > 0 && n > limite) ? limite : n;
}

int rel_produtos_maior_faturamento(BaseDados *bd, int limite, RelProduto **saida)
{
    int n = resumo_produtos(bd, saida);
    n = somente_vendidos(*saida, n);
    qsort(*saida, (size_t)n, sizeof **saida, cmp_prod_faturamento);
    return (limite > 0 && n > limite) ? limite : n;
}

int rel_desempenho_produtos(BaseDados *bd, int produto_id, RelProduto **saida)
{
    int i, n = resumo_produtos(bd, saida);
    if (produto_id > 0) {
        for (i = 0; i < n && (*saida)[i].produto_id != produto_id; i++) {}
        if (i == n) return 0;
        (*saida)[0] = (*saida)[i];
        return 1;
    }
    qsort(*saida, (size_t)n, sizeof **saida, cmp_prod_vendas);
    return n;
}

/* ------------------------------------------------------------ vendas */

static int cmp_venda_asc(const void *a, const void *b)
{
    const Venda *x = *(Venda *const *)a, *y = *(Venda *const *)b;
    int r = strcmp(x->data, y->data);
    return r ? r : x->id - y->id;
}

int rel_vendas_periodo(BaseDados *bd, const char *inicio, const char *fim, Venda ***saida)
{
    int i, n = 0;
    Venda **v;
    *saida = NULL;
    if (!periodo_valido(inicio, fim)) return -1;
    v = malloc((size_t)(bd->n_vendas + 1) * sizeof *v);
    *saida = v;
    if (v == NULL) return 0;
    for (i = 0; i < bd->n_vendas; i++)
        if (na_data(bd->vendas[i].data, inicio, fim)) v[n++] = &bd->vendas[i];
    qsort(v, (size_t)n, sizeof *v, cmp_venda_asc);
    return n;
}

Status rel_faturamento_periodo(BaseDados *bd, const char *inicio, const char *fim,
                               RelFaturamento *saida)
{
    Venda **v;
    int i, n = rel_vendas_periodo(bd, inicio, fim, &v);
    memset(saida, 0, sizeof *saida);
    if (n < 0) return ERRO_VALIDACAO;
    saida->dias = calloc((size_t)n + 1, sizeof *saida->dias);
    if (saida->dias == NULL) { free(v); return ERRO_MEMORIA; }
    for (i = 0; i < n; i++) { /* vendas já estão em ordem cronológica */
        RelDia *d = saida->n_dias ? &saida->dias[saida->n_dias - 1] : NULL;
        if (d == NULL || strncmp(d->dia, v[i]->data, 10) != 0) {
            d = &saida->dias[saida->n_dias++];
            memcpy(d->dia, v[i]->data, 10);
            d->dia[10] = '\0';
        }
        d->qtd_vendas++;
        d->faturamento += v[i]->total;
        saida->qtd_vendas++;
        saida->faturamento += v[i]->total;
    }
    if (saida->qtd_vendas)
        saida->ticket_medio = (saida->faturamento + saida->qtd_vendas / 2) / saida->qtd_vendas;
    free(v);
    return OK;
}

int rel_historico_cliente(BaseDados *bd, int cliente_id, RelHistorico **saida)
{
    Venda **v;
    int i, j, n = 0, nv = venda_listar(bd, &v); /* mais recentes primeiro */
    RelHistorico *r = calloc((size_t)bd->n_itens + 1, sizeof *r);
    *saida = r;
    if (r == NULL) { free(v); return 0; }
    for (i = 0; i < nv; i++) {
        if (v[i]->cliente_id != cliente_id) continue;
        for (j = 0; j < bd->n_itens; j++) {
            const ItemVenda *it = &bd->itens[j];
            const Produto *p;
            if (it->venda_id != v[i]->id) continue;
            p = produto_buscar(bd, it->produto_id);
            r[n].venda_id = v[i]->id;
            strcpy(r[n].data, v[i]->data);
            strcpy(r[n].produto, p ? p->nome : "(produto removido)");
            r[n].quantidade = it->quantidade;
            r[n].preco_unitario = it->preco_unitario;
            r[n].subtotal = (long long)it->quantidade * it->preco_unitario;
            r[n].total_venda = v[i]->total;
            n++;
        }
    }
    free(v);
    return n;
}
