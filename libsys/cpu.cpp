#include "cpu.h"
#include "common.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <regex>
#include <unistd.h>
#include <math.h> 

CPU::CPU() {
    this->CPU_HARDWARE_THREADS = std::thread::hardware_concurrency();
    this->UPDATE_INTERVAL = 1000;
    this->m_isPolling = false;
    this->cpuStat = new CPUStat();
    this->cpuStat->HARDWARE_THREADS = this->CPU_HARDWARE_THREADS;

    std::vector<std::string> lscpu = execcommand("lscpu");
    for (unsigned int i = 0; i < lscpu.size(); i++) {
        std::regex architecture("Architecture.*?((?:[a-z][a-z0-9_]*))");
        std::regex maxMHz      ("CPU max MHz.*?((?:[0-9].*))");
        std::regex minMHz      ("CPU min MHz.*?((?:[0-9].*))");
        std::regex MHz         ("CPU MHz.*?((?:[0-9].*))");
        std::smatch m;

        if (std::regex_search(lscpu[i], m, architecture)) {
            this->cpuStat->ARCHITECTURE = m[1].str();
        } else if (std::regex_search(lscpu[i], m, maxMHz)) {
            this->cpuStat->MAX_FREQ = std::stod(m[1].str());
        } else if (std::regex_search(lscpu[i], m, minMHz)) {
            this->cpuStat->MIN_FREQ = std::stod(m[1].str());
        } else if (std::regex_search(lscpu[i], m, MHz)) {
            this->cpuStat->FREQ = std::stod(m[1].str());
        }                
    }
    std::cout << "Number of hardware threads supported: " << CPU_HARDWARE_THREADS << std::endl;
}

CPUStat CPU::getCPUStat() {
    CPUStat temp;
    this->CPU_Mutex.lock();
    memcpy(&temp, this->cpuStat, sizeof(CPUStat));
    this->CPU_Mutex.unlock();
    return temp;
}

void CPU::START_CPU_POLLING() {
    if (this->m_isPolling) {
        return;
    }
    this->m_isPolling = true;
    std::cout << "Starting CPU worker thread" << std::endl;
    this->m_pollThread = new std::thread(&CPU::CPU_POLL, this);
}

void CPU::CPU_POLL(CPU* cpu) {
    std::cout << "New CPU worker thread" << std::endl;

    while (true) {
        cpu->CPU_Mutex.lock();
        if (!cpu->m_isPolling) {
            cpu->CPU_Mutex.unlock();
            return;
        }
        cpu->CPU_Mutex.unlock();

        // READ TOTAL CPU
        std::vector<std::string> ProcStatLines;
        unsigned long long luser, nice, system, idle, iowait,
            irq, softirq = 0;

        std::ifstream ProcStatFile("/proc/stat");
        for (std::string str; std::getline(ProcStatFile, str); )
            ProcStatLines.push_back(str);

        sscanf(ProcStatLines[0].c_str(), "cpu %llu %llu %llu %llu %llu %llu %llu", &luser, 
            &nice, &system, &idle, &iowait, &irq, &softirq);

        cpu->CPU_Mutex.lock();

        // CALCULATE TOTAL CPU
        cpu->CPU_PREVIOUS_TOTAL = cpu->CPU_TOTAL;
        cpu->CPU_PREVIOUS_WORK = cpu->CPU_WORK;
        cpu->CPU_TOTAL = luser + nice + system + idle + iowait + irq + softirq;
        cpu->CPU_WORK = luser + nice + system;

        cpu->CPU_Mutex.unlock();
        
        for (unsigned int thread = 0; thread < cpu->CPU_HARDWARE_THREADS; thread++) {
            // READ TOTAL THREAD CPU
            unsigned int currentThread = thread + 1;
            unsigned long long tluser, tnice, tsystem, tidle, tiowait,
                tirq, tsoftirq = 0;[[]]

            sscanf(ProcStatLines[currentThread].c_str(), "%*s %llu %llu %llu %llu %llu %llu %llu",
                        &tluser, &tnice, &tsystem, &tidle, &tiowait, &tirq, &tsoftirq);

            cpu->CPU_Mutex.lock();

            // CALCULATE TOTAL THREAD CPU
            cpu->CPU_PREVIOUS_CORES_WORK_AND_TOTAL[thread][0] = cpu->CPU_CORES_WORK_AND_TOTAL[thread][0];
            cpu->CPU_PREVIOUS_CORES_WORK_AND_TOTAL[thread][1] = cpu->CPU_CORES_WORK_AND_TOTAL[thread][1];
            cpu->CPU_CORES_WORK_AND_TOTAL[thread][0] = tluser + tnice + tsystem + tidle + tiowait + tirq + tsoftirq;
            cpu->CPU_CORES_WORK_AND_TOTAL[thread][1] = tluser + tnice + tsystem;
            
            cpu->CPU_Mutex.unlock();
        }
        cpu->CPU_Mutex.lock();

        long double totalOverTime = cpu->CPU_PREVIOUS_TOTAL - cpu->CPU_TOTAL;
        long double workOverTime = cpu->CPU_PREVIOUS_WORK - cpu->CPU_WORK;
        cpu->cpuStat->PERCENT_USAGE = (workOverTime / totalOverTime) * 100;

        cpu->CPU_Mutex.unlock();

        // GET CURRENT CLOCK SPEED FROM COMMAND OUTPUT FROM LSCPU
        std::vector<std::string> lscpu = execcommand("lscpu");
        for (unsigned int i = 0; i < lscpu.size(); i++) {
            std::regex MHz("CPU MHz.*?((?:[0-9].*))");
            std::smatch m;
            if (std::regex_search(lscpu[i], m, MHz)) {
                cpu->CPU_Mutex.lock();
                cpu->cpuStat->FREQ = std::stod(m[1].str());
                cpu->CPU_Mutex.unlock();
            }                
        }

        sleep(1);
    }
}

void CPU::END_CPU_POLLING() {
    if (!this->m_isPolling) {
        return;
    }
    this->m_isPolling = false;
    m_pollThread->join();
    delete m_pollThread;
}

double CPU::CPU_PERCENT(int core) {
    --core;

    long double totalOverTime = 0.0;
    long double workOverTime = 0.0;
    this->CPU_Mutex.lock();

    if (core == -1) {
        totalOverTime = this->CPU_PREVIOUS_TOTAL - this->CPU_TOTAL;
        workOverTime = this->CPU_PREVIOUS_WORK - this->CPU_WORK;
    } else {
        totalOverTime = this->CPU_PREVIOUS_CORES_WORK_AND_TOTAL[core][0] - this->CPU_CORES_WORK_AND_TOTAL[core][0];
        workOverTime = this->CPU_PREVIOUS_CORES_WORK_AND_TOTAL[core][1] - this->CPU_CORES_WORK_AND_TOTAL[core][1];
    }

    this->CPU_Mutex.unlock();

    double percent = (workOverTime / totalOverTime) * 100;
    if (isnan(percent)) percent = -1;
    return (double)percent;
}

std::vector<double> CPU::CPU_CORE_PERCENT() {
    std::vector<double> output;

    // TODO: Return a list of percents, one index is one hardware 
    //thread, index 0 is total percent

    return output;    
}

CPU::~CPU() {
    std::cout << "CPU Destructed" << std::endl;
    if (this->m_isPolling) {
        this->m_isPolling = false;
        m_pollThread->join();
    }
    delete m_pollThread;
}
