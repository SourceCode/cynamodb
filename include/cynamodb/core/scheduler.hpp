#pragma once

#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <deque>
#include <mutex>
#include <random>
#include <memory>
#include <condition_variable>
#include <cynamodb/core/lock_free_queue.hpp>

namespace cynamodb::core {

enum class TaskPriority { Normal, High };

using Task = std::function<void()>;

struct WorkerState {
    std::atomic<uint64_t> tasks_completed{0};
    std::atomic<uint64_t> tasks_stolen{0};
};

class WorkStealingScheduler {
public:
    explicit WorkStealingScheduler(size_t num_threads);
    ~WorkStealingScheduler();

    void submit(Task task, TaskPriority priority = TaskPriority::Normal, std::string_view table_name = "");
    void stop();

private:
    struct Worker {
        size_t id;
        std::deque<Task> local_queue;
        std::mutex queue_mutex;
        std::condition_variable cv;
        bool has_work = false;
        WorkerState state;
        std::thread thread;
    };

    void worker_loop(size_t worker_id);
    void try_steal(size_t stealer_id);

    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<bool> running_{true};
    std::atomic<size_t> next_worker_{0};
    
    // Global fallback queue for overflow
    LockFreeQueue<Task, 1024> global_queue_;
};

} // namespace cynamodb::core
