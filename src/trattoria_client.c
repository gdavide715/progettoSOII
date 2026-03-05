#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/sem.h>
#include <pthread.h>
#include <sys/shm.h>

#include "ipc.h"

volatile int instance_running = 0;

//struttura dati per i thread personale
typedef struct{
    int id;
    int semid;
    int speed;  
    shm_diningroom_t *sala;
    shm_blackboard_t *lavagna;
    shm_cashdesk_t *cassa;
    shm_kitchen_t *cucina;
    strategy_t strategia;
} thread_args_t;

//Serve per accedere alla lavagna in modo atomico
void toggle_blackboard(int semid, int op){      //-1 blocca, 1 sblocca
    struct sembuf sops = { .sem_num = SEMIDX_BLACKBOARD, .sem_op=op, .sem_flg=0};
    semop(semid, &sops, 1);
}

// ================================================================
// STRATEGIA REPUTATION
// Ruoli fissi: Giulia gestisce ordini per qualità massima
// ================================================================
void worker_reputation(thread_args_t *a) {
    int id = a->id;
    int n  = a->sala->tables_n;

    toggle_blackboard(a->semid, -1);

    switch (id) {
        // -------------------------------------------------------
        // Giulia (0): cuoca fissa + cameriera SOLO per prendere ordini
        // -------------------------------------------------------
        case 0:
            if (a->lavagna->cook == -1)
                a->lavagna->cook = id;
            // Ordine da prendere: TABLE_TAKEN ma cibo NON ancora pronto
            // (se food_ready il tavolo aspetta consegna, non ordine)
            for (int i = 0; i < n; i++) {
                if (a->sala->tables[i].state == TABLE_TAKEN &&
                    !a->cucina->food_ready[i]              &&
                    a->lavagna->tables[i].waiter == -1) {
                    a->lavagna->tables[i].waiter = id;
                    break;
                }
            }
            break;
        // -------------------------------------------------------
        // Sara (1): consegna cibo + pulizia tavoli + lavapiatti
        // -------------------------------------------------------
        case 1:
            // Consegna cibo (priorità massima)
            for (int i = 0; i < n; i++) {
                if (a->cucina->food_ready[i] &&
                    a->lavagna->tables[i].waiter == -1) {
                    a->lavagna->tables[i].waiter = id;
                    break;
                }
            }
            // Pulizia: TABLE_FREED = famiglia uscita, tavolo sporco
            for (int i = 0; i < n; i++) {
                if (a->sala->tables[i].state == TABLE_FREED &&
                    a->lavagna->tables[i].cleaner == -1) {
                    a->lavagna->tables[i].cleaner = id;
                    break;
                }
            }
            // Lavapiatti
            if (a->cucina->dirty_plates >= LVL_MED &&
                a->lavagna->dishwasher == -1) {
                a->lavagna->dishwasher = id;
            }
            break;
        // -------------------------------------------------------
        // Fabio (2): cassiere fisso + jolly pulizia/lavapiatti
        // -------------------------------------------------------
        case 2:
            if (a->lavagna->cashier == -1)
                a->lavagna->cashier = id;
            // Jolly pulizia solo se TABLE_FREED
            for (int i = 0; i < n; i++) {
                if (a->sala->tables[i].state == TABLE_FREED &&
                    a->lavagna->tables[i].cleaner == -1) {
                    a->lavagna->tables[i].cleaner = id;
                    break;
                }
            }
            // Jolly lavapiatti solo in emergenza
            if (a->cucina->dirty_plates >= LVL_HIGH &&
                a->lavagna->dishwasher == -1) {
                a->lavagna->dishwasher = id;
            }
            break;
        // -------------------------------------------------------
        // Giorgia (3): consegna cibo + pulizia tavoli + lavapiatti
        // -------------------------------------------------------
        case 3:
            // Consegna cibo
            for (int i = 0; i < n; i++) {
                if (a->cucina->food_ready[i] &&
                    a->lavagna->tables[i].waiter == -1) {
                    a->lavagna->tables[i].waiter = id;
                    break;
                }
            }
            // Pulizia: TABLE_FREED
            for (int i = 0; i < n; i++) {
                if (a->sala->tables[i].state == TABLE_FREED &&
                    a->lavagna->tables[i].cleaner == -1) {
                    a->lavagna->tables[i].cleaner = id;
                    break;
                }
            }
            // Lavapiatti
            if (a->cucina->dirty_plates >= LVL_MED &&
                a->lavagna->dishwasher == -1) {
                a->lavagna->dishwasher = id;
            }
            break;
    }

    toggle_blackboard(a->semid, 1); // UNLOCK
}

