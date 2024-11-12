#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <sys/types.h>

#define NUM_READERS 5
#define NUM_WRITERS 3
#define NUM_ITERATIONS 5  // Define the number of read/write operations for each process

// Shared variables
volatile long long *shared_data; // Shared integer updated to long long to handle larger values
sem_t *write_priority;           // Semaphore for write access with writer priority
sem_t *reader_count_mutex;       // Mutex for managing reader count
volatile int *reader_count;      // Shared counter for readers
volatile int *writer_waiting;    // Counter for waiting writers

void writer_process() {
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        __sync_add_and_fetch(writer_waiting, 1);  // Increment writer_waiting count
        sem_wait(write_priority);                 // Writers get priority access
        __sync_sub_and_fetch(writer_waiting, 1);  // Decrement writer_waiting count

        // Write operation: multiply shared data by 2
        *shared_data *= 2;
        printf("Writer Process Id %d Writing %lld\n", getpid(), *shared_data);

        sem_post(write_priority);    // Release write access for other processes

        sleep(rand() % 3);  // Random delay to simulate work
    }
}

void reader_process() {
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        // Control access to reader count and respect writer priority
        sem_wait(reader_count_mutex);
        if (*writer_waiting > 0) {   // If any writer is waiting, don't allow new readers
            sem_post(reader_count_mutex);
            usleep(100);             // Short wait to retry, respecting writer priority
            continue;
        }
        
        (*reader_count)++;
        if (*reader_count == 1) {    // First reader blocks writers
            sem_wait(write_priority);
        }
        sem_post(reader_count_mutex);

        // Reading section
        printf("Reader's Process Id %d Reading %lld\n", getpid(), *shared_data);

        // Control access to reader count and allow writers if last reader
        sem_wait(reader_count_mutex);
        (*reader_count)--;
        if (*reader_count == 0) {    // Last reader releases writer priority
            sem_post(write_priority);
        }
        sem_post(reader_count_mutex);

        sleep(rand() % 3);  // Random delay to simulate work
    }
}

int main() {
    printf("Total number of readers and writers: %d\n", NUM_READERS + NUM_WRITERS);

    // Set up shared memory for shared_data, reader_count, and writer_waiting
    shared_data = mmap(NULL, sizeof(long long), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    reader_count = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    writer_waiting = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *shared_data = 1LL;        // Initialize shared integer as long long
    *reader_count = 0;         // Initialize reader count
    *writer_waiting = 0;       // Initialize writer waiting count

    // Initialize semaphores
    write_priority = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    reader_count_mutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_init(write_priority, 1, 1);     // Writer priority semaphore, initial value 1
    sem_init(reader_count_mutex, 1, 1); // Mutex for reader count, initial value 1

    // Create writer processes
    for (int i = 0; i < NUM_WRITERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            writer_process();
            exit(0);
        }
    }

    // Create reader processes
    for (int i = 0; i < NUM_READERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            reader_process();
            exit(0);
        }
    }

    // Wait for all child processes to finish
    for (int i = 0; i < NUM_READERS + NUM_WRITERS; i++) {
        wait(NULL);
    }

    // Cleanup
    sem_destroy(write_priority);
    sem_destroy(reader_count_mutex);
    munmap((void *)shared_data, sizeof(long long));
    munmap((void *)reader_count, sizeof(int));
    munmap((void *)writer_waiting, sizeof(int));
    munmap(write_priority, sizeof(sem_t));
    munmap(reader_count_mutex, sizeof(sem_t));

    return 0;
}
