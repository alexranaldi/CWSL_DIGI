#pragma once
#ifndef SAFE_QUEUE
#define SAFE_QUEUE

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

using namespace std::chrono_literals;


template <class T>
class SafeQueue
{
public:
    SafeQueue(void)
        : q()
        , m()
        , c()
    {}

    ~SafeQueue(void)
    {}

    // Add an element to the queue.
    void enqueue(T t)
    {
        std::lock_guard<std::mutex> lock(m);
        q.push(t);
        c.notify_one();
    }

    bool dequeue(T& item) {
        std::unique_lock<std::mutex> lock(m);
        if (q.empty()) 
        { 
            return false; 
        }
        item = q.front();
        q.pop();
        return true;
    }

    T dequeue(void)
    {
        std::unique_lock<std::mutex> lock(m);
        while (q.empty())
        {
            // release lock as long as the wait and reaquire it afterwards.
            c.wait(lock);
        }
        T val = q.front();
        q.pop();
        return val;
    }

    bool dequeue_timeout(T& item)
    {
        std::unique_lock<std::mutex> lock(m);
        int i;
        while (q.empty())
        {
            // release lock as long as the wait and reaquire it afterwards.
            if (c.wait_for(lock, 250ms) == std::cv_status::timeout) {
                return false;
            }
        }
        item = q.front();
        q.pop();
        return true;
    }

    bool empty(void)
    {
        std::unique_lock<std::mutex> lock(m);
        return q.empty();
    }

    size_t size(void)
    {
        std::unique_lock<std::mutex> lock(m);
        return q.size();
    }

private:
	std::queue<T> q;
	mutable std::mutex m;
	std::condition_variable c;
};
#endif