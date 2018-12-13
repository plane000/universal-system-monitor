#include <sys.h>
#include <memstat.h>
#include <cpu.h>

#include <iostream>
#include <unistd.h>

int main(int argc, char** argv) {
    CPU* cpu = new CPU();
    CPU_Instance = cpu;

    Memory* mem = new Memory();
    Memory_Instance = mem;

    cpu->START_CPU_POLLING();
    mem->START_MEMORY_POLLING();

    while(1) {
        sleep(1);
        std::cout << std::endl;
        for (unsigned int thread = 0; thread <= cpu->CPU_HARDWARE_THREADS; thread++) {
            std::cout << "CORE " << thread << " USAGE: " << cpu->CPU_PERCENT(thread) << std::endl;
        }
        std::cout << "CPU FREQUENCY: " << cpu->cpuStat->FREQ << "MHz" << std::endl;
    }
}
