/*
 * dados.c - Persistência em arquivos texto e regras de negócio dos cadastros.
 *
 * Cada entidade é gravada em um arquivo próprio dentro da pasta de dados,
 * um registro por linha, com campos separados por '|':
 *   clientes.txt : id|nome|cpf|email|telefone|endereco|criado_em
 *   produtos.txt : id|nome|descricao|preco_centavos|estoque
 *   vendas.txt   : id|cliente_id|data|total_centavos
 *   itens.txt    : venda_id|produto_id|quantidade|preco_unitario_centavos
 */
#define _POSIX_C_SOURCE 200809L
#define __USE_MINGW_ANSI_STDIO 1

#include "dados.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define CRIAR_PASTA(p) _mkdir(p)
#else
#include <sys/stat.h>
#define CRIAR_PASTA(p) mkdir((p), 0755)
#endif

#define TAM_LINHA 1024

/* ------------------------------------------------------------ utilitários */

static char msg_erro[256];

const char *bd_erro(void) { return msg_erro; }

static Status falha(Status st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_erro, sizeof msg_erro, fmt, ap);
    va_end(ap);
    return st;
}

/* Copia o texto removendo espaços nas pontas e trocando '|' e quebras de linha
 * (que quebrariam o formato do arquivo) por espaço. */
static void copiar_texto(char *dest, size_t tam, const char *orig)
{
    size_t i = 0;
    if (orig == NULL) orig = "";
    while (*orig && isspace((unsigned char)*orig)) orig++;
    for (; orig[i] && i < tam - 1; i++) {
        char c = orig[i];
        dest[i] = (c == '|' || c == '\n' || c == '\r') ? ' ' : c;
    }
    if (orig[i]) { /* truncado: não deixa um caractere UTF-8 pela metade */
        while (i > 0 && ((unsigned char)dest[i - 1] & 0xC0) == 0x80) i--;
        if (i > 0 && (unsigned char)dest[i - 1] >= 0xC0) i--;
    }
    dest[i] = '\0';
    while (i > 0 && isspace((unsigned char)dest[i - 1])) dest[--i] = '\0';
}

/* Mantém apenas os dígitos. Retorna a quantidade de dígitos encontrados. */
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

/* Garante espaço para 'necessario' elementos no vetor dinâmico. */
static int garantir(void **vetor, int *cap, int necessario, size_t tam_elem)
{
    int nova = *cap ? *cap : 16;
    void *novo;
    if (necessario <= *cap) return 1;
    while (nova < necessario) nova *= 2;
    novo = realloc(*vetor, (size_t)nova * tam_elem);
    if (novo == NULL) return 0;
    *vetor = novo;
    *cap = nova;
    return 1;
}

static int compara_sem_caixa(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d) return d;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int contem_sem_caixa(const char *texto, const char *termo)
{
    size_t n = strlen(termo);
    if (n == 0) return 1;
    for (; *texto; texto++) {
        size_t i = 0;
        while (i < n && texto[i] &&
               tolower((unsigned char)texto[i]) == tolower((unsigned char)termo[i]))
            i++;
        if (i == n) return 1;
    }
    return 0;
}

void data_agora(char destino[TAM_DATA])
{
    time_t t = time(NULL);
    strftime(destino, TAM_DATA, "%Y-%m-%d %H:%M:%S", localtime(&t));
}

/* ------------------------------------------------------------ arquivos */

static void caminho(const BaseDados *bd, const char *arquivo, char *dest, size_t tam)
{
    snprintf(dest, tam, "%s/%s", bd->dir, arquivo);
}

/* Divide a linha em campos separados por '|' (preserva campos vazios). */
static int dividir(char *linha, char **campos, int max)
{
    int n = 0;
    char *p = linha;
    campos[n++] = p;
    for (; *p; p++) {
        if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
        if (*p == '|') {
            *p = '\0';
            if (n == max) break;
            campos[n++] = p + 1;
        }
    }
    return n;
}

static void atualiza_prox(int *prox, int id) { if (id >= *prox) *prox = id + 1; }

