#ifndef PROCESSO_H
#define PROCESSO_H

typedef enum {
    PROCESSO_PRONTO,
    PROCESSO_EXECUTANDO,
    PROCESSO_BLOQUEADO,
    TERMINADO
} estado_t;

typedef enum {
    ESPERANDO_ENTRADA,
    ESPERANDO_SAIDA,
    ESPERANDO_PROCESSO,
    NULO
} tipo_bloqueio_t;

struct processo;

typedef struct processo {
    int pid;

    estado_t estado;

    struct processo *proximo_processo;

    int PC;
    int A;
    int X;
    int complemento;

    tipo_bloqueio_t tipo_bloqueio;
    int pid_prioridade;

    int QUANTUM;

} processo;

// Funções Processo
processo *processo_cria(int id, int pc);
void processo_salva_estado_cpu(processo *p, int PC, int A, int X, int complemento);
void processo_bloqueia(processo *p, tipo_bloqueio_t TIPO_BLOQUEIO, int pid_bloqueado);
void processo_desbloqueia(processo *p);

// Tabela
typedef struct {
    processo *primeiro;
    processo *ultimo;
    int id;
} tabela_processos_t;

// Funções Tabela
processo *busca_processo(tabela_processos_t *tabela, int pid);
processo *busca_processo_bloqueado(tabela_processos_t *tabela);
void inicializa_tabela_processos(tabela_processos_t *tabela);
void adiciona_processo(tabela_processos_t *tabela, processo *novo_processo);
void remove_processo_tabela(tabela_processos_t *tabela, processo *processo_remover);
void remove_primeiro_fila(tabela_processos_t *fila);

// Fila Processo
typedef struct {
    processo *primeiro;
    processo *ultimo;
    int id;
} fila_processos_t;



// Construtores Processo
void processo_salva_estado_cpu(processo *p, int PC, int A, int X, int complemento);

// Metodos Set Processo
void setPID(processo *p, int valor);
void setEstado(processo *p, estado_t valor);
void setProximoProcesso(processo *p, struct processo *valor);
void setPC(processo *p, int valor);
void setA(processo *p, int valor);
void setX(processo *p, int valor);
void setComplemento(processo *p, int valor);
void setTipoBloqueio(processo *p, tipo_bloqueio_t valor);
void setPidPrioridade(processo *p, int valor);
void setQuantum(processo *p, int valor);

// Metodos Get Processo
int getPID(processo *p);
estado_t getEstado(processo *p);
struct processo *getProximoProcesso(processo *p);
int getPC(processo *p);
int getA(processo *p);
int getX(processo *p);
int getComplemento(processo *p);
tipo_bloqueio_t getTipoBloqueio(processo *p);
int getPidPrioridade(processo *p);
int getQuantum(processo *p);




#endif // PROCESSO_H
