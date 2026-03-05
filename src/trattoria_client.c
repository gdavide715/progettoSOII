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

//struttura dati per i thread personale
typedef struct{
    int id;
    int semid;
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

//azione che compie ogni membro dello staff
void* staff_worker(void* arg){
    thread_args_t *data = (thread_args_t*)arg;
    int my_id = data->id;
    int q_fatigue = msgget(ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_FATIGUE), 0666);
    msg_fatigue_t fatigue_msg;

    //ogni membro continua a lavorare finché non viene fermato dal server
    while(1){    

        if(data->strategia == STRATEGY_REPUTATION){
            switch(my_id){
                case 0: 
                //GIULIA: prende ordini e cucina (patience: 2 e professionalism: 2)
                    
                    // Entra nella sezione critica
                    toggle_blackboard(data->semid, -1); 

                    int tavolo_da_servire = -1;
                    for (int i = 0; i < data->sala->tables_n; i++) {
                        if (data->sala->tables[i].state == TABLE_TAKEN && 
                            data->sala->tables[i].food_qty == LVL_NONE && 
                            data->lavagna->tables[i].waiter == -1) {
                            tavolo_da_servire = i;
                            break;
                        }
                    }

                    //se c'è un tavolo da servire serve altrimenti cucina
                    if (tavolo_da_servire != -1) {
                        // Trovato un tavolo.
                        // my_id + 1 come mtype per ricevere solo i messaggi destinati a Giulia.
                        msg_fatigue_t f_msg;
                        
                        // Tentiamo di leggere se c'è un aggiornamento sulla fatica da CAMERIERE (ROLE_WAITER)
                        // Nota: msgrcv è non bloccante (IPC_NOWAIT)
                        if (msgrcv(q_fatigue, &f_msg, sizeof(msg_fatigue_t) - sizeof(long), my_id + 1, IPC_NOWAIT) != -1) {
                            
                            if (f_msg.role == ROLE_WAITER && f_msg.new_lvl >= LVL_LOW) {
                                // È stanca! Rilascia il lucchetto, dorme e ricomincia
                                toggle_blackboard(data->semid, 1);
                                printf("[GIULIA] Tavolo %d attende, ma leggo fatica ALTA. Riposo...\n", tavolo_da_servire);
                                
                                sleep(2); 
                                // Giulia riproverà a leggere la coda.
                                continue; 
                            }
                        }

                        // Se non c'erano messaggi di fatica alta o la coda era vuota, serve il tavolo
                        data->lavagna->tables[tavolo_da_servire].waiter = my_id;
                        printf("[GIULIA] Prendo l'ordine al tavolo %d.\n", tavolo_da_servire);
                    } else {
                        // NESSUN ORDINE DA PRENDERE AI TAVOLI: Controlliamo la cucina
                        if (data->cucina->pending_orders > 0 && data->lavagna->cook == -1) {
                            
                            // 1. Controllo fatica per il ruolo COOK
                            msg_fatigue_t f_msg_cook;
                            if (msgrcv(q_fatigue, &f_msg_cook, sizeof(msg_fatigue_t) - sizeof(long), my_id + 1, IPC_NOWAIT) != -1) {
                                if (f_msg_cook.role == ROLE_COOK && f_msg_cook.new_lvl >= LVL_LOW) {
                                    // Troppo stanca per cucinare
                                    toggle_blackboard(data->semid, 1);
                                    printf("[GIULIA] Ci sono ordini in cucina, ma leggo fatica ALTA (COOK). Riposo...\n");
                                    sleep(2);
                                    continue;
                                }
                            }

                            // 2. Se non è stanca, si mette ai fornelli
                            data->lavagna->cook = my_id;
                            printf("[GIULIA] Non ci sono ordini ai tavoli. Vado in cucina (Professionalism: 2).\n");
                        }
                    }

                    toggle_blackboard(data->semid, 1);
                    usleep(10000);
                    

                break;
                case 1://SARA: pulisce, lava piatti e serve
                break;
                case 2://FABIO: cassiere fisso + jolly(sociability: 1)
                break;
                case 3://GIORGIA: pulisce, lava piatti e serve (Giulia è più brava sia in cucina che come cameriera)
                break;

            }

        } else if(data->strategia == STRATEGY_PROFIT){
            //da implementare...
        }
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