static Status carregar(BaseDados *bd, const char *arquivo, int n_campos,
                       Status (*registro)(BaseDados *, char **))
{
    char nome[TAM_DIR + 32], linha[TAM_LINHA], *campos[8];
    FILE *f;
    Status st = OK;
    caminho(bd, arquivo, nome, sizeof nome);
    f = fopen(nome, "r");
    if (f == NULL) return OK; /* arquivo ainda não existe: base vazia */
    while (st == OK && fgets(linha, sizeof linha, f)) {
        if (dividir(linha, campos, 8) >= n_campos) st = registro(bd, campos);
    }
    fclose(f);
    return st;
}

static Status reg_cliente(BaseDados *bd, char **c)
{
    Cliente *x;
    if (!garantir((void **)&bd->clientes, &bd->cap_clientes, bd->n_clientes + 1, sizeof *x))
        return falha(ERRO_MEMORIA, "Memória insuficiente.");
    x = &bd->clientes[bd->n_clientes++];
    x->id = atoi(c[0]);
    copiar_texto(x->nome, sizeof x->nome, c[1]);
    copiar_texto(x->cpf, sizeof x->cpf, c[2]);
    copiar_texto(x->email, sizeof x->email, c[3]);
    copiar_texto(x->telefone, sizeof x->telefone, c[4]);
    copiar_texto(x->endereco, sizeof x->endereco, c[5]);
    copiar_texto(x->criado_em, sizeof x->criado_em, c[6]);
    atualiza_prox(&bd->prox_cliente, x->id);
    return OK;
}

static Status reg_produto(BaseDados *bd, char **c)
{
    Produto *x;
    if (!garantir((void **)&bd->produtos, &bd->cap_produtos, bd->n_produtos + 1, sizeof *x))
        return falha(ERRO_MEMORIA, "Memória insuficiente.");
    x = &bd->produtos[bd->n_produtos++];
    x->id = atoi(c[0]);
    copiar_texto(x->nome, sizeof x->nome, c[1]);
    copiar_texto(x->descricao, sizeof x->descricao, c[2]);
    x->preco = atoll(c[3]);
    x->estoque = atoi(c[4]);
    atualiza_prox(&bd->prox_produto, x->id);
    return OK;
}

static Status reg_venda(BaseDados *bd, char **c)
{
    Venda *x;
    if (!garantir((void **)&bd->vendas, &bd->cap_vendas, bd->n_vendas + 1, sizeof *x))
        return falha(ERRO_MEMORIA, "Memória insuficiente.");
    x = &bd->vendas[bd->n_vendas++];
    x->id = atoi(c[0]);
    x->cliente_id = atoi(c[1]);
    copiar_texto(x->data, sizeof x->data, c[2]);
    x->total = atoll(c[3]);
    atualiza_prox(&bd->prox_venda, x->id);
    return OK;
}

static Status reg_item(BaseDados *bd, char **c)
{
    ItemVenda *x;
    if (!garantir((void **)&bd->itens, &bd->cap_itens, bd->n_itens + 1, sizeof *x))
        return falha(ERRO_MEMORIA, "Memória insuficiente.");
    x = &bd->itens[bd->n_itens++];
    x->venda_id = atoi(c[0]);
    x->produto_id = atoi(c[1]);
    x->quantidade = atoi(c[2]);
    x->preco_unitario = atoll(c[3]);
    return OK;
}

static void esc_clientes(const BaseDados *bd, FILE *f)
{
    int i;
    for (i = 0; i < bd->n_clientes; i++) {
        const Cliente *x = &bd->clientes[i];
        fprintf(f, "%d|%s|%s|%s|%s|%s|%s\n", x->id, x->nome, x->cpf, x->email,
                x->telefone, x->endereco, x->criado_em);
    }
}

static void esc_produtos(const BaseDados *bd, FILE *f)
{
    int i;
    for (i = 0; i < bd->n_produtos; i++) {
        const Produto *x = &bd->produtos[i];
        fprintf(f, "%d|%s|%s|%lld|%d\n", x->id, x->nome, x->descricao, x->preco, x->estoque);
    }
}

static void esc_vendas(const BaseDados *bd, FILE *f)
{
    int i;
    for (i = 0; i < bd->n_vendas; i++) {
        const Venda *x = &bd->vendas[i];
        fprintf(f, "%d|%d|%s|%lld\n", x->id, x->cliente_id, x->data, x->total);
    }
}

static void esc_itens(const BaseDados *bd, FILE *f)
{
    int i;
    for (i = 0; i < bd->n_itens; i++) {
        const ItemVenda *x = &bd->itens[i];
        fprintf(f, "%d|%d|%d|%lld\n", x->venda_id, x->produto_id, x->quantidade,
                x->preco_unitario);
    }
}

