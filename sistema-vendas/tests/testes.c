/*
 * testes.c - Testes automatizados das regras de negócio e dos relatórios.
 *
 * Usam um banco SEPARADO, pois APAGAM todos os dados antes de cada teste.
 * Conexão: variável VENDAS_TESTE_CONEXAO (padrão "host=localhost dbname=vendas_teste").
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
static const char *conexao;
static int ana, bruno, caneta, caderno;

static void preparar(void)
{
    bd_limpar(&bd);
    cliente_cadastrar(&bd, "Ana", "111.111.111-11", "", "", "", &ana);
    cliente_cadastrar(&bd, "Bruno", "22222222222", "", "", "", &bruno);
    produto_cadastrar(&bd, "Caneta", "", 250, 100, &caneta);   /* R$ 2,50 */
    produto_cadastrar(&bd, "Caderno", "", 2000, 50, &caderno); /* R$ 20,00 */
}

static int estoque(int produto_id)
{
    Produto p;
    return produto_buscar(&bd, produto_id, &p) == OK ? p.estoque : -1;
}

static int contar_vendas(void)
{
    Venda *v;
    int n = venda_listar(&bd, &v);
    free(v);
    return n;
}

static void teste_cadastros(void)
{
    Cliente c, *lc;
    Produto p, *lp;
    long long preco = 300;
    int n;
    preparar();
    VERIFICA(cliente_buscar(&bd, ana, &c) == OK && strcmp(c.cpf, "11111111111") == 0);
    VERIFICA(cliente_buscar(&bd, 999, &c) == ERRO_NAO_ENCONTRADO);
    VERIFICA(cliente_cadastrar(&bd, "Outra", "11111111111", "", "", "", NULL) == ERRO_DUPLICADO);
    VERIFICA(cliente_cadastrar(&bd, "", "33333333333", "", "", "", NULL) == ERRO_VALIDACAO);
    VERIFICA(cliente_cadastrar(&bd, "X", "123", "", "", "", NULL) == ERRO_VALIDACAO);
    VERIFICA(produto_cadastrar(&bd, "X", "", -1, 0, NULL) == ERRO_VALIDACAO);
    /* aspas e acentos chegam intactos (parâmetros, sem SQL injection) */
    VERIFICA(cliente_cadastrar(&bd, "D'Ávila'); DROP TABLE clientes;--", "44444444444", "", "",
                               "", &n) == OK);
    VERIFICA(cliente_buscar(&bd, n, &c) == OK &&
             strcmp(c.nome, "D'Ávila'); DROP TABLE clientes;--") == 0);

    VERIFICA(cliente_alterar(&bd, ana, "Ana Maria", NULL, "a@x.com", NULL, NULL) == OK);
    VERIFICA(cliente_buscar(&bd, ana, &c) == OK && strcmp(c.nome, "Ana Maria") == 0 &&
             strcmp(c.email, "a@x.com") == 0);
    VERIFICA(cliente_alterar(&bd, ana, NULL, "22222222222", NULL, NULL, NULL) == ERRO_DUPLICADO);
    VERIFICA(produto_alterar(&bd, caneta, NULL, NULL, &preco, NULL) == OK);
    VERIFICA(produto_buscar(&bd, caneta, &p) == OK && p.preco == 300);

    n = cliente_listar(&bd, "bru", ORD_CLI_NOME, &lc);
    VERIFICA(n == 1 && strcmp(lc[0].nome, "Bruno") == 0);
    free(lc);
    n = produto_listar(&bd, NULL, ORD_PROD_PRECO, &lp);
    VERIFICA(n == 2 && lp[0].id == caneta);
    free(lp);

    VERIFICA(venda_registrar(&bd, ana, &(ItemPedido){caneta, 1}, 1, NULL, NULL) == OK);
    VERIFICA(cliente_excluir(&bd, ana) == ERRO_VINCULO);
    VERIFICA(produto_excluir(&bd, caneta) == ERRO_VINCULO);
    VERIFICA(cliente_excluir(&bd, bruno) == OK);
    VERIFICA(cliente_buscar(&bd, bruno, &c) == ERRO_NAO_ENCONTRADO);
}

