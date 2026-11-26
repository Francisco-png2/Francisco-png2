/*
 * dados.c - Conexão com o PostgreSQL (libpq) e regras de negócio dos cadastros.
 *
 * Todos os comandos usam PQexecParams com parâmetros ($1, $2, ...), nunca
 * concatenando texto digitado pelo usuário no SQL (evita SQL injection).
 */
#define __USE_MINGW_ANSI_STDIO 1

#include "dados.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mesmo conteúdo de sql/schema.sql: executado ao conectar. */
static const char *ESQUEMA =
    "CREATE TABLE IF NOT EXISTS clientes ("
    "  id         SERIAL PRIMARY KEY,"
    "  nome       VARCHAR(100) NOT NULL,"
    "  cpf        CHAR(11)     NOT NULL UNIQUE,"
    "  email      VARCHAR(100),"
    "  telefone   VARCHAR(30),"
    "  endereco   VARCHAR(150),"
    "  criado_em  TIMESTAMP(0) NOT NULL DEFAULT LOCALTIMESTAMP(0));"
    "CREATE TABLE IF NOT EXISTS produtos ("
    "  id         SERIAL PRIMARY KEY,"
    "  nome       VARCHAR(100)  NOT NULL,"
    "  descricao  VARCHAR(200),"
    "  preco      NUMERIC(12,2) NOT NULL CHECK (preco >= 0),"
    "  estoque    INTEGER       NOT NULL DEFAULT 0 CHECK (estoque >= 0));"
    "CREATE TABLE IF NOT EXISTS vendas ("
    "  id         SERIAL PRIMARY KEY,"
    "  cliente_id INTEGER       NOT NULL REFERENCES clientes(id) ON DELETE RESTRICT,"
    "  data       TIMESTAMP(0)  NOT NULL DEFAULT LOCALTIMESTAMP(0),"
    "  total      NUMERIC(12,2) NOT NULL DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS itens_venda ("
    "  id             SERIAL PRIMARY KEY,"
    "  venda_id       INTEGER       NOT NULL REFERENCES vendas(id) ON DELETE CASCADE,"
    "  produto_id     INTEGER       NOT NULL REFERENCES produtos(id) ON DELETE RESTRICT,"
    "  quantidade     INTEGER       NOT NULL CHECK (quantidade > 0),"
    "  preco_unitario NUMERIC(12,2) NOT NULL CHECK (preco_unitario >= 0),"
    "  UNIQUE (venda_id, produto_id));"
    "CREATE INDEX IF NOT EXISTS idx_vendas_cliente ON vendas(cliente_id);"
    "CREATE INDEX IF NOT EXISTS idx_vendas_data    ON vendas(data);"
    "CREATE INDEX IF NOT EXISTS idx_itens_produto  ON itens_venda(produto_id);";

/* ------------------------------------------------------------ utilitários */

static char msg_erro[512];

const char *bd_erro(void) { return msg_erro; }

static Status falha(Status st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_erro, sizeof msg_erro, fmt, ap);
    va_end(ap);
    return st;
}

/* Copia o texto removendo espaços nas pontas, sem cortar caracteres UTF-8. */
static void copiar_texto(char *dest, size_t tam, const char *orig)
{
    size_t i = 0;
    if (orig == NULL) orig = "";
    while (*orig && isspace((unsigned char)*orig)) orig++;
    for (; orig[i] && i < tam - 1; i++) dest[i] = (orig[i] == '\n' || orig[i] == '\r') ? ' ' : orig[i];
    if (orig[i]) {
        while (i > 0 && ((unsigned char)dest[i - 1] & 0xC0) == 0x80) i--;
        if (i > 0 && (unsigned char)dest[i - 1] >= 0xC0) i--;
    }
    dest[i] = '\0';
    while (i > 0 && isspace((unsigned char)dest[i - 1])) dest[--i] = '\0';
}

static size_t somente_digitos(const char *orig, char *dest, size_t tam)
{
    size_t n = 0, total = 0;
    for (; orig && *orig; orig++) {
        if (isdigit((unsigned char)*orig)) {
            if (n < tam - 1) dest[n++] = *orig;
            total++;
        }
    }
    dest[n] = '\0';
    return total;
}

