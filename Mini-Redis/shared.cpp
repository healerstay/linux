#include "shared.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <string>
#include <iostream>
#include <atomic>

SharedDB* db = nullptr;        
sem_t* mutex = nullptr;                  
sem_t* write_lock = nullptr;             
int* read_count = nullptr;               

int aof_fd = -1;                    
std::atomic<bool> running(true); 

void init_shared_memory() { 
    int shm_fd = shm_open("/mini_redis_shm", O_CREAT | O_RDWR, 0666); 
    if (shm_fd == -1) {
        std::cerr << "failed to shm\n";
        exit(1);
    }

    if (ftruncate(shm_fd, sizeof(SharedDB) + sizeof(int)) == -1) {
        std::cerr << "failed to ftruncate\n";
        close(shm_fd);
        exit(1);
    }

    void* ptr = mmap(0, sizeof(SharedDB) + sizeof(int),
                     PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); 
    if (ptr == MAP_FAILED) {
        std::cerr << "failed to mmap\n";
        close(shm_fd);
        exit(1);
    }

    db = (SharedDB*)ptr; 
    read_count = (int*)((char*)ptr + sizeof(SharedDB)); 
    *read_count = 0; 

    close(shm_fd);
}

void init_semaphores() { 
    mutex = sem_open("/mini_redis_mutex", O_CREAT, 0666, 1); 
    if (mutex == SEM_FAILED) {
        std::cerr << "failed to mutex\n";
        exit(1);
    }

    write_lock = sem_open("/mini_redis_write", O_CREAT, 0666, 1); 
    if (write_lock == SEM_FAILED) {
        std::cerr << "failed to write_lock\n";
        sem_close(mutex);
        exit(1);
    }
}

void reader_lock() { 
    sem_wait(mutex);
    (*read_count)++;
    if (*read_count == 1) sem_wait(write_lock);
    sem_post(mutex);
}

void reader_unlock() { 
    sem_wait(mutex);
    (*read_count)--;
    if (*read_count == 0) sem_post(write_lock);
    sem_post(mutex);
}

void writer_lock_func() { sem_wait(write_lock); } 
void writer_unlock_func() { sem_post(write_lock); } 

void init_aof() { 
    aof_fd = open("appendonly.aof", O_CREAT | O_RDWR | O_APPEND, 0666); 
    if (aof_fd == -1) {
        std::cerr << "failed to aof\n";
        exit(1);
    }
}

void append_to_aof(const std::string& cmd) { 
    write(aof_fd, cmd.c_str(), cmd.size()); 
    write(aof_fd, "\n", 1); 
}

void aof_flush_thread() { 
    while (running) {     
        sleep(4);         
        fsync(aof_fd);    
    }
}

void load_aof() { 
    std::ifstream file("appendonly.aof"); 
    if (!file.is_open()) {
        std::cerr << "failed to open aof\n";
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        size_t first = line.find(' ');
        if (first == std::string::npos) continue;

        std::string cmd = line.substr(0, first);
        std::string rest = line.substr(first + 1);

        if (cmd == "SET") {
            size_t second = rest.find(' ');
            if (second == std::string::npos) continue;

            std::string key = rest.substr(0, second);
            std::string value = rest.substr(second + 1);

            for (int i = 0; i < MAX_ENTRIES; i++) {
                if (!db->entries[i].used) {
                    strncpy(db->entries[i].key, key.c_str(), KEY_SIZE);
                    strncpy(db->entries[i].value, value.c_str(), VALUE_SIZE);
                    db->entries[i].used = true;
                    break;
                } else {
                    if (strcmp(db->entries[i].key, key.c_str()) == 0) {
                        strncpy(db->entries[i].key, key.c_str(), KEY_SIZE);
                        strncpy(db->entries[i].value, value.c_str(), VALUE_SIZE);
                        db->entries[i].used = true;
                        break;
                    }
                }
            }
        }
    }
}

void cleanup() { 
    running = false; 

    if (aof_fd != -1) close(aof_fd);

    if (mutex != nullptr) {
        sem_close(mutex);
        sem_unlink("/mini_redis_mutex");
    }

    if (write_lock != nullptr) {
        sem_close(write_lock);
        sem_unlink("/mini_redis_write");
    }

    if (db != nullptr) {
        munmap(db, sizeof(SharedDB) + sizeof(int));
        shm_unlink("/mini_redis_shm");
    }
}