#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/sem.h>
#include <pthread.h>
#include <sys/shm.h>
#include <signal.h>

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
    staff_member_t *member;
    volatile sig_atomic_t *stop;
} thread_args_t;

void toggle_blackboard(int semid, int op){      //-1 blocca, 1 sblocca
    struct sembuf sops = { .sem_num = SEMIDX_BLACKBOARD, .sem_op=op, .sem_flg=0};
    semop(semid, &sops, 1);
}

void* staff_worker(void* arg){
    thread_args_t *data = (thread_args_t*)arg;
    int my_id = data->id;
    printf("Staff worker %d avviato: %s\n", my_id, data->member->name);
    int q_fatigue = msgget(ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_FATIGUE), 0666);
    msg_fatigue_t fatigue_msg;
    role_t my_role;
    my_role = ROLE_NONE;

    while (!*(data->stop)) {
        if(msgrcv(q_fatigue, &fatigue_msg, sizeof(msg_fatigue_t)-sizeof(long), my_id+1, IPC_NOWAIT)!=-1){
            printf("[Staff %d] Nuova stanchezza nel ruolo %d: livello %d\n", my_id, fatigue_msg.role, fatigue_msg.new_lvl);
        }

        //azione da fare mentre il semaforo è bloccato (pulire tavolo, prendere ordine, cambio ruolo,...)
        toggle_blackboard(data->semid, -1);

        if (*(data->stop)) {
            toggle_blackboard(data->semid, 1);
            break;
        }

        
        int sto_lavorando = 0;
        // Verifica se sono assegnato a compiti globali
        if (data->lavagna->cook == my_id || data->lavagna->cashier == my_id || data->lavagna->dishwasher == my_id) {
            sto_lavorando = 1;
        }
        // Verifica se sono assegnato a un tavolo
        for(int i=0; i<data->sala->tables_n; i++) {
            if(data->lavagna->tables[i].waiter == my_id || data->lavagna->tables[i].cleaner == my_id) {
                sto_lavorando = 1;
                break;
            }
        }

        if (!sto_lavorando) {
            my_role = ROLE_NONE; // Se non sono sulla lavagna, sono libero
        }

        
        //rilascio lavapiatti
        if(data->lavagna->dishwasher == data->id && data->cucina->dirty_plates == LVL_NONE){
            my_role = ROLE_NONE;
            data->lavagna->dishwasher=-1;
        }

    //rilascio cassiere
        if(data->cassa->pending_payments == 0 && data->lavagna->cashier == data->id){
            my_role = ROLE_NONE;
            data->lavagna->cashier = -1;
        }

        //rilascio cuoco
        if(data->lavagna->cook == data->id && data->cucina->pending_orders == 0){
            my_role = ROLE_NONE;
            data->lavagna->cook=-1;
        }

        //rilascio cameriere
        for(int i=0; i<data->sala->tables_n; i++){
            if(data->lavagna->tables[i].waiter == data->id && ((data->sala->tables[i].food_qty > LVL_NONE
            && data->cucina->food_ready[i] == TR_FALSE) || data->sala->tables[i].state == TABLE_SERVED)){
                data->lavagna->tables[i].waiter = -1;
                my_role = ROLE_NONE;
            }
        }
        
        //rilascio cleaner
        for(int i=0; i<data->sala->tables_n; i++){
            if(data->lavagna->tables[i].cleaner == data->id && data->sala->tables[i].dirt_level == LVL_NONE){
                data->lavagna->tables[i].cleaner = -1;
                my_role = ROLE_NONE;
            }
        }

        //conteggio del numero di tavoli FREED e TAKEN
        int freed_tables = 0;
        int taken_tables = 0;
        for(int i=0; i<data->sala->tables_n; i++){
            if(data->sala->tables[i].state == TABLE_FREED){
                freed_tables++;
            }
            if(data->sala->tables[i].state == TABLE_TAKEN){
                taken_tables++;
            }
        }

        //pulizia tavoli (se tavolo è FREED metto cleaner)
        if(my_role == ROLE_NONE){
            for(int i=0; i<data->sala->tables_n; i++){
                if(data->sala->tables[i].state == TABLE_FREED && data->lavagna->tables[i].cleaner==-1){
                    if(data->strategia == STRATEGY_REPUTATION){
                        if(data->member->skills[SKILL_HELPER] >= PARAM_MEDIUM || freed_tables > 4){
                            data->lavagna->tables[i].cleaner = my_id;
                            my_role = ROLE_HELPER;
                            break;
                        }
                    }
                    else{
                        data->lavagna->tables[i].cleaner = my_id;
                        my_role = ROLE_HELPER;
                        break;
                    }
                }
            }
        }

        //pagamento (se ci sono pagamenti in cassa)
        if(my_role == ROLE_NONE){
            if(data->cassa->pending_payments > 0 && data->lavagna->cashier == -1){
                if(data->strategia == STRATEGY_REPUTATION){
                    if(data->member->skills[SKILL_CASHIER] >= PARAM_MEDIUM || data->cassa->pending_payments > 4){
                        data->lavagna->cashier = my_id;
                        my_role = ROLE_CASHIER;
                    }
                }
                else{
                    data->lavagna->cashier = my_id;
                    my_role = ROLE_CASHIER;
                }
            }
        }

        //ordine (se tavolo è TAKEN metto un waiter)
        if(my_role == ROLE_NONE){
            for(int i=0; i<data->sala->tables_n; i++){
                if(data->lavagna->tables[i].waiter==-1 && ((data->sala->tables[i].state ==  TABLE_TAKEN &&
                data->sala->tables[i].food_qty == LVL_NONE) || (data->sala->tables[i].food_qty > LVL_NONE &&
                data->cucina->food_ready[i] == TR_TRUE))){
                    if(data->strategia == STRATEGY_REPUTATION){
                        if(data->member->skills[SKILL_WAITER] >= PARAM_MEDIUM || taken_tables > 4){
                            data->lavagna->tables[i].waiter = my_id;
                            my_role = ROLE_WAITER;
                            break;
                        }
                    }
                    else{
                        data->lavagna->tables[i].waiter = my_id;
                        my_role = ROLE_WAITER;
                        break;
                    }
                }
            }
        }

        //cucina (se ci sono ordini in cucina)
        if(my_role == ROLE_NONE){
            if(data->cucina->pending_orders > 0 && data->lavagna->cook == -1){
                if(data->strategia == STRATEGY_REPUTATION){
                    if(data->member->skills[SKILL_COOK] >= PARAM_MEDIUM || data->cucina->pending_orders > 4){
                        data->lavagna->cook = my_id;
                        my_role = ROLE_COOK;
                    }
                }
                else{
                    data->lavagna->cook = my_id;
                    my_role = ROLE_COOK;
                }
            }
        }

        //lavaggio piatti (se ci sono piatti sporchi e nessuno sta lavando)
        if(my_role == ROLE_NONE){
            if(data->cucina->dirty_plates != LVL_NONE && data->lavagna->dishwasher == -1){
                if(data->strategia == STRATEGY_REPUTATION){
                    if(data->member->skills[SKILL_HELPER] >= PARAM_MEDIUM || data->cucina->dirty_plates == LVL_HIGH){
                        data->lavagna->dishwasher = my_id;
                        my_role = ROLE_DISHWASHER;
                    }
                }
                else{
                    data->lavagna->dishwasher = my_id;
                    my_role = ROLE_DISHWASHER;
                }
            }
        }

        toggle_blackboard(data->semid, 1);

        usleep(10000);
        
    }
    return NULL;
}

