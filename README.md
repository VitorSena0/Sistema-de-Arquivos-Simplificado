# Sistema-de-Arquivos-Simplificado
Este projeto foi desenvolvido com um propósito didático, aplicando os conhecimentos vistos nas aulas de Sistemas Operacionsis.



# EXPLICAÇÃO COMPLETA DO CÓDIGO - SISTEMA DE ARQUIVOS SIMPLIFICADO (SFS)

## 1. VISÃO GERAL DO SISTEMA

### O que o código faz
Este código implementa um **sistema de arquivos completo** que simula como sistemas reais (ext4, NTFS) funcionam. Ele permite:
- Criar, ler, escrever e deletar arquivos
- Organizar arquivos em diretórios
- Salvar dados no disco de forma persistente
- Gerenciar espaço livre e recursos

### Estrutura principal
Todo o sistema fica numa única estrutura chamada `SistemaArquivos`:
```c
typedef struct {
    Superbloco superbloco;           // "Cabeçalho" com informações globais
    bool bitmap_inodes[256];         // Quais inodes estão em uso
    bool bitmap_blocos[2048];        // Quais blocos estão em uso
    Inode tabela_inodes[256];        // Metadados de todos os arquivos
    Bloco blocos[2048];              // Dados reais dos arquivos
    uint32_t diretorio_atual;        // Onde estamos navegando
    bool sistema_montado;            // Sistema está pronto para uso
    char caminho_atual[256];         // Caminho atual (ex: "/")
} SistemaArquivos;
```

## 2. CONCEITOS TÉCNICOS FUNDAMENTAIS

### A. SUPERBLOCO - O "Centro de Controle"
```c
typedef struct {
    uint32_t magic;                  // 0xED123456 - Assinatura do sistema
    uint32_t versao;                 // Versão do sistema
    uint32_t total_blocos;           // 2048 blocos total
    uint32_t total_inodes;           // 256 inodes total
    uint32_t blocos_livres;          // Quantos blocos estão disponíveis
    uint32_t inodes_livres;          // Quantos inodes estão disponíveis
    uint32_t bloco_dados_inicio;     // Bloco 100 - onde começam os dados
    uint32_t inode_raiz;             // Inode do diretório raiz "/"
    time_t timestamp_criacao;        // Quando foi criado
} Superbloco;
```

**Função**: O superbloco é como o "índice geral" de uma biblioteca. Ele contém:
- **Magic number**: Verifica se o arquivo é realmente nosso sistema
- **Contadores**: Quantos recursos estão livres/ocupados
- **Layout**: Onde cada componente está localizado no disco
- **Ponto de entrada**: Onde fica o diretório raiz

### B. INODES - Metadados dos Arquivos
```c
typedef struct {
    uint16_t tipo;                   // ARQUIVO_REGULAR ou DIRETORIO
    uint16_t permissoes;             // rwx (0644, 0755, etc)
    uint32_t tamanho;                // Tamanho em bytes
    uint32_t blocos_alocados;        // Quantos blocos usa
    time_t timestamp_criacao;        // Quando foi criado
    time_t timestamp_modificacao;    // Última modificação
    time_t timestamp_acesso;         // Último acesso
    uint32_t ponteiros_diretos[10];  // Quais blocos contêm os dados
} Inode;
```

**Função**: Cada arquivo tem um inode que armazena:
- **Metadados**: Tipo, tamanho, permissões, timestamps
- **Localização**: Ponteiros para os blocos que contêm os dados reais
- **Separação**: Metadados ficam separados dos dados (eficiência)

### C. BLOCOS - Dados Reais
```c
typedef struct {
    uint32_t numero;                 // Número do bloco
    bool em_uso;                     // Se está ocupado
    uint32_t bytes_usados;           // Quantos bytes são realmente usados
    char dados[500];                 // Dados reais (512-12=500 bytes úteis)
} Bloco;
```

**Função**: Os blocos armazenam o conteúdo real dos arquivos:
- **Tamanho fixo**: 512 bytes por bloco (como páginas de um livro)
- **Fragmentação interna**: Arquivo de 10 bytes ocupa bloco inteiro
- **Ponteiros**: Inodes apontam para blocos que contêm os dados

