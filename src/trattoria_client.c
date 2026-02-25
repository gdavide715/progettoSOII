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
            
        } else if(data->strategia == STRATEGY_PROFIT){
            //da implementare...
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {

    //--------Connessione-------

    // 1. Gestione parametri riga di comando
    strategy_t scelta_strategia = STRATEGY_NONE;
    if (argc >= 3 && strcmp(argv[1], "--strategy") == 0) {
        if (strcmp(argv[2], "profit") == 0) scelta_strategia = STRATEGY_PROFIT;
        else if (strcmp(argv[2], "reputation") == 0) scelta_strategia = STRATEGY_REPUTATION;
    }

    if (scelta_strategia == STRATEGY_NONE) {
        fprintf(stderr, "Uso: %s --strategy <profit|reputation>\n", argv[0]);
        exit(1);
    }

    // 2. Chiavi e code
    key_t key_c2s = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_C2S);
    key_t key_s2c = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_S2C);
    int msqid_c2s = msgget(key_c2s, 0666);
    int msqid_s2c = msgget(key_s2c, 0666);

    //chiavi memorie condivise (memorie condivise dove venogno segnati lo stato di sala da pranzo, cucina, lavagna ruoli e cassa)
    
    //stato dei tavoli (TABLE_EMPTY = 0, TABLE_TAKEN = 1, TABLE_SERVED = 2, TABLE_FREED = 3)
    int shm_dr_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_DININGROOM), sizeof(shm_diningroom_t), 0666);
    //ordini pendenti, piatti pronti: vettore di booleani, piatti puliti e sporchi (livello) 
    int shm_ki_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_KITCHEN), sizeof(shm_kitchen_t), 0666);
    //cuoco, cassiere, pulisci piatti, cameriere e pulitore tavoli (per ogni taovlo ci può essere un membro dello staff diverso)
    int shm_bb_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_BLACKBOARD), sizeof(shm_blackboard_t), 0666);
    //pagamenti pendenti
    int shm_cd_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_CASHDESK), sizeof(shm_cashdesk_t), 0666);
    //per evitare che due trhead scrivano assieme nella lavagna
    int semid = semget(ftok(TRATTORIA_FTOK_PATH, PROJ_SEM), SEM_NSEMS, 0666);

    //ora la struttura thread_args_t punta alla stessa area di memoria modificata dal server
    shm_diningroom_t *sala = shmat(shm_dr_id, NULL, 0);
    shm_kitchen_t *cucina = shmat(shm_ki_id, NULL, 0);
    shm_cashdesk_t *cassa = shmat(shm_cd_id, NULL, 0);
    shm_blackboard_t *lavagna = shmat(shm_bb_id, NULL, 0);

    if (msqid_c2s == -1 || msqid_s2c == -1) {
        perror("Errore connessione code (il server è attivo?)");
        exit(1);
    }


    // 3. Handshake -- saluto iniziale
    msg_hello_t hello;
    //azzera la memoria per non inviare spazzatura residua nello stack
    memset(&hello, 0, sizeof(hello));
    hello.mtype = MSGTYPE_HELLO;
    hello.pid = getpid();
    hello.studentid_n = 3; 
    strncpy(hello.studentids[0], "VR517000", STUDENTID_MAXLEN);
    strncpy(hello.studentids[1], "VR517056", STUDENTID_MAXLEN);
    strncpy(hello.studentids[2], "VR517756", STUDENTID_MAXLEN);
    
    hello.has_strategy = TR_TRUE; // Ora specifichiamo la strategia
    hello.strategy = scelta_strategia;

    printf("Client [%d]: Invio saluto con strategia %d...\n", hello.pid, hello.strategy);
    msgsnd(msqid_c2s, &hello, sizeof(msg_hello_t) - sizeof(long), 0);

    // 4. Ricezione risposta (Distinguiamo tra Welcome ed Error)
    // Usiamo un buffer generico grande abbastanza per entrambe
    union {
        long mtype;
        msg_welcome_t welcome;
        msg_error_t error;
    } buffer;

    printf("Client: In attesa di risposta dal server...\n");
    //in buffer.mtype salva il tipo. Se non va a buon fine msgrcv (errore nella chiamata di sistema) esce
    if (msgrcv(msqid_s2c, &buffer, sizeof(msg_welcome_t) - sizeof(long), 0, 0) == -1) {
        perror("Errore msgrcv");
        exit(1);
    }

    //controlla il tipo se c'è un errore esce
    if (buffer.mtype == MSGTYPE_ERROR) {
        printf("ERRORE DAL SERVER: %s\n", buffer.error.message);
        exit(1);
    } 
    

    //altrimenti stampa i membri dello staff e le loro skills
    if (buffer.mtype == MSGTYPE_WELCOME) {
        printf("Connessione stabilita! Gruppo: %s\n", buffer.welcome.group);
        printf("Staff: %d, Tavoli: %d\n", buffer.welcome.staff_n, buffer.welcome.tables_n);

      // Iteriamo per il numero di membri dello staff indicati dal server
      for (int i = 0; i < buffer.welcome.staff_n; i++) {
          staff_member_t s = buffer.welcome.staff[i];
          printf("Membro Staff [%d]: %s\n", i, s.name);
          
          // Stampa delle Skills (Waiter, Cook, Helper, Cashier)
          printf("  Competenze: Waiter:%d, Cook:%d, Helper:%d, Cashier:%d\n", 
                s.skills[SKILL_WAITER], s.skills[SKILL_COOK], 
                s.skills[SKILL_HELPER], s.skills[SKILL_CASHIER]);
          
          // Stampa dei Traits (Patience, Sociability, Professionalism, Resilience)
          printf("  Tratti:     Patience:%d, Sociability:%d, Professionalism:%d, Resilience:%d\n", 
                s.traits[TRAIT_PATIENCE], s.traits[TRAIT_SOCIABILITY], 
                s.traits[TRAIT_PROFESSIONALITY], s.traits[TRAIT_RESISTANCE]);
          printf("--------------------------------------------------\n");
      }
    }

    // -------- gestione istanze -------
    while(1){
        msg_instance_t inst;
        if(msgrcv(msqid_s2c, &inst, sizeof(msg_instance_t)-sizeof(long), 0, 0) == -1)
            break;
        //non c'è un'altra istanza ma era l'ultima
        if(inst.mtype == MSGTYPE_END) break;

        if(inst.mtype == MSGTYPE_INSTANCE){
            printf("Avvio Istanza %d (%d famiglie)\n", inst.instance_id, inst.families_n);
            pthread_t threads[MAX_STAFF];
            thread_args_t t_args[MAX_STAFF];

            //crea un tread per ogni membro dello staff con la propria struttura
            for(int i=0; i<buffer.welcome.staff_n; i++){
                t_args[i] = (thread_args_t){.id = i, .semid=semid, .sala=sala, .cucina=cucina,
                            .lavagna=lavagna, .cassa=cassa, .strategia=inst.strategy};
                pthread_create(&threads[i], NULL, staff_worker, &t_args[i]);
            }

            msg_instance_done_t done;
            //aspetta che l'istanza finisca
            msgrcv(msqid_s2c, &done, sizeof(msg_instance_done_t)-sizeof(long), MSGTYPE_INSTANCE_DONE, 0);
            printf("Instanza completata. Risultato: %s\n", done.average_families_score_review);

            //cancello i trhead e per sicurezza li aspetto con una join
            for(int i=0; i<buffer.welcome.staff_n; i++){
                pthread_cancel(threads[i]);
                pthread_join(threads[i], NULL);
            }
        }
    }

    //distacco memorie condivise
    shmdt(sala); shmdt(cucina); shmdt(lavagna); shmdt(cassa);

    return 0;
}