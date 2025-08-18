/*
 * Sistema de Arquivos Simplificado (SFS)
 * 
 * Sistema de arquivos básico que implementa os conceitos fundamentais
 * requeridos: criação, leitura, escrita, exclusão e listagem de arquivos.
 * 
 * CONCEITOS IMPLEMENTADOS:
 * - Superbloco com metadados do sistema
 * - Inodes para metadados de arquivos/diretórios  
 * - Blocos de dados de tamanho fixo
 * - Bitmaps para gerenciamento de recursos livres/ocupados
 * - Ponteiros diretos
 * - Diretórios estruturados
 * 
 * Compilação: gcc -Wall -Wextra -g sfs_persistente.c -o sfs_persistente
 * Execução: ./sfs_persistente
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// === CONSTANTES FUNDAMENTAIS ===
#define TAMANHO_BLOCO 512           // Tamanho de cada bloco (512 bytes)
#define TOTAL_BLOCOS 2048           // Total de blocos no sistema (1MB)
#define TOTAL_INODES 256            // Total de inodes disponíveis
#define MAX_NOME_ARQUIVO 64         // Tamanho máximo do nome
#define NUM_PONTEIROS_DIRETOS 10    // Ponteiros diretos por inode. Ponteiros diretos são usados para acessar blocos de dados diretamente, sem necessidade de indireção. Então cada inode pode apontar diretamente para até 10 blocos de dados. Com cada bloco tendo 512 bytes, isso permite que cada arquivo tenha até 5.120 bytes de dados diretamente acessíveis sem precisar de blocos indiretos. 
#define MAGIC_NUMBER 0xED123456     // Número mágico do sistema

// === TIPOS DE ARQUIVO ===
#define TIPO_ARQUIVO_REGULAR   0x01
#define TIPO_DIRETORIO         0x02

// === ESTRUTURAS FUNDAMENTAIS ===

// Superbloco - Contém metadados globais do sistema
typedef struct {
    uint32_t magic;                    // Número mágico para validação
    uint32_t versao;                   // Versão do sistema de arquivos
    uint32_t total_blocos;             // Total de blocos no sistema
    uint32_t total_inodes;             // Total de inodes disponíveis
    uint32_t tamanho_bloco;            // Tamanho de cada bloco
    uint32_t blocos_livres;            // Número de blocos livres
    uint32_t inodes_livres;            // Número de inodes livres
    uint32_t bloco_bitmap_inodes;      // Bloco onde começa bitmap de inodes
    uint32_t bloco_bitmap_blocos;      // Bloco onde começa bitmap de blocos
    uint32_t bloco_tabela_inodes;      // Bloco onde começa tabela de inodes
    uint32_t bloco_dados_inicio;       // Primeiro bloco de dados
    uint32_t inode_raiz;               // Inode do diretório raiz
    time_t timestamp_criacao;          // Quando o sistema foi criado
} Superbloco;

// Inode - Metadados de um arquivo ou diretório
typedef struct {
    uint16_t tipo;                           // Tipo do arquivo
    uint16_t permissoes;                     // Permissões (rwx)
    uint32_t tamanho;                        // Tamanho em bytes
    uint32_t blocos_alocados;                // Número de blocos alocados
    time_t timestamp_criacao;                // Data de criação
    time_t timestamp_modificacao;            // Última modificação
    time_t timestamp_acesso;                 // Último acesso
    uint32_t ponteiros_diretos[NUM_PONTEIROS_DIRETOS];    // Ponteiros diretos
} Inode;

// Entrada de diretório
typedef struct {
    uint32_t inode_num;                      // Número do inode
    uint16_t tamanho_nome;                   // Tamanho do nome
    uint8_t tipo_arquivo;                    // Tipo do arquivo
    char nome[MAX_NOME_ARQUIVO];             // Nome do arquivo
} EntradaDiretorio;

// Bloco de dados genérico
typedef struct {
    uint32_t numero;                         // Número do bloco
    bool em_uso;                             // Se está em uso
    uint32_t bytes_usados;                   // Bytes utilizados
    char dados[TAMANHO_BLOCO - 12];          // Dados (descontando metadados)
} Bloco;

// Estrutura principal do sistema
typedef struct {
    Superbloco superbloco;                   // Superbloco
    bool bitmap_inodes[TOTAL_INODES];        // Bitmap de inodes
    bool bitmap_blocos[TOTAL_BLOCOS];        // Bitmap de blocos
    Inode tabela_inodes[TOTAL_INODES];       // Tabela de inodes
    Bloco blocos[TOTAL_BLOCOS];              // Todos os blocos
    uint32_t diretorio_atual;                // Inode do diretório atual
    bool sistema_montado;                    // Se o sistema está montado
    char caminho_atual[256];                 // Caminho atual
} SistemaArquivos;

// === VARIÁVEIS GLOBAIS ===
static SistemaArquivos fs;

// === DECLARAÇÕES DE FUNÇÕES ===
time_t obter_timestamp();
void timestamp_para_string(time_t timestamp, char *buffer, size_t tamanho);
uint32_t alocar_inode();
void liberar_inode(uint32_t inode_num);
uint32_t alocar_bloco();
void liberar_bloco(uint32_t bloco_num);
int ler_dados_inode(uint32_t inode_num, char *buffer, uint32_t tamanho);
int escrever_dados_inode(uint32_t inode_num, const char *dados, uint32_t tamanho);
uint32_t buscar_entrada_diretorio(uint32_t inode_dir, const char *nome);
int adicionar_entrada_diretorio(uint32_t inode_dir, const char *nome, uint32_t inode_filho, uint8_t tipo);
int remover_entrada_diretorio(uint32_t inode_dir, const char *nome);
int salvar_sistema_disco();
int carregar_sistema_disco();
void montar_sistema();

// === FUNÇÕES AUXILIARES ===

// Obtém timestamp atual
time_t obter_timestamp() {
    return time(NULL);
}

// Converte timestamp para string legível
void timestamp_para_string(time_t timestamp, char *buffer, size_t tamanho) {
    struct tm *tm_info = localtime(&timestamp);
    strftime(buffer, tamanho, "%d/%m/%Y %H:%M:%S", tm_info);
}

// === GERENCIAMENTO DE RECURSOS ===

// Aloca um inode livre
uint32_t alocar_inode() {
    for (uint32_t i = 1; i < TOTAL_INODES; i++) {
        if (!fs.bitmap_inodes[i]) {
            fs.bitmap_inodes[i] = true;
            fs.superbloco.inodes_livres--;
            
            // Inicializa o inode
            memset(&fs.tabela_inodes[i], 0, sizeof(Inode));
            fs.tabela_inodes[i].timestamp_criacao = obter_timestamp();
            fs.tabela_inodes[i].timestamp_modificacao = obter_timestamp();
            fs.tabela_inodes[i].timestamp_acesso = obter_timestamp();
            
            printf("[DEBUG] Inode %u alocado\n", i);
            return i;
        }
    }
    return 0; // Sem inodes livres
}

// Libera um inode
void liberar_inode(uint32_t inode_num) {
    if (inode_num > 0 && inode_num < TOTAL_INODES && fs.bitmap_inodes[inode_num]) {
        fs.bitmap_inodes[inode_num] = false;
        fs.superbloco.inodes_livres++;
        memset(&fs.tabela_inodes[inode_num], 0, sizeof(Inode));
        printf("[DEBUG] Inode %u liberado\n", inode_num);
    }
}

// Aloca um bloco livre
uint32_t alocar_bloco() {
    for (uint32_t i = fs.superbloco.bloco_dados_inicio; i < TOTAL_BLOCOS; i++) {
        if (!fs.bitmap_blocos[i]) {
            fs.bitmap_blocos[i] = true;
            fs.superbloco.blocos_livres--;
            
            // Inicializa o bloco
            fs.blocos[i].numero = i;
            fs.blocos[i].em_uso = true;
            fs.blocos[i].bytes_usados = 0;
            memset(fs.blocos[i].dados, 0, TAMANHO_BLOCO - 12);
            
            printf("[DEBUG] Bloco %u alocado\n", i);
            return i;
        }
    }
    return 0; // Sem blocos livres
}

// Libera um bloco
void liberar_bloco(uint32_t bloco_num) {
    if (bloco_num >= fs.superbloco.bloco_dados_inicio && 
        bloco_num < TOTAL_BLOCOS && fs.bitmap_blocos[bloco_num]) {
        fs.bitmap_blocos[bloco_num] = false;
        fs.superbloco.blocos_livres++;
        
        memset(&fs.blocos[bloco_num], 0, sizeof(Bloco));
        printf("[DEBUG] Bloco %u liberado\n", bloco_num);
    }
}

// === OPERAÇÕES COM ARQUIVOS ===

// Lê dados de um inode
int ler_dados_inode(uint32_t inode_num, char *buffer, uint32_t tamanho) {
    if (inode_num >= TOTAL_INODES || !fs.bitmap_inodes[inode_num]) {
        return -1;
    }
    
    Inode *inode = &fs.tabela_inodes[inode_num];
    uint32_t bytes_lidos = 0;
    uint32_t bytes_para_ler = (tamanho < inode->tamanho) ? tamanho : inode->tamanho;
    
    // Lê dos ponteiros diretos
    for (int i = 0; i < NUM_PONTEIROS_DIRETOS && bytes_lidos < bytes_para_ler; i++) {
        if (inode->ponteiros_diretos[i] == 0) break;
        
        uint32_t bloco_num = inode->ponteiros_diretos[i];
        uint32_t bytes_neste_bloco = bytes_para_ler - bytes_lidos;
        if (bytes_neste_bloco > fs.blocos[bloco_num].bytes_usados) {
            bytes_neste_bloco = fs.blocos[bloco_num].bytes_usados;
        }
        
        memcpy(buffer + bytes_lidos, fs.blocos[bloco_num].dados, bytes_neste_bloco);
        bytes_lidos += bytes_neste_bloco;
    }
    
    // Atualiza timestamp de acesso
    inode->timestamp_acesso = obter_timestamp();
    
    return bytes_lidos;
}

// Escreve dados em um inode
int escrever_dados_inode(uint32_t inode_num, const char *dados, uint32_t tamanho) {
    if (inode_num >= TOTAL_INODES || !fs.bitmap_inodes[inode_num]) {
        return -1;
    }
    
    Inode *inode = &fs.tabela_inodes[inode_num];
    
    // Libera blocos antigos
    for (int i = 0; i < NUM_PONTEIROS_DIRETOS; i++) {
        if (inode->ponteiros_diretos[i] != 0) {
            liberar_bloco(inode->ponteiros_diretos[i]);
            inode->ponteiros_diretos[i] = 0;
        }
    }
    
    // Calcula blocos necessários
    uint32_t blocos_necessarios = (tamanho + (TAMANHO_BLOCO - 12) - 1) / (TAMANHO_BLOCO - 12);
    
    if (blocos_necessarios > NUM_PONTEIROS_DIRETOS) {
        printf("Erro: Arquivo muito grande para ponteiros diretos.\n");
        return -1;
    }
    
    // Aloca e escreve novos blocos
    uint32_t bytes_escritos = 0;
    const char *ptr_dados = dados;
    
    for (uint32_t i = 0; i < blocos_necessarios; i++) {
        uint32_t bloco_num = alocar_bloco();
        if (bloco_num == 0) {
            printf("Erro: Sem blocos livres.\n");
            return -1;
        }
        
        inode->ponteiros_diretos[i] = bloco_num;
        
        uint32_t bytes_neste_bloco = tamanho - bytes_escritos;
        if (bytes_neste_bloco > TAMANHO_BLOCO - 12) {
            bytes_neste_bloco = TAMANHO_BLOCO - 12;
        }
        
        memcpy(fs.blocos[bloco_num].dados, ptr_dados, bytes_neste_bloco);
        fs.blocos[bloco_num].bytes_usados = bytes_neste_bloco;
        
        ptr_dados += bytes_neste_bloco;
        bytes_escritos += bytes_neste_bloco;
    }
    
    // Atualiza metadados do inode
    inode->tamanho = tamanho;
    inode->blocos_alocados = blocos_necessarios;
    inode->timestamp_modificacao = obter_timestamp();
    
    return bytes_escritos;
}

// Busca entrada em diretório
uint32_t buscar_entrada_diretorio(uint32_t inode_dir, const char *nome) {
    if (inode_dir >= TOTAL_INODES || !fs.bitmap_inodes[inode_dir]) {
        return 0;
    }
    
    Inode *inode = &fs.tabela_inodes[inode_dir];
    if (inode->tipo != TIPO_DIRETORIO) {
        return 0;
    }
    
    // Lê conteúdo do diretório
    char buffer[TAMANHO_BLOCO * NUM_PONTEIROS_DIRETOS];
    int bytes_lidos = ler_dados_inode(inode_dir, buffer, sizeof(buffer));
    
    if (bytes_lidos <= 0) return 0;
    
    // Procura pela entrada
    char *ptr = buffer;
    while (ptr < buffer + bytes_lidos) {
        EntradaDiretorio *entrada = (EntradaDiretorio*)ptr;
        
        if (strcmp(entrada->nome, nome) == 0) {
            return entrada->inode_num;
        }
        
        ptr += sizeof(EntradaDiretorio);
    }
    
    return 0; // Não encontrado
}

// Adiciona entrada em diretório
int adicionar_entrada_diretorio(uint32_t inode_dir, const char *nome, uint32_t inode_filho, uint8_t tipo) {
    if (inode_dir >= TOTAL_INODES || !fs.bitmap_inodes[inode_dir]) {
        return -1;
    }
    
    Inode *inode = &fs.tabela_inodes[inode_dir];
    if (inode->tipo != TIPO_DIRETORIO) {
        return -1;
    }
    
    // Verifica se já existe
    if (buscar_entrada_diretorio(inode_dir, nome) != 0) {
        printf("Erro: Entrada '%s' já existe no diretório.\n", nome);
        return -1;
    }
    
    // Lê conteúdo atual
    char buffer[TAMANHO_BLOCO * NUM_PONTEIROS_DIRETOS];
    int bytes_atuais = ler_dados_inode(inode_dir, buffer, sizeof(buffer));
    if (bytes_atuais < 0) bytes_atuais = 0;
    
    // Cria nova entrada
    EntradaDiretorio nova_entrada;
    nova_entrada.inode_num = inode_filho;
    nova_entrada.tamanho_nome = strlen(nome);
    nova_entrada.tipo_arquivo = tipo;
    strncpy(nova_entrada.nome, nome, MAX_NOME_ARQUIVO - 1);
    nova_entrada.nome[MAX_NOME_ARQUIVO - 1] = '\0';
    
    // Adiciona nova entrada
    memcpy(buffer + bytes_atuais, &nova_entrada, sizeof(EntradaDiretorio));
    bytes_atuais += sizeof(EntradaDiretorio);
    
    // Escreve de volta
    return escrever_dados_inode(inode_dir, buffer, bytes_atuais);
}

// Remove entrada de diretório
int remover_entrada_diretorio(uint32_t inode_dir, const char *nome) {
    if (inode_dir >= TOTAL_INODES || !fs.bitmap_inodes[inode_dir]) {
        return -1;
    }
    
    Inode *inode = &fs.tabela_inodes[inode_dir];
    if (inode->tipo != TIPO_DIRETORIO) {
        return -1;
    }
    
    // Lê conteúdo do diretório
    char buffer[TAMANHO_BLOCO * NUM_PONTEIROS_DIRETOS];
    int bytes_lidos = ler_dados_inode(inode_dir, buffer, sizeof(buffer));
    
    if (bytes_lidos <= 0) return -1;
    
    // Procura pela entrada e remove
    char *ptr = buffer;
    bool encontrado = false;
    
    while (ptr < buffer + bytes_lidos) {
        EntradaDiretorio *entrada = (EntradaDiretorio*)ptr;
        
        if (strcmp(entrada->nome, nome) == 0) {
            // Remove a entrada movendo o resto do buffer
            size_t bytes_restantes = (buffer + bytes_lidos) - (ptr + sizeof(EntradaDiretorio));
            memmove(ptr, ptr + sizeof(EntradaDiretorio), bytes_restantes);
            bytes_lidos -= sizeof(EntradaDiretorio);
            encontrado = true;
            break;
        }
        
        ptr += sizeof(EntradaDiretorio);
    }
    
    if (!encontrado) {
        return -1;
    }
    
    // Escreve de volta o diretório atualizado
    return escrever_dados_inode(inode_dir, buffer, bytes_lidos);
}

// === OPERAÇÕES DO SISTEMA ===

// Formata o sistema de arquivos
void formatar_sistema() {
    printf("Formatando Sistema de Arquivos Simplificado...\n");
    
    // Inicializa estruturas
    memset(&fs, 0, sizeof(SistemaArquivos));
    
    // Configura superbloco
    fs.superbloco.magic = MAGIC_NUMBER;
    fs.superbloco.versao = 1;
    fs.superbloco.total_blocos = TOTAL_BLOCOS;
    fs.superbloco.total_inodes = TOTAL_INODES;
    fs.superbloco.tamanho_bloco = TAMANHO_BLOCO;
    fs.superbloco.blocos_livres = TOTAL_BLOCOS - 100; // Reserva espaço para metadados
    fs.superbloco.inodes_livres = TOTAL_INODES - 1;   // Reserva inode 0
    fs.superbloco.bloco_bitmap_inodes = 1;
    fs.superbloco.bloco_bitmap_blocos = 5;
    fs.superbloco.bloco_tabela_inodes = 10;
    fs.superbloco.bloco_dados_inicio = 100;
    fs.superbloco.timestamp_criacao = obter_timestamp();
    
    // Marca blocos de sistema como ocupados
    for (uint32_t i = 0; i < fs.superbloco.bloco_dados_inicio; i++) {
        fs.bitmap_blocos[i] = true;
    }
    
    // Cria diretório raiz
    uint32_t inode_raiz = alocar_inode();
    fs.superbloco.inode_raiz = inode_raiz;
    fs.diretorio_atual = inode_raiz;
    strcpy(fs.caminho_atual, "/");
    
    Inode *raiz = &fs.tabela_inodes[inode_raiz];
    raiz->tipo = TIPO_DIRETORIO;
    raiz->permissoes = 0755;
    raiz->tamanho = 0;
    
    // Adiciona entradas . e ..
    adicionar_entrada_diretorio(inode_raiz, ".", inode_raiz, TIPO_DIRETORIO);
    adicionar_entrada_diretorio(inode_raiz, "..", inode_raiz, TIPO_DIRETORIO);
    
    fs.sistema_montado = true;
    
    printf("Sistema formatado com sucesso!\n");
    printf("- Total de blocos: %u\n", fs.superbloco.total_blocos);
    printf("- Total de inodes: %u\n", fs.superbloco.total_inodes);
    printf("- Tamanho do bloco: %u bytes\n", fs.superbloco.tamanho_bloco);
    printf("- Espaço total: %.2f MB\n", 
           (float)(fs.superbloco.total_blocos * fs.superbloco.tamanho_bloco) / (1024*1024));
    
    // Salva o sistema formatado no disco
    salvar_sistema_disco();
}

// Cria um arquivo
void criar_arquivo(const char *nome) {
    printf("Criando arquivo '%s'...\n", nome);
    
    if (!fs.sistema_montado) {
        printf("Erro: Sistema não montado.\n");
        return;
    }
    
    // Verifica se já existe
    if (buscar_entrada_diretorio(fs.diretorio_atual, nome) != 0) {
        printf("Erro: Arquivo '%s' já existe.\n", nome);
        return;
    }
    
    // Aloca inode para o arquivo
    uint32_t inode_num = alocar_inode();
    if (inode_num == 0) {
        printf("Erro: Sem inodes livres.\n");
        return;
    }
    
    // Configura inode
    Inode *inode = &fs.tabela_inodes[inode_num];
    inode->tipo = TIPO_ARQUIVO_REGULAR;
    inode->permissoes = 0644;
    inode->tamanho = 0;
    inode->blocos_alocados = 0;
    
    // Adiciona ao diretório atual
    if (adicionar_entrada_diretorio(fs.diretorio_atual, nome, inode_num, TIPO_ARQUIVO_REGULAR) < 0) {
        liberar_inode(inode_num);
        printf("Erro: Não foi possível adicionar arquivo ao diretório.\n");
        return;
    }
    
    printf("Arquivo '%s' criado com sucesso (inode %u).\n", nome, inode_num);
    
    // Salva mudanças no disco
    salvar_sistema_disco();
}

// Escreve em um arquivo
void escrever_arquivo(const char *nome, const char *dados) {
    printf("Escrevendo no arquivo '%s'...\n", nome);
    
    if (!fs.sistema_montado) {
        printf("Erro: Sistema não montado.\n");
        return;
    }
    
    uint32_t inode_num = buscar_entrada_diretorio(fs.diretorio_atual, nome);
    if (inode_num == 0) {
        printf("Erro: Arquivo '%s' não encontrado.\n", nome);
        return;
    }
    
    Inode *inode = &fs.tabela_inodes[inode_num];
    if (inode->tipo != TIPO_ARQUIVO_REGULAR) {
        printf("Erro: '%s' não é um arquivo regular.\n", nome);
        return;
    }
    
    int resultado = escrever_dados_inode(inode_num, dados, strlen(dados));
    if (resultado < 0) {
        printf("Erro: Falha ao escrever dados.\n");
        return;
    }
    
    printf("Dados escritos com sucesso (%d bytes, %u blocos).\n", 
           resultado, inode->blocos_alocados);
    
    // Salva mudanças no disco
    salvar_sistema_disco();
}

// Lê um arquivo
void ler_arquivo(const char *nome) {
    printf("Lendo arquivo '%s':\n", nome);
    
    if (!fs.sistema_montado) {
        printf("Erro: Sistema não montado.\n");
        return;
    }
    
    uint32_t inode_num = buscar_entrada_diretorio(fs.diretorio_atual, nome);
    if (inode_num == 0) {
        printf("Erro: Arquivo '%s' não encontrado.\n", nome);
        return;
    }
    
    Inode *inode = &fs.tabela_inodes[inode_num];
    if (inode->tipo != TIPO_ARQUIVO_REGULAR) {
        printf("Erro: '%s' não é um arquivo regular.\n", nome);
        return;
    }
    
    if (inode->tamanho == 0) {
        printf("Arquivo vazio.\n");
        return;
    }
    
    char buffer[TAMANHO_BLOCO * NUM_PONTEIROS_DIRETOS];
    int bytes_lidos = ler_dados_inode(inode_num, buffer, sizeof(buffer));
    
    if (bytes_lidos > 0) {
        printf("--- Conteúdo ---\n");
        buffer[bytes_lidos] = '\0';
        printf("%s\n", buffer);
        printf("--- Fim (%d bytes) ---\n", bytes_lidos);
    } else {
        printf("Erro ao ler arquivo.\n");
    }
}

// Exclui um arquivo
void excluir_arquivo(const char *nome) {
    printf("Excluindo arquivo '%s'...\n", nome);
    
    if (!fs.sistema_montado) {
        printf("Erro: Sistema não montado.\n");
        return;
    }
    
    // Não permite excluir . e ..
    if (strcmp(nome, ".") == 0 || strcmp(nome, "..") == 0) {
        printf("Erro: Não é possível excluir '%s'.\n", nome);
        return;
    }
    
    uint32_t inode_num = buscar_entrada_diretorio(fs.diretorio_atual, nome);
    if (inode_num == 0) {
        printf("Erro: Arquivo '%s' não encontrado.\n", nome);
        return;
    }
    
    Inode *inode = &fs.tabela_inodes[inode_num];
    
    // Se for diretório, verifica se está vazio
    if (inode->tipo == TIPO_DIRETORIO) {
        if (inode->tamanho > 2 * sizeof(EntradaDiretorio)) { // Mais que . e ..
            printf("Erro: Diretório '%s' não está vazio.\n", nome);
            return;
        }
    }
    
    // Libera blocos do arquivo
    for (int i = 0; i < NUM_PONTEIROS_DIRETOS; i++) {
        if (inode->ponteiros_diretos[i] != 0) {
            liberar_bloco(inode->ponteiros_diretos[i]);
        }
    }
    
    // Libera o inode
    liberar_inode(inode_num);
    
    // Remove entrada do diretório pai
    if (remover_entrada_diretorio(fs.diretorio_atual, nome) < 0) {
        printf("Erro: Falha ao remover entrada do diretório.\n");
        return;
    }
    
    printf("Arquivo '%s' excluído com sucesso.\n", nome);
    
    // Salva mudanças no disco
    salvar_sistema_disco();
}

// Lista arquivos do diretório atual
void listar_arquivos() {
    printf("Listando arquivos em '%s':\n", fs.caminho_atual);
    
    if (!fs.sistema_montado) {
        printf("Erro: Sistema não montado.\n");
        return;
    }
    
    Inode *inode_dir = &fs.tabela_inodes[fs.diretorio_atual];
    if (inode_dir->tipo != TIPO_DIRETORIO) {
        printf("Erro: Diretório atual inválido.\n");
        return;
    }
    
    // Lê conteúdo do diretório
    char buffer[TAMANHO_BLOCO * NUM_PONTEIROS_DIRETOS];
    int bytes_lidos = ler_dados_inode(fs.diretorio_atual, buffer, sizeof(buffer));
    
    if (bytes_lidos <= 0) {
        printf("Diretório vazio.\n");
        return;
    }
    
    printf("%-20s %-8s %-10s %-8s %-20s\n", 
           "Nome", "Tipo", "Tamanho", "Blocos", "Modificação");
    printf("------------------------------------------------------------------------\n");
    
    char *ptr = buffer;
    int contador = 0;
    
    while (ptr < buffer + bytes_lidos) {
        EntradaDiretorio *entrada = (EntradaDiretorio*)ptr;
        Inode *inode_entrada = &fs.tabela_inodes[entrada->inode_num];
        
        char tipo_str[10];
        switch (entrada->tipo_arquivo) {
            case TIPO_DIRETORIO: strcpy(tipo_str, "DIR"); break;
            case TIPO_ARQUIVO_REGULAR: strcpy(tipo_str, "ARQ"); break;
            default: strcpy(tipo_str, "?"); break;
        }
        
        char timestamp_str[20];
        timestamp_para_string(inode_entrada->timestamp_modificacao, timestamp_str, sizeof(timestamp_str));
        
        printf("%-20s %-8s %-10u %-8u %-20s\n",
               entrada->nome, tipo_str, inode_entrada->tamanho,
               inode_entrada->blocos_alocados, timestamp_str);
        
        contador++;
        ptr += sizeof(EntradaDiretorio);
    }
    
    printf("\nTotal: %d entradas\n", contador);
}

// Mostra informações detalhadas de um arquivo
void info_arquivo(const char *nome) {
    printf("Informações detalhadas de '%s':\n", nome);
    
    if (!fs.sistema_montado) {
        printf("Erro: Sistema não montado.\n");
        return;
    }
    
    uint32_t inode_num = buscar_entrada_diretorio(fs.diretorio_atual, nome);
    if (inode_num == 0) {
        printf("Erro: Arquivo '%s' não encontrado.\n", nome);
        return;
    }
    
    Inode *inode = &fs.tabela_inodes[inode_num];
    
    char tipo_str[20];
    switch (inode->tipo) {
        case TIPO_DIRETORIO: strcpy(tipo_str, "Diretório"); break;
        case TIPO_ARQUIVO_REGULAR: strcpy(tipo_str, "Arquivo Regular"); break;
        default: strcpy(tipo_str, "Desconhecido"); break;
    }
    
    char criacao_str[30], modificacao_str[30], acesso_str[30];
    timestamp_para_string(inode->timestamp_criacao, criacao_str, sizeof(criacao_str));
    timestamp_para_string(inode->timestamp_modificacao, modificacao_str, sizeof(modificacao_str));
    timestamp_para_string(inode->timestamp_acesso, acesso_str, sizeof(acesso_str));
    
    printf("  Inode: %u\n", inode_num);
    printf("  Tipo: %s\n", tipo_str);
    printf("  Tamanho: %u bytes\n", inode->tamanho);
    printf("  Blocos alocados: %u\n", inode->blocos_alocados);
    printf("  Permissões: %o\n", inode->permissoes);
    printf("  Criação: %s\n", criacao_str);
    printf("  Modificação: %s\n", modificacao_str);
    printf("  Acesso: %s\n", acesso_str);
    
    printf("  Ponteiros diretos:\n");
    for (int i = 0; i < NUM_PONTEIROS_DIRETOS; i++) {
        if (inode->ponteiros_diretos[i] != 0) {
            printf("    [%d] -> Bloco %u (%u bytes usados)\n", 
                   i, inode->ponteiros_diretos[i],
                   fs.blocos[inode->ponteiros_diretos[i]].bytes_usados);
        }
    }
}

// Mostra estatísticas do sistema
void estatisticas_sistema() {
    printf("Estatísticas do Sistema de Arquivos:\n");
    
    if (!fs.sistema_montado) {
        printf("Erro: Sistema não montado.\n");
        return;
    }
    
    printf("  Versão: %u\n", fs.superbloco.versao);
    printf("  Total de blocos: %u\n", fs.superbloco.total_blocos);
    printf("  Blocos livres: %u\n", fs.superbloco.blocos_livres);
    printf("  Blocos usados: %u\n", fs.superbloco.total_blocos - fs.superbloco.blocos_livres);
    printf("  Total de inodes: %u\n", fs.superbloco.total_inodes);
    printf("  Inodes livres: %u\n", fs.superbloco.inodes_livres);
    printf("  Inodes usados: %u\n", fs.superbloco.total_inodes - fs.superbloco.inodes_livres);
    printf("  Tamanho do bloco: %u bytes\n", fs.superbloco.tamanho_bloco);
    
    float espaco_total = (float)(fs.superbloco.total_blocos * fs.superbloco.tamanho_bloco) / (1024*1024);
    float espaco_livre = (float)(fs.superbloco.blocos_livres * fs.superbloco.tamanho_bloco) / (1024*1024);
    float percentual_uso = ((float)(fs.superbloco.total_blocos - fs.superbloco.blocos_livres) * 100) / fs.superbloco.total_blocos;
    
    printf("  Espaço total: %.2f MB\n", espaco_total);
    printf("  Espaço livre: %.2f MB\n", espaco_livre);
    printf("  Uso do sistema: %.1f%%\n", percentual_uso);
    
    char criacao_str[30];
    timestamp_para_string(fs.superbloco.timestamp_criacao, criacao_str, sizeof(criacao_str));
    printf("  Criado em: %s\n", criacao_str);
}

// === PERSISTÊNCIA DO SISTEMA ===

#define ARQUIVO_SISTEMA "sfs_disco.bin"

// Salva o sistema completo em arquivo binário
int salvar_sistema_disco() {
    FILE *arquivo = fopen(ARQUIVO_SISTEMA, "wb");
    if (!arquivo) {
        printf("Erro: Não foi possível salvar o sistema no disco.\n");
        return -1;
    }
    
    // Salva toda a estrutura do sistema de arquivos
    size_t bytes_escritos = fwrite(&fs, sizeof(SistemaArquivos), 1, arquivo);
    fclose(arquivo);
    
    if (bytes_escritos != 1) {
        printf("Erro: Falha ao escrever dados no disco.\n");
        return -1;
    }
    
    printf("Sistema salvo no disco com sucesso!\n");
    return 0;
}

// Carrega o sistema do arquivo binário
int carregar_sistema_disco() {
    FILE *arquivo = fopen(ARQUIVO_SISTEMA, "rb");
    if (!arquivo) {
        // Arquivo não existe, sistema não foi criado ainda
        return -1;
    }
    
    // Carrega toda a estrutura do sistema de arquivos
    size_t bytes_lidos = fread(&fs, sizeof(SistemaArquivos), 1, arquivo);
    fclose(arquivo);
    
    if (bytes_lidos != 1) {
        printf("Erro: Falha ao ler dados do disco.\n");
        return -1;
    }
    
    // Verifica se o arquivo é válido
    if (fs.superbloco.magic != MAGIC_NUMBER) {
        printf("Erro: Arquivo de sistema inválido.\n");
        return -1;
    }
    
    printf("Sistema carregado do disco com sucesso!\n");
    printf("- Inodes usados: %u\n", fs.superbloco.total_inodes - fs.superbloco.inodes_livres);
    printf("- Blocos usados: %u\n", fs.superbloco.total_blocos - fs.superbloco.blocos_livres);
    return 0;
}

// Monta o sistema (carrega do disco ou formata se necessário)
void montar_sistema() {
    printf("Montando sistema de arquivos...\n");
    
    if (carregar_sistema_disco() == 0) {
        printf("Sistema existente carregado do disco.\n");
        fs.sistema_montado = true;
    } else {
        printf("Nenhum sistema encontrado. Use 'format' para criar um novo.\n");
        fs.sistema_montado = false;
    }
}

// === INTERFACE DE USUÁRIO ===

void mostrar_ajuda() {
    printf("\nComandos do Sistema de Arquivos Simplificado:\n");
    printf("  mount         - Montar sistema existente\n");
    printf("  format        - Formatar novo sistema\n");
    printf("  ls            - Listar arquivos\n");
    printf("  create <nome> - Criar arquivo\n");
    printf("  write <nome> <dados> - Escrever em arquivo\n");
    printf("  read <nome>   - Ler arquivo\n");
    printf("  delete <nome> - Excluir arquivo\n");
    printf("  info <nome>   - Informações detalhadas\n");
    printf("  stat          - Estatísticas do sistema\n");
    printf("  save          - Salvar sistema manualmente\n");
    printf("  help          - Esta ajuda\n");
    printf("  exit          - Sair\n");
    printf("\nExemplos:\n");
    printf("  mount             # Carrega sistema do disco\n");
    printf("  create arquivo.txt\n");
    printf("  write arquivo.txt \"Olá mundo!\"\n");
    printf("  read arquivo.txt\n");
    printf("  delete arquivo.txt\n");
    printf("  info arquivo.txt\n");
}

void processar_comando(char *linha) {
    char *comando = strtok(linha, " \n");
    if (!comando) return;
    
    if (strcmp(comando, "mount") == 0) {
        montar_sistema();
    } else if (strcmp(comando, "format") == 0) {
        formatar_sistema();
    } else if (strcmp(comando, "ls") == 0) {
        listar_arquivos();
    } else if (strcmp(comando, "create") == 0) {
        char *nome = strtok(NULL, " \n");
        if (!nome) {
            printf("Uso: create <nome>\n");
        } else {
            criar_arquivo(nome);
        }
    } else if (strcmp(comando, "write") == 0) {
        char *nome = strtok(NULL, " ");
        char *dados = strtok(NULL, "\n");
        if (!nome || !dados) {
            printf("Uso: write <nome> <dados>\n");
        } else {
            // Remove aspas se existirem
            if (dados[0] == '"' && dados[strlen(dados)-1] == '"') {
                dados[strlen(dados)-1] = '\0';
                dados++;
            }
            escrever_arquivo(nome, dados);
        }
    } else if (strcmp(comando, "read") == 0) {
        char *nome = strtok(NULL, " \n");
        if (!nome) {
            printf("Uso: read <nome>\n");
        } else {
            ler_arquivo(nome);
        }
    } else if (strcmp(comando, "delete") == 0) {
        char *nome = strtok(NULL, " \n");
        if (!nome) {
            printf("Uso: delete <nome>\n");
        } else {
            excluir_arquivo(nome);
        }
    } else if (strcmp(comando, "info") == 0) {
        char *nome = strtok(NULL, " \n");
        if (!nome) {
            printf("Uso: info <nome>\n");
        } else {
            info_arquivo(nome);
        }
    } else if (strcmp(comando, "stat") == 0) {
        estatisticas_sistema();
    } else if (strcmp(comando, "save") == 0) {
        if (fs.sistema_montado) {
            salvar_sistema_disco();
        } else {
            printf("Erro: Sistema não montado.\n");
        }
    } else if (strcmp(comando, "help") == 0) {
        mostrar_ajuda();
    } else if (strcmp(comando, "exit") == 0) {
        printf("Saindo...\n");
        exit(0);
    } else {
        printf("Comando desconhecido: '%s'. Digite 'help' para ajuda.\n", comando);
    }
}

// === FUNÇÃO PRINCIPAL ===

int main() {
    char linha[1024];
    
    printf("=== SISTEMA DE ARQUIVOS SIMPLIFICADO ===\n");
    printf("Conceitos implementados:\n");
    printf("- Superbloco com metadados globais\n");
    printf("- Inodes para metadados de arquivos\n");
    printf("- Blocos de dados de tamanho fixo\n");
    printf("- Bitmaps para gerenciamento de recursos\n");
    printf("- Ponteiros diretos\n");
    printf("- Operações: criação, leitura, escrita, exclusão, listagem\n");
    printf("- PERSISTÊNCIA: dados salvos automaticamente no disco\n\n");
    
    // Tenta montar sistema existente automaticamente
    montar_sistema();
    
    if (!fs.sistema_montado) {
        printf("Digite 'format' para criar novo sistema ou 'mount' para carregar existente.\n");
    }
    printf("Digite 'help' para ver todos os comandos.\n\n");
    
    while (1) {
        printf("sfs:%s$ ", fs.caminho_atual);
        fflush(stdout);
        
        if (!fgets(linha, sizeof(linha), stdin)) {
            break;
        }
        
        processar_comando(linha);
    }
    
    return 0;
}
