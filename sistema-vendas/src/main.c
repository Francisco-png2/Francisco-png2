/*
 * main.c - Sistema de Gerenciamento de Vendas (interface de terminal).
 *
 * Uso:
 *   ./sistema                          conecta usando a variável VENDAS_CONEXAO
 *                                      (ou "host=localhost dbname=vendas")
 *   ./sistema --conexao "host=localhost dbname=vendas user=postgres password=..."
 *   ./sistema --exemplo                popula o banco com dados de exemplo (se estiver vazio)
 */
#define __USE_MINGW_ANSI_STDIO 1

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "dados.h"
#include "relatorios.h"

#define TAM_ENTRADA 256
#define CONEXAO_PADRAO "host=localhost dbname=vendas"

static BaseDados bd;

/* ================================================================ formatação */

/* Buffers rotativos: permitem usar várias chamadas no mesmo printf. */
static char *buffer_temp(void)
{
    static char bufs[32][64];
    static int i = 0;
    i = (i + 1) % 32;
    return bufs[i];
}

/* 123456 -> "R$ 1.234,56" */
static const char *moeda(long long centavos)
{
    char *s = buffer_temp(), digitos[32], inteiro[48];
    int len, i, k = 0;
    int negativo = centavos < 0;
    if (negativo) centavos = -centavos;
    len = snprintf(digitos, sizeof digitos, "%lld", centavos / 100);
    for (i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) inteiro[k++] = '.';
        inteiro[k++] = digitos[i];
    }
    inteiro[k] = '\0';
    snprintf(s, 64, "%sR$ %s,%02lld", negativo ? "-" : "", inteiro, centavos % 100);
    return s;
}

static const char *num(long long n)
{
    char *s = buffer_temp();
    snprintf(s, 64, "%lld", n);
    return s;
}

/* "AAAA-MM-DD HH:MM:SS" -> "DD/MM/AAAA HH:MM" (ou só a data se for "AAAA-MM-DD") */
static const char *data_br(const char *iso)
{
    char *s = buffer_temp();
    if (strlen(iso) >= 16)
        snprintf(s, 64, "%.2s/%.2s/%.4s %.5s", iso + 8, iso + 5, iso, iso + 11);
    else
        snprintf(s, 64, "%.2s/%.2s/%.4s", iso + 8, iso + 5, iso);
    return s;
}

/* 12345678901 -> 123.456.789-01 */
static const char *cpf_fmt(const char *cpf)
{
    char *s = buffer_temp();
    if (strlen(cpf) == 11)
        snprintf(s, 64, "%.3s.%.3s.%.3s-%.2s", cpf, cpf + 3, cpf + 6, cpf + 9);
    else
        snprintf(s, 64, "%s", cpf);
    return s;
}

/* ================================================================ tabelas */

/* Imprime uma célula com largura fixa, contando caracteres UTF-8 corretamente. */
static void celula(const char *s, int largura)
{
    const unsigned char *p = (const unsigned char *)(s && *s ? s : "-");
    int chars = 0;
    putchar(' ');
    while (*p && chars < largura) {
        putchar(*p++);
        while ((*p & 0xC0) == 0x80) putchar(*p++);
        chars++;
    }
    for (; chars < largura; chars++) putchar(' ');
    printf(" |");
}

static void tab_separador(const int *larguras, int n)
{
    int i, j;
    printf("  +");
    for (i = 0; i < n; i++) {
        for (j = 0; j < larguras[i] + 2; j++) putchar('-');
        putchar('+');
    }
    putchar('\n');
}

/* Imprime uma linha da tabela; recebe n textos como argumentos. */
static void tab_linha(const int *larguras, int n, ...)
{
    int i;
    va_list ap;
    va_start(ap, n);
    printf("  |");
    for (i = 0; i < n; i++) celula(va_arg(ap, const char *), larguras[i]);
    putchar('\n');
    va_end(ap);
}

static void sem_registros(void) { printf("\n  (nenhum registro encontrado)\n"); }

/* ================================================================ entrada */

static void aparar(char *s)
{
    size_t n = strlen(s), i = 0;
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, n - i + 1);
}

static void ler_linha(char *buf, size_t tam)
{
    fflush(stdout);
    if (fgets(buf, (int)tam, stdin) == NULL) { /* fim da entrada (Ctrl+D / Ctrl+Z) */
        printf("\nAté logo!\n");
        bd_desconectar(&bd);
        exit(0);
    }
    if (strchr(buf, '\n') == NULL) { /* descarta o excesso de uma linha longa */
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {}
    }
    aparar(buf);
}

