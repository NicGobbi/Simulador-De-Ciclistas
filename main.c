#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>

#define MAX_RUNNERS_PER_METER 10
#define MAX_THREADS_STARTED 5
#define RANK_TABLE_ENTRIES_PER_REALLOC 5
#define EMPTY_POSITION_VALUE -1
#define QUANTUM_us 1000
#define USE_VARIABLE_QUANTUM 1
#define TRACK_START 0
#define INITIAL_LAP 0

#define SPEED_30KM_H 1
#define SPEED_60KM_H 2
#define SPEED_90KM_H 3 // Nao foi implementada
typedef int speed;

pthread_mutex_t* pista_locks;
pthread_mutex_t rank_lock;
pthread_mutex_t broken_runner_lock;

bool debug = false;
int** pista;
int d;
int thread_count;
int runners_left;
int* per_lap_runners; // quantos corredores devem estar em cada volta (como saber se a volta acabou ou não?)

typedef struct
{
  pthread_t thread_id;
  unsigned int runner_id;
  bool arrive;
  bool keep_going;
  bool started;
  bool lost;

  unsigned long long int last_iteration;
  unsigned int last_lap;
  speed speed;
  unsigned int lap;
  unsigned int pista_position;
  unsigned int pista_line;
} runner;
/* Salva informações sobre as threads corredoreas */
runner* runners;

struct node
{
  struct node* next;
  int which_runner;
  unsigned int size;
};

typedef struct node runner_pile_t;
/* lista dinamica de pilhas que armazena o ranqueamento dos runners em cada volta (O primeiro da pilha eh o ultimo colocado)*/
runner_pile_t* per_lap_rank;
int per_lap_rank_lines;

runner_pile_t* race_early_winners; // Corredores que ganharam antes dos mais atrasados serem eliminados DELETAR
runner_pile_t* race_final_rank; // Rankeamento final da corrida
runner_pile_t* final_broken_runners; // Todos os corredores que quebraram

runner_pile_t* current_lap_broken_runners; // Corredores que quebraram por volta

unsigned int variable_quantum;

/* Funções para gerir pilhas */
void push(struct node* head, unsigned int runner_id)
  {
    struct node* new_rank = malloc(sizeof(struct node));
    new_rank->which_runner = (int)runner_id;
    head->size++;
    new_rank->next = head->next;
    head->next = new_rank;
  }

int pop(struct node* head)
  {
    if(head->size > 0)
      {
        head->size--;
        struct node* garbage = head->next;
        head->next = head->next->next;
        unsigned int runner_id = garbage->which_runner;
        free(garbage);
        return runner_id;
      }
    fprintf(stderr, "Tried to pop empty pile");
    return -1;
  }

void print_pile(struct node* head)
  {
    if (head != NULL)
      {
        fprintf(stderr, " %d ", head->which_runner);
        print_pile(head->next);
      }
  }

void print_final_rank(struct node* head)
  {
    if (head != NULL)
      {
        if(head->which_runner != -1)
          fprintf(stdout, "Corredor: %d -- tf: %llds\n", head->which_runner, (runners[head->which_runner].last_iteration*60)/1000);
        print_final_rank(head->next);
      }
  }

void print_broken_runners(struct node* head)
  {
    if (head != NULL)
      {
        if(head->which_runner != -1)
          fprintf(stdout, "Corredor: %d -- volta em que quebrou: %d\n", head->which_runner, runners[head->which_runner].last_lap);
        print_broken_runners(head->next);
      }
  }

/* Fim de gestão de pilhas */

/* Atualiza a lista de quantos corredores devem ter por volta no caso em que algum corredor quebra*/
void update_runners_per_lap(unsigned int id)
  {
    for(int i = runners[id].last_lap; i < thread_count*2; i++)
      {
        per_lap_runners[i]--;
      }
  }

void print_pista()
  {
    fprintf(stderr, "\n");
    for(int i = 0; i < MAX_RUNNERS_PER_METER; i++)
      {
        for(int j = 0; j < d; j++)
          {
            if(pista[j][i] < 10 && pista[j][i] >= 0)
              fprintf(stderr, "  %d", pista[j][i]);
            else
              fprintf(stderr, " %d", pista[j][i]);
          }
        fprintf(stderr, "\n");
      }
  }

/* Funções para gerir os corredores */
unsigned int determine_speed(int id)
  {
    double random_number = rand()/(1.0*RAND_MAX);
    if(runners[id].speed == SPEED_30KM_H)
      {
        if(random_number >= 0.2)
          runners[id].speed = SPEED_60KM_H;
      }
    else if(runners[id].speed == SPEED_60KM_H)
      {
        if(random_number >= 0.6)
          runners[id].speed = SPEED_30KM_H;
      }
    return runners[id].speed;
  }