/* Campos opcionais vazios são gravados como NULL. */
static const char *ou_nulo(const char *s) { return (s && *s) ? s : NULL; }

/* ------------------------------------------------------------ libpq */

PGresult *bd_executar(BaseDados *bd, Status *st, const char *sql, int n_params,
                      const char *const *params)
{
    PGresult *r = PQexecParams(bd->conn, sql, n_params, NULL, params, NULL, NULL, 0);
    ExecStatusType es = PQresultStatus(r);
    const char *estado;
    Status erro;
    if (es == PGRES_COMMAND_OK || es == PGRES_TUPLES_OK) {
        if (st) *st = OK;
        return r;
    }
    /* Converte o código SQLSTATE do PostgreSQL em um Status do sistema:
     * 23505 = valor único repetido, 23503 = chave estrangeira,
     * 23xxx/22xxx = restrição violada ou dado inválido. */
    estado = PQresultErrorField(r, PG_DIAG_SQLSTATE);
    if (estado == NULL) estado = "";
    if (strcmp(estado, "23505") == 0)
        erro = falha(ERRO_DUPLICADO, "Registro duplicado.");
    else if (strcmp(estado, "23503") == 0)
        erro = falha(ERRO_VINCULO, "O registro está vinculado a outros dados.");
    else if (strncmp(estado, "23", 2) == 0 || strncmp(estado, "22", 2) == 0)
        erro = falha(ERRO_VALIDACAO, "Dados inválidos: %s", PQresultErrorMessage(r));
    else
        erro = falha(ERRO_BANCO, "Erro no banco de dados: %s", PQerrorMessage(bd->conn));
    if (st) *st = erro;
    PQclear(r);
    return NULL;
}

/* Executa um comando sem resultado de interesse. */
static Status comando(BaseDados *bd, const char *sql, int n, const char *const *params)
{
    Status st;
    PGresult *r = bd_executar(bd, &st, sql, n, params);
    PQclear(r);
    return st;
}

int pg_int(const PGresult *r, int l, int c) { return atoi(PQgetvalue(r, l, c)); }

long long pg_ll(const PGresult *r, int l, int c) { return atoll(PQgetvalue(r, l, c)); }

void pg_txt(const PGresult *r, int l, int c, char *dest, size_t tam)
{
    snprintf(dest, tam, "%s", PQgetisnull(r, l, c) ? "" : PQgetvalue(r, l, c));
}

Status bd_conectar(BaseDados *bd, const char *conexao)
{
    bd->conn = PQconnectdb(conexao ? conexao : "");
    if (PQstatus(bd->conn) != CONNECTION_OK) {
        Status st = falha(ERRO_BANCO, "Não foi possível conectar ao PostgreSQL: %s",
                          PQerrorMessage(bd->conn));
        PQfinish(bd->conn);
        bd->conn = NULL;
        return st;
    }
    PQsetClientEncoding(bd->conn, "UTF8");
    /* NOTICE "relation already exists" do CREATE IF NOT EXISTS não interessa */
    PQclear(PQexec(bd->conn, "SET client_min_messages TO WARNING"));
    {
        PGresult *r = PQexec(bd->conn, ESQUEMA);
        Status st = OK;
        if (PQresultStatus(r) != PGRES_COMMAND_OK)
            st = falha(ERRO_BANCO, "Erro ao criar as tabelas: %s", PQerrorMessage(bd->conn));
        PQclear(r);
        return st;
    }
}

void bd_desconectar(BaseDados *bd)
{
    if (bd->conn) PQfinish(bd->conn);
    bd->conn = NULL;
}

Status bd_limpar(BaseDados *bd)
{
    return comando(bd, "TRUNCATE itens_venda, vendas, produtos, clientes RESTART IDENTITY", 0,
                   NULL);
}

static Status iniciar(BaseDados *bd) { return comando(bd, "BEGIN", 0, NULL); }
static Status confirmar_transacao(BaseDados *bd) { return comando(bd, "COMMIT", 0, NULL); }
static void desfazer(BaseDados *bd) { PQclear(PQexec(bd->conn, "ROLLBACK")); }

/* ------------------------------------------------------------ clientes */