/* Lê um texto. padrao != NULL: Enter vazio devolve o padrão. */
static void ler_texto(const char *rotulo, char *buf, size_t tam, const char *padrao,
                      int obrigatorio)
{
    for (;;) {
        if (padrao && *padrao) printf("  %s [%s]: ", rotulo, padrao);
        else printf("  %s: ", rotulo);
        ler_linha(buf, tam);
        if (buf[0] == '\0' && padrao) {
            snprintf(buf, tam, "%s", padrao);
            return;
        }
        if (buf[0] || !obrigatorio) return;
        printf("  ! Campo obrigatório.\n");
    }
}

static int ler_inteiro(const char *rotulo, const int *padrao, int minimo)
{
    char buf[TAM_ENTRADA], pad[32] = "", *fim;
    long v;
    if (padrao) snprintf(pad, sizeof pad, "%d", *padrao);
    for (;;) {
        ler_texto(rotulo, buf, sizeof buf, padrao ? pad : NULL, 1);
        v = strtol(buf, &fim, 10);
        if (*fim == '\0' && v >= minimo && v <= 1000000000L) return (int)v;
        printf("  ! Informe um número inteiro válido");
        if (minimo > -1000000000) printf(" (mínimo %d)", minimo);
        printf(".\n");
    }
}

/* Converte "19,90", "19.9", "1.234,56" ou "20" para centavos. Retorna 0 se inválido. */
static int texto_para_centavos(const char *texto, long long *saida)
{
    long long inteiro = 0, frac = 0;
    int casas = 0, digitos = 0;
    char limpo[TAM_ENTRADA], *s = limpo;
    size_t k = 0;
    /* com vírgula decimal, pontos são separadores de milhar e são ignorados */
    for (; *texto && k < sizeof limpo - 1; texto++)
        if (!(*texto == '.' && strchr(texto, ',') != NULL)) limpo[k++] = *texto;
    limpo[k] = '\0';
    for (; *s && *s != ',' && *s != '.'; s++) {
        if (!isdigit((unsigned char)*s) || digitos > 12) return 0;
        inteiro = inteiro * 10 + (*s - '0');
        digitos++;
    }
    if (*s) {
        for (s++; *s; s++) {
            if (!isdigit((unsigned char)*s) || casas >= 2) return 0;
            frac = frac * 10 + (*s - '0');
            casas++;
        }
    }
    if (digitos == 0 && casas == 0) return 0;
    if (casas == 1) frac *= 10;
    *saida = inteiro * 100 + frac;
    return 1;
}

static long long ler_dinheiro(const char *rotulo, const long long *padrao)
{
    char buf[TAM_ENTRADA], pad[48] = "";
    long long v;
    if (padrao) snprintf(pad, sizeof pad, "%lld,%02lld", *padrao / 100, *padrao % 100);
    for (;;) {
        ler_texto(rotulo, buf, sizeof buf, padrao ? pad : NULL, 1);
        if (texto_para_centavos(buf, &v)) return v;
        printf("  ! Informe um valor válido (ex.: 19,90).\n");
    }
}

