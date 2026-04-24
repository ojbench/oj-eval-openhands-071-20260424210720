#pragma once
#include "interface.h"
#include "definition.h"
// You should not use those functions in runtime.h


#include <algorithm>
#include <cmath>
#include <vector>
#include <map>
#include <unordered_map>
#include <deque>
#include <queue>

namespace oj {

auto generate_tasks(const Description &desc) -> std::vector <Task> {
    std::vector<Task> tasks;
    task_id_t n = desc.task_count;
    
    double total_execution_time = 0;
    priority_t total_priority = 0;
    
    double work_rate = std::pow(PublicInformation::kCPUCount, PublicInformation::kAccel);

    for (task_id_t i = 0; i < n; ++i) {
        Task t;
        t.launch_time = (i * (desc.deadline_time.max - 100)) / n;
        t.execution_time = desc.execution_time_single.min;
        t.priority = desc.priority_single.min;
        
        time_t min_duration = 4 + std::ceil(t.execution_time / work_rate);
        t.deadline = std::min(desc.deadline_time.max, t.launch_time + min_duration + 10);
        
        tasks.push_back(t);
        total_execution_time += t.execution_time;
        total_priority += t.priority;
    }
    
    // Adjust to meet sum constraints
    for (task_id_t i = 0; i < n && total_execution_time < desc.execution_time_sum.min; ++i) {
        time_t can_add = desc.execution_time_single.max - tasks[i].execution_time;
        time_t add = std::min(can_add, (time_t)(desc.execution_time_sum.min - total_execution_time));
        
        tasks[i].execution_time += add;
        total_execution_time += add;
        
        // Update deadline to make it possible
        time_t min_duration = 4 + std::ceil(tasks[i].execution_time / work_rate);
        tasks[i].deadline = std::max(tasks[i].deadline, tasks[i].launch_time + min_duration + 1);
        if (tasks[i].deadline > desc.deadline_time.max) {
            // If we exceeded max deadline, we must reduce execution time
            time_t over = tasks[i].deadline - desc.deadline_time.max;
            // This is a bit rough, but let's just cap it
            tasks[i].deadline = desc.deadline_time.max;
            time_t max_dur = tasks[i].deadline - tasks[i].launch_time - 4;
            if (max_dur > 0) {
                time_t max_exec = std::floor(max_dur * work_rate);
                time_t old_exec = tasks[i].execution_time;
                tasks[i].execution_time = std::min(old_exec, max_exec);
                total_execution_time -= (old_exec - tasks[i].execution_time);
            } else {
                // Should not happen with reasonable constraints
            }
        }
    }
    
    for (task_id_t i = 0; i < n && total_priority < desc.priority_sum.min; ++i) {
        priority_t add = std::min(desc.priority_single.max - tasks[i].priority,
                                  (priority_t)(desc.priority_sum.min - total_priority));
        tasks[i].priority += add;
        total_priority += add;
    }

    return tasks;
}

struct TaskInfo {
    task_id_t id;
    Task task;
    double remaining_work;
    bool running;
    bool saving;
    time_t state_start_time;
    cpu_id_t assigned_cpus;
};

struct CompareTask {
    bool operator()(task_id_t a, task_id_t b);
};

static std::map<task_id_t, TaskInfo> active_tasks;
static std::vector<task_id_t> pending_tasks;
static std::vector<task_id_t> running_tasks;
static std::vector<task_id_t> saving_tasks;
static cpu_id_t current_cpu_usage = 0;
static task_id_t global_task_id_counter = 0;

bool CompareTask::operator()(task_id_t a, task_id_t b) {
    auto it_a = active_tasks.find(a);
    auto it_b = active_tasks.find(b);
    // This should not happen if we manage pending_tasks correctly,
    // but we need a stable comparison.
    if (it_a == active_tasks.end() || it_b == active_tasks.end()) {
        return a < b;
    }
    if (it_a->second.task.priority != it_b->second.task.priority)
        return it_a->second.task.priority < it_b->second.task.priority;
    return it_a->second.task.deadline > it_b->second.task.deadline;
}

auto schedule_tasks(time_t time, std::vector <Task> list, const Description &desc) -> std::vector<Policy> {
    if (time == 0) {
        active_tasks.clear();
        while (!pending_tasks.empty()) pending_tasks.pop();
        running_tasks.clear();
        saving_tasks.clear();
        current_cpu_usage = 0;
        global_task_id_counter = 0;
    }
    for (auto &t : list) {
        active_tasks[global_task_id_counter] = {global_task_id_counter, t, (double)t.execution_time, false, false, 0, 0};
        pending_tasks.push(global_task_id_counter);
        global_task_id_counter++;
    }

    std::vector<Policy> policies;

    // 1. Update status of tasks that were saving
    for (auto it = saving_tasks.begin(); it != saving_tasks.end(); ) {
        TaskInfo &info = active_tasks[*it];
        if (info.state_start_time + PublicInformation::kSaving == time) {
            info.saving = false;
            current_cpu_usage -= info.assigned_cpus;
            info.assigned_cpus = 0;
            if (info.remaining_work <= 0) {
                active_tasks.erase(*it);
            }
            it = saving_tasks.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Check running tasks for completion or deadline
    for (auto it = running_tasks.begin(); it != running_tasks.end(); ) {
        TaskInfo &info = active_tasks[*it];
        double work_rate = std::pow(info.assigned_cpus, PublicInformation::kAccel);
        double work_done = 0;
        if (time - info.state_start_time >= PublicInformation::kStartUp) {
            work_done = (time - info.state_start_time - PublicInformation::kStartUp) * work_rate;
        }
        
        if (work_done >= info.remaining_work) {
            info.remaining_work = 0;
            info.running = false;
            info.saving = true;
            info.state_start_time = time;
            policies.push_back(Saving{info.id});
            saving_tasks.push_back(*it);
            it = running_tasks.erase(it);
        } else if (time + PublicInformation::kSaving + 1 >= info.task.deadline) {
            info.remaining_work -= work_done;
            info.running = false;
            info.saving = true;
            info.state_start_time = time;
            policies.push_back(Saving{info.id});
            saving_tasks.push_back(*it);
            it = running_tasks.erase(it);
        } else {
            ++it;
        }
    }

    // 3. Start new tasks from pending
    if (added) {
        std::sort(pending_tasks.begin(), pending_tasks.end(), [](task_id_t a, task_id_t b) {
            if (active_tasks[a].task.priority != active_tasks[b].task.priority)
                return active_tasks[a].task.priority > active_tasks[b].task.priority;
            return active_tasks[a].task.deadline < active_tasks[b].task.deadline;
        });
    }

    for (auto it = pending_tasks.begin(); it != pending_tasks.end(); ) {
        if (current_cpu_usage >= desc.cpu_count) break;
        
        task_id_t id = *it;
        if (active_tasks.count(id) == 0) {
            it = pending_tasks.erase(it);
            continue;
        }
        TaskInfo &info = active_tasks[id];
        
        time_t available_time = (info.task.deadline > time + 4) ? (info.task.deadline - time - 4) : 0;
        cpu_id_t needed_cpus = 0;
        if (available_time > 0) {
            needed_cpus = std::ceil(std::pow(info.remaining_work / available_time, 1.0 / PublicInformation::kAccel));
        }
        if (needed_cpus == 0) needed_cpus = 1;
        
        if (needed_cpus <= desc.cpu_count && current_cpu_usage + needed_cpus <= desc.cpu_count) {
            info.running = true;
            info.state_start_time = time;
            info.assigned_cpus = needed_cpus;
            current_cpu_usage += needed_cpus;
            policies.push_back(Launch{needed_cpus, id});
            running_tasks.push_back(id);
            it = pending_tasks.erase(it);
        } else if (needed_cpus > desc.cpu_count) {
            active_tasks.erase(id);
            it = pending_tasks.erase(it);
        } else {
            ++it;
        }
    }

    return policies;
}

} // namespace oj

