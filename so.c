// so.c
// sistema operacional
// simulador de computador
// so24b

// INCLUDES {{{1
#include "so.h"
#include "dispositivos.h"
#include "irq.h"
#include "programa.h"
#include "instrucao.h"
#include "processo.h"
#include "assert.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#define MAX_PROCESSOS 10

typedef enum {
  PROC_TERM_TECLADO        =  0,
  PROC_TERM_TECLADO_OK     =  1,
  PROC_TERM_TELA           =  2,
  PROC_TERM_TELA_OK        =  3,

} proc_term_t;



// CONSTANTES E TIPOS {{{1
// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50   // em instruções executadas

struct so_t {
  cpu_t *cpu;
  mem_t *mem;
  es_t *es;
  console_t *console;
  bool erro_interno;

  // t1: tabela de processos, processo corrente, pendências, etc
  tabela_processos_t tabela_processos;
  processo *processo_corrente;
  tabela_processos_t fila_processos_prontos;

  bool *dispositivos_disponiveis;
};

// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
// carrega o programa contido no arquivo na memória do processador; retorna end. inicial
static int so_carrega_programa(so_t *self, char *nome_do_executavel);
// copia para str da memória do processador, até copiar um 0 (retorna true) ou tam bytes
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);

// CRIAÇÃO {{{1

so_t *so_cria(cpu_t *cpu, mem_t *mem, es_t *es, console_t *console)
{
  so_t *self = malloc(sizeof(*self));
  if (self == NULL) return NULL;

  self->cpu = cpu;
  self->mem = mem;
  self->es = es;
  self->console = console;
  self->erro_interno = false;


  // Tabela de Processos
  console_printf("SO_CHECK: Inicializa Tabela Processos");
  inicializa_tabela_processos(&self->tabela_processos);
  self->processo_corrente = NULL;
  inicializa_tabela_processos(&self->fila_processos_prontos);

  self->dispositivos_disponiveis = malloc(4 * sizeof(bool));

  // quando a CPU executar uma instrução CHAMAC, deve chamar a função
  //   so_trata_interrupcao, com primeiro argumento um ptr para o SO
  cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

  // coloca o tratador de interrupção na memória
  // quando a CPU aceita uma interrupção, passa para modo supervisor, 
  //   salva seu estado à partir do endereço 0, e desvia para o endereço
  //   IRQ_END_TRATADOR
  // colocamos no endereço IRQ_END_TRATADOR o programa de tratamento
  //   de interrupção (escrito em asm). esse programa deve conter a 
  //   instrução CHAMAC, que vai chamar so_trata_interrupcao (como
  //   foi definido acima)
  int ender = so_carrega_programa(self, "trata_int.maq");
  if (ender != IRQ_END_TRATADOR) {
    console_printf("SO: problema na carga do programa de tratamento de interrupção");
    self->erro_interno = true;
  }

  // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
  if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) != ERR_OK) {
    console_printf("SO: problema na programação do timer");
    self->erro_interno = true;
  }

  return self;
}

void so_destroi(so_t *self)
{
  cpu_define_chamaC(self->cpu, NULL, NULL);
  free(self);
}


// Funncoes Processo
// So cria processo e adiciona na tabela de processos
static processo *so_cria_processo(so_t *self, char *arquivo)
{

  int PC = so_carrega_programa(self, arquivo);

  processo *p = processo_cria((self->tabela_processos.id)+1, PC);
  adiciona_processo(&self->tabela_processos, p);
  adiciona_processo(&self->fila_processos_prontos, p);

  return p;
}
int so_pega_terminal(processo *p, proc_term_t TERMINAL)
{
  int pid = getPID(p)-1;

  int numero_terminal = ((pid%4)*4);

  return numero_terminal+TERMINAL;
  
}

void so_bloqueia_processo(so_t *self, tipo_bloqueio_t TIPO_BLOQUEIO, int pid_prioridade)
{

  processo_bloqueia(self->processo_corrente, TIPO_BLOQUEIO, pid_prioridade);
  //self->processo_corrente->tipo_bloqueio=TIPO_BLOQUEIO;
  //tomar cuidado
  console_printf("Bloqueia proc: %d de processo: %d, Tipo bloqueio: %d", self->processo_corrente->pid, self->processo_corrente->pid_prioridade, self->processo_corrente->tipo_bloqueio);
  self->processo_corrente = NULL;
}