### D. DIRETÓRIOS - Organização Hierárquica
```c
typedef struct {
    uint32_t inode_num;              // Qual inode representa este arquivo
    uint16_t tamanho_nome;           // Tamanho do nome
    uint8_t tipo_arquivo;            // ARQUIVO ou DIRETORIO
    char nome[64];                   // Nome do arquivo
} EntradaDiretorio;
```

**Função**: Diretórios são arquivos especiais que contêm:
- **Lista de arquivos**: Array de EntradaDiretorio
- **Mapeamento nome→inode**: Como encontrar arquivo pelo nome
- **Hierarquia**: Permite estrutura de pastas

## 3. LÓGICA DE FUNCIONAMENTO PASSO A PASSO

### PASSO 1: Inicialização do Sistema
```c
void formatar_sistema() {
    // 1. Limpa toda a memória
    memset(&fs, 0, sizeof(SistemaArquivos));
    
    // 2. Configura superbloco
    fs.superbloco.magic = MAGIC_NUMBER;       // Assinatura
    fs.superbloco.total_blocos = 2048;        // Tamanho total
    fs.superbloco.blocos_livres = 1948;       // 100 reservados para sistema
    
    // 3. Marca blocos do sistema como ocupados (0-99)
    for (uint32_t i = 0; i < 100; i++) {
        fs.bitmap_blocos[i] = true;
    }
    
    // 4. Cria diretório raiz
    uint32_t inode_raiz = alocar_inode();     // Aloca inode 1
    fs.superbloco.inode_raiz = inode_raiz;    // Define como raiz
    
    // 5. Configura inode da raiz
    Inode *raiz = &fs.tabela_inodes[inode_raiz];
    raiz->tipo = TIPO_DIRETORIO;
    raiz->permissoes = 0755;
    
    // 6. Adiciona entradas especiais . e ..
    adicionar_entrada_diretorio(inode_raiz, ".", inode_raiz, TIPO_DIRETORIO);
    adicionar_entrada_diretorio(inode_raiz, "..", inode_raiz, TIPO_DIRETORIO);
}
```

**O que acontece**:
1. **Zera tudo**: Limpa memória para começar do zero
2. **Configura controle**: Define limites e contadores
3. **Reserva espaço**: Blocos 0-99 para metadados do sistema
4. **Cria raiz**: Todo sistema precisa de um ponto de partida "/"
5. **Entradas especiais**: "." (atual) e ".." (pai) para navegação

### PASSO 2: Alocação de Recursos
```c
uint32_t alocar_inode() {
    // Busca primeiro inode livre (pula o 0)
    for (uint32_t i = 1; i < TOTAL_INODES; i++) {
        if (!fs.bitmap_inodes[i]) {           // Se livre
            fs.bitmap_inodes[i] = true;       // Marca como ocupado
            fs.superbloco.inodes_livres--;    // Atualiza contador
            
            // Inicializa com valores padrão
            memset(&fs.tabela_inodes[i], 0, sizeof(Inode));
            fs.tabela_inodes[i].timestamp_criacao = obter_timestamp();
            
            return i;                         // Retorna número do inode
        }
    }
    return 0; // Sem inodes livres
}
```

**Lógica dos bitmaps**:
- **Bitmap = array de boolean**: `true` = ocupado, `false` = livre
- **Busca linear**: Percorre array até achar posição livre
- **Atomicidade**: Marca como ocupado e atualiza contador junto
- **Inicialização**: Define valores padrão para novo recurso