static void teste_vendas(void)
{
    ItemPedido itens[3], falho[2];
    ItemVenda *iv;
    Venda v;
    long long preco = 1000;
    int vid, n;
    preparar();
    itens[0] = (ItemPedido){caneta, 4};
    itens[1] = (ItemPedido){caderno, 1};
    itens[2] = (ItemPedido){caneta, 1};
    VERIFICA(venda_registrar(&bd, ana, itens, 3, NULL, &vid) == OK);
    VERIFICA(venda_buscar(&bd, vid, &v) == OK && v.total == 5 * 250 + 2000 && v.unidades == 6);
    n = venda_itens(&bd, vid, &iv);
    VERIFICA(n == 2 && iv[0].quantidade == 5 && iv[0].subtotal == 1250); /* repetidos agrupados */
    free(iv);
    VERIFICA(estoque(caneta) == 95);
    /* alterar o preço depois não muda a venda */
    produto_alterar(&bd, caneta, NULL, NULL, &preco, NULL);
    VERIFICA(venda_buscar(&bd, vid, &v) == OK && v.total == 3250);

    /* estoque insuficiente: a transação é desfeita e nada é gravado */
    falho[0] = (ItemPedido){caneta, 1};
    falho[1] = (ItemPedido){caderno, 51};
    VERIFICA(venda_registrar(&bd, ana, falho, 2, NULL, NULL) == ERRO_ESTOQUE);
    VERIFICA(contar_vendas() == 1 && estoque(caneta) == 95);
    falho[1] = (ItemPedido){12345, 1};
    VERIFICA(venda_registrar(&bd, ana, falho, 2, NULL, NULL) == ERRO_NAO_ENCONTRADO);
    VERIFICA(contar_vendas() == 1 && estoque(caneta) == 95);
    VERIFICA(venda_registrar(&bd, 999, itens, 1, NULL, NULL) == ERRO_NAO_ENCONTRADO);
    VERIFICA(venda_registrar(&bd, ana, itens, 0, NULL, NULL) == ERRO_VALIDACAO);

    /* cancelar devolve ao estoque */
    VERIFICA(venda_cancelar(&bd, vid) == OK);
    VERIFICA(estoque(caneta) == 100 && estoque(caderno) == 50);
    VERIFICA(contar_vendas() == 0);
    VERIFICA(venda_cancelar(&bd, vid) == ERRO_NAO_ENCONTRADO);
}

static void teste_persistencia(void)
{
    Cliente c;
    Venda v;
    int id, vid;
    preparar();
    VERIFICA(venda_registrar(&bd, ana, &(ItemPedido){caderno, 3}, 1, "2026-03-15 10:00:00",
                             &vid) == OK);
    bd_desconectar(&bd);
    VERIFICA(bd_conectar(&bd, conexao) == OK); /* nova conexão: dados continuam lá */
    VERIFICA(cliente_buscar(&bd, ana, &c) == OK && strcmp(c.nome, "Ana") == 0);
    VERIFICA(venda_buscar(&bd, vid, &v) == OK && v.total == 6000 &&
             strcmp(v.data, "2026-03-15 10:00:00") == 0);
    VERIFICA(estoque(caderno) == 47);
    VERIFICA(cliente_cadastrar(&bd, "Maria", "88888888888", "", "", "", &id) == OK && id == 3);
}

static void teste_relatorios(void)
{
    RelCliente *rc;
    RelProduto *rp;
    RelHistorico *rh;
    RelFaturamento f;
    Venda *v;
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
    VERIFICA(n == 3 && strcmp(v[0].data, "2026-01-05 10:00:00") == 0 && v[1].unidades == 4);
    free(v);
    VERIFICA(rel_vendas_periodo(&bd, "2026-02-01", "2026-01-01", &v) == -1);

    /* 5. faturamento por período */
    VERIFICA(rel_faturamento_periodo(&bd, "2026-01-01", "2026-01-31", &f) == OK);
    VERIFICA(f.qtd_vendas == 3 && f.faturamento == 9250 && f.n_dias == 3);
    VERIFICA(strcmp(f.dias[0].dia, "2026-01-05") == 0 && f.ticket_medio == 3083);
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
}

int main(void)
{
    conexao = getenv("VENDAS_TESTE_CONEXAO");
    if (conexao == NULL || conexao[0] == '\0') conexao = "host=localhost dbname=vendas_teste";
    if (bd_conectar(&bd, conexao) != OK) {
        printf("%s\n", bd_erro());
        return 2;
    }
    teste_cadastros();
    teste_vendas();
    teste_persistencia();
    teste_relatorios();
    bd_limpar(&bd);
    bd_desconectar(&bd);
    printf("%d verificações, %d falha(s).\n", verificacoes, falhas);
    return falhas ? 1 : 0;
}