static void so_desbloqueia_processo(so_t *self, processo *p)
{
  console_printf("Desbloqueia proc %d de processo: %d, Tipo bloqueio: %d", p->pid, p->pid_prioridade, p->tipo_bloqueio);


  processo_desbloqueia(p);
  adiciona_processo(&self->fila_processos_prontos, p);
  //p->tipo_bloqueio=NULO;
}




// TRATAMENTO DE INTERRUPÇÃO {{{1

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self);
static int so_despacha(so_t *self);

// função a ser chamada pela CPU quando executa a instrução CHAMAC, no tratador de
//   interrupção em assembly
// essa é a única forma de entrada no SO depois da inicialização
// na inicialização do SO, a CPU foi programada para chamar esta função para executar
//   a instrução CHAMAC
// a instrução CHAMAC só deve ser executada pelo tratador de interrupção
//
// o primeiro argumento é um ponteiro para o SO, o segundo é a identificação
//   da interrupção
// o valor retornado por esta função é colocado no registrador A, e pode ser
//   testado pelo código que está após o CHAMAC. No tratador de interrupção em
//   assembly esse valor é usado para decidir se a CPU deve retornar da interrupção
//   (e executar o código de usuário) ou executar PARA e ficar suspensa até receber
//   outra interrupção
static int so_trata_interrupcao(void *argC, int reg_A)
{
  so_t *self = argC;
  irq_t irq = reg_A;
  // esse print polui bastante, recomendo tirar quando estiver com mais confiança
  console_printf("SO: recebi IRQ %d (%s)", irq, irq_nome(irq));

  // salva o estado da cpu no descritor do processo que foi interrompido
  so_salva_estado_da_cpu(self); // Processo corrente aqui é nulo
  if(self->processo_corrente==NULL)
  {
    console_printf("Processo Corrente Inexistente 1");
  }

  // faz o atendimento da interrupção
  so_trata_irq(self, irq);
  // faz o processamento independente da interrupção
  so_trata_pendencias(self);
  // escolhe o próximo processo a executar
  so_escalona(self);
  // recupera o estado do processo escolhido
  return so_despacha(self);
}

static void so_salva_estado_da_cpu(so_t *self)
{
  // t1: salva os registradores que compõem o estado da cpu no descritor do
  //   processo corrente. os valores dos registradores foram colocados pela
  //   CPU na memória, nos endereços IRQ_END_*
  // se não houver processo corrente, não faz nada

  processo *p = self->processo_corrente;

  if (p == NULL) {
    return;
  }
  else{
    int PC, A, X, complemento;
    mem_le(self->mem, IRQ_END_PC, &PC);
    mem_le(self->mem, IRQ_END_A, &A);
    mem_le(self->mem, IRQ_END_X, &X);
    mem_le(self->mem, IRQ_END_complemento, &complemento);
    processo_salva_estado_cpu(p, PC, A, X, complemento);
    console_printf("Não passou por aqui 1");
  }
  

}










