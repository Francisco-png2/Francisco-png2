/*
 * testes.c - Testes automatizados das regras de negócio e dos relatórios.
 * Compilar e executar:  make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/dados.h"
#include "../src/relatorios.h"

static int falhas = 0, verificacoes = 0;

#define VERIFICA(cond)                                                          \
    do {                                                                        \
        verificacoes++;                                                         \
        if (!(cond)) {                                                          \
            falhas++;                                                           \
            printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                       \
    } while (0)

static BaseDados bd;
static int ana, bruno, caneta, caderno;

static void preparar(void)
{
    bd_abrir(&bd, NULL); /* somente memória */
    cliente_cadastrar(&bd, "Ana", "111.111.111-11", "", "", "", &ana);
    cliente_cadastrar(&bd, "Bruno", "22222222222", "", "", "", &bruno);
    produto_cadastrar(&bd, "Caneta", "", 250, 100, &caneta);   /* R$ 2,50 */
    produto_cadastrar(&bd, "Caderno", "", 2000, 50, &caderno); /* R$ 20,00 */
}

static void teste_cadastros(void)
{
    Cliente **lc;
    Produto **lp;
    long long preco = 300;
    int n;
    preparar();
    VERIFICA(strcmp(cliente_buscar(&bd, ana)->cpf, "11111111111") == 0);
    VERIFICA(cliente_cadastrar(&bd, "Outra", "11111111111", "", "", "", NULL) == ERRO_DUPLICADO);
    VERIFICA(cliente_cadastrar(&bd, "", "33333333333", "", "", "", NULL) == ERRO_VALIDACAO);
    VERIFICA(cliente_cadastrar(&bd, "X", "123", "", "", "", NULL) == ERRO_VALIDACAO);
    VERIFICA(produto_cadastrar(&bd, "X", "", -1, 0, NULL) == ERRO_VALIDACAO);
    VERIFICA(cliente_cadastrar(&bd, "Com|barra", "44444444444", "", "", "", &n) == OK);
    VERIFICA(strcmp(cliente_buscar(&bd, n)->nome, "Com barra") == 0);

    VERIFICA(cliente_alterar(&bd, ana, "Ana Maria", NULL, "a@x.com", NULL, NULL) == OK);
    VERIFICA(strcmp(cliente_buscar(&bd, ana)->nome, "Ana Maria") == 0);
    VERIFICA(cliente_alterar(&bd, ana, NULL, "22222222222", NULL, NULL, NULL) == ERRO_DUPLICADO);
    VERIFICA(produto_alterar(&bd, caneta, NULL, NULL, &preco, NULL) == OK);
    VERIFICA(produto_buscar(&bd, caneta)->preco == 300);

    n = cliente_listar(&bd, "bru", ORD_CLI_NOME, &lc);
    VERIFICA(n == 1 && strcmp(lc[0]->nome, "Bruno") == 0);
    free(lc);
    n = produto_listar(&bd, NULL, ORD_PROD_PRECO, &lp);
    VERIFICA(n == 2 && lp[0]->id == caneta);
    free(lp);

    VERIFICA(venda_registrar(&bd, ana, &(ItemPedido){caneta, 1}, 1, NULL, NULL) == OK);
    VERIFICA(cliente_excluir(&bd, ana) == ERRO_VINCULO);
    VERIFICA(produto_excluir(&bd, caneta) == ERRO_VINCULO);
    VERIFICA(cliente_excluir(&bd, bruno) == OK);
    VERIFICA(cliente_buscar(&bd, bruno) == NULL);
    bd_fechar(&bd);
}

