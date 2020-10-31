#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_RUNNERS_PER_METER 10
#define MAX_THREADS_STARTED 5
#define RANK_TABLE_ENTRIES_PER_REALLOC 100
#define EMPTY_POSITION_VALUE -1
#define QUANTUM_us 10000
#define TRACK_START 0
#define INITIAL_LAP 0

pthread_mutex_t* pista_locks;
pthread_mutex_t rank_lock;
int** pista;
int d;

typedef struct
{
  pthread_t thread_id;
  unsigned int runner_id;
  unsigned int arrive;
  unsigned int keep_going;
  unsigned int started;

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
        pista[current_position][current_line] = -1;
        runners[id].pista_position = possible_next_position;
      }
    pthread_mutex_unlock(&pista_locks[possible_next_position]);
    pthread_mutex_unlock(&pista_locks[current_position]);
  }

void set_runner_rank(unsigned int id)
  {
    pthread_mutex_lock(&rank_lock);
    push_rank(&rank_table[runners[id].lap], id);
    runners[id].lap++;
    pthread_mutex_unlock(&rank_lock);
  }

void * runner_thread(void * a)
  {
    int temp;
    int* id = (int*)a;
    while(1)
      {
        if(runners[*id].started)
          {
            move_forward(*id);
            if(runners[*id].pista_position == TRACK_START)
              {
                fprintf(stderr, "here.");
                set_runner_rank(*id);
              }
          }
        runners[*id].arrive = 1;
        while(runners[*id].keep_going == 0) usleep(QUANTUM_us);
        runners[*id].keep_going = 0;
      }
  }

void * coordinator_thread(void * a)
  {
    int aux;
    int* runner_count = (int*)a;
    int started_runners = 0;
    int alternate_start_position = 0;
    fprintf(stderr, "Thread coordinator started\n");
    while(1)
      {
        for(int i = 0; i < *runner_count; i++)
          {
            while (runners[i].arrive == 0) usleep(QUANTUM_us);
            runners[i].arrive = 0;
          }
        /* inicio da corrida */
        int threads_to_start = *runner_count - started_runners;
        //fprintf(stderr, "Threads to start %d.", threads_to_start);
        if(threads_to_start > MAX_THREADS_STARTED)
          threads_to_start = MAX_THREADS_STARTED;
        if(threads_to_start > 0)
          {
            for(int i = 0; i < threads_to_start; i++)
              {
                runners[started_runners].started = 1;
                runners[started_runners].pista_position = TRACK_START;
                runners[started_runners].lap = INITIAL_LAP;
                runners[started_runners].pista_line = i + alternate_start_position;
                pista[TRACK_START][i + alternate_start_position] = runners[started_runners++].runner_id;
                fprintf(stderr, "%d", i + alternate_start_position);
              }
            alternate_start_position = (alternate_start_position + MAX_THREADS_STARTED) % 2*MAX_THREADS_STARTED;
          }

        print_pista();
        if(rank_table[0].size == 30)
          {
            fprintf(stderr, "\n");
            print_lap_rank(&rank_table[0]);
            fprintf(stderr, "\n");
            fprintf(stderr, "size: %d\n", rank_table[0].size);
            scanf("%d", &aux);
          }

        fprintf(stderr, "ready for next run.");
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
    
    pthread_mutex_init(&rank_lock, NULL);
    rank_table = malloc(sizeof(rank)*RANK_TABLE_ENTRIES_PER_REALLOC);
    for(int i = 0; i < RANK_TABLE_ENTRIES_PER_REALLOC; i++)
      {
        rank_table[i].size = 0;
        rank_table[i].next = NULL;
        rank_table[i].which_runner = -1;
      }
    for(int i = 0; i < thread_count; i++)
      {
        runners[i].runner_id = i;
        runners[i].started = 0;
        pthread_create(&runners[i].thread_id, NULL, runner_thread, &runners[i].runner_id);
      }
    pthread_t coord;
    pthread_create(&coord, NULL, coordinator_thread, &thread_count);
    pthread_join(coord, NULL);
  }
