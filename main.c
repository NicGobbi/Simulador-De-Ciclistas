#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define TRACK_QUANT 10

unsigned int* arrive;
unsigned int* cont;
pthread_t* threads;
unsigned int* thread_ids;
pthread_mutex_t* locks;
int** pista;


void * runner_thread(void * a)
  {
    int temp;
    int* id = (int*)a;
    fprintf(stderr, "Thread %d started\n", *id);
    while(1)
      {
        pthread_mutex_lock(locks[posi])
        pthread_mutex_lock(locks[posi+1])
        for(int i = 0; i < 500; i++)
          {
            temp++;
            usleep(1);
          }
        arrive[*id] = 1;
        fprintf(stderr, "Thread %d is finished for this round", *id);
        while(cont[*id] == 0) usleep(1000);
        cont[*id] = 0;
      }
  }

void * coordinator_thread(void * a)
  {
    int buff;
    int* worker_count = (int*)a;
    fprintf(stderr, "Thread coordinator started\n");
    while(1)
      {
        for(int i = 0; i < *worker_count; i++)
          {
            while (arrive[i] == 0);
            arrive[i] = 0;
          }
        fprintf(stderr, "ready for next run?");
        scanf("%d", &buff);
        for(int i = 0; i < *worker_count; i++) cont[i] = 1;
      }
  }

int main()
  {
    // initializations
    printf("How many worker threads?");
    int thread_count;
    int d;
    scanf("%d", &thread_count);
    printf("Track size");
    scanf("%d", &d);
    threads = malloc(sizeof(pthread_t)*thread_count);
    arrive = malloc(sizeof(int)*thread_count);
    cont = malloc(sizeof(int)*thread_count);
    thread_ids = malloc(sizeof(int)*thread_count);
    locks = malloc(sizeof(pthread_mutex_t)*d);
    pista = malloc(sizeof(int*)*d);
    for(int i = 0; i < d; i++)
      {
        pista[i] = malloc(sizeof(int)*TRACK_QUANT);
        pthread_mutex_init(&locks[i], NULL);
      }
    for(int i = 0; i < thread_count; i++)
      {
        thread_ids[i] = i;
        fprintf(stderr, "*i= %p, *tid = %p, i = %d, tid = %d\n", &i,&thread_ids[i], i, thread_ids[i]);
        pthread_create(&threads[i], NULL, runner_thread, &thread_ids[i]);
      }
    pthread_t coord;
    pthread_create(&coord, NULL, coordinator_thread, &thread_count);
    pthread_join(coord, NULL);
  }

