#pragma once
#ifndef SAFE_QUEUE
#define SAFE_QUEUE

#include <queue>
#include <mutex>
#include <condition_variable>

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