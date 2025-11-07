#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    struct thread_data* targs = (struct thread_data *) thread_param;

    usleep(targs->wait_to_obtain_ms * 1000); // 1 sec

    pthread_mutex_lock(targs->mutex);

    usleep(targs->wait_to_release_ms * 1000);

    pthread_mutex_unlock(targs->mutex);

    targs->thread_complete_success = true;

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    struct thread_data* targs = malloc(sizeof(struct thread_data));
    if (targs == NULL) {
        ERROR_LOG("Failed to allocate memory for thread_data");
        return false;
    }
    targs->mutex = mutex;
    targs->wait_to_obtain_ms = wait_to_obtain_ms;
    targs->wait_to_release_ms = wait_to_release_ms;
    targs->thread_complete_success = false;

    int rc = pthread_create(thread, NULL, threadfunc, targs);

    if (rc != 0) {
        ERROR_LOG("Failed to create the thread!");
        free(targs);
        return false;
    }
    return true;
}

