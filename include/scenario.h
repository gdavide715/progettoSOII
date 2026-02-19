#pragma once

#include <stdint.h>

// ---- Scenario limits ----

#define MAX_STAFF   4
#define MAX_TABLES  5
#define MAX_NAME    16
#define MAX_SURNAME 16
#define MAX_GROUP   128

#define STUDENTID_MAX 3
#define STUDENTID_MAXLEN 9

#define NUM_ROLES 5
#define NUM_SKILLS 4
#define NUM_TRAITS 4

// ---- Staff roles ----
typedef enum {
  ROLE_WAITER = 0,        // order taking / food serving (per-table)
  ROLE_COOK = 1,          // common station
  ROLE_HELPER = 2,        // table cleaning / assisting (per-table)
  ROLE_CASHIER = 3,       // common station
  ROLE_DISHWASHER = 4,    // common station
  ROLE_NONE = -1
} role_t;

typedef enum {
  SKILL_WAITER = 0,
  SKILL_COOK = 1,
  SKILL_HELPER = 2,
  SKILL_CASHIER = 3
} skill_t;

typedef enum {
  TRAIT_PATIENCE = 0,
  TRAIT_SOCIABILITY = 1,
  TRAIT_PROFESSIONALITY = 2,
  TRAIT_RESISTANCE = 3
} trait_t;

// ---- Table state ----
typedef enum {
  TABLE_EMPTY = 0,
  TABLE_TAKEN = 1,
  TABLE_SERVED = 2,
  TABLE_FREED = 3
} table_state_t;

// ---- Strategy enumeration ----
typedef enum {
  STRATEGY_NONE = 0,
  STRATEGY_PROFIT = 1,
  STRATEGY_REPUTATION = 2
} strategy_t;

// ---- Confidence bucket for staff parameters ----
typedef enum {
  PARAM_LOW = 0,
  PARAM_MEDIUM = 1,
  PARAM_HIGH = 2
} param_bucket_t;

// ---- General enum for level, including none ----
typedef enum { 
  LVL_NONE = 0, 
  LVL_LOW = 1, 
  LVL_MED = 2, 
  LVL_HIGH = 3 
} level_t;
