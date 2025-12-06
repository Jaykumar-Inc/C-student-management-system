#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <sys/stat.h>

/* ------------------- FILE NAMES ------------------- */
#define STUDENT_FILE "students.txt"
#define CREDENTIAL_FILE "credentials.txt"

/* ------------------- STRUCTURES ------------------- */
struct Student {
    int roll;
    char name[50];
    float marks;
};

/* ANSI color codes for simple UI styling */
#define COL_RESET "\x1b[0m"
#define COL_GREEN "\x1b[32m"
#define COL_CYAN  "\x1b[36m"
#define COL_YELLOW "\x1b[33m"
#define COL_RED   "\x1b[31m"

/* ------------------- GLOBAL VARIABLES ------------------- */
char currentRole[16];
char currentUser[50];

/* SQLite DB handle */
sqlite3 *g_db = NULL;

/* ------------------- FUNCTION DECLARATIONS ------------------- */
int loginSystem();
void mainMenu();
void adminMenu();
void staffMenu();
void guestMenu();
void addStudent();
void displayStudents();
void searchStudent();
void updateStudent();
void deleteStudent();

/* DB helpers */
int init_db();
void close_db();
void migrate_files_to_db();

/* UI helpers */
void flush_stdin() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}
void print_header(const char *title) {
    printf(COL_GREEN "\n==== %s ====" COL_RESET "\n", title);
}
int confirm_prompt(const char *prompt) {
    char ans[8];
    printf(COL_YELLOW "%s (y/N): " COL_RESET, prompt);
    if (fgets(ans, sizeof(ans), stdin) == NULL) return 0;
    return (ans[0] == 'y' || ans[0] == 'Y');
}

/* ---------------------- MAIN ---------------------- */
int main() {
    if (init_db() != 0) {
        fprintf(stderr, "Failed to initialize DB.\n");
        return 1;
    }

    if (loginSystem()) {
        mainMenu();
    } else {
        printf("\nLogin Failed. Exiting...\n");
    }

    close_db();
    return 0;
}

/* ---------------------- DB & MIGRATION ---------------------- */
static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

int init_db() {
    int rc;

    rc = sqlite3_open("students.db", &g_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open DB: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    const char *sql_create_students =
        "CREATE TABLE IF NOT EXISTS students("
        "roll INTEGER PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "marks REAL NOT NULL);";

    const char *sql_create_creds =
        "CREATE TABLE IF NOT EXISTS credentials("
        "username TEXT PRIMARY KEY,"
        "password TEXT NOT NULL,"
        "role TEXT NOT NULL);";

    char *errmsg = NULL;
    rc = sqlite3_exec(g_db, sql_create_students, 0, 0, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    rc = sqlite3_exec(g_db, sql_create_creds, 0, 0, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }

    migrate_files_to_db();

    /* Ensure at least one admin exists */
    const char *check_admin = "SELECT COUNT(*) FROM credentials;";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(g_db, check_admin, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int cnt = sqlite3_column_int(stmt, 0);
            if (cnt == 0) {
                const char *ins = "INSERT INTO credentials(username,password,role) VALUES('admin','admin','ADMIN');";
                sqlite3_exec(g_db, ins, 0, 0, &errmsg);
            }
        }
    }
    if (stmt) sqlite3_finalize(stmt);

    return 0;
}

void close_db() {
    if (g_db) sqlite3_close(g_db);
}

void migrate_files_to_db() {
    /* Migrate students.txt */
    if (file_exists(STUDENT_FILE)) {
        FILE *f = fopen(STUDENT_FILE, "r");
        if (f) {
            int roll; char name[128]; float marks;
            const char *sql = "INSERT OR IGNORE INTO students(roll,name,marks) VALUES(?,?,?);";
            sqlite3_stmt *stmt = NULL;
            sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
            while (fscanf(f, "%d %127s %f", &roll, name, &marks) == 3) {
                sqlite3_reset(stmt);
                sqlite3_bind_int(stmt, 1, roll);
                sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 3, marks);
                sqlite3_step(stmt);
            }
            if (stmt) sqlite3_finalize(stmt);
            fclose(f);
        }
    }

    /* Migrate credentials.txt (format: username password ROLE) */
    if (file_exists(CREDENTIAL_FILE)) {
        FILE *f = fopen(CREDENTIAL_FILE, "r");
        if (f) {
            char user[128], pass[128], role[32];
            const char *sql = "INSERT OR IGNORE INTO credentials(username,password,role) VALUES(?,?,?);";
            sqlite3_stmt *stmt = NULL;
            sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
            while (fscanf(f, "%127s %127s %31s", user, pass, role) == 3) {
                sqlite3_reset(stmt);
                sqlite3_bind_text(stmt, 1, user, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, pass, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, role, -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
            }
            if (stmt) sqlite3_finalize(stmt);
            fclose(f);
        }
    }
}

