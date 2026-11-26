/*
 * dados.h - Estruturas do sistema, persistência em arquivos e operações de
 *           cadastro, consulta, alteração e exclusão de clientes, produtos e vendas.
 *
 * Valores monetários são armazenados em CENTAVOS (long long) para evitar erros
 * de arredondamento de ponto flutuante.
 * Datas são armazenadas no formato "AAAA-MM-DD HH:MM:SS".
 */
#ifndef DADOS_H
#define DADOS_H

#define TAM_NOME      100
#define TAM_CPF       12   /* 11 dígitos + '\0' */
#define TAM_EMAIL     100
#define TAM_TELEFONE  30
#define TAM_ENDERECO  150
#define TAM_DESCRICAO 200
#define TAM_DATA      20   /* "AAAA-MM-DD HH:MM:SS" */
#define TAM_DIR       256

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
    char      data[TAM_DATA];
    long long total;      /* centavos */
} Venda;

typedef struct {
    int       venda_id;
    int       produto_id;
    int       quantidade;
    long long preco_unitario;  /* preço no momento da venda, em centavos */
} ItemVenda;

/* Item informado ao registrar uma venda. */
typedef struct {
    int produto_id;
    int quantidade;
} ItemPedido;

/* Base de dados carregada em memória e gravada em arquivos a cada alteração. */
typedef struct {
    Cliente   *clientes; int n_clientes, cap_clientes, prox_cliente;
    Produto   *produtos; int n_produtos, cap_produtos, prox_produto;
    Venda     *vendas;   int n_vendas,   cap_vendas,   prox_venda;
    ItemVenda *itens;    int n_itens,    cap_itens;
    char dir[TAM_DIR];   /* pasta dos arquivos; vazio = somente memória */
} BaseDados;

typedef enum {
    OK = 0,
    ERRO_NAO_ENCONTRADO,
    ERRO_VALIDACAO,
    ERRO_DUPLICADO,
    ERRO_ESTOQUE,
    ERRO_VINCULO,
    ERRO_ARQUIVO,
    ERRO_MEMORIA
} Status;

/* Ordenações disponíveis nas listagens */
typedef enum { ORD_CLI_NOME, ORD_CLI_ID, ORD_CLI_CADASTRO } OrdemCliente;
typedef enum { ORD_PROD_NOME, ORD_PROD_ID, ORD_PROD_PRECO, ORD_PROD_ESTOQUE } OrdemProduto;

/* Mensagem descritiva do último erro. */
const char *bd_erro(void);

/* Abre a base. dir == NULL mantém tudo só em memória (usado nos testes). */
Status bd_abrir(BaseDados *bd, const char *dir);
Status bd_salvar(BaseDados *bd);
void   bd_fechar(BaseDados *bd);

/* Data/hora atual em "AAAA-MM-DD HH:MM:SS". */
void data_agora(char destino[TAM_DATA]);

/* ---------- Clientes ---------- */
Status   cliente_cadastrar(BaseDados *bd, const char *nome, const char *cpf, const char *email,
                           const char *telefone, const char *endereco, int *novo_id);
Cliente *cliente_buscar(BaseDados *bd, int id);
/* Campos NULL são mantidos. */
Status   cliente_alterar(BaseDados *bd, int id, const char *nome, const char *cpf,
                         const char *email, const char *telefone, const char *endereco);
Status   cliente_excluir(BaseDados *bd, int id);
/* Retorna a quantidade e um vetor de ponteiros (liberar com free). filtro pode ser NULL. */
int      cliente_listar(BaseDados *bd, const char *filtro, OrdemCliente ordem, Cliente ***saida);

/* ---------- Produtos ---------- */
Status   produto_cadastrar(BaseDados *bd, const char *nome, const char *descricao,
                           long long preco, int estoque, int *novo_id);
Produto *produto_buscar(BaseDados *bd, int id);
/* Ponteiros NULL mantêm o valor atual. */
Status   produto_alterar(BaseDados *bd, int id, const char *nome, const char *descricao,
                         const long long *preco, const int *estoque);
Status   produto_excluir(BaseDados *bd, int id);
int      produto_listar(BaseDados *bd, const char *filtro, OrdemProduto ordem, Produto ***saida);

/* ---------- Vendas ---------- */
/* data == NULL usa a data/hora atual. Produtos repetidos são somados.
 * A operação é "tudo ou nada": se algum item falhar, nada é alterado. */
Status   venda_registrar(BaseDados *bd, int cliente_id, const ItemPedido *itens, int n_itens,
                         const char *data, int *novo_id);
Venda   *venda_buscar(BaseDados *bd, int id);
int      venda_itens(BaseDados *bd, int venda_id, ItemVenda ***saida);
/* Lista as vendas da mais recente para a mais antiga. */
int      venda_listar(BaseDados *bd, Venda ***saida);
/* Exclui a venda e devolve os itens ao estoque. */
Status   venda_cancelar(BaseDados *bd, int id);

#endif
