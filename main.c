#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_RUNNERS_PER_METER 10
#define MAX_THREADS_STARTED 5
#define RANK_TABLE_ENTRIES_PER_REALLOC 5
#define EMPTY_POSITION_VALUE -1
#define QUANTUM_us 10000
#define TRACK_START 0
#define INITIAL_LAP 0

pthread_mutex_t* pista_locks;
pthread_mutex_t rank_lock;

int** pista;
int d;
int runners_left;

typedef struct
{
  pthread_t thread_id;
  unsigned int runner_id;
  unsigned int arrive;
  unsigned int keep_going;
  unsigned int started;
  unsigned int lost;

  unsigned int lap;
  unsigned int pista_position;
  unsigned int pista_line;
} runner;

runner* runners;

typedef struct node
{
  struct node* next;
  int which_runner;
  unsigned int size;
} rank;

rank* rank_table;
int rank_table_lines;

/* Funções para gerir o ranqueamento por volta da corrida */
void push_rank(rank* head, unsigned int runner_id)
  {
    rank* new_rank = malloc(sizeof(rank));
    new_rank->which_runner = (int)runner_id;
    head->size++;
    new_rank->next = head->next;
    head->next = new_rank;
  }

void print_lap_rank(rank* head)
  {
    if (head != NULL)
      {
        fprintf(stderr, " %d ", head->which_runner);
        print_lap_rank(head->next);
      }
  }
/* Fim de gestão do ranqueamento por volta da corrida */

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
void move_forward(int id) // Corredor com identificador <id> tenta se mover para a frente
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
    pthread_mutex_unlock(&pista_locks[possible_next_position]);
    pthread_mutex_unlock(&pista_locks[current_position]);
  }

void exit_race(int id) // Corredor toma ultimas ações para sair da corrida
  {
    int current_position = runners[id].pista_position;
    int current_line = runners[id].pista_line;
    pthread_mutex_lock(&pista_locks[current_position]);
    pista[current_position][current_line] = -1;
    pthread_mutex_unlock(&pista_locks[current_position]);
  }

void set_runner_rank(unsigned int id) // Corredor sinaliza para vetor de ranks que terminou a volta
  {
    pthread_mutex_lock(&rank_lock);
    if (rank_table_lines - 1 == runners[id].lap)
      {
        rank_table = realloc(rank_table, sizeof(rank)*(RANK_TABLE_ENTRIES_PER_REALLOC + rank_table_lines));
        for(int i = rank_table_lines; i < RANK_TABLE_ENTRIES_PER_REALLOC + rank_table_lines; i++)
          {
            rank_table[i].size = 0;
            rank_table[i].next = NULL;
            rank_table[i].which_runner = -1;
          }
        rank_table_lines += RANK_TABLE_ENTRIES_PER_REALLOC;
      }
    push_rank(&rank_table[runners[id].lap], id);
    runners[id].lap++;
    pthread_mutex_unlock(&rank_lock);
  }
/* Fim de funcoes para gerir os corredores */

void * runner_thread(void * a) // Thread que representa um corredor
  {
    int* id = (int*)a;
    while(1)
      {
        if(runners[*id].started)
          {
            move_forward(*id);
            if(runners[*id].pista_position == TRACK_START)
              {
                set_runner_rank(*id);
              }
          }
        runners[*id].arrive = 1;
        while(runners[*id].keep_going == 0) usleep(QUANTUM_us);
        if (runners[*id].lost)
          break;
        runners[*id].keep_going = 0;
      }
    exit_race(*id);
    runners[*id].arrive = 1;
  }

void * coordinator_thread(void * a) // Thread que faz a barreira de sincronização e toma decisões (excluir threads perdedoras por exemplo)
  {
    int current_lap = 0;
    int* runner_count = (int*)a;
    int started_runners = 0;
    int alternate_start_position = 0;
    int aux;
    fprintf(stderr, "Thread coordinator started\n");
    while(1)
      {
        for(int i = 0; i < *runner_count; i++)
          {
            while (runners[i].arrive == 0) usleep(QUANTUM_us);
            if(!runners[i].lost)
              runners[i].arrive = 0;
          }
        /* inicio da corrida */
        int threads_to_start = *runner_count - started_runners;
        if(threads_to_start > 0)
          {
            if(threads_to_start > MAX_THREADS_STARTED)
              threads_to_start = MAX_THREADS_STARTED;
            for(int i = 0; i < threads_to_start; i++)
              {
                runners[started_runners].started = 1;
                runners[started_runners].pista_position = TRACK_START;
                runners[started_runners].lap = INITIAL_LAP;
                runners[started_runners].pista_line = i + alternate_start_position;
                pista[TRACK_START][i + alternate_start_position] = runners[started_runners++].runner_id;
              }
            alternate_start_position = (alternate_start_position + MAX_THREADS_STARTED) % 2*MAX_THREADS_STARTED;
          }

        //print_pista();
        /* Se uma volta se passou */
        // fprintf(stderr, " %d ", rank_table[current_lap].size);
        // fprintf(stderr, " %d ", rank_table_lines);
        // fprintf(stderr, " %d ", runners_left);
        if(current_lap < rank_table_lines && rank_table[current_lap].size == runners_left)
          {
            fprintf(stderr, "\n");
            print_lap_rank(&rank_table[current_lap]);
            fprintf(stderr, "\n");
            int last_positioned_id = rank_table[current_lap].next->which_runner;
            runners[last_positioned_id].lost = 1;  // ultimo colocado da volta entao aviso que perdeu.
            runners[last_positioned_id].keep_going = 1;  // mando continuar.
            pthread_join(runners[last_positioned_id].thread_id, NULL);
            runners_left--;
            current_lap++;
            //scanf("%d", &aux);
          }
        if (runners_left == 0)
          break;
        //fprintf(stderr, "ready for next run.");
        for(int i = 0; i < *runner_count; i++) runners[i].keep_going = 1;
      }
  }

int main()
  {
    // initializacoes
    printf("How many worker threads?");
    int thread_count;
    scanf("%d", &thread_count);
    printf("Track size");
    scanf("%d", &d);
    runners_left = thread_count;
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
    print_pista();
    /* iniciando tabela dinamica de rankeamento */
    pthread_mutex_init(&rank_lock, NULL);
    rank_table = malloc(sizeof(rank)*RANK_TABLE_ENTRIES_PER_REALLOC);
    rank_table_lines = RANK_TABLE_ENTRIES_PER_REALLOC;
    for(int i = 0; i < RANK_TABLE_ENTRIES_PER_REALLOC; i++)
      {
        rank_table[i].size = 0;
        rank_table[i].next = NULL;
        rank_table[i].which_runner = -1;
      }
    
    /* iniciando threads corredoras*/
    for(int i = 0; i < thread_count; i++)
      {
        runners[i].runner_id = i;
        runners[i].started = 0;
        runners[i].lost = 0;
        pthread_create(&runners[i].thread_id, NULL, runner_thread, &runners[i].runner_id);
      }
    pthread_t coord;
    pthread_create(&coord, NULL, coordinator_thread, &thread_count);
    pthread_join(coord, NULL);
  }
