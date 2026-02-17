#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>

#include "ipc.h"

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

    return 0;
}