/* ---------------------- LOGIN SYSTEM ---------------------- */
int loginSystem() {
    char username[64], password[64];
    int rc;

    print_header("LOGIN SCREEN");
    printf("Username: ");
    if (scanf("%63s", username) != 1) return 0;
    printf("Password: ");
    if (scanf("%63s", password) != 1) return 0;
    flush_stdin();

    /* Query credentials table */
    const char *sql = "SELECT role FROM credentials WHERE username = ? AND password = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;

    rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "DB error: %s\n", sqlite3_errmsg(g_db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *role = sqlite3_column_text(stmt, 0);
        strncpy(currentRole, (const char*)role, sizeof(currentRole)-1);
        currentRole[sizeof(currentRole)-1] = '\0';
        strncpy(currentUser, username, sizeof(currentUser)-1);
        currentUser[sizeof(currentUser)-1] = '\0';
        sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

/* ---------------------- MAIN MENU ---------------------- */
void mainMenu() {
    if (strcmp(currentRole, "ADMIN") == 0)
        adminMenu();
    else if (strcmp(currentRole, "STAFF") == 0)
        staffMenu();
    else
        guestMenu();
}

/* ---------------------- ADMIN MENU ---------------------- */
void adminMenu() {
    int choice;
    do {
        printf(COL_CYAN "\n===== ADMIN MENU (user: %s) =====\n" COL_RESET, currentUser);
        printf("1. Add Student\n");
        printf("2. Display Students\n");
        printf("3. Search Student\n");
        printf("4. Update Student\n");
        printf("5. Delete Student\n");
        printf("6. Logout\n");
        printf("Enter choice: ");
        if (scanf("%d", &choice) != 1) { flush_stdin(); choice = -1; }

        switch (choice) {
            case 1: addStudent(); break;
            case 2: displayStudents(); break;
            case 3: searchStudent(); break;
            case 4: updateStudent(); break;
            case 5: deleteStudent(); break;
            case 6: printf("Logging out...\n"); return;
            default: printf(COL_RED "Invalid choice!" COL_RESET "\n");
        }
        flush_stdin();
    } while (1);
}

/* ---------------------- STAFF MENU ---------------------- */
void staffMenu() {
    int choice;
    do {
        printf(COL_CYAN "\n===== STAFF MENU (user: %s) =====\n" COL_RESET, currentUser);
        printf("1. Display Students\n");
        printf("2. Search Student\n");
        printf("3. Logout\n");
        printf("Enter choice: ");
        if (scanf("%d", &choice) != 1) { flush_stdin(); choice = -1; }

        switch (choice) {
            case 1: displayStudents(); break;
            case 2: searchStudent(); break;
            case 3: printf("Logging out...\n"); return;
            default: printf(COL_RED "Invalid choice!" COL_RESET "\n");
        }
        flush_stdin();
    } while (1);
}

/* ---------------------- GUEST MENU ---------------------- */
void guestMenu() {
    int choice;
    do {
        printf(COL_CYAN "\n===== GUEST MENU (user: %s) =====\n" COL_RESET, currentUser);
        printf("1. Display Students\n");
        printf("2. Logout\n");
        printf("Enter choice: ");
        if (scanf("%d", &choice) != 1) { flush_stdin(); choice = -1; }

        switch (choice) {
            case 1: displayStudents(); break;
            case 2: printf("Logging out...\n"); return;
            default: printf(COL_RED "Invalid choice!" COL_RESET "\n");
        }
        flush_stdin();
    } while (1);
}

/* ---------------------- ADD STUDENT ---------------------- */
void addStudent() {
    struct Student st;
    printf("\n");
    print_header("ADD STUDENT");
    printf("Enter Roll: ");
    if (scanf("%d", &st.roll) != 1) { flush_stdin(); printf(COL_RED "Invalid input." COL_RESET "\n"); return; }
    printf("Enter Name: ");
    if (scanf("%49s", st.name) != 1) { flush_stdin(); return; }
    printf("Enter Marks: ");
    if (scanf("%f", &st.marks) != 1) { flush_stdin(); printf(COL_RED "Invalid marks." COL_RESET "\n"); return; }
    flush_stdin();

    const char *sql = "INSERT INTO students(roll,name,marks) VALUES(?,?,?);";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "DB error: %s\n", sqlite3_errmsg(g_db));
        return;
    }
    sqlite3_bind_int(stmt, 1, st.roll);
    sqlite3_bind_text(stmt, 2, st.name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, st.marks);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Insert failed: %s\n", sqlite3_errmsg(g_db));
    } else {
        printf(COL_GREEN "Student Added Successfully!" COL_RESET "\n");
    }
    sqlite3_finalize(stmt);
}

