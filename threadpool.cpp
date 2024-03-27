#include <iostream>
#include<thread>
#include<mutex>
#include <chrono>
#include<time.h>
#include<vector>
#include<queue>
#include<future>
#include <mutex>
#include <queue>
#include <functional>
#include <future>
#include <thread>
#include <utility>
#include <vector>
#include <condition_variable>
#include<string>
#include<shared_mutex>
using namespace std;

/**
 * @brief 一个线程安全的队列模板类。
 * 
 * @tparam T 队列元素的类型。
 * 这段代码提供了一个简洁但功能齐全的线程安全队列，支持基本的队列操作如入队（push）、出队（pop）、检查队列是否为空（empty）和获取队列大小（size）。
 * 通过使用共享锁和独占锁，它允许多个线程安全地访问队列，适用于需要线程同步的并发编程场景。
 */
template<typename T>
struct safe_queue {
    // 成员变量
    queue<T> que; ///< 主队列，用于存储元素。
    shared_mutex _m; ///< 互斥量，用于同步对队列的访问。

    /**
     * @brief 检查队列是否为空。
     * 
     * @return true 如果队列为空。
     * @return false 如果队列非空。
     */
    bool empty() {
        shared_lock<shared_mutex> lc(_m); // 使用共享锁来读取，允许并发读操作。
        return que.empty(); // 返回队列是否为空。
    }

    /**
     * @brief 获取队列中的元素数量。
     * 
     * @return 队列中的元素数量。
     */
    auto size() {
        shared_lock<shared_mutex> lc(_m); // 使用共享锁来读取，允许并发读操作。
        return que.size(); // 返回队列大小。
    }

    /**
     * @brief 向队列中添加一个元素。
     * 
     * @param t 要添加到队列的元素。
     */
    void push(T& t) {
        unique_lock<shared_mutex> lc(_m); // 使用独占锁来修改队列，防止同时有其他写操作。
        que.push(t); // 将元素添加到队列。
    }

    /**
     * @brief 从队列中弹出一个元素。
     * 
     * 如果队列为空，此函数返回false并且不修改传入的参数。
     * 
     * @param t 引用参数，用于存储弹出的元素。
     * @return true 如果成功弹出元素。
     * @return false 如果队列为空，无法弹出元素。
     */
    bool pop(T& t) {
        unique_lock<shared_mutex> lc(_m); // 使用独占锁来修改队列。
        if (que.empty()) return false; // 检查队列是否为空，为空则返回false。
        t = move(que.front()); // 移动队列前端元素到t。
        que.pop(); // 从队列中弹出元素。
        return true; // 返回成功弹出元素。
    }
};

/**
 * @brief 线程池类，用于管理线程集合以执行任务。
 * 这段代码实现了一个简单的线程池，可以接收返回值的任务。它使用了一个线程安全队列来存储任务，工作线程会从中取出任务执行。
 * 通过条件变量和互斥量协同工作，确保了任务的正确分发和线程的同步。
 * 此外，通过禁用复制构造函数和赋值操作符，保证了线程池对象的唯一性和安全性。
 * 线程池的析构函数确保了所有的工作线程都能在线程池销毁前正确地结束。
 */
class ThreadPool {
private:
    // 工作线程的内部类定义。
    class worker {
    public:
        ThreadPool* pool; ///< 指向所属线程池的指针。

        /**
         * @brief 构造函数。
         * 
         * @param _pool 指向创建此worker的ThreadPool对象的指针。
         */
        worker(ThreadPool* _pool) : pool{ _pool } {}