/* Grava em um arquivo temporário e só depois substitui o original,
 * para não corromper os dados se a gravação for interrompida. */
static Status gravar(const BaseDados *bd, const char *arquivo,
                     void (*escrever)(const BaseDados *, FILE *))
{
    char final[TAM_DIR + 32], temp[TAM_DIR + 40];
    FILE *f;
    caminho(bd, arquivo, final, sizeof final);
    snprintf(temp, sizeof temp, "%s.tmp", final);
    f = fopen(temp, "w");
    if (f == NULL) return falha(ERRO_ARQUIVO, "Não foi possível gravar %s.", final);
    escrever(bd, f);
    if (fclose(f) != 0) return falha(ERRO_ARQUIVO, "Erro ao gravar %s.", final);
    remove(final);
    if (rename(temp, final) != 0) return falha(ERRO_ARQUIVO, "Erro ao substituir %s.", final);
    return OK;
}

Status bd_abrir(BaseDados *bd, const char *dir)
{
    Status st;
    memset(bd, 0, sizeof *bd);
    bd->prox_cliente = bd->prox_produto = bd->prox_venda = 1;
    if (dir == NULL || dir[0] == '\0') return OK;
    copiar_texto(bd->dir, sizeof bd->dir, dir);
    CRIAR_PASTA(bd->dir); /* ignora erro se já existir */
    if ((st = carregar(bd, "clientes.txt", 7, reg_cliente)) != OK) return st;
    if ((st = carregar(bd, "produtos.txt", 5, reg_produto)) != OK) return st;
    if ((st = carregar(bd, "vendas.txt", 4, reg_venda)) != OK) return st;
    return carregar(bd, "itens.txt", 4, reg_item);
}

Status bd_salvar(BaseDados *bd)
{
    Status st;
    if (bd->dir[0] == '\0') return OK;
    if ((st = gravar(bd, "clientes.txt", esc_clientes)) != OK) return st;
    if ((st = gravar(bd, "produtos.txt", esc_produtos)) != OK) return st;
    if ((st = gravar(bd, "vendas.txt", esc_vendas)) != OK) return st;
    return gravar(bd, "itens.txt", esc_itens);
}

void bd_fechar(BaseDados *bd)
{
    free(bd->clientes);
    free(bd->produtos);
    free(bd->vendas);
    free(bd->itens);
    memset(bd, 0, sizeof *bd);
}

/* ------------------------------------------------------------ clientes */

Cliente *cliente_buscar(BaseDados *bd, int id)
{
    int i;
    for (i = 0; i < bd->n_clientes; i++)
        if (bd->clientes[i].id == id) return &bd->clientes[i];
    return NULL;
}

static int cpf_em_uso(const BaseDados *bd, const char *cpf, int ignorar_id)
{
    int i;
    for (i = 0; i < bd->n_clientes; i++)
        if (bd->clientes[i].id != ignorar_id && strcmp(bd->clientes[i].cpf, cpf) == 0) return 1;
    return 0;
}

/* Valida e normaliza os campos do cliente em 'c'. */
static Status validar_cliente(const BaseDados *bd, Cliente *c, const char *cpf_digitado)
{
    char digitos[64];
    if (c->nome[0] == '\0') return falha(ERRO_VALIDACAO, "O nome do cliente é obrigatório.");
    if (somente_digitos(cpf_digitado, digitos, sizeof digitos) != 11)
        return falha(ERRO_VALIDACAO, "O CPF deve conter 11 dígitos.");
    strcpy(c->cpf, digitos);
    if (cpf_em_uso(bd, c->cpf, c->id))
        return falha(ERRO_DUPLICADO, "Já existe um cliente com o CPF %s.", c->cpf);
    return OK;
}

