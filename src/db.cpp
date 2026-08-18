#include "db.h"
#include <sqlite3.h>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <filesystem>

sqlite3* db;

static void execute_file(const char* path)
{
    auto size = std::filesystem::file_size(path);
    std::string content(size, '\0');
    std::ifstream in(path);
    in.read(&content[0], size);

    char* error_msg;
    sqlite3_exec(db, content.c_str(), NULL, NULL, &error_msg);
    if (error_msg != NULL) {
        printf("SQLite3 error: %s\n", error_msg);
        sqlite3_free(error_msg);
    }
}

void db_init()
{
    sqlite3_open("build/mp.db", &db);

    execute_file("sql/schema.sql");

    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(*) FROM Songs";
    sqlite3_prepare(db, query, -1, &stmt, NULL);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW) {
        puts("failed");
        sqlite3_finalize(stmt);
        return;
    }
    int num_rows = sqlite3_column_int(stmt, 0);
    printf("num rows: %d\n", num_rows);
    // load the example data
    if (num_rows == 0) {
        execute_file("sql/insert.sql");
    }
    sqlite3_finalize(stmt);
}

void db_cleanup()
{
    sqlite3_close(db);
}