static void teste_vendas(void)
{
    ItemPedido itens[] = {{0, 4}, {0, 1}, {0, 1}};
    ItemPedido falho[] = {{0, 1}, {0, 51}};
    ItemVenda **iv;
    long long preco = 1000;
    int vid, n;
    preparar();
    itens[0].produto_id = caneta;
    itens[1].produto_id = caderno;
    itens[2].produto_id = caneta;
    VERIFICA(venda_registrar(&bd, ana, itens, 3, NULL, &vid) == OK);
    VERIFICA(venda_buscar(&bd, vid)->total == 5 * 250 + 2000);
    n = venda_itens(&bd, vid, &iv);
    VERIFICA(n == 2); /* produtos repetidos agrupados */
    free(iv);
    VERIFICA(produto_buscar(&bd, caneta)->estoque == 95);
    /* alterar o preço depois não muda a venda */
    produto_alterar(&bd, caneta, NULL, NULL, &preco, NULL);
    VERIFICA(venda_buscar(&bd, vid)->total == 3250);

    /* estoque insuficiente: nada é gravado */
    falho[0].produto_id = caneta;
    falho[1].produto_id = caderno;
    VERIFICA(venda_registrar(&bd, ana, falho, 2, NULL, NULL) == ERRO_ESTOQUE);
    VERIFICA(bd.n_vendas == 1 && produto_buscar(&bd, caneta)->estoque == 95);
    VERIFICA(venda_registrar(&bd, 999, itens, 1, NULL, NULL) == ERRO_NAO_ENCONTRADO);
    VERIFICA(venda_registrar(&bd, ana, itens, 0, NULL, NULL) == ERRO_VALIDACAO);

    /* cancelar devolve ao estoque */
    VERIFICA(venda_cancelar(&bd, vid) == OK);
    VERIFICA(produto_buscar(&bd, caneta)->estoque == 100);
    VERIFICA(produto_buscar(&bd, caderno)->estoque == 50);
    VERIFICA(bd.n_vendas == 0 && bd.n_itens == 0);
    bd_fechar(&bd);
}

static void teste_persistencia(void)
{
    const char *dir = "dados_teste_tmp";
    const char *arquivos[] = {"clientes.txt", "produtos.txt", "vendas.txt", "itens.txt"};
    char caminho[128];
    int i, c, p;
    VERIFICA(bd_abrir(&bd, dir) == OK);
    cliente_cadastrar(&bd, "José da Silva", "99999999999", "jose@x.com", "", "Rua A, 10", &c);
    produto_cadastrar(&bd, "Régua", "30 cm", 590, 10, &p);
    venda_registrar(&bd, c, &(ItemPedido){p, 3}, 1, "2026-03-15 10:00:00", NULL);
    bd_fechar(&bd);

    VERIFICA(bd_abrir(&bd, dir) == OK);
    VERIFICA(bd.n_clientes == 1 && strcmp(bd.clientes[0].nome, "José da Silva") == 0);
    VERIFICA(strcmp(bd.clientes[0].endereco, "Rua A, 10") == 0);
    VERIFICA(bd.n_produtos == 1 && bd.produtos[0].estoque == 7 && bd.produtos[0].preco == 590);
    VERIFICA(bd.n_vendas == 1 && bd.vendas[0].total == 1770);
    VERIFICA(bd.n_itens == 1 && bd.itens[0].quantidade == 3);
    /* novos IDs continuam a sequência */
    VERIFICA(cliente_cadastrar(&bd, "Maria", "88888888888", "", "", "", &c) == OK && c == 2);
    bd_fechar(&bd);

    for (i = 0; i < 4; i++) {
        snprintf(caminho, sizeof caminho, "%s/%s", dir, arquivos[i]);
        remove(caminho);
    }
    remove(dir);
}