Status cliente_cadastrar(BaseDados *bd, const char *nome, const char *cpf, const char *email,
                         const char *telefone, const char *endereco, int *novo_id)
{
    Cliente c;
    Status st;
    memset(&c, 0, sizeof c);
    c.id = bd->prox_cliente;
    copiar_texto(c.nome, sizeof c.nome, nome);
    copiar_texto(c.email, sizeof c.email, email);
    copiar_texto(c.telefone, sizeof c.telefone, telefone);
    copiar_texto(c.endereco, sizeof c.endereco, endereco);
    if ((st = validar_cliente(bd, &c, cpf)) != OK) return st;
    data_agora(c.criado_em);
    if (!garantir((void **)&bd->clientes, &bd->cap_clientes, bd->n_clientes + 1, sizeof c))
        return falha(ERRO_MEMORIA, "Memória insuficiente.");
    bd->clientes[bd->n_clientes++] = c;
    bd->prox_cliente++;
    if (novo_id) *novo_id = c.id;
    return bd_salvar(bd);
}

Status cliente_alterar(BaseDados *bd, int id, const char *nome, const char *cpf,
                       const char *email, const char *telefone, const char *endereco)
{
    Cliente *atual = cliente_buscar(bd, id);
    Cliente c;
    Status st;
    if (atual == NULL) return falha(ERRO_NAO_ENCONTRADO, "Cliente %d não encontrado.", id);
    c = *atual;
    if (nome) copiar_texto(c.nome, sizeof c.nome, nome);
    if (email) copiar_texto(c.email, sizeof c.email, email);
    if (telefone) copiar_texto(c.telefone, sizeof c.telefone, telefone);
    if (endereco) copiar_texto(c.endereco, sizeof c.endereco, endereco);
    if ((st = validar_cliente(bd, &c, cpf ? cpf : atual->cpf)) != OK) return st;
    *atual = c;
    return bd_salvar(bd);
}

Status cliente_excluir(BaseDados *bd, int id)
{
    int i;
    Cliente *c = cliente_buscar(bd, id);
    if (c == NULL) return falha(ERRO_NAO_ENCONTRADO, "Cliente %d não encontrado.", id);
    for (i = 0; i < bd->n_vendas; i++)
        if (bd->vendas[i].cliente_id == id)
            return falha(ERRO_VINCULO,
                         "Não é possível excluir um cliente que possui vendas registradas.");
    i = (int)(c - bd->clientes);
    memmove(c, c + 1, (size_t)(bd->n_clientes - i - 1) * sizeof *c);
    bd->n_clientes--;
    return bd_salvar(bd);
}

static OrdemCliente ordem_cli_atual;

static int cmp_cliente(const void *a, const void *b)
{
    const Cliente *x = *(Cliente *const *)a, *y = *(Cliente *const *)b;
    int r = 0;
    if (ordem_cli_atual == ORD_CLI_NOME) r = compara_sem_caixa(x->nome, y->nome);
    else if (ordem_cli_atual == ORD_CLI_CADASTRO) r = strcmp(x->criado_em, y->criado_em);
    return r ? r : x->id - y->id;
}

int cliente_listar(BaseDados *bd, const char *filtro, OrdemCliente ordem, Cliente ***saida)
{
    int i, n = 0;
    Cliente **v = malloc((size_t)(bd->n_clientes + 1) * sizeof *v);
    *saida = v;
    if (v == NULL) return 0;
    for (i = 0; i < bd->n_clientes; i++) {
        Cliente *c = &bd->clientes[i];
        if (filtro == NULL || contem_sem_caixa(c->nome, filtro) || contem_sem_caixa(c->cpf, filtro)
            || contem_sem_caixa(c->email, filtro))
            v[n++] = c;
    }
    ordem_cli_atual = ordem;
    qsort(v, (size_t)n, sizeof *v, cmp_cliente);
    return n;
}

/* ------------------------------------------------------------ produtos */

Produto *produto_buscar(BaseDados *bd, int id)
{
    int i;
    for (i = 0; i < bd->n_produtos; i++)
        if (bd->produtos[i].id == id) return &bd->produtos[i];
    return NULL;
}

static Status validar_produto(const Produto *p)
{
    if (p->nome[0] == '\0') return falha(ERRO_VALIDACAO, "O nome do produto é obrigatório.");
    if (p->preco < 0) return falha(ERRO_VALIDACAO, "O preço não pode ser negativo.");
    if (p->estoque < 0) return falha(ERRO_VALIDACAO, "O estoque não pode ser negativo.");
    return OK;
}

