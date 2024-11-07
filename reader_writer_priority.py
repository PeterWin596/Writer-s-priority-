#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

volatile int *shared_var;      // Shared integer variable
sem_t *write_sem, *read_sem;   // Semaphores for write and read access
volatile int *waiting_writers; // Count of writers waiting
volatile int *active_readers;  // Count of active readers

void writer() {
    int pid = getpid();

    sem_wait(write_sem);  // Wait for exclusive access as a writer
    *shared_var *= 2;     // Update shared variable
    printf("Writer Process Id %d Writing %d\n", pid, *shared_var);
    sem_post(write_sem);  // Release write access
}

void reader() {
    int pid = getpid();

    // Increment active reader count
    sem_wait(read_sem);
    (*active_readers)++;
    if (*active_readers == 1) {
        sem_wait(write_sem);  // First reader blocks writers
    }
    sem_post(read_sem);

    // Reading shared variable
    printf("Reader's Process Id %d Reading %d\n", pid, *shared_var);

    // Decrement active reader count
    sem_wait(read_sem);
    (*active_readers)--;
    if (*active_readers == 0) {
        sem_post(write_sem);  // Last reader releases writers
    }
    sem_post(read_sem);
}

int main(int argc, char **argv) {
    int i, pid, kids, shmfd;

    // Number of readers and writers (set by command line or default)
    kids = argc > 1 ? atoi(argv[1]) : 3;

    // Setup shared memory for variables
    shmfd = shm_open("/shared_memory", O_RDWR | O_CREAT, 0666);
    if (shmfd < 0) {
        fprintf(stderr, "Could not create shared memory\n");
        exit(1);
    }
    ftruncate(shmfd, 3 * sizeof(int));  // Three shared memory integers
    shared_var = (int *)mmap(NULL, 3 * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
    if (shared_var == MAP_FAILED) {
        fprintf(stderr, "Could not map shared memory\n");
        exit(1);
    }
    close(shmfd);
    shm_unlink("/shared_memory");

    // Initialize shared variables
    shared_var[0] = 1;      // Shared integer for reading/writing
    waiting_writers = &shared_var[1];
    active_readers = &shared_var[2];
    *waiting_writers = 0;
    *active_readers = 0;

    // Setup semaphores
    write_sem = sem_open("write_sem", O_CREAT, 0666, 1);
    read_sem = sem_open("read_sem", O_CREAT, 0666, 1);
    sem_unlink("write_sem");
    sem_unlink("read_sem");

    // Create writer (parent) and reader (child) processes
    for (i = 0; i < kids; i++) {
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Fork failed\n");
            exit(1);
        } else if (pid > 0) {
            // Parent process (Writer)
            writer();
        } else {
            // Child process (Reader)
            reader();
            exit(0);
        }
    }

    // Wait for all child processes
    for (i = 0; i < kids; i++) wait(NULL);

    // Output final value of shared_var
    printf("Final value of shared_var = %d\n", *shared_var);

    return 0;
}