### PASSO 3: Criação de Arquivo
```c
void criar_arquivo(const char *nome) {
    // 1. VERIFICAÇÕES
    if (!fs.sistema_montado) return;                    // Sistema montado?
    if (buscar_entrada_diretorio(..., nome) != 0) {    // Já existe?
        printf("Arquivo já existe\n");
        return;
    }
    
    // 2. ALOCA RECURSOS
    uint32_t inode_num = alocar_inode();                // Pega inode livre
    if (inode_num == 0) {
        printf("Sem inodes livres\n");
        return;
    }
    
    // 3. CONFIGURA INODE
    Inode *inode = &fs.tabela_inodes[inode_num];
    inode->tipo = TIPO_ARQUIVO_REGULAR;                 // Tipo: arquivo normal
    inode->permissoes = 0644;                           // rw-r--r--
    inode->tamanho = 0;                                 // Vazio inicialmente
    
    // 4. ADICIONA AO DIRETÓRIO PAI
    adicionar_entrada_diretorio(fs.diretorio_atual, nome, inode_num, TIPO_ARQUIVO_REGULAR);
    
    // 5. PERSISTE NO DISCO
    salvar_sistema_disco();
}
```

**Sequência lógica**:
1. **Validação**: Verifica se operação é possível
2. **Alocação**: Reserva recursos necessários (inode)
3. **Configuração**: Define metadados do arquivo
4. **Integração**: Conecta arquivo ao diretório pai
5. **Persistência**: Salva mudanças no disco

### PASSO 4: Escrita de Dados
```c
int escrever_dados_inode(uint32_t inode_num, const char *dados, uint32_t tamanho) {
    Inode *inode = &fs.tabela_inodes[inode_num];
    
    // 1. LIBERA BLOCOS ANTIGOS (sobrescrever)
    for (int i = 0; i < NUM_PONTEIROS_DIRETOS; i++) {
        if (inode->ponteiros_diretos[i] != 0) {
            liberar_bloco(inode->ponteiros_diretos[i]);
            inode->ponteiros_diretos[i] = 0;
        }
    }
    
    // 2. CALCULA BLOCOS NECESSÁRIOS
    uint32_t bytes_por_bloco = 500;  // 500 bytes úteis por bloco
    uint32_t blocos_necessarios = (tamanho + bytes_por_bloco - 1) / bytes_por_bloco;
    
    // 3. ALOCA E ESCREVE BLOCOS
    uint32_t bytes_escritos = 0;
    for (uint32_t i = 0; i < blocos_necessarios; i++) {
        uint32_t bloco_num = alocar_bloco();            // Pega bloco livre
        inode->ponteiros_diretos[i] = bloco_num;        // Conecta ao inode
        
        // Calcula quanto escrever neste bloco
        uint32_t bytes_neste_bloco = min(500, tamanho - bytes_escritos);
        
        // Copia dados para o bloco
        memcpy(fs.blocos[bloco_num].dados, dados + bytes_escritos, bytes_neste_bloco);
        fs.blocos[bloco_num].bytes_usados = bytes_neste_bloco;
        
        bytes_escritos += bytes_neste_bloco;
    }
    
    // 4. ATUALIZA METADADOS
    inode->tamanho = tamanho;
    inode->blocos_alocados = blocos_necessarios;
    inode->timestamp_modificacao = obter_timestamp();
    
    return bytes_escritos;
}
```

**Processo de escrita**:
1. **Limpeza**: Remove dados antigos (sobrescrita)
2. **Cálculo**: Determina quantos blocos serão necessários
3. **Alocação sequencial**: Pega blocos livres e conecta ao inode
4. **Cópia de dados**: Transfere dados para os blocos
5. **Atualização**: Metadados refletem nova situação

### PASSO 5: Busca e Navegação
```c
uint32_t buscar_entrada_diretorio(uint32_t inode_dir, const char *nome) {
    // 1. LÊ CONTEÚDO DO DIRETÓRIO
    char buffer[TAMANHO_BLOCO * NUM_PONTEIROS_DIRETOS];
    int bytes_lidos = ler_dados_inode(inode_dir, buffer, sizeof(buffer));
    
    // 2. PERCORRE ENTRADAS
    char *ptr = buffer;
    while (ptr < buffer + bytes_lidos) {
        EntradaDiretorio *entrada = (EntradaDiretorio*)ptr;
        
        // 3. COMPARA NOMES
        if (strcmp(entrada->nome, nome) == 0) {
            return entrada->inode_num;                  // Encontrou!
        }
        
        ptr += sizeof(EntradaDiretorio);                // Próxima entrada
    }
    
    return 0; // Não encontrado
}
```