/* Vê-se o corredor deve quebrar ou não e sinaliza para a thread coordenadora pela pilha current_lap_broken_runners*/
bool break_with_chance(unsigned int id)
  {
    double random_number = rand()/(1.0*RAND_MAX);
    if(random_number >= 0.95)
      {
        pthread_mutex_lock(&broken_runner_lock);
        push(current_lap_broken_runners, id);
        pthread_mutex_unlock(&broken_runner_lock);
        fprintf(stderr, "Corredor %d quebrou\n", id);
        return true;
      }
    return false;
  }

void move_forward(int id)
  {
    int current_position = runners[id].pista_position;
    int current_line = runners[id].pista_line;
    int possible_next_position = (current_position + 1)%d;
    if(current_position != 0)
      {
        pthread_mutex_lock(&pista_locks[current_position]);
        pthread_mutex_lock(&pista_locks[possible_next_position]);
      }
    else
      {
        pthread_mutex_lock(&pista_locks[possible_next_position]);
        pthread_mutex_lock(&pista_locks[current_position]);
      }
    if(pista[possible_next_position][current_line] == EMPTY_POSITION_VALUE)
      {
        pista[possible_next_position][current_line] = id;
        pista[current_position][current_line] = EMPTY_POSITION_VALUE;
        runners[id].pista_position = possible_next_position;
      }
    else if(runners[id].speed != SPEED_30KM_H)
      {
        for(int i = current_line + 1; i < MAX_RUNNERS_PER_METER; i++)
          {
            if(pista[possible_next_position][i] == EMPTY_POSITION_VALUE && pista[current_position][i] == EMPTY_POSITION_VALUE)
              {
                runners[id].pista_position = possible_next_position;
                runners[id].pista_line = i;
                pista[current_position][current_line] = EMPTY_POSITION_VALUE;
                pista[possible_next_position][i] = id;
                break;
              }
          }
      }
    pthread_mutex_unlock(&pista_locks[possible_next_position]);
    pthread_mutex_unlock(&pista_locks[current_position]);
  }

/* Funcao chamda por um corredor quando a thread coordenadora sinaliza que ele deve sair da corrida */
void exit_race(int id)
  {
    int current_position = runners[id].pista_position;
    int current_line = runners[id].pista_line;
    pthread_mutex_lock(&pista_locks[current_position]);
    pista[current_position][current_line] = EMPTY_POSITION_VALUE;
    pthread_mutex_unlock(&pista_locks[current_position]);
    runners[id].started = false; // para o caso em que a thread deveria ter sido eliminada antes mas acabou se apressando -> sai da corrida e espera coordenador coletar seu ranking e terminar a thread 
  }

/* Funcao chamada pela thread coordenadora para retirar um corredor da partida */
void remove_runner(int id, int current_iteration, bool lost)
  {
    if(!runners[id].lost)
      {
        runners[id].lost = true;  // Avisa que perdeu.
        runners[id].keep_going = true;  // Manda continuar.
        pthread_join(runners[id].thread_id, NULL);
        runners_left--;
        runners[id].last_iteration = current_iteration;
        runners[id].last_lap = runners[id].lap;
        if(lost) // perdeu
          {
            push(race_final_rank, id);
          }
        else // quebrou
          {
            push(final_broken_runners, id);
            update_runners_per_lap(id);
          }
      }
  }

/* Funcao chamada pelo corredor para adicionar a sua posição em determinada volta na pilha correspondente a volta */
void set_runner_rank(unsigned int id)
  {
    pthread_mutex_lock(&rank_lock);
    /* Expande lista de ponteiros que apontam para pilhas a medida que corredores avançam na quantidade de voltas */
    if (per_lap_rank_lines - 1 == runners[id].lap)
      {
        per_lap_rank = realloc(per_lap_rank, sizeof(runner_pile_t)*(RANK_TABLE_ENTRIES_PER_REALLOC + per_lap_rank_lines));
        for(int i = per_lap_rank_lines; i < RANK_TABLE_ENTRIES_PER_REALLOC + per_lap_rank_lines; i++)
          {
            per_lap_rank[i].size = 0;
            per_lap_rank[i].next = NULL;
            per_lap_rank[i].which_runner = -1;
          }
        per_lap_rank_lines += RANK_TABLE_ENTRIES_PER_REALLOC;
      }
    push(&per_lap_rank[runners[id].lap], id);
    if(per_lap_rank[runners[id].lap].size == per_lap_runners[runners[id].lap] && runners[id].lap % 2 == 1)
      exit_race(id);
    runners[id].lap++;
    pthread_mutex_unlock(&rank_lock);
  }