int main(int argc, char *argv[]) {
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

    //chiavi memorie condivise
    int shm_dr_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_DININGROOM), sizeof(shm_diningroom_t), 0666);
    int shm_ki_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_KITCHEN), sizeof(shm_kitchen_t), 0666);
    int shm_bb_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_BLACKBOARD), sizeof(shm_blackboard_t), 0666);
    int shm_cd_id = shmget(ftok(TRATTORIA_FTOK_PATH, PROJ_CASHDESK), sizeof(shm_cashdesk_t), 0666);
    int semid = semget(ftok(TRATTORIA_FTOK_PATH, PROJ_SEM), SEM_NSEMS, 0666);

    //puntatori alle strutture
    shm_diningroom_t *sala = shmat(shm_dr_id, NULL, 0);
    shm_kitchen_t *cucina = shmat(shm_ki_id, NULL, 0);
    shm_cashdesk_t *cassa = shmat(shm_cd_id, NULL, 0);
    shm_blackboard_t *lavagna = shmat(shm_bb_id, NULL, 0);

    if (msqid_c2s == -1 || msqid_s2c == -1) {
        perror("Errore connessione code (il server è attivo?)");
        exit(1);
    }

    // 3. Invio HELLO
    msg_hello_t hello;
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
    if (msgrcv(msqid_s2c, &buffer, sizeof(msg_welcome_t) - sizeof(long), 0, 0) == -1) {
        perror("Errore msgrcv");
        exit(1);
    }

    if (buffer.mtype == MSGTYPE_ERROR) {
        printf("ERRORE DAL SERVER: %s\n", buffer.error.message);
        exit(1);
    } 
    
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

    volatile sig_atomic_t stop_instance = 0;

    while (1) {

        msg_instance_t inst;

        if (msgrcv(msqid_s2c,
                &inst,
                sizeof(msg_instance_t) - sizeof(long),
                0, 0) == -1) {
            perror("msgrcv");
            break;
        }

        /* ---- CHIUSURA SERVER ---- */
        if (inst.mtype == MSGTYPE_END) {
            printf("Server ha inviato MSGTYPE_END. Chiusura client.\n");
            break;
        }

        /* ---- NUOVA ISTANZA ---- */
        if (inst.mtype == MSGTYPE_INSTANCE) {

            printf("Avvio Istanza %d (%d famiglie)\n",
                inst.instance_id,
                inst.families_n);

            stop_instance = 0;

            pthread_t threads[MAX_STAFF];
            thread_args_t t_args[MAX_STAFF];

            for (int i = 0; i < buffer.welcome.staff_n; i++) {

                t_args[i] = (thread_args_t){
                    .id = i,
                    .semid = semid,
                    .sala = sala,
                    .cucina = cucina,
                    .lavagna = lavagna,
                    .cassa = cassa,
                    .strategia = inst.strategy,
                    .member = &buffer.welcome.staff[i],
                    .stop = &stop_instance
                };

                pthread_create(&threads[i],
                            NULL,
                            staff_worker,
                            &t_args[i]);
            }

            /* ---- ATTESA FINE ISTANZA ---- */

            msg_instance_done_t done;

            if (msgrcv(msqid_s2c,
                    &done,
                    sizeof(msg_instance_done_t) - sizeof(long),
                    MSGTYPE_INSTANCE_DONE,
                    0) == -1) {
                perror("msgrcv INSTANCE_DONE");
                break;
            }

            printf("Istanza completata. Risultato: %s\n",
                done.average_families_score_review);

            /* ---- TERMINAZIONE THREAD ---- */

            stop_instance = 1;

            for (int i = 0; i < buffer.welcome.staff_n; i++) {
                pthread_join(threads[i], NULL);
            }

            printf("Thread terminati correttamente.\n");
        }
    }

    /* ---- CLEANUP FINALE ---- */

    shmdt(sala);
    shmdt(cucina);
    shmdt(lavagna);
    shmdt(cassa);

    printf("Client terminato correttamente.\n");
    return 0;
}