#include <iostream>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>
#include <random>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <iomanip>
// 包含 usb_mem 库的头文件
#include "ubs_mem_def.h"
#include "ubs_mem.h"
// 辅助函数：通过 pagemap 获取虚拟地址对应的物理地址 (需要 root 权限)
uintptr_t get_physical_address(uintptr_t virtual_address) {
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    
    size_t pagesize = getpagesize();
    size_t offset = (virtual_address / pagesize) * sizeof(uint64_t);
    
    uint64_t page_info;
    if (pread(fd, &page_info, sizeof(page_info), offset) != sizeof(page_info)) {
        close(fd);
        return 0;
    }
    close(fd);
    
    // 检查页是否在内存中 (第63位)
    if ((page_info & (1ULL << 63)) == 0) { 
        return 0;
    }
    
    // 获取物理页框号 (PFN, 0-54位)
    uint64_t pfn = page_info & ((1ULL << 55) - 1);
    return (pfn * pagesize) + (virtual_address % pagesize);
}
// 辅助函数：生成指定长度的随机字符串
std::string generate_random_string(size_t length) {
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> distribution(0, sizeof(alphanum) - 2);
    
    std::string str;
    str.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        str += alphanum[distribution(generator)];
    }
    return str;
}

int ubs_mem_share_memory_map_demo(const std::string& shared_name)
{
    size_t length = 0x400000UL; // 4MB
    void *address = nullptr;

    /* Map a shared memory. */
    auto ret = ubsmem_shmem_map(nullptr, length, PROT_WRITE | PROT_READ, MAP_SHARED, shared_name.c_str(), 0, &address);
    if (ret != UBSM_OK) {
        std::cerr << "Failed to map shared memory. ret: " << ret << std::endl;
        return -1;
    }
    std::cout << "Map shared memory succeeded. VAddr: " << address << std::endl;
    /* Do your work here... */
    std::cout << "Starting 120s random read/write test (10,000 times/sec)..." << std::endl;

    std::mt19937 rng(std::random_device{}());
    const size_t str_len = 16;
    std::uniform_int_distribution<size_t> offset_dist(0, length - str_len - 1);
    for (int sec = 0; sec < 120; ++sec) {
        auto start_time = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) {
            // 生成随机偏移量
            size_t offset = offset_dist(rng);
            char* ptr = static_cast<char*>(address) + offset;

            // 1. 写随机字符串
            std::string rand_str = generate_random_string(str_len);
            std::memcpy(ptr, rand_str.c_str(), str_len);

            // 2. 读字符串
            char read_buf[str_len + 1] = {0};
            std::memcpy(read_buf, ptr, str_len);

            // 为了避免IO瓶颈，每秒仅打印第一次读写的虚拟地址和物理地址
            if (i == 0) {
                uintptr_t vaddr = reinterpret_cast<uintptr_t>(ptr);
                uintptr_t paddr = get_physical_address(vaddr);

                std::cout << "[Sec " << std::setw(3) << sec + 1 << "/120] "
                          << "VAddr: 0x" << std::hex << vaddr
                          << " | PAddr: 0x" << paddr << std::dec
                          << " | Data: " << read_buf << std::endl;
            }
        }
        // 补齐1秒钟的剩余时间
        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        if (elapsed.count() < 1000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 - elapsed.count()));
        }
    }
    std::cout << "Test completed." << std::endl;
    /* Flush and invalidate the data cache after accessing shared memory to ensure coherency. */
    ret = ubsmem_shmem_set_ownership(shared_name.c_str(), address, length, PROT_NONE);
    if (ret != UBSM_OK) {
        std::cerr << "Failed to set shared memory ownership. ret: " << ret << std::endl;
        return -1;
    }
    /* Unmap the shared memory. */
    ret = ubsmem_shmem_unmap(address, length);
    if (ret != UBSM_OK) {
        std::cerr << "Failed to unmap shared memory. ret: " << ret << std::endl;
        return -1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    std::string shared_name = "default_shared_mem";
    // 允许通过命令行参数传递 shared_name
    if (argc > 1) {
        shared_name = argv[1];
    } else {
        std::cout << "Usage: " << argv[0] << " [shared_memory_name]" << std::endl;
        std::cout << "Using default name: " << shared_name << std::endl;
    }
    int ret = ubs_mem_share_memory_map_demo(shared_name);
    if (ret == 0) {
        std::cout << "Program exited successfully." << std::endl;
    } else {
        std::cerr << "Program exited with error." << std::endl;
    }
    return ret;
}