/* Fim de funcoes para gerir os corredores */

/* Thread que representa um corredor */
void * runner_thread(void * a)
  {
    int* id = (int*)a;
    speed current_speed = runners[*id].speed;
    unsigned long long int iterations = 0;
    while(1)
      {
        if(runners[*id].started)
          {
            switch(current_speed)
              {
                case SPEED_60KM_H:
                  move_forward(*id);
                  if(runners[*id].pista_position == TRACK_START) // uma volta se passou 
                    {
                      current_speed = determine_speed(*id);
                      if(runners[*id].lap % 6 == 0 && runners[*id].lap != 0)
                        {
                          if(break_with_chance(*id))
                            break;
                        }
                      set_runner_rank(*id);
                    }
                  break;

                case SPEED_30KM_H:
                  if(iterations % 2 == 0)
                    {
                      move_forward(*id);
                      if(runners[*id].pista_position == TRACK_START) // uma volta se passou 
                        {
                          current_speed = determine_speed(*id);
                          if(runners[*id].lap % 6 == 0 && runners[*id].lap != 0)
                            {
                              if(break_with_chance(*id))
                                break;
                            }
                          set_runner_rank(*id);
                        }
                    }
                  break;
              }
            iterations++;
          }
        runners[*id].arrive = true;          
        while(runners[*id].keep_going == false) usleep(variable_quantum);
        if (runners[*id].lost)
          break;
        runners[*id].keep_going = false;
      }
    runners[*id].arrive = true;
    exit_race(*id);
  }

/* Thread que coordena os corredores. (barreira de sincronização, retira corredores, atualiza o quantum baseado na quantidade de corredores...) */
void * coordinator_thread(void * a) 
  {
    int iterations = 0;
    int current_lap = 0;
    int* runner_count = (int*)a;
    int started_runners = 0;
    int alternate_start_position = 0;
    int aux;
    while(1)
      {
        for(int i = 0; i < *runner_count; i++)
          {
            while (runners[i].arrive == false) usleep(variable_quantum);
            if(!runners[i].lost)
              runners[i].arrive = false;
          }

        /* adicionar corredores */
        int threads_to_start = *runner_count - started_runners;
        if(threads_to_start > 0 && iterations % 2 == 0)
          {
            if(threads_to_start > MAX_THREADS_STARTED)
              threads_to_start = MAX_THREADS_STARTED;
            for(int i = 0; i < threads_to_start; i++)
              {
                runners[started_runners].started = true;
                runners[started_runners].pista_position = TRACK_START;
                runners[started_runners].lap = INITIAL_LAP;
                runners[started_runners].pista_line = i + alternate_start_position;
                pista[TRACK_START][i + alternate_start_position] = runners[started_runners++].runner_id;
              }
            alternate_start_position = (alternate_start_position + MAX_THREADS_STARTED) % 2*MAX_THREADS_STARTED;
          }
        if(debug)
          {
            print_pista();
            fprintf(stderr, "\nper_lap_runners[current_lap]: %d\n", per_lap_runners[current_lap]);
            fprintf(stderr, "\nper_lap_rank[current_lap].size: %d\n", per_lap_rank[current_lap].size);
            fprintf(stderr, "\nrunners_left: %d\n", runners_left);
            fprintf(stderr, "Volta %d: ", current_lap);
            print_pile(per_lap_rank[current_lap].next);
            fprintf(stderr, "\n");
          }

        /* Uma volta se passou. current_lap é a volta em que o último corredor está */
        if(per_lap_rank[current_lap].size == per_lap_runners[current_lap])
          {
            fprintf(stderr, "Volta %d: ", current_lap);
            print_pile(per_lap_rank[current_lap].next);
            fprintf(stderr, "per_lap_runners[current_lap]: %d, per_lap_rank[current_lap].size: %d\n", per_lap_runners[current_lap], per_lap_rank[current_lap].size);
            if(runners_left == 1 && per_lap_rank[current_lap].size != 1)
              {
                fprintf(stderr, "Erro");
                exit(1);
              }
            else if(runners_left == 1)
              {
                int last_positioned_id = per_lap_rank[current_lap].next->which_runner;
                remove_runner(last_positioned_id, iterations, true);
                break;
              }
            if(current_lap % 2 == 1)
              {
                int last_positioned_id = per_lap_rank[current_lap].next->which_runner;
                remove_runner(last_positioned_id, iterations, true);
              }
            if(USE_VARIABLE_QUANTUM)
              {
                if(runners_left == 160)
                  {
                    variable_quantum = 100;
                  }
                else if(runners_left == 300)
                  {
                    variable_quantum = 500;
                  }
              }
            current_lap++;
          }
        else if(per_lap_rank[current_lap].size >= per_lap_runners[current_lap])
          {
            pop(&per_lap_rank[current_lap]); 
          }
        /* Retira corredores que quebraram na última volta */
        while(current_lap_broken_runners->size > 0 && runners_left > 5)
          {
            unsigned int broken = pop(current_lap_broken_runners);
            remove_runner(broken, iterations, false);
          }
        iterations++;
        for(int i = 0; i < *runner_count; i++) runners[i].keep_going = true;
      }
    fprintf(stdout, "Rankeamento da corrida:\n");
    print_final_rank(race_final_rank);
    fprintf(stdout, "\nCorredores que quebraram:\n");
    print_broken_runners(final_broken_runners);
  }