#define SQL_CLIENTE_CAMPOS \
    "id, nome, cpf, email, telefone, endereco, to_char(criado_em, 'YYYY-MM-DD HH24:MI:SS')"

static void ler_cliente(const PGresult *r, int l, Cliente *c)
{
    c->id = pg_int(r, l, 0);
    pg_txt(r, l, 1, c->nome, sizeof c->nome);
    pg_txt(r, l, 2, c->cpf, sizeof c->cpf);
    pg_txt(r, l, 3, c->email, sizeof c->email);
    pg_txt(r, l, 4, c->telefone, sizeof c->telefone);
    pg_txt(r, l, 5, c->endereco, sizeof c->endereco);
    pg_txt(r, l, 6, c->criado_em, sizeof c->criado_em);
}

static Status validar_cliente(Cliente *c, const char *cpf_digitado)
{
    char digitos[64];
    if (c->nome[0] == '\0') return falha(ERRO_VALIDACAO, "O nome do cliente é obrigatório.");
    if (somente_digitos(cpf_digitado, digitos, sizeof digitos) != 11)
        return falha(ERRO_VALIDACAO, "O CPF deve conter 11 dígitos.");
    strcpy(c->cpf, digitos);
    return OK;
}

Status cliente_cadastrar(BaseDados *bd, const char *nome, const char *cpf, const char *email,
                         const char *telefone, const char *endereco, int *novo_id)
{
    Cliente c;
    Status st;
    PGresult *r;
    const char *p[5];
    memset(&c, 0, sizeof c);
    copiar_texto(c.nome, sizeof c.nome, nome);
    copiar_texto(c.email, sizeof c.email, email);
    copiar_texto(c.telefone, sizeof c.telefone, telefone);
    copiar_texto(c.endereco, sizeof c.endereco, endereco);
    if ((st = validar_cliente(&c, cpf)) != OK) return st;
    p[0] = c.nome; p[1] = c.cpf; p[2] = ou_nulo(c.email);
    p[3] = ou_nulo(c.telefone); p[4] = ou_nulo(c.endereco);
    r = bd_executar(bd, &st,
                    "INSERT INTO clientes (nome, cpf, email, telefone, endereco) "
                    "VALUES ($1, $2, $3, $4, $5) RETURNING id", 5, p);
    if (st == ERRO_DUPLICADO) return falha(st, "Já existe um cliente com o CPF %s.", c.cpf);
    if (r == NULL) return st;
    if (novo_id) *novo_id = pg_int(r, 0, 0);
    PQclear(r);
    return OK;
}

Status cliente_buscar(BaseDados *bd, int id, Cliente *saida)
{
    char sid[16];
    const char *p[1] = {sid};
    Status st;
    PGresult *r;
    snprintf(sid, sizeof sid, "%d", id);
    r = bd_executar(bd, &st, "SELECT " SQL_CLIENTE_CAMPOS " FROM clientes WHERE id = $1", 1, p);
    if (r == NULL) return st;
    if (PQntuples(r) == 0) st = falha(ERRO_NAO_ENCONTRADO, "Cliente %d não encontrado.", id);
    else if (saida) ler_cliente(r, 0, saida);
    PQclear(r);
    return st;
}

Status cliente_alterar(BaseDados *bd, int id, const char *nome, const char *cpf,
                       const char *email, const char *telefone, const char *endereco)
{
    Cliente c;
    Status st;
    char sid[16];
    const char *p[6];
    if ((st = cliente_buscar(bd, id, &c)) != OK) return st;
    if (nome) copiar_texto(c.nome, sizeof c.nome, nome);
    if (email) copiar_texto(c.email, sizeof c.email, email);
    if (telefone) copiar_texto(c.telefone, sizeof c.telefone, telefone);
    if (endereco) copiar_texto(c.endereco, sizeof c.endereco, endereco);
    {
        char cpf_atual[TAM_CPF];
        strcpy(cpf_atual, c.cpf);
        if ((st = validar_cliente(&c, cpf ? cpf : cpf_atual)) != OK) return st;
    }
    snprintf(sid, sizeof sid, "%d", id);
    p[0] = c.nome; p[1] = c.cpf; p[2] = ou_nulo(c.email);
    p[3] = ou_nulo(c.telefone); p[4] = ou_nulo(c.endereco); p[5] = sid;
    st = comando(bd,
                 "UPDATE clientes SET nome = $1, cpf = $2, email = $3, telefone = $4, "
                 "endereco = $5 WHERE id = $6", 6, p);
    if (st == ERRO_DUPLICADO) return falha(st, "Já existe um cliente com o CPF %s.", c.cpf);
    return st;
}

