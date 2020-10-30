#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define TRACK_QUANT 10
#define MAX_THREADS_STARTED 5
#define EMPTY_POSITION_VALUE -1

pthread_mutex_t* locks;
int** pista;
int d;

typedef struct
{
  pthread_t thread_id;
  unsigned int runner_id;
  unsigned int arrive;
  unsigned int keep_going;
  unsigned int started;

  unsigned int pista_position;
  unsigned int pista_line;
} runner;

runner* runners;

void print_pista()
  {
    fprintf(stderr, "\n");
    for(int i = 0; i < TRACK_QUANT; i++)
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
        pthread_mutex_lock(&locks[current_position]);
        pthread_mutex_lock(&locks[possible_next_position]);
      }
    else
      {
        pthread_mutex_lock(&locks[possible_next_position]);
        pthread_mutex_lock(&locks[current_position]);
      }
    if(pista[possible_next_position][current_line] == EMPTY_POSITION_VALUE)
      {
        pista[possible_next_position][current_line] = id;
        pista[current_position][current_line] = -1;
        runners[id].pista_position = possible_next_position;
      }
    pthread_mutex_unlock(&locks[possible_next_position]);
    pthread_mutex_unlock(&locks[current_position]);
  }

void * runner_thread(void * a)
  {
    int temp;
    int* id = (int*)a;
    int posi = 0;
    //fprintf(stderr, "Thread %d started\n", *id);
    while(1)
      {
        if(runners[*id].started)
          {
            move_forward(*id);
          }
        runners[*id].arrive = 1;
        //fprintf(stderr, "Thread %d is finished for this round", *id);
        while(runners[*id].keep_going == 0) usleep(60000);
        runners[*id].keep_going = 0;
      }
  }

void * coordinator_thread(void * a)
  {
    int buff;
    int* runner_count = (int*)a;
    int started_runners = 0;
    int alternate_start_position = 0;
    fprintf(stderr, "Thread coordinator started\n");
    while(1)
      {
        for(int i = 0; i < *runner_count; i++)
          {
            while (runners[i].arrive == 0);
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
                runners[started_runners].pista_position = 0;
                runners[started_runners].pista_line = i + alternate_start_position;
                pista[0][i + alternate_start_position] = runners[started_runners++].runner_id;
                fprintf(stderr, "%d", i + alternate_start_position);
              }
            alternate_start_position = (alternate_start_position + MAX_THREADS_STARTED) % 2*MAX_THREADS_STARTED;
          }
        /* debug thread initialization */
        print_pista();
        fprintf(stderr, "ready for next run.");
        for(int i = 0; i < *runner_count; i++) runners[i].keep_going = 1;
      }
  }

int main()
  {
    // initializations
    printf("How many worker threads?");
    int thread_count;
    scanf("%d", &thread_count);
    printf("Track size");
    scanf("%d", &d);
    runners = malloc(sizeof(runner)*thread_count);
    locks = malloc(sizeof(pthread_mutex_t)*d);
    pista = malloc(sizeof(int*)*d);
    for(int i = 0; i < d; i++)
      {
        pista[i] = malloc(sizeof(int)*TRACK_QUANT);
        pthread_mutex_init(&locks[i], NULL);
        for(int j = 0; j < TRACK_QUANT; j++)
          {
            pista[i][j] = EMPTY_POSITION_VALUE;
          }
      }
    print_pista();
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