static void so_trata_pendencia_entrada(so_t *self, processo *p)
{
  int estado;
  if (es_le(self->es, so_pega_terminal(p, PROC_TERM_TECLADO_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado do teclado");
    self->erro_interno = true;
    return;
  }
  if (estado == 0)
  {
    return;
  } 
  int dado;
  if (es_le(self->es, so_pega_terminal(p, PROC_TERM_TECLADO), &dado) != ERR_OK) {
    console_printf("SO: problema no acesso ao teclado");
    self->erro_interno = true;
    return;
  }
  setA(p, dado);
  so_desbloqueia_processo(self, p);
}


static void so_trata_pendencia_saida(so_t *self, processo *p)
{
  int estado;
  /////////////////////////console_printf("PID processo_atual: %d", self->processo_corrente->pid);
  if (es_le(self->es, so_pega_terminal(p, PROC_TERM_TELA_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado da tela");
    self->erro_interno = true;
    return;
  }
  if (estado == 0)
  {
    return;
  }
  int dado;
  dado = getX(p);
  if (es_escreve(self->es, so_pega_terminal(p, PROC_TERM_TELA), dado) != ERR_OK) {
    console_printf("SO: problema no acesso à tela");
    self->erro_interno = true;
    return;
  }
  setA(p, 0);
  so_desbloqueia_processo(self, p);
}


static void so_trata_pendencia_processo(so_t *self, processo *p)
{
  int pid = getPidPrioridade(p);

  processo *processo_prioridade = busca_processo(&self->tabela_processos, pid);

  if (getEstado(processo_prioridade)==TERMINADO || processo_prioridade==NULL)
  {
    so_desbloqueia_processo(self, p);
    console_printf("Desbloqueia processo %d", p->pid);

  }
}




/* static void gambiarra_benhur_libera_entrada(so_t *self, processo *p)
{

  int dispositivo_ok = so_pega_terminal(p, PROC_TERM_TECLADO_OK);
  int dispositivo = so_pega_terminal(p, PROC_TERM_TECLADO);

  for(;;)
  {
    int estado;
    if (es_le(self->es, dispositivo_ok, &estado) != ERR_OK) {
      console_printf("SO: problema no acesso ao estado do teclado");
      self->erro_interno = true;
      return;
    }
    if (estado != 0) break;

    //self->metricas->tempo_total_ocioso++;
    console_tictac(self->console);
  }

  int dado;
  if (es_le(self->es, dispositivo, &dado) != ERR_OK) {
    console_printf("SO: problema no acesso ao teclado");
    self->erro_interno = true;
    return;
  }

  setA(p, dado);
  so_desbloqueia_processo(self, p);
} */

/* static void gambiarra_benhur_libera_saida(so_t *self, processo *p)
{
  int dispositivo_ok = so_pega_terminal(p, PROC_TERM_TELA_OK);
  int dispositivo = so_pega_terminal(p, PROC_TERM_TELA);

  for(;;)
  {
    int estado;
    if (es_le(self->es, dispositivo_ok, &estado) != ERR_OK) {
      console_printf("SO: problema no acesso ao estado da tela");
      self->erro_interno = true;
      return;
    }
    if (estado != 0) break;

    //self->metricas->tempo_total_ocioso++;
    console_tictac(self->console);
  }

  int dado;
  dado = getX(p);
  if (es_escreve(self->es, dispositivo, dado) != ERR_OK) {
    console_printf("SO: problema no acesso à tela");
    self->erro_interno = true;
    return;
  }

  setA(p, 0);
  so_desbloqueia_processo(self, p);
} */






/* static void so_trata_processos_bloqueados(so_t *self)
{
  int tamanho = self->tabela_processos.id;
  processo *p = self->tabela_processos.primeiro;
  for (int i = 0; i < tamanho; i++)
  {
    if ((p == NULL) || (getEstado(p) == PROCESSO_BLOQUEADO && getTipoBloqueio(p) != ESPERANDO_PROCESSO))
    {
      break;
    }
    p = p->proximo_processo;
  }


  if (p==NULL)
  {
    return;
  }
  tipo_bloqueio_t t = getTipoBloqueio(p);
  
  

  if (t == ESPERANDO_ENTRADA)
  {
    gambiarra_benhur_libera_entrada(self, p);
  }
  if (t == ESPERANDO_SAIDA)
  {
    gambiarra_benhur_libera_saida(self, p);
  }
  
  
  
} */

static void so_trata_pendencias(so_t *self)
{
  // t1: realiza ações que não são diretamente ligadas com a interrupção que
  //   está sendo atendida:
  // - E/S pendente
  // - desbloqueio de processos
  // - contabilidades
  tabela_processos_t *tabela = &self->tabela_processos;
  if (tabela == NULL || tabela->primeiro == NULL) {
        return;
    }

  processo *atual = tabela->primeiro;

  // Percorre a lista para procurar o processo
  while (atual != NULL) {
      if (atual->estado == PROCESSO_BLOQUEADO) {
          tipo_bloqueio_t bloqueio = getTipoBloqueio(atual);
          if (bloqueio==ESPERANDO_ENTRADA)
          {
            so_trata_pendencia_entrada(self, atual);
          }
          if (bloqueio==ESPERANDO_SAIDA)
          {
            so_trata_pendencia_saida(self, atual);
          }
          if (bloqueio==ESPERANDO_PROCESSO)
          {
            so_trata_pendencia_processo(self, atual);
          }
      }
      atual = atual->proximo_processo;
  }

  /* if (self->fila_processos_prontos.primeiro==NULL && self->processo_corrente==NULL)
  {
    so_trata_processos_bloqueados(self);
  } */
}



/* static void so_escalona_quantum(so_t *self)
{
  // escolhe o próximo processo a executar, que passa a ser o processo
  //   corrente; pode continuar sendo o mesmo de antes ou não
  // t1: na primeira versão, escolhe um processo caso o processo corrente não possa continuar
  //   executando. depois, implementar escalonador melhor
  processo *p = self->processo_corrente;
  //processo *aux = self->tabela_processos.primeiro;
  if(p!=NULL && getQuantum(p)==0)
  {
    adiciona_processo(&self->fila_processos_prontos, p);
    setEstado(p, PROCESSO_PRONTO);
    p=NULL;
  }


  if (p == NULL || getEstado(p) != PROCESSO_EXECUTANDO) {
      if (self->fila_processos_prontos.primeiro!=NULL)
      {
        p = self->fila_processos_prontos.primeiro;
        remove_primeiro_fila(&self->fila_processos_prontos);
        self->processo_corrente = p;
        setQuantum(p, 10);
        setEstado(p, PROCESSO_EXECUTANDO);
        return;
      }
      

      // Se nenhum processo estiver pronto, define processo_corrente como NULL
      self->processo_corrente = NULL; 
    }
  

} */


static void so_escalona(so_t *self)
{
  // escolhe o próximo processo a executar, que passa a ser o processo
  //   corrente; pode continuar sendo o mesmo de antes ou não
  // t1: na primeira versão, escolhe um processo caso o processo corrente não possa continuar
  //   executando. depois, implementar escalonador melhor
  processo *p = self->processo_corrente;
  processo *aux = self->tabela_processos.primeiro;

  processo *aux2 = self->tabela_processos.primeiro;

  // imprime o estado de cada processo na tabela

  console_printf("-----------------------");

  while (aux2 != NULL)
  {
    console_printf("PID: %d - Estado: %d", aux2->pid, aux2->estado);
    aux2 = aux2->proximo_processo;
  }

  if (p == NULL || getEstado(p) != PROCESSO_EXECUTANDO) {
        while (aux != NULL) {
            if (aux->estado == PROCESSO_PRONTO) {
                self->processo_corrente = aux; // Atualiza o processo corrente
                setEstado(self->processo_corrente, PROCESSO_EXECUTANDO);
                return;
            }
            aux = aux->proximo_processo; // Avança na lista de processos
        }

        // Se nenhum processo estiver pronto, define processo_corrente como NULL
        self->processo_corrente = NULL; 
    }
  

}

static int so_despacha(so_t *self)
{
  // t1: se houver processo corrente, coloca o estado desse processo onde ele
  //   será recuperado pela CPU (em IRQ_END_*) e retorna 0, senão retorna 1
  // o valor retornado será o valor de retorno de CHAMAC
  if (self->erro_interno) return 1;
  else{
    processo *p = self->processo_corrente;

    if (p == NULL) {
      return 1;
    }
    else{
      int PC = getPC(p);
      int A = getA(p);
      int X = getX(p);
      int complemento = getComplemento(p);
      //nao chegou nesse print
      console_printf("PC: %d - A: %d - X: %d - complemento: %d", PC, A, X, complemento);

      mem_escreve(self->mem, IRQ_END_PC, PC);
      mem_escreve(self->mem, IRQ_END_A, A);
      mem_escreve(self->mem, IRQ_END_X, X);
      mem_escreve(self->mem, IRQ_END_complemento, complemento);
      assert(getEstado(p)==PROCESSO_EXECUTANDO);
    }
    
    return 0;
  } 
}

// TRATAMENTO DE UMA IRQ {{{1

// funções auxiliares para tratar cada tipo de interrupção
static void so_trata_irq_reset(so_t *self);
static void so_trata_irq_chamada_sistema(so_t *self);
static void so_trata_irq_err_cpu(so_t *self);
static void so_trata_irq_relogio(so_t *self);
static void so_trata_irq_desconhecida(so_t *self, int irq);

static void so_trata_irq(so_t *self, int irq)
{
  // verifica o tipo de interrupção que está acontecendo, e atende de acordo
  switch (irq) {
    case IRQ_RESET:
      so_trata_irq_reset(self);
      /* int pid =self->tabela_processos.primeiro->pid;
      int pc =self->tabela_processos.primeiro->PC;
      pc+=pid; */
      break;
    case IRQ_SISTEMA:
      so_trata_irq_chamada_sistema(self);
      break;
    case IRQ_ERR_CPU:
      so_trata_irq_err_cpu(self);
      break;
    case IRQ_RELOGIO:
      so_trata_irq_relogio(self);
      break;
    default:
      so_trata_irq_desconhecida(self, irq);
  }
}

// interrupção gerada uma única vez, quando a CPU inicializa
static void so_trata_irq_reset(so_t *self)
{
  // t1: deveria criar um processo para o init, e inicializar o estado do
  //   processador para esse processo com os registradores zerados, exceto
  //   o PC e o modo.
  // como não tem suporte a processos, está carregando os valores dos
  //   registradores diretamente para a memória, de onde a CPU vai carregar
  //   para os seus registradores quando executar a instrução RETI


  // coloca o programa init na memória

  /* int PC = so_carrega_programa(self, "init.maq");

  processo *p = processo_cria((self->tabela_processos.id)+1, PC);
  adiciona_processo(&self->tabela_processos, p); */

  processo *p = so_cria_processo(self, "init.maq");
  self->processo_corrente = p;
  console_printf("so_trata_irq_reset: PID: %d - PC: %d - A: %d - X: %d - complemento: %d", getPID(p), getPC(p), getA(p), getX(p), getComplemento(p));

  int ender = getPC(p);
  if (ender != 100) {
    console_printf("SO: problema na carga do programa inicial");
    self->erro_interno = true;
    return;
  }

  // altera o PC para o endereço de carga
  mem_escreve(self->mem, IRQ_END_PC, ender);
  // passa o processador para modo usuário
  mem_escreve(self->mem, IRQ_END_modo, usuario);
}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self)
{
  // Ocorreu um erro interno na CPU
  // O erro está codificado em IRQ_END_erro
  // Em geral, causa a morte do processo que causou o erro
  // Ainda não temos processos, causa a parada da CPU
  int err_int;
  // t1: com suporte a processos, deveria pegar o valor do registrador erro
  //   no descritor do processo corrente, e reagir de acordo com esse erro
  //   (em geral, matando o processo)
  mem_le(self->mem, IRQ_END_erro, &err_int);
  err_t err = err_int;
  console_printf("SO: IRQ não tratada -- erro na CPU: %s", err_nome(err));
  self->erro_interno = true;
}

// interrupção gerada quando o timer expira
static void so_trata_irq_relogio(so_t *self)
{
  // rearma o interruptor do relógio e reinicializa o timer para a próxima interrupção
  err_t e1, e2;
  e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0); // desliga o sinalizador de interrupção
  e2 = es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO);
  if (e1 != ERR_OK || e2 != ERR_OK) {
    console_printf("SO: problema da reinicialização do timer");
    self->erro_interno = true;
  }
  // t1: deveria tratar a interrupção
  //   por exemplo, decrementa o quantum do processo corrente, quando se tem
  //   um escalonador com quantum
  processo *p = self->processo_corrente;
  if (p!=NULL)
  {
    setQuantum(p, (getQuantum(p)-1));
  }

  console_printf("SO: interrupção do relógio (não tratada)");
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t *self, int irq)
{
  console_printf("SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
  self->erro_interno = true;
}

// CHAMADAS DE SISTEMA {{{1

// funções auxiliares para cada chamada de sistema
static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);
static void so_chamada_espera_proc(so_t *self);

static void so_trata_irq_chamada_sistema(so_t *self)
{
  // a identificação da chamada está no registrador A
  // t1: com processos, o reg A tá no descritor do processo corrente
  int id_chamada;
  if (mem_le(self->mem, IRQ_END_A, &id_chamada) != ERR_OK) {
    console_printf("SO: erro no acesso ao id da chamada de sistema");
    self->erro_interno = true;
    return;
  }
  console_printf("SO: chamada de sistema %d", id_chamada);
  switch (id_chamada) {
    case SO_LE:
      so_chamada_le(self);
      break;
    case SO_ESCR:
      so_chamada_escr(self);
      break;
    case SO_CRIA_PROC:
      so_chamada_cria_proc(self);
      break;
    case SO_MATA_PROC:
      so_chamada_mata_proc(self);
      break;
    case SO_ESPERA_PROC:
      
      so_chamada_espera_proc(self);
      
      break;
    default:
      console_printf("SO: chamada de sistema desconhecida (%d)", id_chamada);
      // t1: deveria matar o processo
      self->erro_interno = true;
  }
}

bool dispositivo_ocupado(so_t *self)
{

  int terminal = so_pega_terminal(self->processo_corrente, 0);
  if (self->dispositivos_disponiveis[terminal]==false)

  {
    return true;
  }

  self->dispositivos_disponiveis[terminal] = false;

  return false;
}



// implementação da chamada se sistema SO_LE
// faz a leitura de um dado da entrada corrente do processo, coloca o dado no reg A
static void so_chamada_le(so_t *self)
{
  // implementação com espera ocupada
  //   T1: deveria realizar a leitura somente se a entrada estiver disponível,
  //     senão, deveria bloquear o processo.
  //   no caso de bloqueio do processo, a leitura (e desbloqueio) deverá
  //     ser feita mais tarde, em tratamentos pendentes em outra interrupção,
  //     ou diretamente em uma interrupção específica do dispositivo, se for
  //     o caso
  // implementação lendo direto do terminal A
  //   T1: deveria usar dispositivo de entrada corrente do processo
  int estado;
  if (es_le(self->es, so_pega_terminal(self->processo_corrente, PROC_TERM_TECLADO_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado do teclado");
    self->erro_interno = true;
    return;
  }
  if (estado == 0)
  {
    so_bloqueia_processo(self, ESPERANDO_ENTRADA, -1);
    return;
  }    // como não está saindo do SO, a unidade de controle não está executando seu laço.
  // esta gambiarra faz pelo menos a console ser atualizada
  // T1: com a implementação de bloqueio de processo, esta gambiarra não
  //   deve mais existir.

  int dado;
  if (es_le(self->es, so_pega_terminal(self->processo_corrente, PROC_TERM_TECLADO), &dado) != ERR_OK) {
    console_printf("SO: problema no acesso ao teclado");
    self->erro_interno = true;
    return;
  }
  // escreve no reg A do processador
  // (na verdade, na posição onde o processador vai pegar o A quando retornar da int)
  // T1: se houvesse processo, deveria escrever no reg A do processo
  // T1: o acesso só deve ser feito nesse momento se for possível; se não, o processo
  //   é bloqueado, e o acesso só deve ser feito mais tarde (e o processo desbloqueado)
  setA(self->processo_corrente, dado);
}

// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
static void so_chamada_escr(so_t *self)
{
  // implementação com espera ocupada
  //   T1: deveria bloquear o processo se dispositivo ocupado
  // implementação escrevendo direto do terminal A
  //   T1: deveria usar o dispositivo de saída corrente do processo
  
  int estado;
  /////////////////////////console_printf("PID processo_atual: %d", self->processo_corrente->pid);
  if (es_le(self->es, so_pega_terminal(self->processo_corrente, PROC_TERM_TELA_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado da tela");
    self->erro_interno = true;
    return;
  }
  if (estado == 0)
  {
    so_bloqueia_processo(self, ESPERANDO_SAIDA, -1);
    return;
  }
  // como não está saindo do SO, a unidade de controle não está executando seu laço.
  // esta gambiarra faz pelo menos a console ser atualizada
  // T1: não deve mais existir quando houver suporte a processos, porque o SO não poderá
  //   executar por muito tempo, permitindo a execução do laço da unidade de controle

  int dado;
  // está lendo o valor de X e escrevendo o de A direto onde o processador colocou/vai pegar
  // T1: deveria usar os registradores do processo que está realizando a E/S
  // T1: caso o processo tenha sido bloqueado, esse acesso deve ser realizado em outra execução
  //   do SO, quando ele verificar que esse acesso já pode ser feito.
  mem_le(self->mem, IRQ_END_X, &dado);
  if (es_escreve(self->es, so_pega_terminal(self->processo_corrente, PROC_TERM_TELA), dado) != ERR_OK) {
    console_printf("SO: problema no acesso à tela");
    self->erro_interno = true;
    return;
  }
  setA(self->processo_corrente, 0);

}

// implementação da chamada se sistema SO_CRIA_PROC
// cria um processo
static void so_chamada_cria_proc(so_t *self)
{
  // ainda sem suporte a processos, carrega programa e passa a executar ele
  // quem chamou o sistema não vai mais ser executado, coitado!
  // T1: deveria criar um novo processo

  // em X está o endereço onde está o nome do arquivo
  processo *processo_atual = self->processo_corrente;
  int ender_proc = getX(processo_atual);
  // t1: deveria ler o X do descritor do processo criador
  processo *p;
  //console_printf("x: %d", getX())
  if (true) {
    char nome[100];
    if (copia_str_da_mem(100, nome, self->mem, ender_proc)) {
      p = so_cria_processo(self, nome);
  
      //int ender_carga = getPC(p);
      setA(processo_atual, getPID(p));
      return;
    }
  }
  // deveria escrever -1 (se erro) ou o PID do processo criado (se OK) no reg A
  //   do processo que pediu a criação
  setA(processo_atual, -1);
}

// implementação da chamada se sistema SO_MATA_PROC
// mata o processo com pid X (ou o processo corrente se X é 0)
static void so_chamada_mata_proc(so_t *self)
{
  // T1: deveria matar um processo
  // ainda sem suporte a processos, retorna erro -1
  console_printf("SO: SO_MATA_PROC não implementada");
  

  processo *p_corrente = self->processo_corrente;

  if (p_corrente != NULL) {
    int pid = getX(p_corrente);
    processo *p_eliminar;
    if (pid == 0)
    {
     p_eliminar = p_corrente;
    }
    else{
     p_eliminar = busca_processo(&self->tabela_processos,  pid);
    }

    if (p_eliminar!=NULL)
    {
      //mem_escreve(self->mem, IRQ_END_A, 0);
      setA(p_eliminar, 0);
      setEstado(p_eliminar, TERMINADO);
    }
    else{
      //mem_escreve(self->mem, IRQ_END_A, -1);
      setA(p_eliminar, -1);
      console_printf("SO: nao encontrado PID corresponde ao processo a ser eliminado");
    }
    
  }
}

// implementação da chamada se sistema SO_ESPERA_PROC
// espera o fim do processo com pid X
static void so_chamada_espera_proc(so_t *self)
{
  // T1: deveria bloquear o processo se for o caso (e desbloquear na morte do esperado)
  // ainda sem suporte a processos, retorna erro -1
  int pid_atual = getPID(self->processo_corrente);
  int pid = getX(self->processo_corrente);
  // T1: deveria bloquear o processo se for o caso (e desbloquear na morte do esperado)
  // ainda sem suporte a processos, retorna erro -1
  processo *processo_esperado;
  
  processo_esperado = busca_processo(&self->tabela_processos, pid);
  
  

  if(processo_esperado == NULL || pid == pid_atual)
  {
    console_printf("Caiu no NULL %d", pid);
    setA(self->processo_corrente, -1);
    return;
  }

  if (getEstado(processo_esperado) == TERMINADO)
  {
    setA(self->processo_corrente, 0);
  }
  else{
    so_bloqueia_processo(self, ESPERANDO_PROCESSO, pid);
  }
  
}

// CARGA DE PROGRAMA {{{1

// carrega o programa na memória
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, char *nome_do_executavel)
{
  // programa para executar na nossa CPU
  programa_t *prog = prog_cria(nome_do_executavel);
  if (prog == NULL) {
    console_printf("Erro na leitura do programa '%s'\n", nome_do_executavel);
    return -1;
  }

  int end_ini = prog_end_carga(prog);
  int end_fim = end_ini + prog_tamanho(prog);

  for (int end = end_ini; end < end_fim; end++) {
    if (mem_escreve(self->mem, end, prog_dado(prog, end)) != ERR_OK) {
      console_printf("Erro na carga da memória, endereco %d\n", end);
      return -1;
    }
  }

  prog_destroi(prog);
  console_printf("SO: carga de '%s' em %d-%d", nome_do_executavel, end_ini, end_fim);
  return end_ini;
}

// ACESSO À MEMÓRIA DOS PROCESSOS {{{1

// copia uma string da memória do simulador para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// T1: deveria verificar se a memória pertence ao processo
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender)
{
  for (int indice_str = 0; indice_str < tam; indice_str++) {
    int caractere;
    if (mem_le(mem, ender + indice_str, &caractere) != ERR_OK) {
      return false;
    }
    if (caractere < 0 || caractere > 255) {
      return false;
    }
    str[indice_str] = caractere;
    if (caractere == 0) {
      return true;
    }
  }
  // estourou o tamanho de str
  return false;
}

// vim: foldmethod=marker