Status cliente_excluir(BaseDados *bd, int id)
{
    char sid[16];
    const char *p[1] = {sid};
    Status st;
    if ((st = cliente_buscar(bd, id, NULL)) != OK) return st;
    snprintf(sid, sizeof sid, "%d", id);
    st = comando(bd, "DELETE FROM clientes WHERE id = $1", 1, p);
    if (st == ERRO_VINCULO)
        return falha(st, "Não é possível excluir um cliente que possui vendas registradas.");
    return st;
}

int cliente_listar(BaseDados *bd, const char *filtro, OrdemCliente ordem, Cliente **saida)
{
    static const char *ordens[] = {"nome, id", "id", "criado_em, id"};
    char sql[512];
    const char *p[1] = {filtro ? filtro : ""};
    PGresult *r;
    int i, n;
    *saida = NULL;
    /* a ordenação vem de uma lista fixa, nunca do texto digitado */
    snprintf(sql, sizeof sql,
             "SELECT " SQL_CLIENTE_CAMPOS " FROM clientes "
             "WHERE nome ILIKE '%%' || $1 || '%%' OR cpf LIKE '%%' || $1 || '%%' "
             "   OR email ILIKE '%%' || $1 || '%%' ORDER BY %s",
             ordens[ordem]);
    r = bd_executar(bd, NULL, sql, 1, p);
    if (r == NULL) return -1;
    n = PQntuples(r);
    *saida = calloc((size_t)n + 1, sizeof **saida);
    if (*saida == NULL) { PQclear(r); falha(ERRO_MEMORIA, "Memória insuficiente."); return -1; }
    for (i = 0; i < n; i++) ler_cliente(r, i, &(*saida)[i]);
    PQclear(r);
    return n;
}

/* ------------------------------------------------------------ produtos */

#define SQL_PRODUTO_CAMPOS "id, nome, descricao, (preco * 100)::bigint, estoque"

static void ler_produto(const PGresult *r, int l, Produto *p)
{
    p->id = pg_int(r, l, 0);
    pg_txt(r, l, 1, p->nome, sizeof p->nome);
    pg_txt(r, l, 2, p->descricao, sizeof p->descricao);
    p->preco = pg_ll(r, l, 3);
    p->estoque = pg_int(r, l, 4);
}

static Status validar_produto(const Produto *p)
{
    if (p->nome[0] == '\0') return falha(ERRO_VALIDACAO, "O nome do produto é obrigatório.");
    if (p->preco < 0) return falha(ERRO_VALIDACAO, "O preço não pode ser negativo.");
    if (p->preco > 999999999999LL) return falha(ERRO_VALIDACAO, "Preço muito alto.");
    if (p->estoque < 0) return falha(ERRO_VALIDACAO, "O estoque não pode ser negativo.");
    return OK;
}

Status produto_cadastrar(BaseDados *bd, const char *nome, const char *descricao,
                         long long preco, int estoque, int *novo_id)
{
    Produto p;
    Status st;
    PGresult *r;
    char spreco[32], sestoque[16];
    const char *params[4];
    memset(&p, 0, sizeof p);
    copiar_texto(p.nome, sizeof p.nome, nome);
    copiar_texto(p.descricao, sizeof p.descricao, descricao);
    p.preco = preco;
    p.estoque = estoque;
    if ((st = validar_produto(&p)) != OK) return st;
    snprintf(spreco, sizeof spreco, "%lld", preco);
    snprintf(sestoque, sizeof sestoque, "%d", estoque);
    params[0] = p.nome; params[1] = ou_nulo(p.descricao); params[2] = spreco; params[3] = sestoque;
    r = bd_executar(bd, &st,
                    "INSERT INTO produtos (nome, descricao, preco, estoque) "
                    "VALUES ($1, $2, $3::bigint / 100.0, $4) RETURNING id", 4, params);
    if (r == NULL) return st;
    if (novo_id) *novo_id = pg_int(r, 0, 0);
    PQclear(r);
    return OK;
}

