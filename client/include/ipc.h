#pragma once

#include "scenario.h"

#include <sys/types.h> // pid_t

// ---- Boolean type (uniform across messages + SHM) ----
// Fixed-width for stable shared-memory / message layouts.
typedef uint8_t tr_bool_t;
#define TR_FALSE ((tr_bool_t)0)
#define TR_TRUE  ((tr_bool_t)1)

// ---- Project key prefix ----

#define TRATTORIA_FTOK_PATH "/tmp/trattoria_ipc_key"

// ---- Project key suffixes ----

// Admin message queue from client to server
#define PROJ_MSG_C2S      0x41
// Admin message queue from server to client
#define PROJ_MSG_S2C      0x42
// Dedicated message queue for fatigue notifications (server -> client)
#define PROJ_MSG_FATIGUE  0x48
// Shared memories
#define PROJ_DININGROOM   0x43
#define PROJ_KITCHEN      0x44
#define PROJ_BLACKBOARD   0x45
// Shared memory: Cash desk (read-only view for client)
#define PROJ_CASHDESK     0x49
#define PROJ_SEM          0x47

// The semaphore set contains additional internal semaphores reserved for the server.
// Clients must still use SEM_NSEMS when attaching to the set.
#define SEM_NSEMS      4
// The only index that clients need
#define SEMIDX_BLACKBOARD 0

// ---- Message queue message types ----

#define MSGTYPE_HELLO   1
#define MSGTYPE_WELCOME 2
#define MSGTYPE_END     3
#define MSGTYPE_ERROR   4
#define MSGTYPE_INSTANCE 5
#define MSGTYPE_INSTANCE_DONE 6

// ---- Messages (wire formats) ----

typedef struct {
  long mtype;                 // MSGTYPE_HELLO
  pid_t pid;
  int studentid_n;            // 1..STUDENTID_MAX
  char studentids[STUDENTID_MAX][STUDENTID_MAXLEN];
  tr_bool_t has_strategy;     // TR_FALSE/TR_TRUE
  strategy_t strategy;        // meaningful only if has_strategy==TR_TRUE
} msg_hello_t;

typedef struct {
  char name[MAX_NAME];
  // Staff parameters as confidence buckets (server never sends exact numeric values).
  // Abilities (skills): waiter, cook, helper, cashier.
  param_bucket_t skills[NUM_SKILLS];
  // Traits: patience, sociability, professionalism, resilience.
  param_bucket_t traits[NUM_TRAITS];
} staff_member_t;

typedef struct {
  long mtype;                  // MSGTYPE_WELCOME
  int staff_n;
  int tables_n;
  tr_bool_t verify_mode;       
  strategy_t imposed_strategy; // 0=none, 1=profit, 2=reputation
  // Group identifier (derived from the sorted student IDs).
  // Does not change across instances.
  char group[MAX_GROUP];

  staff_member_t staff[MAX_STAFF];
} msg_welcome_t;

typedef struct {
  long mtype;               // MSGTYPE_ERROR
  int code;                 // 0 generic, 1 strategy_not_allowed, ...
  char message[128];
} msg_error_t;

// A server-defined "instance" of the simulation.
// In verify mode the server will send multiple instances over time.
typedef struct {
  long mtype;               // MSGTYPE_INSTANCE
  int instance_id;
  strategy_t strategy;
  int speed;
  int families_n;
} msg_instance_t;

typedef struct {
  long mtype;               // MSGTYPE_INSTANCE_DONE
  int instance_id;
  double total_families_time;
  char average_families_score_review[32];
} msg_instance_done_t;

typedef struct {
  long mtype;                 // MSGTYPE_END
  int reason;                 // 0=normal, 1=timeout, ...
} msg_end_t;

// --- Message Queue; Fatigue notification (server -> client) ---
// Demultiplexing: mtype = staff_id + 1 (SysV requires mtype > 0).
typedef struct {
  long mtype;        // staff_id + 1
  int staff_id;      
  role_t role;       // role whose perceived fatigue increased
  level_t new_lvl;
} msg_fatigue_t;

// ---- Shared Memory: Dining Room (read-only view for client) ----

typedef struct {
  table_state_t state;
  char surname[MAX_SURNAME];
  level_t dirt_level;   // visible after eating
  level_t food_qty;     // visible after order
} table_t;

typedef struct {
  int tables_n;
  table_t tables[MAX_TABLES];
} shm_diningroom_t;

// ---- Shared Memory: Kitchen (read-only view for client) ----
typedef struct {
  int tables_n;
  tr_bool_t food_ready[MAX_TABLES];
  int pending_orders;
  level_t clean_plates;             
  level_t dirty_plates;
} shm_kitchen_t;

// ---- Shared Memory: Blackboard (RW with mutex/sem, but client may read-only) ----

typedef struct {
  int waiter;   // staff id or -1
  int cleaner;  // staff id or -1
} table_assigment_t;

typedef struct {
  int tables_n;
  table_assigment_t tables[MAX_TABLES];
  int cook;       // staff id or -1
  int cashier;    // staff id or -1
  int dishwasher; // staff id or -1
} shm_blackboard_t;

// ---- Shared Memory: Cash Desk (read-only view for client) ----
// Exposes the number of pending payments currently queued for the common cash desk.
// This SHM is updated ONLY by the server cash-desk process.
typedef struct {
  int pending_payments;
} shm_cashdesk_t;