Status produto_cadastrar(BaseDados *bd, const char *nome, const char *descricao,
                         long long preco, int estoque, int *novo_id)
{
    Produto p;
    Status st;
    memset(&p, 0, sizeof p);
    p.id = bd->prox_produto;
    copiar_texto(p.nome, sizeof p.nome, nome);
    copiar_texto(p.descricao, sizeof p.descricao, descricao);
    p.preco = preco;
    p.estoque = estoque;
    if ((st = validar_produto(&p)) != OK) return st;
    if (!garantir((void **)&bd->produtos, &bd->cap_produtos, bd->n_produtos + 1, sizeof p))
        return falha(ERRO_MEMORIA, "Memória insuficiente.");
    bd->produtos[bd->n_produtos++] = p;
    bd->prox_produto++;
    if (novo_id) *novo_id = p.id;
    return bd_salvar(bd);
}

Status produto_alterar(BaseDados *bd, int id, const char *nome, const char *descricao,
                       const long long *preco, const int *estoque)
{
    Produto *atual = produto_buscar(bd, id);
    Produto p;
    Status st;
    if (atual == NULL) return falha(ERRO_NAO_ENCONTRADO, "Produto %d não encontrado.", id);
    p = *atual;
    if (nome) copiar_texto(p.nome, sizeof p.nome, nome);
    if (descricao) copiar_texto(p.descricao, sizeof p.descricao, descricao);
    if (preco) p.preco = *preco;
    if (estoque) p.estoque = *estoque;
    if ((st = validar_produto(&p)) != OK) return st;
    *atual = p;
    return bd_salvar(bd);
}

Status produto_excluir(BaseDados *bd, int id)
{
    int i;
    Produto *p = produto_buscar(bd, id);
    if (p == NULL) return falha(ERRO_NAO_ENCONTRADO, "Produto %d não encontrado.", id);
    for (i = 0; i < bd->n_itens; i++)
        if (bd->itens[i].produto_id == id)
            return falha(ERRO_VINCULO, "Não é possível excluir um produto que já foi vendido.");
    i = (int)(p - bd->produtos);
    memmove(p, p + 1, (size_t)(bd->n_produtos - i - 1) * sizeof *p);
    bd->n_produtos--;
    return bd_salvar(bd);
}

static OrdemProduto ordem_prod_atual;

static int cmp_produto(const void *a, const void *b)
{
    const Produto *x = *(Produto *const *)a, *y = *(Produto *const *)b;
    int r = 0;
    switch (ordem_prod_atual) {
    case ORD_PROD_NOME: r = compara_sem_caixa(x->nome, y->nome); break;
    case ORD_PROD_PRECO: r = (x->preco > y->preco) - (x->preco < y->preco); break;
    case ORD_PROD_ESTOQUE: r = (x->estoque > y->estoque) - (x->estoque < y->estoque); break;
    default: break;
    }
    return r ? r : x->id - y->id;
}

int produto_listar(BaseDados *bd, const char *filtro, OrdemProduto ordem, Produto ***saida)
{
    int i, n = 0;
    Produto **v = malloc((size_t)(bd->n_produtos + 1) * sizeof *v);
    *saida = v;
    if (v == NULL) return 0;
    for (i = 0; i < bd->n_produtos; i++) {
        Produto *p = &bd->produtos[i];
        if (filtro == NULL || contem_sem_caixa(p->nome, filtro)
            || contem_sem_caixa(p->descricao, filtro))
            v[n++] = p;
    }
    ordem_prod_atual = ordem;
    qsort(v, (size_t)n, sizeof *v, cmp_produto);
    return n;
}

/* ------------------------------------------------------------ vendas */

Venda *venda_buscar(BaseDados *bd, int id)
{
    int i;
    for (i = 0; i < bd->n_vendas; i++)
        if (bd->vendas[i].id == id) return &bd->vendas[i];
    return NULL;
}