        /**
         * @brief 重载()操作符，定义工作线程的任务执行逻辑。
         */
        void operator ()() {
            // 循环，直到线程池被关闭。
            while (!pool->is_shut_down) {
                {
                    // 锁定互斥量。
                    unique_lock<mutex> lock(pool->_m);
                    // 等待条件变量，直到有任务可执行或线程池关闭。
                    pool->cv.wait(lock, [this]() {
                        return this->pool->is_shut_down ||
                            !this->pool->que.empty();
                        });
                }
                function<void()> func; // 定义函数对象。
                bool flag = pool->que.pop(func); // 尝试从任务队列中弹出任务。
                if (flag) {
                    func(); // 如果成功获取任务，则执行。
                }
            }
        }
    };

public:
    bool is_shut_down; ///< 标记线程池是否已关闭。
    safe_queue<std::function<void()>> que; ///< 线程安全的任务队列。
    vector<std::thread> threads; ///< 存储工作线程的向量。
    mutex _m; ///< 用于同步的互斥量。
    condition_variable cv; ///< 条件变量，用于通知工作线程。

    /**
     * @brief 构造函数。
     * 
     * @param n 线程池中线程的数量。
     */
    ThreadPool(int n) : threads(n), is_shut_down{ false } {
        for (auto& t : threads) t = thread{ worker(this) }; // 创建工作线程。
    }

    // 禁止复制和移动构造。
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /**
     * @brief 提交一个任务到线程池。
     * 
     * @tparam F 任务函数的类型。
     * @tparam Args 任务函数参数的类型。
     * @param f 任务函数。
     * @param args 任务函数的参数。
     * @return std::future<decltype(f(args...))> 任务的返回值的future。
     */
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        // 创建任务函数。
        function<decltype(f(args...))()> func = [&f, args...]() { return f(args...); };
        // 包装成std::packaged_task对象。
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);
        // 创建包装函数，以适配无参数任务队列。
        std::function<void()> wrapper_func = [task_ptr]() {
            (*task_ptr)();
        };
        que.push(wrapper_func); // 将任务推入队列。
        cv.notify_one(); // 通知一个工作线程。
        return task_ptr->get_future(); // 返回任务的future。
    }

    /**
     * @brief 析构函数。
     */
    ~ThreadPool() {
        auto f = submit([]() {}); // 提交一个空任务以确保至少有一个工作线程被唤醒。
        f.get(); // 等待空任务完成。
        is_shut_down = true; // 标记线程池为关闭状态。
        cv.notify_all(); // 唤醒所有工作线程。
        for (auto& t : threads) { // 等待所有工作线程结束。
            if (t.joinable()) t.join();
        }
    }
};

// 定义全局互斥量，用于同步对标准输出的访问。
mutex _m;
/*
这段代码在主函数中创建了一个拥有8个工作线程的ThreadPool实例。
然后，它循环20次，每次都向线程池提交一个任务。每个任务简单地检查它接收到的ID参数是否为奇数；
如果是，它会通过休眠0.2秒来模拟较长的执行时间。无论ID是奇数还是偶数，每个任务都会尝试获取互斥锁并向标准输出打印它的ID。

需要注意的是，由于所有任务共享同一个全局互斥锁以同步对std::cout的访问，
这确保了即使多个线程同时达到打印步骤，输出也不会相互干扰。
最后，当main函数结束时，pool的生命周期结束，触发ThreadPool的析构函数，它会等待所有剩余的任务完成，然后安全地关闭所有线程。
*/

int main() {
    // 创建一个包含8个线程的线程池。
    ThreadPool pool(8);
    
    // 指定要提交给线程池执行的任务数量。
    int n = 20;
    
    // 循环提交任务到线程池。
    for (int i = 1; i <= n; i++) {
        // 提交任务到线程池。
        // 每个任务接收一个整数ID作为参数。
        pool.submit([](int id) {
            // 如果ID是奇数，任务会休眠0.2秒。
            // 这用来模拟执行时间的差异。
            if (id % 2 == 1) {
                this_thread::sleep_for(0.2s);
            }
            
            // 锁定全局互斥量，以同步对cout的访问。
            // 这防止了多个线程同时写入到标准输出，从而避免了输出混乱。
            unique_lock<mutex> lc(_m);
            
            // 打印当前任务的ID。
            cout << "id : " << id << endl;
        }, i); // 注意这里的i作为参数传递给了任务。
    }
    
    // main函数结束时，局部变量pool会被销毁。
    // ThreadPool的析构函数会被调用，它等待所有任务完成并清理线程池。
}
