#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>
#include <signal.h>

typedef struct {
  int BankAccount;
  sem_t mutex;
} SharedData;

SharedData *shared;
int ShmID;

pid_t processes[100];
int processCount = 0;

volatile sig_atomic_t stop = 0;

void handleSignal(int sig) {
    (void)sig;
    stop = 1;
}

void cleanup(void) {
    for (int i = 0; i < processCount; i++) {
        kill(processes[i], SIGTERM);
    }

    for (int i = 0; i < processCount; i++) {
        while (waitpid(processes[i], NULL, 0) == -1) {
            if (errno != EINTR)
                break;
        }
    }

    sem_destroy(&shared->mutex);
    shmdt(shared);
    shmctl(ShmID, IPC_RMID, NULL);
}


void DadProcess(void) {
    int localBalance, amount, num;

    srand(time(NULL) ^ getpid());

    while (!stop) {
        sleep(rand() % 6);

        if (stop)
            break;

        sem_wait(&shared->mutex);
        localBalance = shared->BankAccount;
        sem_post(&shared->mutex);

        printf("Dear Old Dad: Attempting to Check Balance\n");

        num = rand() % 100;

        if (num % 2 == 0) {

            if (localBalance < 100) {

                amount = rand() % 100 + 1;
                num = rand() % 100;

                if (num % 2 == 0) {

                    sem_wait(&shared->mutex);

                    localBalance = shared->BankAccount;
                    localBalance += amount;

                    shared->BankAccount = localBalance;

                    printf("Dear Old Dad: Deposits $%d / Balance = $%d\n",
                           amount, localBalance);

                    sem_post(&shared->mutex);
                }
                else {
                    printf("Dear Old Dad: Doesn't have any money to give\n");
                }
            }
            else {
                printf("Dear Old Dad: Thinks Student has enough Cash ($%d)\n",
                       localBalance);
            }
        }
        else {
            printf("Dear Old Dad: Last Checking Balance = $%d\n",
                   localBalance);
        }

        fflush(stdout);
    }
}

/* Lovable Mom */
void MomProcess(void) {
    int localBalance, amount;

    srand(time(NULL) ^ getpid());

    while (!stop) {
        sleep(rand() % 11);

        if (stop)
            break;

        sem_wait(&shared->mutex);
        localBalance = shared->BankAccount;
        sem_post(&shared->mutex);

        printf("Lovable Mom: Attempting to Check Balance\n");

        if (localBalance <= 100) {

            amount = rand() % 125 + 1;

            sem_wait(&shared->mutex);

            /* Recheck balance while holding the semaphore */
            localBalance = shared->BankAccount;

            if (localBalance <= 100) {

                localBalance += amount;
                shared->BankAccount = localBalance;

                printf("Lovable Mom: Deposits $%d / Balance = $%d\n",
                       amount, localBalance);
            }

            sem_post(&shared->mutex);
        }

        fflush(stdout);
    }
}

/* Poor Student */
void StudentProcess(void) {
    int localBalance, need, num;

    srand(time(NULL) ^ getpid());

    while (!stop) {
        sleep(rand() % 6);

        if (stop)
            break;

        sem_wait(&shared->mutex);
        localBalance = shared->BankAccount;
        sem_post(&shared->mutex);

        printf("Poor Student: Attempting to Check Balance\n");

        num = rand() % 100;

        if (num % 2 == 0) {

            need = rand() % 50 + 1;

            printf("Poor Student needs $%d\n", need);

            sem_wait(&shared->mutex);

            /* Read and update account atomically */
            localBalance = shared->BankAccount;

            if (need <= localBalance) {

                localBalance -= need;
                shared->BankAccount = localBalance;

                printf("Poor Student: Withdraws $%d / Balance = $%d\n",
                       need, localBalance);
            }
            else {
                printf("Poor Student: Not Enough Cash ($%d)\n",
                       localBalance);
            }

            sem_post(&shared->mutex);
        }
        else {
            printf("Poor Student: Last Checking Balance = $%d\n",
                   localBalance);
        }

        fflush(stdout);
    }
}

/* Main */
int main(int argc, char *argv[]) {
    int adults, students;
    pid_t pid;

    if (argc != 3) {
        printf("Usage: %s #Adults(1 or 2) #Students\n", argv[0]);
        exit(1);
    }

    adults = atoi(argv[1]);
    students = atoi(argv[2]);

    if ((adults < 1 || adults > 2) ||
        students < 1 || students > 98) {
        printf("Invalid number of processes\n");
        exit(1);
    }

    /* Create shared memory */
    ShmID = shmget(IPC_PRIVATE, sizeof(SharedData),
                    IPC_CREAT | 0666);

    if (ShmID < 0) {
        perror("shmget");
        exit(1);
    }

    shared = (SharedData *)shmat(ShmID, NULL, 0);

    if (shared == (void *)-1) {
        perror("shmat");
        shmctl(ShmID, IPC_RMID, NULL);
        exit(1);
    }

    /* Initialize shared bank account */
    shared->BankAccount = 0;

    /* Initialize process-shared semaphore */
    if (sem_init(&shared->mutex, 1, 1) == -1) {
        perror("sem_init");
        shmdt(shared);
        shmctl(ShmID, IPC_RMID, NULL);
        exit(1);
    }

    /* Install signal handlers in parent */
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    printf("BankAccount = %d\n", shared->BankAccount);
    printf("Starting Dad, Mom (if selected), and %d Students\n",
           students);

    fflush(stdout);

    /* Create Student processes */
    for (int i = 0; i < students; i++) {

        pid = fork();

        if (pid == 0) {
            /* Child should not run parent's cleanup */
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);

            StudentProcess();
            shmdt(shared);
            exit(0);
        }

        if (pid > 0) {
            processes[processCount++] = pid;
        }
        else {
            perror("fork");
            stop = 1;
            break;
        }
    }

    /* Create Mom if two adults were selected */
    if (!stop && adults == 2) {

        pid = fork();

        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);

            MomProcess();
            shmdt(shared);
            exit(0);
        }

        if (pid > 0) {
            processes[processCount++] = pid;
        }
        else {
            perror("fork");
            stop = 1;
        }
    }

    /* Parent becomes Dear Old Dad */
    DadProcess();

    /* Terminate children and release shared resources */
    cleanup();

    return 0;
}