// ================================================================
// STRATEGIA PROFIT
// Tutti prendono ordini e si coprono: nessuno idle, massimo throughput
// ================================================================
static void worker_profit(thread_args_t *a) {
    int id = a->id;
    int n  = a->sala->tables_n;

    toggle_blackboard(a->semid, -1);

    //DA IMPLEMENTARE

    toggle_blackboard(a->semid, 1);
}

// ================================================================
// DISPATCHER
// ================================================================
void *staff_worker(void *arg) {
    thread_args_t *a = (thread_args_t *)arg;

    int sleep_us = 10000 / (a->speed > 0 ? a->speed : 1);
    if (sleep_us < 500) sleep_us = 500;

    while (instance_running) {
        if (a->strategia == STRATEGY_REPUTATION)
            worker_reputation(a);
        else
            worker_profit(a);

        usleep(sleep_us);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    // 1. Parametri riga di comando
    strategy_t scelta_strategia = STRATEGY_NONE;
    if (argc >= 3 && strcmp(argv[1], "--strategy") == 0) {
        if      (strcmp(argv[2], "profit")     == 0) scelta_strategia = STRATEGY_PROFIT;
        else if (strcmp(argv[2], "reputation") == 0) scelta_strategia = STRATEGY_REPUTATION;
    }
    if (scelta_strategia == STRATEGY_NONE) {
        fprintf(stderr, "Uso: %s --strategy <profit|reputation>\n", argv[0]);
        exit(1);
    }

    // 2. Code di messaggi
    key_t key_c2s = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_C2S);
    key_t key_s2c = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_S2C);
    int msqid_c2s = msgget(key_c2s, 0666);
    int msqid_s2c = msgget(key_s2c, 0666);
    if (msqid_c2s == -1 || msqid_s2c == -1) {
        perror("msgget (il server è attivo?)");
        exit(1);
    }

    // 3. Shared memory - shmget + shmat con controlli
    int shm_dr_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_DININGROOM), sizeof(shm_diningroom_t), 0666);
    int shm_ki_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_KITCHEN),    sizeof(shm_kitchen_t),    0666);
    int shm_bb_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_BLACKBOARD), sizeof(shm_blackboard_t), 0666);
    int shm_cd_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_CASHDESK),   sizeof(shm_cashdesk_t),   0666);
    if (shm_dr_id == -1 || shm_ki_id == -1 || shm_bb_id == -1 || shm_cd_id == -1) {
        perror("shmget");
        exit(1);
    }

    shm_diningroom_t *sala    = shmat(shm_dr_id, NULL, 0);
    shm_kitchen_t    *cucina  = shmat(shm_ki_id, NULL, 0);
    shm_blackboard_t *lavagna = shmat(shm_bb_id, NULL, 0);
    shm_cashdesk_t   *cassa   = shmat(shm_cd_id, NULL, 0);
    if (sala    == (void*)-1 || cucina  == (void*)-1 ||
        lavagna == (void*)-1 || cassa   == (void*)-1) {
        perror("shmat");
        exit(1);
    }

    int semid = semget(ftok(TRATTORIA_FTOK_PATH, PROJ_SEM), SEM_NSEMS, 0666);
    if (semid == -1) { perror("semget"); exit(1); }

    // 4. Handshake
    msg_hello_t hello;
    memset(&hello, 0, sizeof(hello));
    hello.mtype        = MSGTYPE_HELLO;
    hello.pid          = getpid();
    hello.studentid_n  = 3;
    strncpy(hello.studentids[0], "VR517000", STUDENTID_MAXLEN);
    strncpy(hello.studentids[1], "VR517056", STUDENTID_MAXLEN);
    strncpy(hello.studentids[2], "VR517756", STUDENTID_MAXLEN);
    hello.has_strategy = TR_TRUE;
    hello.strategy     = scelta_strategia;

    printf("Client [%d]: invio saluto con strategia %d...\n", hello.pid, hello.strategy);
    msgsnd(msqid_c2s, &hello, sizeof(msg_hello_t) - sizeof(long), 0);

    // 5. Risposta welcome/error
    union {
        long          mtype;
        msg_welcome_t welcome;
        msg_error_t   error;
    } wbuf;

    printf("Client: attesa risposta dal server...\n");
    if (msgrcv(msqid_s2c, &wbuf, sizeof(wbuf) - sizeof(long), 0, 0) == -1) {
        perror("msgrcv welcome");
        exit(1);
    }
    if (wbuf.mtype == MSGTYPE_ERROR) {
        fprintf(stderr, "Errore dal server: %s\n", wbuf.error.message);
        exit(1);
    }
    if (wbuf.mtype != MSGTYPE_WELCOME) {
        fprintf(stderr, "Messaggio inatteso: %ld\n", wbuf.mtype);
        exit(1);
    }

    printf("Connessione stabilita! Gruppo: %s\n", wbuf.welcome.group);
    printf("Staff: %d, Tavoli: %d\n", wbuf.welcome.staff_n, wbuf.welcome.tables_n);
    for (int i = 0; i < wbuf.welcome.staff_n; i++) {
        staff_member_t s = wbuf.welcome.staff[i];
        printf("Membro Staff [%d]: %s\n", i, s.name);
        printf("  Competenze: Waiter:%d, Cook:%d, Helper:%d, Cashier:%d\n",
               s.skills[SKILL_WAITER], s.skills[SKILL_COOK],
               s.skills[SKILL_HELPER], s.skills[SKILL_CASHIER]);
        printf("  Tratti: Patience:%d, Sociability:%d, Professionalism:%d, Resilience:%d\n",
               s.traits[TRAIT_PATIENCE], s.traits[TRAIT_SOCIABILITY],
               s.traits[TRAIT_PROFESSIONALITY], s.traits[TRAIT_RESISTANCE]);
        printf("--------------------------------------------------\n");
    }

    // 6. Loop istanze — buffer union abbastanza grande per tutti i tipi attesi
    while (1) {
        union {
            long                mtype;
            msg_instance_t      instance;
            msg_end_t           end;
        } ibuf;

        if (msgrcv(msqid_s2c, &ibuf, sizeof(ibuf) - sizeof(long), 0, 0) == -1) {
            perror("msgrcv istanza");
            break;
        }

        if (ibuf.mtype == MSGTYPE_END) {
            printf("Fine sessione ricevuta dal server.\n");
            break;
        }

        if (ibuf.mtype != MSGTYPE_INSTANCE) {
            fprintf(stderr, "Messaggio inatteso nel loop istanze: %ld\n", ibuf.mtype);
            continue;
        }

        printf("Avvio istanza %d (strategia=%d, famiglie=%d, speed=%d)\n",
               ibuf.instance.instance_id, ibuf.instance.strategy,
               ibuf.instance.families_n,  ibuf.instance.speed);

        // Avvio thread per ogni membro dello staff
        pthread_t     threads[MAX_STAFF];
        thread_args_t t_args[MAX_STAFF];
        instance_running = 1;

        for (int i = 0; i < wbuf.welcome.staff_n; i++) {
            t_args[i] = (thread_args_t){
                .id        = i,
                .semid     = semid,
                .speed = ibuf.instance.speed,
                .sala      = sala,
                .cucina    = cucina,
                .lavagna   = lavagna,
                .cassa     = cassa,
                .strategia = ibuf.instance.strategy
            };
            pthread_create(&threads[i], NULL, staff_worker, &t_args[i]);
        }

        // Attesa completamento istanza
        msg_instance_done_t done;
        if (msgrcv(msqid_s2c, &done, sizeof(done) - sizeof(long),
                   MSGTYPE_INSTANCE_DONE, 0) == -1) {
            perror("msgrcv instance_done");
        } else {
            printf("Istanza %d completata. Risultato: %s\n",
                   done.instance_id, done.average_families_score_review);
        }

        // Terminazione ordinata dei thread tramite flag
        instance_running = 0;
        for (int i = 0; i < wbuf.welcome.staff_n; i++)
            pthread_join(threads[i], NULL);
    }

    // 7. Distacco memorie condivise
    shmdt(sala);
    shmdt(cucina);
    shmdt(lavagna);
    shmdt(cassa);

    return 0;
}