#pragma once

#include <vector>
#include <string>
#include <iostream>

std::vector<std::string> exec(std::string command) {
   char buffer[128];
    std::vector<std::string> result;
    FILE* pipe = popen(command.c_str(), "r");
    try {
        while (fgets(buffer, sizeof buffer, pipe) != NULL) {
            result.push_back(buffer);
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }
    pclose(pipe);
    return result;
}

