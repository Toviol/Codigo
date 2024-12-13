#include <stdlib.h>
#include "processo.h"


// Funcoes Processo

processo *processo_cria(int pid, int PC) {
    processo *p = (processo*) malloc(sizeof(processo));
    p->pid = pid;
    p->estado = PROCESSO_PRONTO;
    p->proximo_processo = NULL;
    p->PC = PC;
    p->A = 0;
    p->X = 0;
    p->complemento = 0;
    p->tipo_bloqueio = NULO;
    p->pid_prioridade = -1;
    p->QUANTUM = -1;
    return p;
}

void processo_salva_estado_cpu(processo *p, int PC, int A, int X, int complemento) {
    if (p == NULL) {
        //console_printf("Erro: o ponteiro para o processo é nulo.\n");
        return;
    }
    p->PC = PC;
    p->A = A;
    p->X = X;
    p->complemento = complemento;
}

void processo_bloqueia(processo *p, tipo_bloqueio_t TIPO_BLOQUEIO, int pid_prioridade)
{
    if (p->estado==PROCESSO_EXECUTANDO)
    {
        p->estado=PROCESSO_BLOQUEADO;
        p->tipo_bloqueio = TIPO_BLOQUEIO;
        p->pid_prioridade = pid_prioridade;
    }
    else{
        //console_printf("Processo nao bloqueado pq nao estava executando");
    }
}

void processo_desbloqueia(processo *p)
{
    if (p->estado==PROCESSO_BLOQUEADO)
    {
        p->estado=PROCESSO_PRONTO;
        p->tipo_bloqueio = NULO;
    }
    else{
        //console_printf("Processo nao desbloqueado pq nao estava bloqueado");
    }
}


// Funcoes tabela

void inicializa_tabela_processos(tabela_processos_t *tabela) {
    tabela->primeiro = NULL;
    tabela->ultimo = NULL;
    tabela->id = 0;
}

void adiciona_processo(tabela_processos_t *tabela, processo *novo_processo) {

    tabela->id++;

    if (tabela->primeiro == NULL) {
        // Primeiro processo na tabela
        tabela->primeiro = novo_processo;
        tabela->ultimo = novo_processo;
    } else {
        // Adiciona ao final da lista
        tabela->ultimo->proximo_processo = novo_processo;
        tabela->ultimo = novo_processo;
    }
    novo_processo->proximo_processo = NULL; // Final da lista
}

processo *busca_processo(tabela_processos_t *tabela, int pid) {
    if (tabela == NULL || tabela->primeiro == NULL) {
        // A tabela está vazia ou inválida
        return NULL;
    }

    processo *atual = tabela->primeiro;

    // Percorre a lista para procurar o processo
    while (atual != NULL) {
        if (atual->pid == pid) {
            // Processo encontrado
            return atual;
        }
        atual = atual->proximo_processo;
    }
    
    // Processo não encontrado
    return NULL;
}

processo *busca_processo_bloqueado(tabela_processos_t *tabela) {
    if (tabela == NULL || tabela->primeiro == NULL) {
        // A tabela está vazia ou inválida
        return NULL;
    }

    processo *atual = tabela->primeiro;

    // Percorre a lista para procurar o processo
    while (atual != NULL) {
        if (atual->estado == PROCESSO_BLOQUEADO) {
            // Processo encontrado
            return atual;
        }
        atual = atual->proximo_processo;
    }

    // Processo não encontrado
    return NULL;
}

void remove_processo_tabela(tabela_processos_t *tabela, processo *processo_remover) {
    if (tabela->primeiro == NULL || processo_remover == NULL) {
        // A tabela está vazia ou o processo a remover é inválido
        return;
    }

    processo *atual = tabela->primeiro;
    processo *anterior = NULL;

    // Percorre a lista para encontrar o processo
    while (atual != NULL) {
        if (atual == processo_remover) {
            // Processo encontrado
            if (anterior == NULL) {
                // O processo a remover é o primeiro da lista
                tabela->primeiro = atual->proximo_processo;
                if (tabela->primeiro == NULL) {
                    // A tabela ficou vazia
                    tabela->ultimo = NULL;
                }
            } else {
                // Removendo um processo do meio ou fim da lista
                anterior->proximo_processo = atual->proximo_processo;
                if (atual == tabela->ultimo) {
                    // O processo a remover era o último da lista
                    tabela->ultimo = anterior;
                }
            }

            // Libera o processo, se necessário
            //atual->proximo_processo = NULL;
            return;
        }

        // Avança na lista
        anterior = atual;
        atual = atual->proximo_processo;
    }
}

void remove_primeiro_fila(tabela_processos_t *fila)
{
    remove_processo_tabela(fila, fila->primeiro);
    fila->id-=1;
}


// Gets e Sets
// Métodos Set Processo

void setPID(processo *p, int valor) {
    p->pid = valor;
}

void setEstado(processo *p, estado_t valor) {
    p->estado = valor;
}

void setProximoProcesso(processo *p, struct processo *valor) {
    p->proximo_processo = valor;
}

void setPC(processo *p, int valor) {
    p->PC = valor;
}

void setA(processo *p, int valor) {
    p->A = valor;
}

void setX(processo *p, int valor) {
    p->X = valor;
}

void setComplemento(processo *p, int valor) {
    p->complemento = valor;
}

void setTipoBloqueio(processo *p, tipo_bloqueio_t valor){
    p->tipo_bloqueio = valor;
}

void setPidPrioridade(processo *p, int valor){
    p->pid_prioridade = valor;
}

void setQuantum(processo *p, int valor){
    p->QUANTUM = valor;
}

// Métodos Get Processo
int getPID(processo *p) {
    return p->pid;
}

estado_t getEstado(processo *p) {
    return p->estado;
}

struct processo *getProximoProcesso(processo *p) {
    return p->proximo_processo;
}

int getPC(processo *p) {
    return p->PC;
}

int getA(processo *p) {
    return p->A;
}

int getX(processo *p) {
    return p->X;
}

int getComplemento(processo *p) {
    return p->complemento;
}

tipo_bloqueio_t getTipoBloqueio(processo *p){
    return p->tipo_bloqueio;
}

int getPidPrioridade(processo *p){
    return p->pid_prioridade;
}

int getQuantum(processo *p){
    return p->QUANTUM;
}