static void teste_relatorios(void)
{
    RelCliente *rc;
    RelProduto *rp;
    RelHistorico *rh;
    RelFaturamento f;
    Venda **v;
    int n;
    preparar();
    venda_registrar(&bd, ana, &(ItemPedido){caneta, 10}, 1, "2026-01-05 10:00:00", NULL); /* 25 */
    venda_registrar(&bd, ana, &(ItemPedido){caneta, 2}, 1, "2026-01-20 23:59:00", NULL);  /* 5 */
    venda_registrar(&bd, ana, &(ItemPedido){caneta, 4}, 1, "2026-02-01 09:00:00", NULL);  /* 10 */
    venda_registrar(&bd, bruno, (ItemPedido[]){{caderno, 3}, {caneta, 1}}, 2,
                    "2026-01-10 08:00:00", NULL); /* 62,50 */

    /* 1. melhores clientes */
    n = rel_melhores_clientes(&bd, 0, 10, &rc);
    VERIFICA(n == 2 && rc[0].cliente_id == ana && rc[0].qtd_compras == 3);
    free(rc);
    n = rel_melhores_clientes(&bd, 1, 10, &rc);
    VERIFICA(n == 2 && rc[0].cliente_id == bruno);
    free(rc);
    n = rel_melhores_clientes(&bd, 1, 1, &rc);
    VERIFICA(n == 1);
    free(rc);

    /* 2. clientes com maior valor */
    n = rel_clientes_maior_valor(&bd, 10, &rc);
    VERIFICA(n == 2 && rc[0].valor_total == 6250 && rc[1].valor_total == 4000);
    VERIFICA(rc[1].ticket_medio == 1333);
    free(rc);

    /* 3. produtos mais vendidos */
    n = rel_produtos_mais_vendidos(&bd, 10, &rp);
    VERIFICA(n == 2 && rp[0].produto_id == caneta && rp[0].unidades == 17);
    free(rp);

    /* 4. vendas por período (inclui a venda de 20/01 às 23:59) */
    n = rel_vendas_periodo(&bd, "2026-01-01", "2026-01-20", &v);
    VERIFICA(n == 3 && strcmp(v[0]->data, "2026-01-05 10:00:00") == 0);
    free(v);
    VERIFICA(rel_vendas_periodo(&bd, "2026-02-01", "2026-01-01", &v) == -1);

    /* 5. faturamento por período */
    VERIFICA(rel_faturamento_periodo(&bd, "2026-01-01", "2026-01-31", &f) == OK);
    VERIFICA(f.qtd_vendas == 3 && f.faturamento == 9250 && f.n_dias == 3);
    free(f.dias);

    /* 6. histórico do cliente */
    n = rel_historico_cliente(&bd, bruno, &rh);
    VERIFICA(n == 2 && rh[0].total_venda == 6250);
    free(rh);
    n = rel_historico_cliente(&bd, ana, &rh);
    VERIFICA(n == 3 && strcmp(rh[0].data, "2026-02-01 09:00:00") == 0); /* mais recente 1º */
    free(rh);

    /* 7. desempenho por produto (inclui produto nunca vendido) */
    produto_cadastrar(&bd, "Régua", "", 500, 5, NULL);
    n = rel_desempenho_produtos(&bd, 0, &rp);
    VERIFICA(n == 3 && rp[0].produto_id == caneta && rp[0].qtd_vendas == 4);
    VERIFICA(rp[0].unidades == 17 && rp[0].faturamento == 4250);
    VERIFICA(rp[2].qtd_vendas == 0 && rp[2].faturamento == 0);
    free(rp);
    n = rel_desempenho_produtos(&bd, caderno, &rp);
    VERIFICA(n == 1 && rp[0].unidades == 3);
    free(rp);

    /* 8. produtos com maior faturamento */
    n = rel_produtos_maior_faturamento(&bd, 10, &rp);
    VERIFICA(n == 2 && rp[0].produto_id == caderno && rp[0].faturamento == 6000);
    VERIFICA(rp[0].participacao + rp[1].participacao > 99.99);
    free(rp);
    bd_fechar(&bd);
}

int main(void)
{
    teste_cadastros();
    teste_vendas();
    teste_persistencia();
    teste_relatorios();
    printf("%d verificações, %d falha(s).\n", verificacoes, falhas);
    return falhas ? 1 : 0;
}