static int data_valida(int d, int m, int a)
{
    static const int dias[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max;
    if (a < 1900 || a > 9999 || m < 1 || m > 12 || d < 1) return 0;
    max = dias[m - 1];
    if (m == 2 && ((a % 4 == 0 && a % 100 != 0) || a % 400 == 0)) max = 29;
    return d <= max;
}

/* Lê "DD/MM/AAAA" e grava em iso como "AAAA-MM-DD". padrao_iso pode ser NULL. */
static void ler_data(const char *rotulo, const char *padrao_iso, char iso[11])
{
    char buf[TAM_ENTRADA], rot[128], pad[16] = "";
    int d, m, a;
    char extra;
    if (padrao_iso) snprintf(pad, sizeof pad, "%s", data_br(padrao_iso));
    snprintf(rot, sizeof rot, "%s (DD/MM/AAAA)", rotulo);
    for (;;) {
        ler_texto(rot, buf, sizeof buf, padrao_iso ? pad : NULL, 1);
        if (sscanf(buf, "%d/%d/%d%c", &d, &m, &a, &extra) == 3 && data_valida(d, m, a)) {
            char tmp[40];
            snprintf(tmp, sizeof tmp, "%04d-%02d-%02d", a, m, d);
            memcpy(iso, tmp, 11); /* data já validada: sempre 10 caracteres */
            return;
        }
        printf("  ! Data inválida.\n");
    }
}

static int confirmar(const char *pergunta)
{
    char buf[TAM_ENTRADA];
    printf("  %s (s/n): ", pergunta);
    ler_linha(buf, sizeof buf);
    return buf[0] == 's' || buf[0] == 'S';
}

static void pausar(void)
{
    char buf[TAM_ENTRADA];
    printf("\n  [Enter para continuar]");
    ler_linha(buf, sizeof buf);
}

/* Mostra o resultado de uma operação; retorna 1 se deu certo. */
static int resultado(Status st, const char *msg_ok)
{
    if (st == OK) {
        if (msg_ok) printf("  OK: %s\n", msg_ok);
        return 1;
    }
    printf("  ! %s\n", bd_erro());
    return 0;
}

/* Mostra a mensagem de erro quando uma listagem falha (n < 0). */
static int lista_ok(int n)
{
    if (n < 0) printf("  ! %s\n", bd_erro());
    return n >= 0;
}

/* ================================================================ clientes */

static const int L_CLI[] = {4, 24, 14, 26, 15};

static void tabela_clientes(const Cliente *v, int n)
{
    int i;
    if (n == 0) { sem_registros(); return; }
    tab_separador(L_CLI, 5);
    tab_linha(L_CLI, 5, "ID", "Nome", "CPF", "E-mail", "Telefone");
    tab_separador(L_CLI, 5);
    for (i = 0; i < n; i++)
        tab_linha(L_CLI, 5, num(v[i].id), v[i].nome, cpf_fmt(v[i].cpf), v[i].email,
                  v[i].telefone);
    tab_separador(L_CLI, 5);
}

static void cli_cadastrar(void)
{
    char nome[TAM_NOME], cpf[TAM_ENTRADA], email[TAM_EMAIL], tel[TAM_TELEFONE],
        end[TAM_ENDERECO];
    int id;
    ler_texto("Nome", nome, sizeof nome, NULL, 1);
    ler_texto("CPF", cpf, sizeof cpf, NULL, 1);
    ler_texto("E-mail", email, sizeof email, NULL, 0);
    ler_texto("Telefone", tel, sizeof tel, NULL, 0);
    ler_texto("Endereço", end, sizeof end, NULL, 0);
    if (resultado(cliente_cadastrar(&bd, nome, cpf, email, tel, end, &id), NULL))
        printf("  OK: Cliente cadastrado com ID %d.\n", id);
}

static void cli_listar(void)
{
    char filtro[TAM_ENTRADA], ordem[TAM_ENTRADA];
    Cliente *v;
    OrdemCliente o = ORD_CLI_NOME;
    int n;
    ler_texto("Filtrar por nome/CPF/e-mail (vazio = todos)", filtro, sizeof filtro, NULL, 0);
    ler_texto("Ordenar por (nome/id/cadastro)", ordem, sizeof ordem, "nome", 0);
    if (strcmp(ordem, "id") == 0) o = ORD_CLI_ID;
    else if (strcmp(ordem, "cadastro") == 0) o = ORD_CLI_CADASTRO;
    n = cliente_listar(&bd, filtro, o, &v);
    if (lista_ok(n)) {
        tabela_clientes(v, n);
        printf("  %d cliente(s).\n", n);
    }
    free(v);
}

/* Pede o ID e carrega o cliente em c. Retorna 1 se encontrou. */
static int pedir_cliente(Cliente *c)
{
    int id = ler_inteiro("ID do cliente", NULL, 1);
    return resultado(cliente_buscar(&bd, id, c), NULL);
}

static void cli_consultar(void)
{
    Cliente c;
    if (!pedir_cliente(&c)) return;
    printf("\n  ID ........: %d\n", c.id);
    printf("  Nome ......: %s\n", c.nome);
    printf("  CPF .......: %s\n", cpf_fmt(c.cpf));
    printf("  E-mail ....: %s\n", c.email[0] ? c.email : "-");
    printf("  Telefone ..: %s\n", c.telefone[0] ? c.telefone : "-");
    printf("  Endereço ..: %s\n", c.endereco[0] ? c.endereco : "-");
    printf("  Cadastro ..: %s\n", data_br(c.criado_em));
}

static void cli_alterar(void)
{
    char nome[TAM_NOME], cpf[TAM_ENTRADA], email[TAM_EMAIL], tel[TAM_TELEFONE],
        end[TAM_ENDERECO];
    Cliente c;
    if (!pedir_cliente(&c)) return;
    printf("  (Enter mantém o valor atual)\n");
    ler_texto("Nome", nome, sizeof nome, c.nome, 1);
    ler_texto("CPF", cpf, sizeof cpf, c.cpf, 1);
    ler_texto("E-mail", email, sizeof email, c.email, 0);
    ler_texto("Telefone", tel, sizeof tel, c.telefone, 0);
    ler_texto("Endereço", end, sizeof end, c.endereco, 0);
    resultado(cliente_alterar(&bd, c.id, nome, cpf, email, tel, end), "Cliente alterado.");
}

static void cli_excluir(void)
{
    Cliente c;
    if (!pedir_cliente(&c)) return;
    printf("  Cliente: %s\n", c.nome);
    if (confirmar("Confirma a exclusão?"))
        resultado(cliente_excluir(&bd, c.id), "Cliente excluído.");
}

/* ================================================================ produtos */

static const int L_PROD[] = {4, 24, 30, 14, 8};

static void tabela_produtos(const Produto *v, int n)
{
    int i;
    if (n == 0) { sem_registros(); return; }
    tab_separador(L_PROD, 5);
    tab_linha(L_PROD, 5, "ID", "Nome", "Descrição", "Preço", "Estoque");
    tab_separador(L_PROD, 5);
    for (i = 0; i < n; i++)
        tab_linha(L_PROD, 5, num(v[i].id), v[i].nome, v[i].descricao, moeda(v[i].preco),
                  num(v[i].estoque));
    tab_separador(L_PROD, 5);
}

static void prod_cadastrar(void)
{
    char nome[TAM_NOME], desc[TAM_DESCRICAO];
    long long preco;
    int estoque, zero = 0, id;
    ler_texto("Nome", nome, sizeof nome, NULL, 1);
    ler_texto("Descrição", desc, sizeof desc, NULL, 0);
    preco = ler_dinheiro("Preço (R$)", NULL);
    estoque = ler_inteiro("Estoque inicial", &zero, 0);
    if (resultado(produto_cadastrar(&bd, nome, desc, preco, estoque, &id), NULL))
        printf("  OK: Produto cadastrado com ID %d.\n", id);
}

static void prod_listar(void)
{
    char filtro[TAM_ENTRADA], ordem[TAM_ENTRADA];
    Produto *v;
    OrdemProduto o = ORD_PROD_NOME;
    int n;
    ler_texto("Filtrar por nome/descrição (vazio = todos)", filtro, sizeof filtro, NULL, 0);
    ler_texto("Ordenar por (nome/id/preco/estoque)", ordem, sizeof ordem, "nome", 0);
    if (strcmp(ordem, "id") == 0) o = ORD_PROD_ID;
    else if (strcmp(ordem, "preco") == 0 || strcmp(ordem, "preço") == 0) o = ORD_PROD_PRECO;
    else if (strcmp(ordem, "estoque") == 0) o = ORD_PROD_ESTOQUE;
    n = produto_listar(&bd, filtro, o, &v);
    if (lista_ok(n)) {
        tabela_produtos(v, n);
        printf("  %d produto(s).\n", n);
    }
    free(v);
}

static int pedir_produto(Produto *p)
{
    int id = ler_inteiro("ID do produto", NULL, 1);
    return resultado(produto_buscar(&bd, id, p), NULL);
}

static void prod_consultar(void)
{
    Produto p;
    if (!pedir_produto(&p)) return;
    printf("\n  ID .........: %d\n", p.id);
    printf("  Nome .......: %s\n", p.nome);
    printf("  Descrição ..: %s\n", p.descricao[0] ? p.descricao : "-");
    printf("  Preço ......: %s\n", moeda(p.preco));
    printf("  Estoque ....: %d\n", p.estoque);
}

static void prod_alterar(void)
{
    char nome[TAM_NOME], desc[TAM_DESCRICAO];
    long long preco;
    int estoque;
    Produto p;
    if (!pedir_produto(&p)) return;
    printf("  (Enter mantém o valor atual)\n");
    ler_texto("Nome", nome, sizeof nome, p.nome, 1);
    ler_texto("Descrição", desc, sizeof desc, p.descricao, 0);
    preco = ler_dinheiro("Preço (R$)", &p.preco);
    estoque = ler_inteiro("Estoque", &p.estoque, 0);
    resultado(produto_alterar(&bd, p.id, nome, desc, &preco, &estoque), "Produto alterado.");
}

static void prod_excluir(void)
{
    Produto p;
    if (!pedir_produto(&p)) return;
    printf("  Produto: %s\n", p.nome);
    if (confirmar("Confirma a exclusão?"))
        resultado(produto_excluir(&bd, p.id), "Produto excluído.");
}

/* ================================================================ vendas */

static int mostrar_venda(int venda_id)
{
    static const int L[] = {24, 5, 14, 14};
    Venda v;
    ItemVenda *itens;
    int i, n;
    if (!resultado(venda_buscar(&bd, venda_id, &v), NULL)) return 0;
    printf("\n  Venda #%d - %s - Cliente: %s\n", v.id, data_br(v.data), v.cliente_nome);
    n = venda_itens(&bd, venda_id, &itens);
    if (lista_ok(n)) {
        tab_separador(L, 4);
        tab_linha(L, 4, "Produto", "Qtd", "Preço unit.", "Subtotal");
        tab_separador(L, 4);
        for (i = 0; i < n; i++)
            tab_linha(L, 4, itens[i].produto_nome, num(itens[i].quantidade),
                      moeda(itens[i].preco_unitario), moeda(itens[i].subtotal));
        tab_separador(L, 4);
        printf("  TOTAL: %s\n", moeda(v.total));
    }
    free(itens);
    return 1;
}

static void venda_registrar_cli(void)
{
    ItemPedido *itens = NULL;
    int n = 0, cap = 0, venda_id;
    char buf[TAM_ENTRADA], rot[TAM_NOME + 80];
    Cliente c;
    if (!pedir_cliente(&c)) return;
    printf("  Cliente: %s\n", c.nome);
    printf("  Adicione os itens (ID do produto vazio para finalizar).\n");
    for (;;) {
        Produto p;
        char *fim;
        long id;
        ler_texto("ID do produto", buf, sizeof buf, NULL, 0);
        if (buf[0] == '\0') break;
        id = strtol(buf, &fim, 10);
        if (*fim != '\0' || id <= 0 || produto_buscar(&bd, (int)id, &p) != OK) {
            printf("  ! Produto não encontrado.\n");
            continue;
        }
        if (n == cap) {
            ItemPedido *novo = realloc(itens, (size_t)(cap = cap ? cap * 2 : 8) * sizeof *itens);
            if (novo == NULL) { printf("  ! Memória insuficiente.\n"); break; }
            itens = novo;
        }
        snprintf(rot, sizeof rot, "Quantidade de '%s' (%s, estoque %d)", p.nome, moeda(p.preco),
                 p.estoque);
        itens[n].produto_id = p.id;
        itens[n].quantidade = ler_inteiro(rot, NULL, 1);
        n++;
    }
    if (n == 0) {
        printf("  Venda cancelada (nenhum item).\n");
    } else if (resultado(venda_registrar(&bd, c.id, itens, n, NULL, &venda_id), NULL)) {
        mostrar_venda(venda_id);
        printf("  OK: Venda %d registrada.\n", venda_id);
    }
    free(itens);
}

static void tabela_vendas(const Venda *v, int n)
{
    static const int L[] = {5, 16, 24, 6, 14};
    int i;
    if (n == 0) { sem_registros(); return; }
    tab_separador(L, 5);
    tab_linha(L, 5, "ID", "Data", "Cliente", "Itens", "Total");
    tab_separador(L, 5);
    for (i = 0; i < n; i++)
        tab_linha(L, 5, num(v[i].id), data_br(v[i].data), v[i].cliente_nome, num(v[i].unidades),
                  moeda(v[i].total));
    tab_separador(L, 5);
}

static void venda_listar_cli(void)
{
    Venda *v;
    int n = venda_listar(&bd, &v);
    if (lista_ok(n)) {
        tabela_vendas(v, n);
        printf("  %d venda(s).\n", n);
    }
    free(v);
}

static void venda_detalhar_cli(void) { mostrar_venda(ler_inteiro("ID da venda", NULL, 1)); }

static void venda_cancelar_cli(void)
{
    int id = ler_inteiro("ID da venda", NULL, 1);
    if (!mostrar_venda(id)) return;
    if (confirmar("Confirma o cancelamento? Os itens voltarão ao estoque"))
        resultado(venda_cancelar(&bd, id), "Venda cancelada.");
}

/* ================================================================ relatórios */

static int ler_limite(void)
{
    int dez = 10;
    return ler_inteiro("Quantos exibir", &dez, 1);
}

/* Pede um período; padrão: do dia 1º do mês atual até hoje. */
static void ler_periodo(char inicio[11], char fim[11])
{
    char hoje[11], primeiro[11];
    time_t t = time(NULL);
    strftime(hoje, sizeof hoje, "%Y-%m-%d", localtime(&t));
    snprintf(primeiro, sizeof primeiro, "%.8s01", hoje);
    for (;;) {
        ler_data("Data inicial", primeiro, inicio);
        ler_data("Data final", hoje, fim);
        if (strcmp(inicio, fim) <= 0) return;
        printf("  ! A data inicial deve ser anterior ou igual à data final.\n");
    }
}

static void tabela_rel_clientes(const RelCliente *r, int n, int com_ticket)
{
    static const int L[] = {4, 24, 10, 16, 14};
    int i, cols = com_ticket ? 5 : 4;
    if (n == 0) { sem_registros(); return; }
    tab_separador(L, cols);
    tab_linha(L, cols, "ID", "Cliente", "Compras", "Valor total", "Ticket médio");
    tab_separador(L, cols);
    for (i = 0; i < n; i++)
        tab_linha(L, cols, num(r[i].cliente_id), r[i].nome, num(r[i].qtd_compras),
                  moeda(r[i].valor_total), moeda(r[i].ticket_medio));
    tab_separador(L, cols);
}

static void rel1_melhores_clientes(void)
{
    char crit[TAM_ENTRADA];
    RelCliente *r;
    int por_valor, limite, n;
    ler_texto("Critério (quantidade/valor)", crit, sizeof crit, "quantidade", 0);
    por_valor = (crit[0] == 'v' || crit[0] == 'V');
    limite = ler_limite();
    n = rel_melhores_clientes(&bd, por_valor, limite, &r);
    printf("\n  MELHORES CLIENTES - por %s\n", por_valor ? "valor total" : "quantidade de compras");
    if (lista_ok(n)) tabela_rel_clientes(r, n, 0);
    free(r);
}

static void rel2_clientes_maior_valor(void)
{
    RelCliente *r;
    int n = rel_clientes_maior_valor(&bd, ler_limite(), &r);
    printf("\n  CLIENTES COM MAIOR VALOR EM COMPRAS\n");
    if (lista_ok(n)) tabela_rel_clientes(r, n, 1);
    free(r);
}

static void rel3_produtos_mais_vendidos(void)
{
    static const int L[] = {4, 24, 10, 16};
    RelProduto *r;
    int i, n = rel_produtos_mais_vendidos(&bd, ler_limite(), &r);
    printf("\n  PRODUTOS MAIS VENDIDOS\n");
    if (!lista_ok(n)) {
    } else if (n == 0) {
        sem_registros();
    } else {
        tab_separador(L, 4);
        tab_linha(L, 4, "ID", "Produto", "Unidades", "Faturamento");
        tab_separador(L, 4);
        for (i = 0; i < n; i++)
            tab_linha(L, 4, num(r[i].produto_id), r[i].nome, num(r[i].unidades),
                      moeda(r[i].faturamento));
        tab_separador(L, 4);
    }
    free(r);
}

static void rel4_vendas_periodo(void)
{
    char ini[11], fim[11];
    Venda *v;
    long long total = 0;
    int i, n;
    ler_periodo(ini, fim);
    n = rel_vendas_periodo(&bd, ini, fim, &v);
    printf("\n  VENDAS DE %s A %s\n", data_br(ini), data_br(fim));
    if (lista_ok(n)) {
        tabela_vendas(v, n);
        for (i = 0; i < n; i++) total += v[i].total;
        printf("  %d venda(s) - total %s\n", n, moeda(total));
    }
    free(v);
}

static void rel5_faturamento_periodo(void)
{
    static const int L[] = {12, 10, 16};
    char ini[11], fim[11];
    RelFaturamento f;
    int i;
    ler_periodo(ini, fim);
    if (!resultado(rel_faturamento_periodo(&bd, ini, fim, &f), NULL)) return;
    printf("\n  FATURAMENTO DE %s A %s\n", data_br(ini), data_br(fim));
    if (f.n_dias == 0) sem_registros();
    else {
        tab_separador(L, 3);
        tab_linha(L, 3, "Dia", "Vendas", "Faturamento");
        tab_separador(L, 3);
        for (i = 0; i < f.n_dias; i++)
            tab_linha(L, 3, data_br(f.dias[i].dia), num(f.dias[i].qtd_vendas),
                      moeda(f.dias[i].faturamento));
        tab_separador(L, 3);
    }
    printf("  Vendas: %d  |  Faturamento: %s  |  Ticket médio: %s\n", f.qtd_vendas,
           moeda(f.faturamento), moeda(f.ticket_medio));
    free(f.dias);
}

static void rel6_historico_cliente(void)
{
    static const int L[] = {5, 16, 22, 5, 14, 14};
    RelHistorico *r;
    Cliente c;
    long long gasto = 0;
    int i, n, compras = 0;
    if (!pedir_cliente(&c)) return;
    n = rel_historico_cliente(&bd, c.id, &r);
    printf("\n  HISTÓRICO DE COMPRAS - %s\n", c.nome);
    if (!lista_ok(n)) { free(r); return; }
    if (n == 0) sem_registros();
    else {
        tab_separador(L, 6);
        tab_linha(L, 6, "Venda", "Data", "Produto", "Qtd", "Preço unit.", "Subtotal");
        tab_separador(L, 6);
        for (i = 0; i < n; i++) {
            tab_linha(L, 6, num(r[i].venda_id), data_br(r[i].data), r[i].produto,
                      num(r[i].quantidade), moeda(r[i].preco_unitario), moeda(r[i].subtotal));
            if (i == 0 || r[i].venda_id != r[i - 1].venda_id) {
                compras++;
                gasto += r[i].total_venda;
            }
        }
        tab_separador(L, 6);
    }
    printf("  %d compra(s) - total gasto %s\n", compras, moeda(gasto));
    free(r);
}

static void tabela_rel_produtos(const RelProduto *r, int n)
{
    static const int L[] = {4, 24, 8, 10, 16, 9};
    int i;
    char pct[16];
    if (!lista_ok(n)) return;
    if (n == 0) { sem_registros(); return; }
    tab_separador(L, 6);
    tab_linha(L, 6, "ID", "Produto", "Vendas", "Unidades", "Faturamento", "% total");
    tab_separador(L, 6);
    for (i = 0; i < n; i++) {
        snprintf(pct, sizeof pct, "%.2f%%", r[i].participacao);
        tab_linha(L, 6, num(r[i].produto_id), r[i].nome, num(r[i].qtd_vendas),
                  num(r[i].unidades), moeda(r[i].faturamento), pct);
    }
    tab_separador(L, 6);
}

static void rel7_desempenho_produtos(void)
{
    char buf[TAM_ENTRADA];
    RelProduto *r;
    int n;
    ler_texto("ID do produto (vazio = todos)", buf, sizeof buf, NULL, 0);
    n = rel_desempenho_produtos(&bd, buf[0] ? atoi(buf) : 0, &r);
    printf("\n  DESEMPENHO POR PRODUTO (nº de vendas, unidades e valor gerado)\n");
    tabela_rel_produtos(r, n);
    free(r);
}

static void rel8_produtos_maior_faturamento(void)
{
    RelProduto *r;
    int n = rel_produtos_maior_faturamento(&bd, ler_limite(), &r);
    printf("\n  PRODUTOS COM MAIOR FATURAMENTO\n");
    tabela_rel_produtos(r, n);
    free(r);
}

/* ================================================================ menus */

typedef struct {
    const char *texto;
    void (*acao)(void);
} Opcao;

static void executar_menu(const char *titulo, const Opcao *opcoes, int n)
{
    char buf[TAM_ENTRADA];
    for (;;) {
        int i, escolha;
        printf("\n=== %s ===\n", titulo);
        for (i = 0; i < n; i++) printf("  %d. %s\n", i + 1, opcoes[i].texto);
        printf("  0. Voltar\nEscolha: ");
        ler_linha(buf, sizeof buf);
        if (strcmp(buf, "0") == 0) return;
        escolha = atoi(buf);
        if (escolha < 1 || escolha > n) {
            printf("  ! Opção inválida.\n");
            continue;
        }
        opcoes[escolha - 1].acao();
        pausar();
    }
}

static void menu_clientes(void)
{
    static const Opcao op[] = {
        {"Cadastrar", cli_cadastrar},       {"Listar / pesquisar", cli_listar},
        {"Consultar por ID", cli_consultar}, {"Alterar", cli_alterar},
        {"Excluir", cli_excluir},
    };
    executar_menu("CLIENTES", op, 5);
}

static void menu_produtos(void)
{
    static const Opcao op[] = {
        {"Cadastrar", prod_cadastrar},       {"Listar / pesquisar", prod_listar},
        {"Consultar por ID", prod_consultar}, {"Alterar", prod_alterar},
        {"Excluir", prod_excluir},
    };
    executar_menu("PRODUTOS", op, 5);
}

static void menu_vendas(void)
{
    static const Opcao op[] = {
        {"Registrar venda", venda_registrar_cli},
        {"Listar vendas", venda_listar_cli},
        {"Detalhar venda", venda_detalhar_cli},
        {"Cancelar venda", venda_cancelar_cli},
    };
    executar_menu("VENDAS", op, 4);
}

static void menu_relatorios(void)
{
    static const Opcao op[] = {
        {"Melhores clientes", rel1_melhores_clientes},
        {"Clientes com maior valor em compras", rel2_clientes_maior_valor},
        {"Produtos mais vendidos", rel3_produtos_mais_vendidos},
        {"Vendas por período", rel4_vendas_periodo},
        {"Faturamento por período", rel5_faturamento_periodo},
        {"Histórico de compras do cliente", rel6_historico_cliente},
        {"Desempenho por produto", rel7_desempenho_produtos},
        {"Produtos com maior faturamento", rel8_produtos_maior_faturamento},
    };
    executar_menu("RELATÓRIOS", op, 8);
}

/* ================================================================ exemplo */

static unsigned long semente = 42;
static int aleatorio(int max) /* 0..max-1, determinístico */
{
    semente = semente * 1103515245UL + 12345UL;
    return (int)((semente / 65536UL) % 32768UL) % max;
}

static void popular_exemplo(void)
{
    static const char *clientes[][3] = {
        {"Ana Souza", "11111111111", "ana@email.com"},
        {"Bruno Lima", "22222222222", "bruno@email.com"},
        {"Carla Mendes", "33333333333", "carla@email.com"},
        {"Diego Rocha", "44444444444", "diego@email.com"},
        {"Elisa Prado", "55555555555", "elisa@email.com"},
    };
    static const struct { const char *nome; long long preco; } produtos[] = {
        {"Notebook", 350000}, {"Mouse", 7990},       {"Teclado", 14990},
        {"Monitor 24\"", 89900}, {"Headset", 24990}, {"Webcam", 19900},
    };
    int i, j, nc, np, cids[5], pids[6];
    Cliente *lc;
    Produto *lp;
    time_t agora = time(NULL);
    nc = cliente_listar(&bd, NULL, ORD_CLI_ID, &lc);
    np = produto_listar(&bd, NULL, ORD_PROD_ID, &lp);
    free(lc);
    free(lp);
    if (nc != 0 || np != 0) {
        printf("O banco já possui dados; exemplo não aplicado.\n");
        return;
    }
    for (i = 0; i < 5; i++)
        cliente_cadastrar(&bd, clientes[i][0], clientes[i][1], clientes[i][2], "", "", &cids[i]);
    for (i = 0; i < 6; i++)
        produto_cadastrar(&bd, produtos[i].nome, "", produtos[i].preco, 500, &pids[i]);
    for (i = 0; i < 40; i++) {
        ItemPedido itens[3];
        char data[TAM_DATA];
        int n = 1 + aleatorio(3), inicio = aleatorio(6);
        time_t t = agora - (time_t)aleatorio(91) * 86400 - (time_t)aleatorio(10) * 3600;
        strftime(data, sizeof data, "%Y-%m-%d %H:%M:%S", localtime(&t));
        for (j = 0; j < n; j++) {
            itens[j].produto_id = pids[(inicio + j) % 6];
            itens[j].quantidade = 1 + aleatorio(3);
        }
        venda_registrar(&bd, cids[aleatorio(5)], itens, n, data, NULL);
    }
    printf("Dados de exemplo inseridos: 5 clientes, 6 produtos, 40 vendas.\n");
}

/* ================================================================ main */

int main(int argc, char **argv)
{
    const char *conexao = getenv("VENDAS_CONEXAO");
    int exemplo = 0, i;
    char buf[TAM_ENTRADA];

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    if (conexao == NULL || conexao[0] == '\0') conexao = CONEXAO_PADRAO;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--conexao") == 0 && i + 1 < argc) conexao = argv[++i];
        else if (strcmp(argv[i], "--exemplo") == 0) exemplo = 1;
        else {
            printf("Uso: %s [--conexao \"host=... dbname=... user=... password=...\"] [--exemplo]\n",
                   argv[0]);
            return 1;
        }
    }

    if (bd_conectar(&bd, conexao) != OK) {
        printf("%s\n", bd_erro());
        printf("Verifique se o PostgreSQL está em execução, se o banco existe e os dados de\n"
               "acesso. Ex.: %s --conexao \"host=localhost dbname=vendas user=postgres "
               "password=SUA_SENHA\"\n", argv[0]);
        return 1;
    }
    printf("Conectado ao banco '%s' em %s.\n", PQdb(bd.conn),
           PQhost(bd.conn) ? PQhost(bd.conn) : "localhost");
    if (exemplo) popular_exemplo();

    for (;;) {
        printf("\n=== SISTEMA DE GERENCIAMENTO DE VENDAS ===\n");
        printf("  1. Clientes\n  2. Produtos\n  3. Vendas\n  4. Relatórios\n  0. Sair\nEscolha: ");
        ler_linha(buf, sizeof buf);
        if (strcmp(buf, "1") == 0) menu_clientes();
        else if (strcmp(buf, "2") == 0) menu_produtos();
        else if (strcmp(buf, "3") == 0) menu_vendas();
        else if (strcmp(buf, "4") == 0) menu_relatorios();
        else if (strcmp(buf, "0") == 0) break;
        else printf("  ! Opção inválida.\n");
    }
    printf("Até logo!\n");
    bd_desconectar(&bd);
    return 0;
}