int main(int argc, char *argv[])
  {
    // inicializacoes
    srand(time(NULL));
    d = atoi(argv[1]);
    thread_count = atoi(argv[2]);
    if(argv[3] != NULL && strcmp(argv[3],"-d") == 0)
      debug = true;
    runners_left = thread_count;
    variable_quantum = QUANTUM_us;
    if(USE_VARIABLE_QUANTUM)
      {
        if(thread_count > 300)
          {
            variable_quantum = 1000;
          }
        else
          {
            variable_quantum = 100;
          }
      }
    runners = malloc(sizeof(runner)*thread_count);
    pista_locks = malloc(sizeof(pthread_mutex_t)*d);
    pista = malloc(sizeof(int*)*d);
    for(int i = 0; i < d; i++)
      {
        pista[i] = malloc(sizeof(int)*MAX_RUNNERS_PER_METER);
        pthread_mutex_init(&pista_locks[i], NULL);
        for(int j = 0; j < MAX_RUNNERS_PER_METER; j++)
          {
            pista[i][j] = EMPTY_POSITION_VALUE;
          }
      }

    /* inicializando tabela dinamica de rankeamento */
    pthread_mutex_init(&rank_lock, NULL);
    per_lap_rank = malloc(sizeof(runner_pile_t)*RANK_TABLE_ENTRIES_PER_REALLOC);
    per_lap_rank_lines = RANK_TABLE_ENTRIES_PER_REALLOC;
    for(int i = 0; i < RANK_TABLE_ENTRIES_PER_REALLOC; i++)
      {
        per_lap_rank[i].size = 0;
        per_lap_rank[i].next = NULL;
        per_lap_rank[i].which_runner = -1;
      }

    /* inicializando vetor com quantidade estimada de corredores por volta */
    per_lap_runners = malloc(sizeof(int)*thread_count*2);
    for(int i = 0; i < 2*thread_count; i+=2)
      {
        per_lap_runners[i] = thread_count - i/2;
        per_lap_runners[i + 1] = thread_count - i/2;
      }

    /* iniciando pilha de broken runners por volta + mutex que garante acesso a pilha*/
    pthread_mutex_init(&broken_runner_lock, NULL);
    current_lap_broken_runners = malloc(sizeof(runner_pile_t));
    (*current_lap_broken_runners).size = 0;
    (*current_lap_broken_runners).next = NULL;
    (*current_lap_broken_runners).which_runner = -1;

    /* inicializando rank final e pilha com todos os ciclistas que quebraram */
    final_broken_runners = malloc(sizeof(runner_pile_t));
    (*final_broken_runners).size = 0;
    (*final_broken_runners).next = NULL;
    (*final_broken_runners).which_runner = -1;

    race_final_rank = malloc(sizeof(runner_pile_t));
    (*race_final_rank).size = 0;
    (*race_final_rank).next = NULL;
    (*race_final_rank).which_runner = -1;

    /* inicializando pilha de corredores que ganham antes dos mais atrasados serem eliminados */ // DELETAR
    race_early_winners = malloc(sizeof(runner_pile_t));
    (*race_early_winners).size = 0;
    (*race_early_winners).next = NULL;
    (*race_early_winners).which_runner = -1;

    /* iniciando threads corredoras*/
    for(int i = 0; i < thread_count; i++)
      {
        runners[i].runner_id = i;
        runners[i].started = false;
        runners[i].lost = false;
        runners[i].speed = SPEED_30KM_H;
        runners[i].keep_going = false;
        runners[i].arrive = false;
        pthread_create(&runners[i].thread_id, NULL, runner_thread, &runners[i].runner_id);
      }
    pthread_t coord;
    pthread_create(&coord, NULL, coordinator_thread, &thread_count);
    pthread_join(coord, NULL);
  }
