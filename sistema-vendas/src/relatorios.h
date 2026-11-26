/*
 * relatorios.h - Relatórios gerenciais sobre clientes, produtos e vendas.
 *
 * Todas as funções que retornam vetores alocam memória: libere com free().
 * Períodos são informados como "AAAA-MM-DD" e incluem os dois dias extremos.
 */
#ifndef RELATORIOS_H
#define RELATORIOS_H

#include "dados.h"

typedef struct {
    int       cliente_id;
    char      nome[TAM_NOME];
    int       qtd_compras;
    long long valor_total;   /* centavos */
    long long ticket_medio;  /* centavos */
} RelCliente;

typedef struct {
    int       produto_id;
    char      nome[TAM_NOME];
    int       qtd_vendas;    /* em quantas vendas o produto apareceu */
    int       unidades;      /* unidades vendidas */
    long long faturamento;   /* centavos */
    double    participacao;  /* % do faturamento total */
} RelProduto;

typedef struct {
    char      dia[11];       /* AAAA-MM-DD */
    int       qtd_vendas;
    long long faturamento;
} RelDia;

typedef struct {
    int       qtd_vendas;
    long long faturamento;
    long long ticket_medio;
    RelDia   *dias;          /* detalhamento diário (liberar com free) */
    int       n_dias;
} RelFaturamento;

typedef struct {
    int       venda_id;
    char      data[TAM_DATA];
    char      produto[TAM_NOME];
    int       quantidade;
    long long preco_unitario;
    long long subtotal;
    long long total_venda;
} RelHistorico;

/* 1. Melhores clientes: por_valor = 0 ordena por nº de compras; 1 por valor total. */
int rel_melhores_clientes(BaseDados *bd, int por_valor, int limite, RelCliente **saida);
/* 2. Clientes com maior valor total de compras (com ticket médio). */
int rel_clientes_maior_valor(BaseDados *bd, int limite, RelCliente **saida);
/* 3. Produtos com maior quantidade de unidades vendidas. */
int rel_produtos_mais_vendidos(BaseDados *bd, int limite, RelProduto **saida);
/* 4. Vendas no período (ordem cronológica). Retorna -1 se o período for inválido. */
int rel_vendas_periodo(BaseDados *bd, const char *inicio, const char *fim, Venda ***saida);
/* 5. Faturamento no período. Retorna ERRO_VALIDACAO se o período for inválido. */
Status rel_faturamento_periodo(BaseDados *bd, const char *inicio, const char *fim,
                               RelFaturamento *saida);
/* 6. Histórico de compras de um cliente (uma linha por item, mais recentes primeiro). */
int rel_historico_cliente(BaseDados *bd, int cliente_id, RelHistorico **saida);
/* 7. Nº de vendas, unidades e valor gerado por produto (produto_id = 0: todos). */
int rel_desempenho_produtos(BaseDados *bd, int produto_id, RelProduto **saida);
/* 8. Produtos que geraram maior faturamento. */
int rel_produtos_maior_faturamento(BaseDados *bd, int limite, RelProduto **saida);

#endif