Status produto_buscar(BaseDados *bd, int id, Produto *saida)
{
    char sid[16];
    const char *p[1] = {sid};
    Status st;
    PGresult *r;
    snprintf(sid, sizeof sid, "%d", id);
    r = bd_executar(bd, &st, "SELECT " SQL_PRODUTO_CAMPOS " FROM produtos WHERE id = $1", 1, p);
    if (r == NULL) return st;
    if (PQntuples(r) == 0) st = falha(ERRO_NAO_ENCONTRADO, "Produto %d não encontrado.", id);
    else if (saida) ler_produto(r, 0, saida);
    PQclear(r);
    return st;
}

Status produto_alterar(BaseDados *bd, int id, const char *nome, const char *descricao,
                       const long long *preco, const int *estoque)
{
    Produto p;
    Status st;
    char sid[16], spreco[32], sestoque[16];
    const char *params[5];
    if ((st = produto_buscar(bd, id, &p)) != OK) return st;
    if (nome) copiar_texto(p.nome, sizeof p.nome, nome);
    if (descricao) copiar_texto(p.descricao, sizeof p.descricao, descricao);
    if (preco) p.preco = *preco;
    if (estoque) p.estoque = *estoque;
    if ((st = validar_produto(&p)) != OK) return st;
    snprintf(sid, sizeof sid, "%d", id);
    snprintf(spreco, sizeof spreco, "%lld", p.preco);
    snprintf(sestoque, sizeof sestoque, "%d", p.estoque);
    params[0] = p.nome; params[1] = ou_nulo(p.descricao); params[2] = spreco;
    params[3] = sestoque; params[4] = sid;
    return comando(bd,
                   "UPDATE produtos SET nome = $1, descricao = $2, preco = $3::bigint / 100.0, "
                   "estoque = $4 WHERE id = $5", 5, params);
}

Status produto_excluir(BaseDados *bd, int id)
{
    char sid[16];
    const char *p[1] = {sid};
    Status st;
    if ((st = produto_buscar(bd, id, NULL)) != OK) return st;
    snprintf(sid, sizeof sid, "%d", id);
    st = comando(bd, "DELETE FROM produtos WHERE id = $1", 1, p);
    if (st == ERRO_VINCULO)
        return falha(st, "Não é possível excluir um produto que já foi vendido.");
    return st;
}

int produto_listar(BaseDados *bd, const char *filtro, OrdemProduto ordem, Produto **saida)
{
    static const char *ordens[] = {"nome, id", "id", "preco, id", "estoque, id"};
    char sql[512];
    const char *p[1] = {filtro ? filtro : ""};
    PGresult *r;
    int i, n;
    *saida = NULL;
    snprintf(sql, sizeof sql,
             "SELECT " SQL_PRODUTO_CAMPOS " FROM produtos "
             "WHERE nome ILIKE '%%' || $1 || '%%' OR COALESCE(descricao, '') ILIKE '%%' || $1 || '%%' "
             "ORDER BY %s",
             ordens[ordem]);
    r = bd_executar(bd, NULL, sql, 1, p);
    if (r == NULL) return -1;
    n = PQntuples(r);
    *saida = calloc((size_t)n + 1, sizeof **saida);
    if (*saida == NULL) { PQclear(r); falha(ERRO_MEMORIA, "Memória insuficiente."); return -1; }
    for (i = 0; i < n; i++) ler_produto(r, i, &(*saida)[i]);
    PQclear(r);
    return n;
}

/* ------------------------------------------------------------ vendas */

