#ifndef SHARED_H
#define SHARED_H

#include <semaphore.h>
#include <atomic>
#include <string>

#define MAX_ENTRIES 1024
#define KEY_SIZE 64
#define VALUE_SIZE 256

struct Entry {
    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    bool used;
};

struct SharedDB {
    Entry entries[MAX_ENTRIES];
};

extern SharedDB* db;
extern sem_t* mutex;
extern sem_t* write_lock;
extern int* read_count;

extern int aof_fd;
extern std::atomic<bool> running;

void init_shared_memory();  
void init_semaphores();     
void init_aof();            
void load_aof();            

void reader_lock();         
void reader_unlock();       
void writer_lock_func();    
void writer_unlock_func();  

void append_to_aof(const std::string& cmd);
void aof_flush_thread();     

void cleanup();

#endif 