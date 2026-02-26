#include <cynamodb/core/memory_resource.hpp>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>

namespace cynamodb::core {

TrackingMemoryResource& MemoryManager::global_resource() {
    static TrackingMemoryResource resource;
    return resource;
}

void MemoryManager::initialize() {
    std::pmr::set_default_resource(&global_resource());
}

double MemoryManager::get_fragmentation_ratio() {
    return 0.0;
}

void MemoryManager::on_memory_pressure() {
    std::cerr << "[MemoryManager] Memory pressure detected!" << std::endl;
}

} // namespace cynamodb::core
