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

int instance_running = 0;

//struttura dati per i thread personale
typedef struct{
    int id;
    int semid;
    int speed;  
    int msqid_fatigue;              // coda notifiche stanchezza
    level_t fatigue[NUM_ROLES];     // stanchezza per ruolo (aggiornata dal thread stesso)
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

//funzione per leggere la stanchezza (non bloccante)
void update_fatigue(thread_args_t *a) {
    // Ogni thread legge solo i propri messaggi (mtype = id + 1)
    msg_fatigue_t msg;
    while (msgrcv(a->msqid_fatigue, &msg,
                  sizeof(msg_fatigue_t) - sizeof(long),
                  a->id + 1,          // solo i miei messaggi
                  IPC_NOWAIT) != -1)  // non bloccante
    {
        a->fatigue[msg.role] = msg.new_lvl;
        printf("[staff %d] stanchezza ruolo %d → %d\n",
               a->id, msg.role, msg.new_lvl);
    }
}

// ================================================================
// STRATEGIA REPUTATION (non bisogna gestire la stanchezza influisce solo sulle skills e quindi sulla velocità)
// ================================================================
void worker_reputation(thread_args_t *a) {
    int id = a->id;
    int nTables  = a->sala->tables_n;

    toggle_blackboard(a->semid, -1);

    switch (id) {
        // -------------------------------------------------------
        // Giulia (0): cuoca fissa + cameriera SOLO per prendere ordini
        // -------------------------------------------------------
        case 0:
            if (a->lavagna->cook == -1)
                a->lavagna->cook = id;

            // Si assegna come cameriera solo per prendere ordini:
            // il tavolo deve essere occupato (TABLE_TAKEN),
            // il cibo non ancora ordinato (!food_ready),
            // e nessun altro cameriere già assegnato

            for (int i = 0; i < nTables; i++) {
                if (a->sala->tables[i].state == TABLE_TAKEN &&
                    !a->cucina->food_ready[i]              &&
                    a->lavagna->tables[i].waiter == -1) 
                {
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
            for (int i = 0; i < nTables; i++) {
                if (a->cucina->food_ready[i] &&
                    a->lavagna->tables[i].waiter == -1) {
                    a->lavagna->tables[i].waiter = id;
                    break;
                }
            }
            // Pulizia: TABLE_FREED = famiglia uscita, tavolo sporco
            for (int i = 0; i < nTables; i++) {
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
            for (int i = 0; i < nTables; i++) {
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
            for (int i = 0; i < nTables; i++) {
                if (a->cucina->food_ready[i] &&
                    a->lavagna->tables[i].waiter == -1) {
                    a->lavagna->tables[i].waiter = id;
                    break;
                }
            }
            // Pulizia: TABLE_FREED
            for (int i = 0; i < nTables; i++) {
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
// ================================================================
static void worker_profit(thread_args_t *a) {
    int id = a->id;
    int n  = a->sala->tables_n;

    toggle_blackboard(a->semid, -1);

    /* Cassiere SEMPRE assegnato, come nella reputation */
    if (a->lavagna->cashier == -1)
        a->lavagna->cashier = id;

    /* Cuoco SEMPRE assegnato */
    if (a->lavagna->cook == -1)
        a->lavagna->cook = id;

    /* Consegna cibo pronto */
    for (int i = 0; i < n; i++) {
        if (a->cucina->food_ready[i] &&
            a->lavagna->tables[i].waiter == -1) {
            a->lavagna->tables[i].waiter = id;
            break;
        }
    }

    /* Prendi ordine */
    for (int i = 0; i < n; i++) {
        if (a->sala->tables[i].state == TABLE_TAKEN &&
            a->sala->tables[i].food_qty == LVL_NONE &&
            a->lavagna->tables[i].waiter == -1) {
            a->lavagna->tables[i].waiter = id;
            break;
        }
    }

    /* Pulizia tavoli */
    for (int i = 0; i < n; i++) {
        if (a->sala->tables[i].state == TABLE_FREED &&
            a->lavagna->tables[i].cleaner == -1) {
            a->lavagna->tables[i].cleaner = id;
            break;
        }
    }

    /* Lavapiatti */
    if (a->cucina->dirty_plates != LVL_NONE && a->lavagna->dishwasher == -1)
        a->lavagna->dishwasher = id;

    toggle_blackboard(a->semid, 1);
}

// ================================================================
// DISPATCHER
// ================================================================
void *staff_worker(void *arg) {
    //cast per accedere ai campi
    thread_args_t *a = (thread_args_t *)arg;

    int speed = a->speed;
    if (speed <= 0) speed = 1;

    int sleep_us = 10000 / speed;

    if (sleep_us < 500) sleep_us = 500;

    //Il thread gira in loop finché instance_running è 1
    while (instance_running) {
        if (a->strategia == STRATEGY_REPUTATION)
            worker_reputation(a);
        else
            worker_profit(a);

        //dorme per i millisecondi calcolati prima
        usleep(sleep_us);
    }

    return NULL;
}

int main(int argc, char *argv[]) {

    //--------Connessione-------

    // 1. Parametri riga di comando
    strategy_t scelta_strategia = STRATEGY_NONE;

    // controllo che la strategia si scelta correttamente 
    if (argc >= 3 && strcmp(argv[1], "--strategy") == 0) {
        if      (strcmp(argv[2], "profit")     == 0) scelta_strategia = STRATEGY_PROFIT;
        else if (strcmp(argv[2], "reputation") == 0) scelta_strategia = STRATEGY_REPUTATION;
    }

    if (scelta_strategia == STRATEGY_NONE) {
        fprintf(stderr, "Uso: %s --strategy <profit|reputation>\n", argv[0]);
        exit(1);
    }

    // 2. Code di messaggi

    //genera una chiave IPC univoca a partire da un file sul filesystem e un numero identificativo (genera la stessa chiave generata nel server)
    key_t key_c2s = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_C2S);
    //                    "/tmp/trattoria_ipc_key"   0x41
    key_t key_s2c = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_S2C);
    //                    "/tmp/trattoria_ipc_key"   0x42

    //cerca una coda di messaggi già esistente con quella chiave e restituisce il suo ID
    int msqid_c2s = msgget(key_c2s, 0666);  // coda client → server
    int msqid_s2c = msgget(key_s2c, 0666);  // coda server → client

    //se non esiste il server non è attivo
    if (msqid_c2s == -1 || msqid_s2c == -1) {
        perror("Il server non è attivo!");
        exit(1);
    }

    //coda fatica
    key_t key_fat  = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_FATIGUE);
    int msqid_fatigue = msgget(key_fat, 0666);
    if (msqid_fatigue == -1) {
        perror("errore msgget fatigue");
        exit(1);
    }

    // 3. Shared memory (dove venogno segnati lo stato di sala da pranzo, cucina, lavagna ruoli e cassa)

    //genera chiavi con percorso univoco e numero (uguale anche per il server)
    key_t key_dr = ftok(TRATTORIA_FTOK_PATH, PROJ_DININGROOM);
    key_t key_ki = ftok(TRATTORIA_FTOK_PATH, PROJ_KITCHEN);
    key_t key_bb = ftok(TRATTORIA_FTOK_PATH, PROJ_BLACKBOARD);
    key_t key_cd = ftok(TRATTORIA_FTOK_PATH, PROJ_CASHDESK);

    //cerca una memoria condivisa già esistente con quella chiave e restituisce il suo ID

    //stato dei tavoli (TABLE_EMPTY = 0, TABLE_TAKEN = 1, TABLE_SERVED = 2, TABLE_FREED = 3)
    int shm_dr_id = shmget(key_dr, sizeof(shm_diningroom_t), 0666);
    //ordini pendenti, piatti pronti: vettore di booleani, piatti puliti e sporchi (livello) 
    int shm_ki_id = shmget(key_ki, sizeof(shm_kitchen_t),    0666);
    //cuoco, cassiere, pulisci piatti, cameriere e pulitore tavoli (per ogni taovlo ci può essere un membro dello staff diverso)
    int shm_bb_id = shmget(key_bb, sizeof(shm_blackboard_t), 0666);
    //pagamenti pendenti
    int shm_cd_id = shmget(key_cd, sizeof(shm_cashdesk_t),   0666);

    //controlla eventuali errori di shmget
    if (shm_dr_id == -1 || shm_ki_id == -1 || shm_bb_id == -1 || shm_cd_id == -1) {
        perror("errore shmget memorie condivise");
        exit(1);
    }


    //ora la struttura thread_args_t punta alla stessa area di memoria modificata dal server
    shm_diningroom_t *sala    = shmat(shm_dr_id, NULL, 0);
    shm_kitchen_t    *cucina  = shmat(shm_ki_id, NULL, 0);
    shm_blackboard_t *lavagna = shmat(shm_bb_id, NULL, 0);
    shm_cashdesk_t   *cassa   = shmat(shm_cd_id, NULL, 0);

    
    if (sala    == (void*)-1 || cucina  == (void*)-1 ||
        lavagna == (void*)-1 || cassa   == (void*)-1) {
        perror("errore shmat memorie condivise");
        exit(1);
    }

    //per evitare che due trhead scrivano assieme nella lavagna
    key_t key_sem = ftok(TRATTORIA_FTOK_PATH, PROJ_SEM);
    int semid = semget(key_sem, SEM_NSEMS, 0666);

    //errore creazione semafoto
    if (semid == -1) { 
        perror("errore shmget semaforo"); 
        exit(1); 
    }

    // 4.  Handshake -- saluto iniziale

    msg_hello_t hello;

    //forse meglio (potrebbe contenere valori casuali rimasti nello stack da chiamate di funzione precedenti)
    //msg_hello_t hello = {0};

    //compilamento parametri hello 
    hello.mtype = MSGTYPE_HELLO;
    hello.pid = getpid();
    hello.studentid_n  = 3;
    strncpy(hello.studentids[0], "VR517000", STUDENTID_MAXLEN);
    strncpy(hello.studentids[1], "VR517056", STUDENTID_MAXLEN);
    strncpy(hello.studentids[2], "VR517756", STUDENTID_MAXLEN);
    hello.has_strategy = TR_TRUE;
    hello.strategy     = scelta_strategia;

    printf("Client [%d]: invio saluto con strategia %d...\n", hello.pid, hello.strategy);
    msgsnd(msqid_c2s, &hello, sizeof(msg_hello_t) - sizeof(long), 0);

    // 5. Risposta welcome/error (Distinguiamo tra Welcome ed Error)

     // Usiamo un buffer generico grande abbastanza per entrambe
    union {
        long          mtype;
        msg_welcome_t welcome;
        msg_error_t   error;
    } wbuf;

    //in buffer.mtype salva il tipo. Se non va a buon fine msgrcv (errore nella chiamata di sistema) esce
    printf("Client: attesa risposta dal server...\n");
    if (msgrcv(msqid_s2c, &wbuf, sizeof(wbuf) - sizeof(long), 0, 0) == -1) {
        perror("errore msgrcv welcome");
        exit(1);
    }

    //controlla il tipo se c'è un errore esce
    if (wbuf.mtype == MSGTYPE_ERROR) {
        fprintf(stderr, "Errore dal server: %s\n", wbuf.error.message);
        exit(1);
    }

    //nè errore nè weolcome ---> tipo inatteso esce dal programma
    if (wbuf.mtype != MSGTYPE_WELCOME) {
        fprintf(stderr, "Messaggio inatteso: %ld\n", wbuf.mtype);
        exit(1);
    }

    //Tutto era corretto stampa le skills e i tratti petti del personale
    printf("Connessione stabilita! Gruppo: %s\n", wbuf.welcome.group);
    printf("Staff: %d, Tavoli: %d\n", wbuf.welcome.staff_n, wbuf.welcome.tables_n);

    //scorri tutti i membri
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
        //struttura abbastanza grnde per ottenere messaggi di tipo istanza e di tipo fine
        union {
            long                mtype;
            msg_instance_t      instance;
            msg_end_t           end;
        } ibuf;

        if (msgrcv(msqid_s2c, &ibuf, sizeof(ibuf) - sizeof(long), 0, 0) == -1) {
            perror("errore msgrcv istanza");
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

        //Stampa di tutte le informazioni di istanza
        printf("Avvio istanza %d (strategia=%d, famiglie=%d, speed=%d)\n",
               ibuf.instance.instance_id, ibuf.instance.strategy,
               ibuf.instance.families_n,  ibuf.instance.speed);

        // Avvio thread per ogni membro dello staff
        pthread_t     threads[MAX_STAFF];
        thread_args_t t_args[MAX_STAFF];
        //Il flag instance_running = 1 segnala ai thread che devono lavorare.
        instance_running = 1;

        //ogni thread ha la propria struttura
        for (int i = 0; i < wbuf.welcome.staff_n; i++) {
            t_args[i] = (thread_args_t){
                .id        = i,
                .semid     = semid,
                .speed = ibuf.instance.speed,
                .msqid_fatigue  = msqid_fatigue,
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
            perror("errore msgrcv instance_done");
        } else {
            printf("Istanza %d completata. Risultato: %s\n",
                   done.instance_id, done.average_families_score_review);
        }

        // Terminazione ordinata dei thread con la join (più sicuro)
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