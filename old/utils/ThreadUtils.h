#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <time.h>

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
		ThreadWrapper(Runnable& runnable) : m_runnable(runnable), m_param(NULL), m_isRunning(false), m_thread(-1) {};
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
				nanosleep(CLOCK_REALTIME, &tspec);
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
			wrapper->getRunnable().run(getRunParam());
			return (void*)NULL;
		}
	};

}