/* ---------------------- DISPLAY STUDENTS ---------------------- */
void displayStudents() {
    print_header("STUDENT RECORDS");
    printf(COL_YELLOW "%5s  %-20s %6s" COL_RESET "\n", "ROLL", "NAME", "MARKS");
    printf("------------------------------------------------\n");

    const char *sql = "SELECT roll,name,marks FROM students ORDER BY roll;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "DB error: %s\n", sqlite3_errmsg(g_db));
        return;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int roll = sqlite3_column_int(stmt, 0);
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        double marks = sqlite3_column_double(stmt, 2);
        printf("%5d  %-20s %6.2f\n", roll, name, marks);
    }
    sqlite3_finalize(stmt);
}

/* ---------------------- SEARCH STUDENT ---------------------- */
void searchStudent() {
    int roll;
    print_header("SEARCH STUDENT");
    printf("Enter Roll Number to Search: ");
    if (scanf("%d", &roll) != 1) { flush_stdin(); printf(COL_RED "Invalid input." COL_RESET "\n"); return; }
    flush_stdin();

    const char *sql = "SELECT roll,name,marks FROM students WHERE roll = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) { fprintf(stderr, "DB error: %s\n", sqlite3_errmsg(g_db)); return; }

    sqlite3_bind_int(stmt, 1, roll);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        printf(COL_GREEN "\n====== STUDENT FOUND ======" COL_RESET "\n");
        printf("Roll Number : %d\n", sqlite3_column_int(stmt, 0));
        printf("Name        : %s\n", sqlite3_column_text(stmt, 1));
        printf("Marks       : %.2f\n", sqlite3_column_double(stmt, 2));
    } else {
        printf(COL_RED "\nStudent with Roll %d NOT FOUND!" COL_RESET "\n", roll);
    }
    sqlite3_finalize(stmt);
}

/* ---------------------- UPDATE STUDENT ---------------------- */
void updateStudent() {
    int roll;
    print_header("UPDATE STUDENT");
    printf("Enter Roll Number to Update: ");
    if (scanf("%d", &roll) != 1) { flush_stdin(); printf(COL_RED "Invalid input." COL_RESET "\n"); return; }
    flush_stdin();

    const char *sql_check = "SELECT roll FROM students WHERE roll = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql_check, -1, &stmt, NULL) != SQLITE_OK) { fprintf(stderr, "DB error\n"); return; }
    sqlite3_bind_int(stmt, 1, roll);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_ROW) { printf(COL_RED "Student Not Found!" COL_RESET "\n"); return; }

    struct Student st;
    st.roll = roll;
    printf("Enter New Name: ");
    if (scanf("%49s", st.name) != 1) { flush_stdin(); return; }
    printf("Enter New Marks: ");
    if (scanf("%f", &st.marks) != 1) { flush_stdin(); return; }
    flush_stdin();

    const char *sql = "UPDATE students SET name = ?, marks = ? WHERE roll = ?;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) { fprintf(stderr, "DB error\n"); return; }
    sqlite3_bind_text(stmt, 1, st.name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, st.marks);
    sqlite3_bind_int(stmt, 3, st.roll);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) printf(COL_RED "Update failed." COL_RESET "\n"); else printf(COL_GREEN "Student Updated Successfully!" COL_RESET "\n");
    sqlite3_finalize(stmt);
}

/* ---------------------- DELETE STUDENT ---------------------- */
void deleteStudent() {
    int roll;
    print_header("DELETE STUDENT");
    printf("Enter Roll Number to Delete: ");
    if (scanf("%d", &roll) != 1) { flush_stdin(); printf(COL_RED "Invalid input." COL_RESET "\n"); return; }
    flush_stdin();

    if (!confirm_prompt("Are you sure you want to delete this student?")) {
        printf("Aborted.\n");
        return;
    }

    const char *sql = "DELETE FROM students WHERE roll = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) { fprintf(stderr, "DB error\n"); return; }
    sqlite3_bind_int(stmt, 1, roll);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) printf(COL_RED "Delete failed." COL_RESET "\n"); else printf(COL_GREEN "Student Deleted Successfully!" COL_RESET "\n");
    sqlite3_finalize(stmt);
}