**Lógica de busca**:
1. **Leitura**: Carrega conteúdo completo do diretório
2. **Percurso**: Examina cada entrada sequencialmente
3. **Comparação**: Usa strcmp para comparar nomes
4. **Retorno**: Devolve número do inode ou 0 (não encontrado)

### PASSO 6: Persistência Completa
```c
int salvar_sistema_disco() {
    FILE *arquivo = fopen("sfs_disco.bin", "wb");
    
    // Salva TODA a estrutura de uma vez
    size_t bytes_escritos = fwrite(&fs, sizeof(SistemaArquivos), 1, arquivo);
    fclose(arquivo);
    
    if (bytes_escritos != 1) return -1;
    return 0;
}

int carregar_sistema_disco() {
    FILE *arquivo = fopen("sfs_disco.bin", "rb");
    if (!arquivo) return -1;
    
    // Carrega TODA a estrutura de uma vez
    size_t bytes_lidos = fread(&fs, sizeof(SistemaArquivos), 1, arquivo);
    fclose(arquivo);
    
    // VALIDAÇÃO CRÍTICA
    if (fs.superbloco.magic != MAGIC_NUMBER) {
        printf("Arquivo corrompido!\n");
        return -1;
    }
    
    return 0;
}
```

**Estratégia de persistência**:
- **Serialização total**: Salva estrutura inteira de uma vez
- **Simplicidade**: Uma operação fwrite/fread
- **Atomicidade**: Ou salva tudo ou nada
- **Validação**: Magic number detecta corrupção

## 4. RELACIONAMENTO ENTRE CONCEITOS

### A. Hierarquia de Dados
```
ARQUIVO.TXT (usuário vê)
    ↓
ENTRADA DIRETÓRIO (nome="arquivo.txt", inode=5)
    ↓
INODE 5 (metadados: tamanho=1500, ponteiros=[101,102,103])
    ↓
BLOCOS 101,102,103 (dados reais: "conteúdo do arquivo...")
```

### B. Fluxo de Operações
```
1. USUÁRIO: read arquivo.txt
2. BUSCA: encontra entrada no diretório atual
3. INODE: pega metadados do inode correspondente
4. BLOCOS: lê dados dos blocos apontados pelo inode
5. RESULTADO: retorna conteúdo para o usuário
```

### C. Gerenciamento de Recursos
```
SUPERBLOCO ←→ BITMAPS ←→ RECURSOS REAIS
    ↓           ↓              ↓
Contadores   Estado      Inodes/Blocos
"50 livres"  [T,F,T,F]   [usado,livre,usado,livre]
```

## 5. EFICIÊNCIA E LIMITAÇÕES

### Pontos Fortes
- **Simplicidade**: Fácil de entender e debugar
- **Atomicidade**: Operações são sempre consistentes
- **Acesso direto**: Inode[N] = O(1), sem busca

### Limitações
- **Fragmentação interna**: Arquivo de 1 byte usa bloco de 512 bytes
- **Tamanho de arquivo**: Máximo 10 blocos = ~5KB por arquivo
- **Busca linear**: O(n) para encontrar arquivo em diretório
- **Sem cache**: Sempre relê dados do disco

### Comparação com Sistemas Reais
- **ext4**: Usa ponteiros indiretos para arquivos grandes
- **NTFS**: Cache inteligente e otimizações de performance
- **ZFS**: Copy-on-write e snapshots automáticos

## 6. CASOS DE USO TÍPICOS

### Exemplo Completo: Criar e Escrever Arquivo
```
1. format                    → Inicializa sistema
2. create teste.txt          → Aloca inode 1, adiciona ao diretório raiz
3. write teste.txt "dados"   → Aloca bloco 100, escreve dados, conecta ao inode
4. save                      → Persiste tudo no disco
5. exit                      → Sai do programa

6. ./sfs_persistente         → Executa novamente
7. mount                     → Carrega estado do disco
8. read teste.txt            → Recupera dados (persistência funcionou!)
```

Este sistema implementa todos os conceitos fundamentais de um sistema de arquivos real, de forma simplificada mas funcionalmente completa. É uma excelente base para entender como sistemas como ext4, NTFS e outros funcionam internamente.