int bd_ler_vendas(PGresult *r, Venda **saida)
{
    int i, n;
    *saida = NULL;
    if (r == NULL) return -1;
    n = PQntuples(r);
    *saida = calloc((size_t)n + 1, sizeof **saida);
    if (*saida == NULL) { PQclear(r); falha(ERRO_MEMORIA, "Memória insuficiente."); return -1; }
    for (i = 0; i < n; i++) {
        Venda *v = &(*saida)[i];
        v->id = pg_int(r, i, 0);
        v->cliente_id = pg_int(r, i, 1);
        pg_txt(r, i, 2, v->cliente_nome, sizeof v->cliente_nome);
        pg_txt(r, i, 3, v->data, sizeof v->data);
        v->total = pg_ll(r, i, 4);
        v->unidades = pg_int(r, i, 5);
    }
    PQclear(r);
    return n;
}

Status venda_registrar(BaseDados *bd, int cliente_id, const ItemPedido *itens, int n_itens,
                       const char *data, int *novo_id)
{
    ItemPedido *ag;
    long long *precos, total = 0;
    int i, j, n = 0, venda_id = 0;
    char sid[16], sqtd[16], spid[16], stotal[32], sdata[TAM_DATA];
    const char *p[4];
    Status st;
    PGresult *r;

    if ((st = cliente_buscar(bd, cliente_id, NULL)) != OK) return st;
    if (n_itens <= 0) return falha(ERRO_VALIDACAO, "A venda deve conter pelo menos um item.");

    /* 1) agrupa produtos repetidos */
    ag = malloc((size_t)n_itens * sizeof *ag);
    precos = malloc((size_t)n_itens * sizeof *precos);
    if (ag == NULL || precos == NULL) {
        free(ag); free(precos);
        return falha(ERRO_MEMORIA, "Memória insuficiente.");
    }
    for (i = 0; i < n_itens; i++) {
        if (itens[i].quantidade <= 0) {
            free(ag); free(precos);
            return falha(ERRO_VALIDACAO, "A quantidade de cada item deve ser maior que zero.");
        }
        for (j = 0; j < n && ag[j].produto_id != itens[i].produto_id; j++) {}
        if (j == n) ag[n++] = itens[i];
        else ag[j].quantidade += itens[i].quantidade;
    }

    /* 2) transação: tudo ou nada */
    if ((st = iniciar(bd)) != OK) { free(ag); free(precos); return st; }

    /* trava as linhas dos produtos (FOR UPDATE) para que duas vendas
     * simultâneas não vendam o mesmo estoque */
    for (j = 0; j < n && st == OK; j++) {
        snprintf(spid, sizeof spid, "%d", ag[j].produto_id);
        p[0] = spid;
        r = bd_executar(bd, &st,
                        "SELECT nome, estoque, (preco * 100)::bigint FROM produtos "
                        "WHERE id = $1 FOR UPDATE", 1, p);
        if (r == NULL) break;
        if (PQntuples(r) == 0)
            st = falha(ERRO_NAO_ENCONTRADO, "Produto %d não encontrado.", ag[j].produto_id);
        else if (pg_int(r, 0, 1) < ag[j].quantidade)
            st = falha(ERRO_ESTOQUE,
                       "Estoque insuficiente para '%s' (disponível: %d, solicitado: %d).",
                       PQgetvalue(r, 0, 0), pg_int(r, 0, 1), ag[j].quantidade);
        else {
            precos[j] = pg_ll(r, 0, 2);
            total += precos[j] * ag[j].quantidade;
        }
        PQclear(r);
    }

    /* 3) grava a venda */
    if (st == OK) {
        snprintf(sid, sizeof sid, "%d", cliente_id);
        snprintf(stotal, sizeof stotal, "%lld", total);
        if (data) copiar_texto(sdata, sizeof sdata, data);
        p[0] = sid; p[1] = data ? sdata : NULL; p[2] = stotal;
        r = bd_executar(bd, &st,
                        "INSERT INTO vendas (cliente_id, data, total) "
                        "VALUES ($1, COALESCE($2::timestamp, LOCALTIMESTAMP(0)), $3::bigint / 100.0) "
                        "RETURNING id", 3, p);
        if (r) { venda_id = pg_int(r, 0, 0); PQclear(r); }
    }

    /* 4) grava os itens com o preço atual e baixa o estoque */
    snprintf(sid, sizeof sid, "%d", venda_id);
    for (j = 0; j < n && st == OK; j++) {
        char spreco[32];
        snprintf(spid, sizeof spid, "%d", ag[j].produto_id);
        snprintf(sqtd, sizeof sqtd, "%d", ag[j].quantidade);
        snprintf(spreco, sizeof spreco, "%lld", precos[j]);
        p[0] = sid; p[1] = spid; p[2] = sqtd; p[3] = spreco;
        st = comando(bd,
                     "INSERT INTO itens_venda (venda_id, produto_id, quantidade, preco_unitario) "
                     "VALUES ($1, $2, $3, $4::bigint / 100.0)", 4, p);
        if (st == OK) {
            p[0] = sqtd; p[1] = spid;
            st = comando(bd, "UPDATE produtos SET estoque = estoque - $1 WHERE id = $2", 2, p);
        }
    }

    free(ag);
    free(precos);
    if (st == OK) st = confirmar_transacao(bd);
    if (st != OK) { desfazer(bd); return st; }
    if (novo_id) *novo_id = venda_id;
    return OK;
}

