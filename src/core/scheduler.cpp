#include <cynamodb/core/scheduler.hpp>
#include <algorithm>

namespace cynamodb::core {

WorkStealingScheduler::WorkStealingScheduler(size_t num_threads) {
    if (num_threads == 0) num_threads = 1;
    for (size_t i = 0; i < num_threads; ++i) {
        auto w = std::make_unique<Worker>();
        w->id = i;
        workers_.push_back(std::move(w));
    }
    for (size_t i = 0; i < num_threads; ++i) {
        workers_[i]->thread = std::thread(&WorkStealingScheduler::worker_loop, this, i);
    }
}

WorkStealingScheduler::~WorkStealingScheduler() {
    stop();
}

void WorkStealingScheduler::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) return;

    for (auto& w : workers_) {
        {
            std::lock_guard lock(w->queue_mutex);
            w->has_work = true;
        }
        w->cv.notify_all();
    }

    for (auto& w : workers_) {
        if (w->thread.joinable()) w->thread.join();
    }
}

void WorkStealingScheduler::submit(Task task, TaskPriority priority, std::string_view table_name) {
    if (!running_) return;
    
    size_t target_id = 0;
    if (!table_name.empty()) {
        target_id = std::hash<std::string_view>{}(table_name) % workers_.size();
    } else {
        target_id = next_worker_.fetch_add(1, std::memory_order_relaxed) % workers_.size();
    }

    auto& w = workers_[target_id];
    {
        std::lock_guard lock(w->queue_mutex);
        if (priority == TaskPriority::High) {
            w->local_queue.push_front(std::move(task));
        } else {
            w->local_queue.push_back(std::move(task));
        }
        w->has_work = true;
    }
    w->cv.notify_one();
}

void WorkStealingScheduler::worker_loop(size_t id) {
    auto& self = workers_[id];
    while (running_) {
        Task task;
        bool found = false;

        {
            std::unique_lock lock(self->queue_mutex);
            if (!self->local_queue.empty()) {
                task = std::move(self->local_queue.front());
                self->local_queue.pop_front();
                found = true;
            } else if (running_) {
                lock.unlock();
                // Try global queue first
                auto global_task = global_queue_.dequeue();
                if (global_task) {
                    task = std::move(*global_task);
                    found = true;
                } else {
                    try_steal(id);
                    lock.lock();
                    if (!self->local_queue.empty()) {
                        task = std::move(self->local_queue.front());
                        self->local_queue.pop_front();
                        found = true;
                    } else if (running_) {
                        self->has_work = false;
                        self->cv.wait_for(lock, std::chrono::milliseconds(10), [this, &self] {
                            return !running_ || self->has_work || !self->local_queue.empty();
                        });
                        if (!self->local_queue.empty()) {
                            task = std::move(self->local_queue.front());
                            self->local_queue.pop_front();
                            found = true;
                        }
                    }
                }
            }
        }

        if (found && task) {
            task();
            self->state.tasks_completed.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void WorkStealingScheduler::try_steal(size_t stealer_id) {
    if (!running_ || workers_.size() <= 1) return;
    
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, workers_.size() - 1);
    
    size_t victim_id = dist(rng);
    if (victim_id == stealer_id) return;

    auto& victim = workers_[victim_id];
    auto& stealer = workers_[stealer_id];

    if (victim->queue_mutex.try_lock()) {
        if (victim->local_queue.size() > 1) {
            size_t to_steal = victim->local_queue.size() / 2;
            {
                std::lock_guard s_lock(stealer->queue_mutex);
                for (size_t i = 0; i < to_steal; ++i) {
                    stealer->local_queue.push_back(std::move(victim->local_queue.back()));
                    victim->local_queue.pop_back();
                }
                stealer->has_work = true;
            }
            stealer->state.tasks_stolen.fetch_add(to_steal, std::memory_order_relaxed);
            stealer->cv.notify_one();
        }
        victim->queue_mutex.unlock();
    }
}

} // namespace cynamodb::core
