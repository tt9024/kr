#pragma once

#include <stdio.h>
#include <stdlib.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef __USE_GNU
#define __USE_GNU
#endif

#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <atomic>

namespace utils {

    /// a template for Runnable
    class Runnable {
    public:
        void run(void* para);
        void stop();
    };

    template<typename Runnable>
    class ThreadWrapper {
    public:
        // note runnable has to live in its own scope. 
        ThreadWrapper(Runnable& runnable) : m_runnable(runnable), m_param(NULL), m_isRunning(false), m_thread(0) {};
        void run(void* para) {
            if (!m_isRunning) {
                m_param = para;
                int ret = pthread_create( &m_thread, NULL, ThreadWrapper<Runnable>::threadFunc, (void*) this);
                if (ret != 0) {
                    throw std::runtime_error(std::string("pthread creation error! errno=") + std::to_string(errno));
                }
                m_isRunning = true;
            }
        }
        void join() {
            if (m_isRunning)
                pthread_join(m_thread, NULL);
        }
        void stop() {
            if (m_isRunning) {
                m_runnable.stop();
                m_isRunning = false;
            };
        }

        void setAffinity(int cpuID) {
              cpu_set_t mask;
              CPU_ZERO(&mask);
              CPU_SET(cpuID, &mask);
              if (sched_setaffinity(0, sizeof(mask), &mask) != 0) {
                  throw std::runtime_error(std::string("set thread affinity error! errno=") + std::to_string(errno));
              }
        };

        ~ThreadWrapper() {
            if (m_isRunning) {
                m_runnable.stop();
                //sleep 100 millisecond and wait any pending loops
                struct timespec tspec;
                tspec.tv_sec = 0;
                tspec.tv_nsec = 100000000 ;
                nanosleep(&tspec, &tspec);
            }
            pthread_cancel(m_thread);
        };

        // getters
        Runnable& getRunnable() const { return m_runnable; };
        void* getRunParam() const { return m_param; };

    private:
        Runnable& m_runnable;
        void* m_param;
        bool m_isRunning;
        pthread_t m_thread;
        static void* threadFunc(void* para) {
            ThreadWrapper<Runnable> *wrapper = (ThreadWrapper<Runnable>*)para;
            wrapper->getRunnable().run(wrapper->getRunParam());
            return (void*)NULL;
        }
    };

    /*
     * Context scope spin lock, exception safe. 
     * Lock is obtained upon construction and is released upon destruction. 
     * The input parameter "LockType& lock" is assumed to be a member variable
     * or a temparary whose life span is longer than the SpinLock object. 
     * Examples refer to the unittests.
     */
    class SpinLock {
    public:
        using LockType = std::atomic<bool>;
        static std::shared_ptr<LockType> CreateSpinLock(bool initial_value = false) {
            return std::make_shared<LockType>(initial_value);
        }

        explicit SpinLock(LockType& lock) noexcept 
        :_lock(&lock)
        {
            acquire();
        }

        ~SpinLock() {
            if (_lock) {
                release();
            }
        }

        static std::shared_ptr<SpinLock> TryLock(LockType& lock) noexcept {
            // try lock, if success, return a shared pointer to the spin lock
            // the destruct releases it out-of-scope
            // if failed, return a nullptr
            auto sl = std::make_shared<SpinLock>();
            sl->_lock=&lock;
            if (sl->try_lock()) {
                return sl;
            }
            sl->_lock = nullptr;
            return std::shared_ptr<SpinLock>();
        }

        SpinLock() : _lock(NULL) {};
    private:
        void acquire() noexcept {
            for (;;) {
                if (!_lock->exchange(true, std::memory_order_acquire)) {
                    break;
                }
                while (_lock->load(std::memory_order_relaxed)) {
                    __builtin_ia32_pause();
                }
            }
        }

        void release() noexcept {
            _lock->store(false, std::memory_order_release);
        }

        bool try_lock() noexcept {
            return !_lock->load(std::memory_order_relaxed) &&
                   !_lock->exchange(true, std::memory_order_acquire);
        }


        LockType* _lock;

        // no copy or assign
        SpinLock(const SpinLock&) = delete;
        void operator=(const SpinLock&) = delete;
    };
}
