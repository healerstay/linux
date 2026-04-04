#include "shared.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <fstream>

SharedDB* db = nullptr;        
sem_t* mutex;                  
sem_t* write_lock;             
int* read_count;               

int aof_fd;                    
std::atomic<bool> running(true); 

void init_shared_memory() { 
    int shm_fd = shm_open("/mini_redis_shm", O_CREAT | O_RDWR, 0666); 
    ftruncate(shm_fd, sizeof(SharedDB) + sizeof(int)); 

    void* ptr = mmap(0, sizeof(SharedDB) + sizeof(int),
                     PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); 

    db = (SharedDB*)ptr; 
    read_count = (int*)((char*)ptr + sizeof(SharedDB)); 

    *read_count = 0; 
}

void init_semaphores() { 
    mutex = sem_open("/mini_redis_mutex", O_CREAT, 0666, 1); 
    write_lock = sem_open("/mini_redis_write", O_CREAT, 0666, 1); 
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
                if (!db->entries[i].used ||
                    strcmp(db->entries[i].key, key.c_str()) == 0) {

                    strncpy(db->entries[i].key, key.c_str(), KEY_SIZE);
                    strncpy(db->entries[i].value, value.c_str(), VALUE_SIZE);
                    db->entries[i].used = true;
                    break;
                }
            }
        }
    }
}

void cleanup() { 
        running = false; 

        close(aof_fd); 

        sem_close(mutex); 
        sem_close(write_lock); 

        sem_unlink("/mini_redis_mutex"); 
        sem_unlink("/mini_redis_write"); 

        munmap(db, sizeof(SharedDB) + sizeof(int)); 
        shm_unlink("/mini_redis_shm"); 
    }