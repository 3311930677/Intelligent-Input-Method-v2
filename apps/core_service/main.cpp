#include "owo/ipc/named_pipe.h"

#include <iostream>

int main() {
    std::cout << "OwO Core Service P1 prototype is ready.\n";
    return owo::ipc::run_core_server(owo::ipc::kCorePipeName);
}