Status venda_registrar(BaseDados *bd, int cliente_id, const ItemPedido *itens, int n_itens,
                       const char *data, int *novo_id)
{
    ItemPedido *ag;
    int i, j, n = 0;
    Venda v;

    if (cliente_buscar(bd, cliente_id) == NULL)
        return falha(ERRO_NAO_ENCONTRADO, "Cliente %d não encontrado.", cliente_id);
    if (n_itens <= 0) return falha(ERRO_VALIDACAO, "A venda deve conter pelo menos um item.");

    /* 1) agrupa produtos repetidos */
    ag = malloc((size_t)n_itens * sizeof *ag);
    if (ag == NULL) return falha(ERRO_MEMORIA, "Memória insuficiente.");
    for (i = 0; i < n_itens; i++) {
        if (itens[i].quantidade <= 0) {
            free(ag);
            return falha(ERRO_VALIDACAO, "A quantidade de cada item deve ser maior que zero.");
        }
        for (j = 0; j < n && ag[j].produto_id != itens[i].produto_id; j++) {}
        if (j == n) ag[n++] = itens[i];
        else ag[j].quantidade += itens[i].quantidade;
    }

    /* 2) valida tudo antes de alterar qualquer dado */
    for (j = 0; j < n; j++) {
        Produto *p = produto_buscar(bd, ag[j].produto_id);
        if (p == NULL) {
            Status st = falha(ERRO_NAO_ENCONTRADO, "Produto %d não encontrado.", ag[j].produto_id);
            free(ag);
            return st;
        }
        if (p->estoque < ag[j].quantidade) {
            Status st = falha(ERRO_ESTOQUE,
                              "Estoque insuficiente para '%s' (disponível: %d, solicitado: %d).",
                              p->nome, p->estoque, ag[j].quantidade);
            free(ag);
            return st;
        }
    }
    if (!garantir((void **)&bd->vendas, &bd->cap_vendas, bd->n_vendas + 1, sizeof v)
        || !garantir((void **)&bd->itens, &bd->cap_itens, bd->n_itens + n, sizeof *bd->itens)) {
        free(ag);
        return falha(ERRO_MEMORIA, "Memória insuficiente.");
    }

    /* 3) grava a venda, os itens (com o preço atual) e baixa o estoque */
    memset(&v, 0, sizeof v);
    v.id = bd->prox_venda++;
    v.cliente_id = cliente_id;
    if (data) copiar_texto(v.data, sizeof v.data, data);
    else data_agora(v.data);
    for (j = 0; j < n; j++) {
        Produto *p = produto_buscar(bd, ag[j].produto_id);
        ItemVenda *it = &bd->itens[bd->n_itens++];
        it->venda_id = v.id;
        it->produto_id = p->id;
        it->quantidade = ag[j].quantidade;
        it->preco_unitario = p->preco;
        p->estoque -= ag[j].quantidade;
        v.total += (long long)ag[j].quantidade * p->preco;
    }
    bd->vendas[bd->n_vendas++] = v;
    free(ag);
    if (novo_id) *novo_id = v.id;
    return bd_salvar(bd);
}

int venda_itens(BaseDados *bd, int venda_id, ItemVenda ***saida)
{
    int i, n = 0;
    ItemVenda **v = malloc((size_t)(bd->n_itens + 1) * sizeof *v);
    *saida = v;
    if (v == NULL) return 0;
    for (i = 0; i < bd->n_itens; i++)
        if (bd->itens[i].venda_id == venda_id) v[n++] = &bd->itens[i];
    return n;
}

static int cmp_venda_desc(const void *a, const void *b)
{
    const Venda *x = *(Venda *const *)a, *y = *(Venda *const *)b;
    int r = strcmp(y->data, x->data);
    return r ? r : y->id - x->id;
}

int venda_listar(BaseDados *bd, Venda ***saida)
{
    int i;
    Venda **v = malloc((size_t)(bd->n_vendas + 1) * sizeof *v);
    *saida = v;
    if (v == NULL) return 0;
    for (i = 0; i < bd->n_vendas; i++) v[i] = &bd->vendas[i];
    qsort(v, (size_t)bd->n_vendas, sizeof *v, cmp_venda_desc);
    return bd->n_vendas;
}

Status venda_cancelar(BaseDados *bd, int id)
{
    int i, k = 0;
    Venda *v = venda_buscar(bd, id);
    if (v == NULL) return falha(ERRO_NAO_ENCONTRADO, "Venda %d não encontrada.", id);
    /* devolve ao estoque e remove os itens */
    for (i = 0; i < bd->n_itens; i++) {
        ItemVenda *it = &bd->itens[i];
        if (it->venda_id == id) {
            Produto *p = produto_buscar(bd, it->produto_id);
            if (p) p->estoque += it->quantidade;
        } else {
            bd->itens[k++] = *it;
        }
    }
    bd->n_itens = k;
    i = (int)(v - bd->vendas);
    memmove(v, v + 1, (size_t)(bd->n_vendas - i - 1) * sizeof *v);
    bd->n_vendas--;
    return bd_salvar(bd);
}