Status venda_buscar(BaseDados *bd, int id, Venda *saida)
{
    char sid[16];
    const char *p[1] = {sid};
    Venda *v;
    int n;
    snprintf(sid, sizeof sid, "%d", id);
    n = bd_ler_vendas(bd_executar(bd, NULL,
                                  "SELECT " SQL_VENDA_CAMPOS
                                  "FROM vendas v JOIN clientes c ON c.id = v.cliente_id "
                                  "WHERE v.id = $1", 1, p),
                      &v);
    if (n < 0) return ERRO_BANCO;
    if (n == 0) { free(v); return falha(ERRO_NAO_ENCONTRADO, "Venda %d não encontrada.", id); }
    if (saida) *saida = v[0];
    free(v);
    return OK;
}

int venda_itens(BaseDados *bd, int venda_id, ItemVenda **saida)
{
    char sid[16];
    const char *p[1] = {sid};
    PGresult *r;
    int i, n;
    *saida = NULL;
    snprintf(sid, sizeof sid, "%d", venda_id);
    r = bd_executar(bd, NULL,
                    "SELECT i.produto_id, p.nome, i.quantidade, (i.preco_unitario * 100)::bigint, "
                    "       (i.quantidade * i.preco_unitario * 100)::bigint "
                    "FROM itens_venda i JOIN produtos p ON p.id = i.produto_id "
                    "WHERE i.venda_id = $1 ORDER BY i.id", 1, p);
    if (r == NULL) return -1;
    n = PQntuples(r);
    *saida = calloc((size_t)n + 1, sizeof **saida);
    if (*saida == NULL) { PQclear(r); falha(ERRO_MEMORIA, "Memória insuficiente."); return -1; }
    for (i = 0; i < n; i++) {
        ItemVenda *it = &(*saida)[i];
        it->produto_id = pg_int(r, i, 0);
        pg_txt(r, i, 1, it->produto_nome, sizeof it->produto_nome);
        it->quantidade = pg_int(r, i, 2);
        it->preco_unitario = pg_ll(r, i, 3);
        it->subtotal = pg_ll(r, i, 4);
    }
    PQclear(r);
    return n;
}

int venda_listar(BaseDados *bd, Venda **saida)
{
    return bd_ler_vendas(bd_executar(bd, NULL,
                                     "SELECT " SQL_VENDA_CAMPOS
                                     "FROM vendas v JOIN clientes c ON c.id = v.cliente_id "
                                     "ORDER BY v.data DESC, v.id DESC", 0, NULL),
                         saida);
}

Status venda_cancelar(BaseDados *bd, int id)
{
    char sid[16];
    const char *p[1] = {sid};
    Status st;
    if ((st = venda_buscar(bd, id, NULL)) != OK) return st;
    snprintf(sid, sizeof sid, "%d", id);
    if ((st = iniciar(bd)) != OK) return st;
    st = comando(bd,
                 "UPDATE produtos p SET estoque = p.estoque + i.quantidade "
                 "FROM itens_venda i WHERE i.venda_id = $1 AND p.id = i.produto_id", 1, p);
    if (st == OK) st = comando(bd, "DELETE FROM vendas WHERE id = $1", 1, p); /* itens: CASCADE */
    if (st == OK) st = confirmar_transacao(bd);
    if (st != OK) desfazer(bd);
    return st;
}
