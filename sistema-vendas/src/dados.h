/*
 * dados.h - Acesso ao banco PostgreSQL (libpq) e operações de cadastro,
 *           consulta, alteração e exclusão de clientes, produtos e vendas.
 *
 * No banco, valores monetários são NUMERIC(12,2). No programa eles são
 * tratados em CENTAVOS (long long) para evitar erros de ponto flutuante.
 * Datas chegam ao programa no formato "AAAA-MM-DD HH:MM:SS".
 */
#ifndef DADOS_H
#define DADOS_H

#include <libpq-fe.h>

#define TAM_NOME      101  /* VARCHAR(100) + '\0' */
#define TAM_CPF       12   /* 11 dígitos + '\0' */
#define TAM_EMAIL     101
#define TAM_TELEFONE  31
#define TAM_ENDERECO  151
#define TAM_DESCRICAO 201
#define TAM_DATA      20   /* "AAAA-MM-DD HH:MM:SS" */

typedef struct {
    int  id;
    char nome[TAM_NOME];
    char cpf[TAM_CPF];
    char email[TAM_EMAIL];
    char telefone[TAM_TELEFONE];
    char endereco[TAM_ENDERECO];
    char criado_em[TAM_DATA];
} Cliente;

typedef struct {
    int       id;
    char      nome[TAM_NOME];
    char      descricao[TAM_DESCRICAO];
    long long preco;      /* centavos */
    int       estoque;
} Produto;

typedef struct {
    int       id;
    int       cliente_id;
    char      cliente_nome[TAM_NOME];
    char      data[TAM_DATA];
    long long total;      /* centavos */
    int       unidades;   /* soma das quantidades dos itens */
} Venda;

typedef struct {
    int       produto_id;
    char      produto_nome[TAM_NOME];
    int       quantidade;
    long long preco_unitario;  /* preço no momento da venda, em centavos */
    long long subtotal;
} ItemVenda;

/* Item informado ao registrar uma venda. */
typedef struct {
    int produto_id;
    int quantidade;
} ItemPedido;

typedef struct {
    PGconn *conn;
} BaseDados;

typedef enum {
    OK = 0,
    ERRO_NAO_ENCONTRADO,
    ERRO_VALIDACAO,
    ERRO_DUPLICADO,
    ERRO_ESTOQUE,
    ERRO_VINCULO,
    ERRO_BANCO,
    ERRO_MEMORIA
} Status;

typedef enum { ORD_CLI_NOME, ORD_CLI_ID, ORD_CLI_CADASTRO } OrdemCliente;
typedef enum { ORD_PROD_NOME, ORD_PROD_ID, ORD_PROD_PRECO, ORD_PROD_ESTOQUE } OrdemProduto;

/* Mensagem descritiva do último erro. */
const char *bd_erro(void);

/* Conecta ao PostgreSQL e cria as tabelas, se ainda não existirem.
 * conexao: string de conexão da libpq, ex. "host=localhost dbname=vendas user=postgres". */
Status bd_conectar(BaseDados *bd, const char *conexao);
void   bd_desconectar(BaseDados *bd);
/* Apaga todos os dados e reinicia os IDs (usado nos testes). */
Status bd_limpar(BaseDados *bd);

/* ---------- Funções auxiliares de consulta (usadas também em relatorios.c) ---------- */
/* Executa um comando com parâmetros ($1, $2, ...) passados como texto.
 * Retorna o resultado (liberar com PQclear) ou NULL em caso de erro. */
PGresult *bd_executar(BaseDados *bd, Status *st, const char *sql, int n_params,
                      const char *const *params);
int       pg_int(const PGresult *r, int linha, int coluna);
long long pg_ll(const PGresult *r, int linha, int coluna);
void      pg_txt(const PGresult *r, int linha, int coluna, char *dest, size_t tam);

/* Colunas e leitura padrão de vendas (FROM vendas v JOIN clientes c). */
#define SQL_VENDA_CAMPOS                                                             \
    "v.id, v.cliente_id, c.nome, to_char(v.data, 'YYYY-MM-DD HH24:MI:SS'), "          \
    "(v.total * 100)::bigint, "                                                       \
    "COALESCE((SELECT SUM(quantidade) FROM itens_venda WHERE venda_id = v.id), 0) "
int bd_ler_vendas(PGresult *r, Venda **saida);

/* ---------- Clientes ---------- */
Status cliente_cadastrar(BaseDados *bd, const char *nome, const char *cpf, const char *email,
                         const char *telefone, const char *endereco, int *novo_id);
Status cliente_buscar(BaseDados *bd, int id, Cliente *saida);
/* Campos NULL são mantidos. */
Status cliente_alterar(BaseDados *bd, int id, const char *nome, const char *cpf,
                       const char *email, const char *telefone, const char *endereco);
Status cliente_excluir(BaseDados *bd, int id);
/* Retorna a quantidade (ou -1 em erro) e um vetor alocado (liberar com free). */
int    cliente_listar(BaseDados *bd, const char *filtro, OrdemCliente ordem, Cliente **saida);

/* ---------- Produtos ---------- */
Status produto_cadastrar(BaseDados *bd, const char *nome, const char *descricao,
                         long long preco, int estoque, int *novo_id);
Status produto_buscar(BaseDados *bd, int id, Produto *saida);
/* Ponteiros NULL mantêm o valor atual. */
Status produto_alterar(BaseDados *bd, int id, const char *nome, const char *descricao,
                       const long long *preco, const int *estoque);
Status produto_excluir(BaseDados *bd, int id);
int    produto_listar(BaseDados *bd, const char *filtro, OrdemProduto ordem, Produto **saida);

/* ---------- Vendas ---------- */
/* data == NULL usa a data/hora atual. Produtos repetidos são somados.
 * Executada em uma transação: se algum item falhar, nada é gravado. */
Status venda_registrar(BaseDados *bd, int cliente_id, const ItemPedido *itens, int n_itens,
                       const char *data, int *novo_id);
Status venda_buscar(BaseDados *bd, int id, Venda *saida);
int    venda_itens(BaseDados *bd, int venda_id, ItemVenda **saida);
/* Lista as vendas da mais recente para a mais antiga. */
int    venda_listar(BaseDados *bd, Venda **saida);
/* Exclui a venda e devolve os itens ao estoque (em uma transação). */
Status venda_cancelar(BaseDados *bd, int id);

#endif
