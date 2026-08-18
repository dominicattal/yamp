#ifndef DB_H
#define DB_H

#include <sqlite3.h>

extern sqlite3* db;

void db_init();
void db_cleanup();

#endif
